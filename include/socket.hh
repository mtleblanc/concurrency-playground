#pragma once

#include <format>
#include <netdb.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

class AddressInfo {
  addrinfo *addressInfo_;

public:
  AddressInfo(int port, std::string &address, const addrinfo *hints = nullptr) {
    auto decimal_port = std::to_string(port);
    if (getaddrinfo(address.data(), decimal_port.data(), hints,
                    &addressInfo_) != 0 ||
        addressInfo_ == NULL) {
      throw std::invalid_argument(
          std::format("invalid address or port: \"{}:{}\"", address, port));
    }
  }
  ~AddressInfo() { freeaddrinfo(addressInfo_); }

  auto operator->() { return addressInfo_; }
};

class Socket {
  int f_socket;

public:
  Socket(AddressInfo addressInfo) {
    f_socket =
        socket(addressInfo->ai_family, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
    if (f_socket == -1) {
      throw std::bad_alloc();
    }
  }
  ~Socket() { close(f_socket); }
};

class UdpServer {
  AddressInfo addressInfo_;
  Socket socket_;
  constexpr static addrinfo hints = [] {
    addrinfo h{};
    h.ai_family   = AF_UNSPEC;
    h.ai_socktype = SOCK_DGRAM;
    h.ai_protocol = IPPROTO_UDP;
    return h;
  }();

public:
  UdpServer(int port, std::string &address)
      : addressInfo_(port, address, &hints), socket_{addressInfo_} {}
};
