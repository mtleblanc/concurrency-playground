#pragma once

#include "util.hh"
#include <expected>
#include <netdb.h>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace Asio {

class AddressInfo {
public:
  AddressInfo() {}
  AddressInfo(std::string &address, int port, const addrinfo *hints = nullptr);
  AddressInfo(const AddressInfo &) = delete;
  AddressInfo(AddressInfo &&o)
      : addressInfo_{std::exchange(o.addressInfo_, nullptr)} {}
  AddressInfo &operator=(const AddressInfo &) = delete;
  AddressInfo &operator=(AddressInfo &&o) {
    if (this != &o) {
      release();
      addressInfo_ = std::exchange(o.addressInfo_, nullptr);
    }
    return *this;
  };
  ~AddressInfo() { release(); }

  auto *operator*() { return addressInfo_; }
  auto *operator->() { return addressInfo_; }

private:
  void release() {
    if (addressInfo_ != nullptr) {
      ::freeaddrinfo(addressInfo_);
    }
    addressInfo_ = nullptr;
  }

  addrinfo *addressInfo_;
};

inline auto unexpectedErrno() {
  return std::unexpected{std::make_error_code(static_cast<std::errc> errno)};
}

class Socket {
  int fd_{-1};

public:
  Socket() {}
  Socket(int fd) : fd_{fd} {}
  Socket(int domain, int type, int proto);
  Socket(const Socket &) = delete;
  Socket(Socket &&o) : fd_{std::exchange(o.fd_, -1)} {}
  Socket &operator=(const Socket &) = delete;
  Socket &operator=(Socket &&o) {
    if (this != &o) {
      release();
      fd_ = std::exchange(o.fd_, -1);
    }
    return *this;
  };
  ~Socket() { release(); }

  operator int() const { return fd_; }
  auto fd() const { return fd_; }

  Result<Socket> accept() {
    auto fd = ::accept(fd_, nullptr, nullptr);
    if (fd < 0) {
      return unexpectedErrno();
    }
    return Socket(fd);
  }

  Result<int> read(char *data, int dataSize) {
    auto n = ::recv(fd_, data, dataSize, MSG_DONTWAIT);
    if (n < 0) {
      return unexpectedErrno();
    }
    return n;
  }

  Result<int> write(const char *data, int dataSize) {
    auto n = ::send(fd_, data, dataSize, MSG_DONTWAIT);
    if (n < 0) {
      return unexpectedErrno();
    }
    return n;
  }

private:
  void release() {
    if (fd_ != -1) {
      ::close(fd_);
    }
    fd_ = -1;
  }
};
} // namespace Asio
