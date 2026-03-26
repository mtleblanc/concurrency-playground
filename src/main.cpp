#include "coroutine.hh"
#include "socket.hh"
#include <functional>
#include <print>

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
AsioCoroutine coro() { co_return; }
int main() {
  std::println("concurrency playground");
  coro();
}
