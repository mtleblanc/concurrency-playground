#pragma once

#include <cstring>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <netdb.h>
#include <poll.h>
#include <print>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

class AddressInfo {
  addrinfo *addressInfo_{};

public:
  AddressInfo() {}
  AddressInfo(std::string &address, int port, const addrinfo *hints = nullptr) {
    auto decimal_port = std::to_string(port);
    if (::getaddrinfo(address.data(), decimal_port.data(), hints,
                      &addressInfo_) != 0 ||
        addressInfo_ == NULL) {
      throw std::invalid_argument(
          std::format("invalid address or port: \"{}:{}\"", address, port));
    }
  }
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
  Socket(int domain, int type, int proto) {
    fd_ = ::socket(domain, type, proto);
    if (fd_ == -1) {
      throw std::bad_alloc();
    }
  }
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
        socket_{addressInfo_->ai_family, SOCK_STREAM | SOCK_CLOEXEC,
                IPPROTO_TCP} {
    if (::bind(socket_, addressInfo_->ai_addr, addressInfo_->ai_addrlen)) {
      throw std::bad_alloc{};
    }
    if (::listen(socket_, 0)) {
      throw std::bad_alloc{};
    }
  }

  int fd() const { return socket_; }
  auto accept() const {
    return std::make_shared<Socket>(::accept(socket_, nullptr, nullptr));
  }
};

class Monitor;
class Multiplex;

class Monitor {
  using Action = std::function<bool()>;
  int fd_;
  Multiplex *mp_;
  std::optional<Action> readReady_;
  std::optional<Action> writeReady_;
  friend class Multiplex;

public:
  Monitor(int fd, Multiplex *mp) : fd_{fd}, mp_{mp} {}
  Monitor(const Monitor&) = delete;
  Monitor(Monitor&&) = default;
  Monitor& operator=(const Monitor&) = delete;
  Monitor& operator=(Monitor&&) = default;
  ~Monitor() = default;
  void onRead(Action f);
  void onWrite(Action f);
  void doRead();
  void doWrite();
};

class Multiplex {
  std::map<int, Monitor> sockets_{};
  std::map<int, int> fdToIndex_{};
  std::vector<pollfd> fds_{};

public:
  void doPoll();
  void run();
  Monitor *monitor(int fd);
  void enableRead(int fd);
  void enableWrite(int fd);
  void disableRead(int fd);
  void disableWrite(int fd);
};

inline void Monitor::onRead(Action f) {
  if (readReady_) {
    throw std::logic_error{"read already pending"};
  }
  readReady_ = std::move(f);
  mp_->enableRead(fd_);
}
inline void Monitor::onWrite(Action f) {
  if (writeReady_) {
    throw std::logic_error{"write already pending"};
  }
  writeReady_ = std::move(f);
  mp_->enableWrite(fd_);
}
inline void Monitor::doRead() {
  if (!readReady_ || !(*readReady_)()) {
    mp_->disableRead(fd_);
    readReady_ = {};
  }
}
inline void Monitor::doWrite() {
  if (!writeReady_ || !(*writeReady_)()) {
    mp_->disableWrite(fd_);
    writeReady_ = {};
  }
}

inline void Multiplex::doPoll() {
  auto n = poll(fds_.data(), fds_.size(), -1);
  if (n <= 0) {
    return;
  }
  auto N = std::ssize(fds_);
  for (auto i = 0; i < N; ++i) {
    auto &fd = fds_[i];
    auto& monitor = sockets_.at(fd.fd);
    if (fd.revents & POLLIN) {
      monitor.doRead();
    }
    if (fd.revents & POLLOUT) {
      monitor.doWrite();
    }
  }
}

inline void Multiplex::run() {
  for (;;) {
    doPoll();
  }
}

inline Monitor *Multiplex::monitor(int fd) {
  sockets_.emplace(fd, Monitor{fd, this});
  fds_.emplace_back(fd, 0, 0);
  fdToIndex_.emplace(fd, fds_.size() - 1);
  return &sockets_.at(fd);
}

inline void Multiplex::enableRead(int fd) {
  fds_[fdToIndex_[fd]].events |= POLLIN;
}

inline void Multiplex::enableWrite(int fd) {
  fds_[fdToIndex_[fd]].events |= POLLOUT;
}

inline void Multiplex::disableRead(int fd) {
  fds_[fdToIndex_[fd]].events &= ~POLLIN;
}

inline void Multiplex::disableWrite(int fd) {
  fds_[fdToIndex_[fd]].events &= ~POLLOUT;
}

class TcpAsio {
  using ReadFunction = std::function<void(std::string &)>;
  using WriteFunction = std::function<void()>;
  using ReadyFunction = std::function<bool()>;

  Multiplex multiplex;
  class Reader {
    ReadFunction f;
    std::shared_ptr<Socket> conn;

  public:
    Reader(ReadFunction f, std::shared_ptr<Socket> conn)
        : f{std::move(f)}, conn{std::move(conn)} {}
    bool operator()() {
      std::string data(512, '\0');
      auto read = ::read(conn->fd(), data.data(), data.size());
      if (read < 0) {
        throw errno;
      }
      data.resize(read);
      f(data);
      return false;
    }
  };
  class Writer {
    WriteFunction f;
    std::string data_;
    int index{};
    std::shared_ptr<Socket> conn;

  public:
    Writer(WriteFunction f, std::string data, std::shared_ptr<Socket> conn)
        : f{std::move(f)}, data_{std::move(data)},
          conn{std::move(conn)} {}
    bool operator()() {
      auto remaining = std::string_view{data_};
      remaining = remaining.substr(index);
      auto written = ::write(conn->fd(), remaining.data(), remaining.size());
      if (written < 0) {
        throw errno;
      }
      if (written < std::ssize(remaining)) {
        index += written;
        return true;
      }
      f();
      return false;
    }
  };

public:
  class Conn {
    Monitor *mon;
    std::shared_ptr<Socket> conn;

  public:
    Conn(Monitor *mon, std::shared_ptr<Socket> conn) : mon{mon}, conn{conn} {}
    void read(ReadFunction f) { mon->onRead(Reader(std::move(f), conn)); }
    void write(std::string data, WriteFunction f) {
      mon->onWrite(Writer(std::move(f), std::move(data), conn));
    }
  };

  class Server {
    TcpServer server_;
    Multiplex mp;
    Monitor *serverMonitor_;
    std::function<void(Conn)> onAccept_;

  public:
    Server(TcpServer server, std::function<void(Conn)> onAccept)
        : server_{std::move(server)}, serverMonitor_{mp.monitor(server_.fd())},
          onAccept_{std::move(onAccept)} {
      serverMonitor_->onRead([&]() {
        auto conn = server_.accept();
        auto mon = mp.monitor(conn->fd());
        auto conn2 = Conn{mon, conn};
        onAccept_(std::move(conn2));
        return true;
      });
    }
    void run() { mp.run(); }
  };
};
