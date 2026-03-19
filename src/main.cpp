#include "socket.hh"
#include <print>
#include <thread>

auto process(TcpConnection conn) {
  while (1) {
    conn.echo();
  }
}
int main() {
  std::println("concurrency playground");
  auto address = std::string{"0.0.0.0"};
  auto server = TcpServer{address, 12345};

  while (1) {
    auto conn = server.accept();
    auto t = std::thread{process, std::move(conn)};
    t.detach();
  }
}
