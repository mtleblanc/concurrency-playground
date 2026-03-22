#include "socket.hh"
#include <functional>
#include <memory>
#include <print>

auto process(TcpConnection conn) {
  while (1) {
    conn.echo();
  }
}

class EchoConnection {
  std::shared_ptr<TcpConnection> conn_;

public:
  EchoConnection(std::shared_ptr<TcpConnection> conn) : conn_{conn} {}
  void operator()() { conn_->echo(); }
};

int main() {
  std::println("concurrency playground");
  auto address = std::string{"0.0.0.0"};
  auto server = TcpServer{address, 12345};

  // while (1) {
  //   auto conn = server.accept();
  //   auto t = std::thread{process, std::move(conn)};
  //   t.detach();
  // }
  //
  using Action = std::function<void()>;
  auto multiplex = Multiplex<Action>{};
  auto echoConnections = std::vector<EchoConnection>{};
  auto serverRead = [&]() {
    auto conn = std::make_shared<TcpConnection>(server.accept());
    echoConnections.emplace_back(conn);
    auto connectionMonitor = Monitor<Action>{
        conn->fd(), std::optional{echoConnections.back()}, std::nullopt};
    multiplex.add(std::move(connectionMonitor));
  };
  multiplex.add(
      Monitor<Action>{server.fd(), std::optional{serverRead}, std::nullopt});
  while (1) {
    multiplex.run();
  }
}
