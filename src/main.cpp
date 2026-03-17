#include "socket.hh"
#include <print>

int main() {
  std::println("concurrency playground");
  auto address = std::string{"0.0.0.0"};
  auto server = UdpServer{12345, address};

  while (1) {
    server.echo();
  }
}
