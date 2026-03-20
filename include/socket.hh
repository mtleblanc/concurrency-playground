#pragma once

#include <exception>
#include <format>
#include <map>
#include <netdb.h>
#include <poll.h>
#include <ranges>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

class AddressInfo {
  addrinfo *addressInfo_;

public:
  AddressInfo(std::string &address, int port, const addrinfo *hints = nullptr) {
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
  Socket(AddressInfo addressInfo, int type, int proto) {
    f_socket = socket(addressInfo->ai_family, type, proto);
    if (f_socket == -1) {
      throw std::bad_alloc();
    }
  }
  ~Socket() { close(f_socket); }
  operator int() { return f_socket; }
};

class UdpServer {
  AddressInfo addressInfo_;
  Socket socket_;
  constexpr static addrinfo hints = [] {
    addrinfo h{};
    h.ai_family = AF_UNSPEC;
    h.ai_socktype = SOCK_DGRAM;
    h.ai_protocol = IPPROTO_UDP;
    return h;
  }();

public:
  UdpServer(std::string &address, int port)
      : addressInfo_{address, port, &hints},
        socket_{addressInfo_, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP} {
    if (bind(socket_, addressInfo_->ai_addr, addressInfo_->ai_addrlen) != 0) {
      throw std::bad_alloc{};
    }
  }

  auto echo() {
    auto buf = std::array<char, 512>{};
    auto clientAddress = sockaddr{};
    auto clientAddressSize = socklen_t{sizeof(clientAddress)};
    auto r = recvfrom(socket_, buf.data(), buf.size(), 0, &clientAddress,
                      &clientAddressSize);
    if (r != -1) {
      auto n =
          sendto(socket_, buf.data(), r, 0, &clientAddress, clientAddressSize);
      if (n < 0) {
        throw std::bad_exception{};
      }
    }
  }
};

class TcpServer;
class TcpConnection {
  int fd_;
  friend class TcpServer;
  TcpConnection(int fd) : fd_{fd} {}

public:
  TcpConnection() : fd_(-1) {}
  TcpConnection(const TcpConnection &) = delete;
  TcpConnection(TcpConnection &&o) {
    fd_ = o.fd_;
    o.fd_ = -1;
  }
  auto operator=(const TcpConnection &) = delete;
  auto operator=(TcpConnection &&o) {
    if (fd_ >= 0) {
      close(fd_);
    }
    fd_ = o.fd_;
    o.fd_ = -1;
  }
  ~TcpConnection() {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  auto echo() {
    auto buf = std::array<char, 512>{};
    auto r = recv(fd_, buf.data(), buf.size(), 0);
    if (r != -1) {
      auto n = send(fd_, buf.data(), r, 0);
      if (n < 0) {
        throw std::bad_exception{};
      }
    }
  }
};

class TcpServer {
  AddressInfo addressInfo_;
  Socket socket_;
  constexpr static addrinfo hints = [] {
    addrinfo h{};
    h.ai_family = AF_UNSPEC;
    h.ai_socktype = SOCK_STREAM;
    h.ai_protocol = IPPROTO_TCP;
    return h;
  }();

public:
  TcpServer(std::string &address, int port)
      : addressInfo_{address, port, &hints},
        socket_{addressInfo_, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP} {
    if (bind(socket_, addressInfo_->ai_addr, addressInfo_->ai_addrlen) != 0) {
      throw std::bad_alloc{};
    }
    if (listen(socket_, 0) != 0) {
      throw std::bad_alloc{};
    }
  }

  auto accept() {
    auto fd = ::accept(socket_, nullptr, nullptr);
    return TcpConnection{fd};
  }
};

template <typename Action> struct Monitor {
  int fd_;
  std::optional<Action> readReady_;
  std::optional<Action> writeReady_;
};

template <typename Action> class Multiplex {
  std::map<int, Monitor<Action>> sockets_{};
  std::vector<pollfd> fds_{};

public:
  auto add(Monitor<Action> m) {
    auto fd = m.fd_;
    auto [_, succ] = sockets_.insert({m.fd_, std::move(m)});
    if (!succ) {
      throw std::invalid_argument{"fd already in watch set"};
    }
    fds_.push_back({m.fd_});
    if (m.readReady) {
      fds_.back().events |= POLLIN;
    }
    if (m.writeReady) {
      fds_.back().events |= POLLOUT;
    }
  }

  void doPoll() {
    auto n = poll(fds_.data(), fds_.size(), -1);
    if (n <= 0) {
      return;
    }
    auto ready = [](const auto &fd) { return fd.revents != 0; };
    for (const auto &fd : fds_ | std::views::filter(ready)) {
      auto monitor = sockets_.at(fd.fd);
      if (fd.revents & POLLIN) {
        *monitor.readReady();
      }
      if (fd.revents & POLLOUT) {
        *monitor.writeReady();
      }
    }
  }

  void run() {
    for (;;) {
      doPoll();
    }
  }
};
