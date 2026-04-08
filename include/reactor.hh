#pragma once

#include "sockets_raii.hh"
#include "util.hh"
#include <cassert>
#include <expected>
#include <functional>
#include <optional>
#include <poll.h>

namespace Asio {

class IOContext : MoveOnly {
public:
  struct Handle {
    int fd;
    int generation;
  };
  using ReadyAction = std::function<void(IOContext &, Handle, Socket &)>;

  void run();
  void readable(Handle, ReadyAction);
  void writeable(Handle, ReadyAction);
  std::optional<std::reference_wrapper<Socket>> get(Handle);
  Handle watch(Socket);
  std::optional<Socket> unwatch(Handle);

private:
  void doRead(int);
  void doWrite(int);

  struct Slot {
    Socket socket{};
    ReadyAction readable{};
    ReadyAction writeable{};
    bool occupied{};
    int generation{};
    int fdsIndex;

    Handle handle() { return {socket.fd(), generation}; }
  };

  std::vector<pollfd> pollFds_{};
  std::unordered_map<int, Slot> socketIndices{};
};

} // namespace Asio
