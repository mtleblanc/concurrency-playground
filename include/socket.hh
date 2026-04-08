#pragma once

#include "util.hh"
#include <cstring>
#include <expected>
#include <netdb.h>
#include <poll.h>
#include <print>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
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

  static Socket listenOn(std::string &address, int port);

  operator int() const { return fd_; }
  auto fd() const { return fd_; }

  Result<Socket> accept(sockaddr *addr = nullptr, socklen_t *socklen = nullptr);
  Result<ssize_t> read(char *data, size_t dataSize, int flags = MSG_DONTWAIT);
  Result<ssize_t> write(const char *data, size_t dataSize,
                        int flags = MSG_DONTWAIT);
  Result<void> bind(const sockaddr *addr, socklen_t socklen);
  Result<void> listen(int backlog);

  Result<void> reuseAddress();

private:
  void release() {
    if (fd_ != -1) {
      ::close(fd_);
    }
    fd_ = -1;
  }
};
} // namespace Asio
