#include "coroutine.hh"
#include "socket.hh"
#include <functional>
#include <print>

using namespace Asio;

class EchoConnection {
  TcpAsio::Conn *conn_;

  void echo(std::string data) {
    conn_->write(std::move(data), [&](auto res) {
      if (res) {
        return;
      }
      conn_->read([&](auto res, auto data) {
        if (res) {
          return;
        }
        echo(std::move(data));
      });
    });
  }

public:
  EchoConnection(TcpAsio::Conn *conn) : conn_{conn} {
    conn_->read([&](auto res, auto data) {
      if (res) {
        return;
      }
      echo(std::move(data));
    });
  }
};

[[maybe_unused]] void runProactor() {
  auto address = std::string{"0.0.0.0"};
  auto server = TcpServer{address, 12345};
  auto connections = std::vector<std::unique_ptr<TcpAsio::Conn>>{};
  auto echoConnections = std::vector<std::unique_ptr<EchoConnection>>{};
  auto multiplexServer =
      TcpAsio::Server{std::move(server), [&](auto conn) {
                        auto connp =
                            std::make_unique<TcpAsio::Conn>(std::move(conn));
                        connections.emplace_back(std::move(connp));
                        auto c = connections.back().get();
                        auto ec = std::make_unique<EchoConnection>(c);
                        echoConnections.emplace_back(std::move(ec));
                      }};
  multiplexServer.run();
}

[[maybe_unused]] AsioCoroutine echo(std::shared_ptr<TcpAsio::Conn> conn) {
  for (;;) {
    auto [ec, data] = co_await ReadAwaitable{conn};
    if (ec) {
      co_return;
    }
    ec = co_await WriteAwaitable(conn, data);
    if (ec) {
      co_return;
    }
  }
}

[[maybe_unused]] AsioCoroutine
serve(std::shared_ptr<TcpAsio::ReactorServer> server) {
  for (;;) {
    auto [ec, conn] = co_await AcceptAwaitable(server);
    if (ec) {
      continue;
    }
    echo(conn);
  }
}

[[maybe_unused]] void coroReadWrite(TcpServer server) {
  auto multiplexServer =
      TcpAsio::Server{std::move(server), [&](auto conn) {
                        auto connp =
                            std::make_shared<TcpAsio::Conn>(std::move(conn));
                        echo(std::move(connp));
                      }};
  multiplexServer.run();
}

[[maybe_unused]] void fullCoro(TcpServer server) {
  Multiplex mp;
  auto mon = mp.monitor(server.fd());
  auto rs =
      std::make_shared<TcpAsio::ReactorServer>(&mp, mon, std::move(server));
  serve(rs);
  mp.run();
}

int main() {
  std::println("concurrency playground");
  auto address = std::string{"0.0.0.0"};
  auto server = TcpServer{address, 12345};
  fullCoro(std::move(server));
}
