#include "socket.hh"
#include <functional>
#include <print>

auto process(TcpConnection conn) {
  while (1) {
    conn.echo();
  }
}

class EchoConnection {
  TcpConnection *conn_;

public:
  EchoConnection(TcpConnection *conn) : conn_{conn} {}
  EchoConnection(TcpConnection &conn) : conn_{&conn} {}
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
  auto connections = std::vector<TcpConnection>{};
  auto echoConnections = std::vector<EchoConnection>{};
  auto serverRead = [&]() {
    connections.push_back(server.accept());
    echoConnections.emplace_back(connections.back());
    auto connectionMonitor =
        Monitor<Action>{connections.back().fd(),
                        std::optional{echoConnections.back()}, std::nullopt};
    multiplex.add(std::move(connectionMonitor));
  };
  multiplex.add(
      Monitor<Action>{server.fd(), std::optional{serverRead}, std::nullopt});
  while (1) {
    multiplex.run();
  }
}
