#pragma once

#include <netdb.h>
#include <string>
#include <unistd.h>
#include <utility>

class AddressInfo {
  addrinfo *addressInfo_{};

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

  operator int() const { return fd_; }
  auto fd() const { return fd_; }

private:
  void release() {
    if (fd_ != -1) {
      ::close(fd_);
    }
    fd_ = -1;
  }
};
