#include "socket.hh"
#include <print>

int main() {
  std::println("concurrency playground");
  auto address = std::string{"0.0.0.0"};
  auto server = TcpServer{address, 12345};

  auto conn = server.accept();
  while (1) {
    conn.echo();
  }
}
