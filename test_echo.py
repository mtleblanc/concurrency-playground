#!/usr/bin/env python3
"""
Echo server test: spawns N threads, each making a TCP connection to 0.0.0.0:12345,
sending random data, and verifying the echoed reply matches exactly.
"""

import argparse
import os
import socket
import sys
import threading
import time


HOST = "127.0.0.1"
PORT = 12345
CHUNK = 65536  # send/recv in 64 KiB chunks


def run_connection(
    conn_id: int,
    data: bytes,
    results: list,
    errors: list,
    records: list | None,
    start_time: float,
    timeout: float | None,
) -> None:
    def log(op: str, nbytes: int) -> None:
        if records is not None:
            records.append((time.perf_counter() - start_time, conn_id, op, nbytes))

    log("send", 0)
    try:
        with socket.create_connection((HOST, PORT)) as sock:
            total = len(data)
            sent = 0
            received = bytearray()
            log("send", 0)
            # Send all data, receiving whatever arrives in between to avoid
            # blocking if the server's send buffer fills (deadlock avoidance).
            while sent < total or len(received) < total:
                if sent < total:
                    chunk = data[sent : sent + CHUNK]
                    n = sock.send(chunk)
                    sent += n
                    log("send", n)

                # Non-blocking drain
                sock.setblocking(False)
                drained = 0
                try:
                    while True:
                        chunk = sock.recv(CHUNK)
                        if not chunk:
                            break
                        received.extend(chunk)
                        drained += len(chunk)
                except BlockingIOError:
                    pass
                sock.settimeout(timeout)
                if drained:
                    log("recv", drained)

                if sent == total and len(received) < total:
                    # Nothing left to send; just wait for the rest
                    remaining = total - len(received)
                    try:
                        chunk = sock.recv(min(CHUNK, remaining))
                    except TimeoutError:
                        raise TimeoutError(
                            f"[{conn_id}] No data from server for {timeout}s "
                            f"({len(received)}/{total} bytes received)"
                        )
                    if not chunk:
                        raise ConnectionError(
                            f"[{conn_id}] Server closed connection after "
                            f"{len(received)}/{total} bytes received"
                        )
                    received.extend(chunk)
                    log("recv", len(chunk))

            if bytes(received) == data:
                results.append(conn_id)
                print(f"  [conn {conn_id}] OK  ({total:,} bytes)")
            else:
                # Find first mismatch for a useful error message
                for i, (a, b) in enumerate(zip(data, received)):
                    if a != b:
                        errors.append(
                            f"[conn {conn_id}] MISMATCH at byte {i}: "
                            f"sent {a:#04x}, got {b:#04x}"
                        )
                        break
                else:
                    errors.append(
                        f"[conn {conn_id}] LENGTH MISMATCH: "
                        f"sent {total}, got {len(received)}"
                    )
                print(f"  [conn {conn_id}] FAIL", file=sys.stderr)

    except Exception as exc:  # noqa: BLE001
        errors.append(f"[conn {conn_id}] EXCEPTION: {exc}")
        print(f"  [conn {conn_id}] ERROR: {exc}", file=sys.stderr)


def main() -> None:
    parser = argparse.ArgumentParser(description="TCP echo server stress test")
    parser.add_argument(
        "-n", "--connections",
        type=int,
        default=4,
        metavar="N",
        help="number of concurrent connections (default: 4)",
    )
    parser.add_argument(
        "-s", "--size",
        type=int,
        default=1024 * 1024,
        metavar="BYTES",
        help="bytes to send per connection (default: 1048576 = 1 MiB)",
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="print per-event send/recv timeline after completion",
    )
    parser.add_argument(
        "-t", "--timeout",
        type=float,
        default=10.0,
        metavar="SECS",
        help="seconds to wait for echo data before failing (default: 10, 0 = no timeout)",
    )
    args = parser.parse_args()

    n_conns = args.connections
    data_size = args.size
    timeout = args.timeout or None  # 0 → None (no timeout)

    print(f"Echo test: {n_conns} connection(s), {data_size:,} bytes each → {HOST}:{PORT}")

    # Generate one random payload per connection so each has distinct data
    payloads = [os.urandom(data_size) for _ in range(n_conns)]

    results: list[int] = []
    errors: list[str] = []

    # Each thread gets its own records list; merged after join (no locking needed).
    per_thread_records: list[list] = [[] if args.verbose else None for _ in range(n_conns)]

    start_time = time.perf_counter()

    threads = [
        threading.Thread(
            target=run_connection,
            args=(i, payloads[i], results, errors, per_thread_records[i], start_time, timeout),
            daemon=True,
        )
        for i in range(n_conns)
    ]

    for t in threads:
        t.start()
    for t in threads:
        t.join()

    print()
    passed = len(results)
    failed = len(errors)
    print(f"Results: {passed}/{n_conns} passed, {failed}/{n_conns} failed")

    if errors:
        print("\nErrors:")
        for e in errors:
            print(f"  {e}", file=sys.stderr)

    if args.verbose:
        all_records = sorted(
            (r for thread_records in per_thread_records for r in thread_records),
            key=lambda r: r[0],
        )
        print(f"\n{'Time (ms)':>10}  {'Conn':>4}  {'Op':>4}  {'Bytes':>10}  {'Cumulative':>10}")
        cumulative = [0] * n_conns
        for ts, conn_id, op, nbytes in all_records:
            cumulative[conn_id] += nbytes if op == "recv" else 0
            print(
                f"{ts * 1000:>10.1f}  {conn_id:>4}  {op:>4}  {nbytes:>10,}  {cumulative[conn_id]:>10,}"
            )

    if errors:
        sys.exit(1)


if __name__ == "__main__":
    main()
