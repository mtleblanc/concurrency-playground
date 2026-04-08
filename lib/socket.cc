#include "socket.hh"
#include <print>

namespace Asio {
AddressInfo::AddressInfo(std::string &address, int port,
                         const addrinfo *hints) {
  auto decimal_port = std::to_string(port);
  if (::getaddrinfo(address.data(), decimal_port.data(), hints,
                    &addressInfo_) != 0 ||
      addressInfo_ == NULL) {
    throw std::invalid_argument(
        std::format("invalid address or port: \"{}:{}\"", address, port));
  }
}

Socket::Socket(int domain, int type, int proto) {
  fd_ = ::socket(domain, type, proto);
  if (fd_ == -1) {
    throw std::bad_alloc();
  }
}

constexpr static addrinfo hints = [] {
  addrinfo h{};
  h.ai_family = AF_UNSPEC;
  h.ai_socktype = SOCK_STREAM;
  h.ai_protocol = IPPROTO_TCP;
  return h;
}();

Socket Socket::listenOn(std::string &address, int port) {
  Socket socket_{AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP};
  auto addressInfo_ = AddressInfo{address, port, &hints};
  if (::bind(socket_, addressInfo_->ai_addr, addressInfo_->ai_addrlen)) {
    throw std::bad_alloc{};
  }
  if (::listen(socket_, SOMAXCONN)) {
    throw std::bad_alloc{};
  }
  return socket_;
}
} // namespace Asio
