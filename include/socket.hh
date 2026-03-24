#pragma once

#include <cstring>
#include <exception>
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
  AddressInfo(const AddressInfo &) = delete;
  AddressInfo(AddressInfo &&) = delete;
  AddressInfo &operator=(const AddressInfo &) = delete;
  AddressInfo &operator=(AddressInfo &&) = delete;
  ~AddressInfo() { freeaddrinfo(addressInfo_); }

  auto operator->() { return addressInfo_; }
};

class Socket {
  int f_socket;

public:
  Socket(AddressInfo *addressInfo, int type, int proto) {
    f_socket = socket((*addressInfo)->ai_family, type, proto);
    if (f_socket == -1) {
      throw std::bad_alloc();
    }
  }
  ~Socket() { close(f_socket); }
  operator int() const { return f_socket; }
};

// class UdpServer {
//   AddressInfo addressInfo_;
//   Socket socket_;
//   constexpr static addrinfo hints = [] {
//     addrinfo h{};
//     h.ai_family = AF_UNSPEC;
//     h.ai_socktype = SOCK_DGRAM;
//     h.ai_protocol = IPPROTO_UDP;
//     return h;
//   }();
//
// public:
//   UdpServer(std::string &address, int port)
//       : addressInfo_{address, port, &hints},
//         socket_{addressInfo_, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP} {
//     if (bind(socket_, addressInfo_->ai_addr, addressInfo_->ai_addrlen) != 0)
//     {
//       throw std::bad_alloc{};
//     }
//   }
//
//   auto echo() {
//     auto buf = std::array<char, 512>{};
//     auto clientAddress = sockaddr{};
//     auto clientAddressSize = socklen_t{sizeof(clientAddress)};
//     auto r = recvfrom(socket_, buf.data(), buf.size(), 0, &clientAddress,
//                       &clientAddressSize);
//     if (r != -1) {
//       auto n =
//           sendto(socket_, buf.data(), r, 0, &clientAddress,
//           clientAddressSize);
//       if (n < 0) {
//         throw std::bad_exception{};
//       }
//     }
//   }
// };

class TcpServer;
class TcpConnection {
  int fd_;
  friend class TcpServer;

public:
  TcpConnection(int fd) : fd_{fd} {}
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

  [[nodiscard]] int fd() const { return fd_; }
  auto echo() {
    auto buf = std::array<char, 512>{};
    auto r = recv(fd_, buf.data(), buf.size(), 0);
    if (r != -1) {
      auto n = send(fd_, buf.data(), r, 0);
      if (n < 0) {
        throw std::bad_exception{};
      }
    } else {
      auto errorCode = errno;
      auto msg = strerror_r(errno, buf.data(), buf.size());
      std::print("Error reading fd {}, {}: {}", fd_, errorCode, msg);
    }
  }
};

class TcpServer {
  std::unique_ptr<AddressInfo> addressInfo_;
  std::unique_ptr<Socket> socket_;
  constexpr static addrinfo hints = [] {
    addrinfo h{};
    h.ai_family = AF_UNSPEC;
    h.ai_socktype = SOCK_STREAM;
    h.ai_protocol = IPPROTO_TCP;
    return h;
  }();

public:
  TcpServer(std::string &address, int port)
      : addressInfo_{std::make_unique<AddressInfo>(address, port, &hints)},
        socket_{std::make_unique<Socket>(
            addressInfo_.get(), SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP)} {
    if (bind(*socket_, (*addressInfo_)->ai_addr, (*addressInfo_)->ai_addrlen) !=
        0) {
      throw std::bad_alloc{};
    }
    if (listen(*socket_, 0) != 0) {
      throw std::bad_alloc{};
    }
  }

  int fd() const { return *socket_; }

  auto accept() {
    auto fd = ::accept(*socket_, nullptr, nullptr);
    return std::make_shared<TcpConnection>(fd);
  }
};

template <typename Action> class Monitor;
template <typename Action> class Multiplex;

template <typename Action> class Monitor {
  int fd_;
  Multiplex<Action> *mp_;
  std::optional<Action> readReady_;
  std::optional<Action> writeReady_;
  friend class Multiplex<Action>;

public:
  Monitor(int fd, Multiplex<Action> *mp) : fd_{fd}, mp_{mp} {}
  void onRead(Action f);
  void onWrite(Action f);
  void completeRead();
  void completeWrite();
};

template <typename Action> class Multiplex {
  std::map<int, Monitor<Action>> sockets_{};
  std::map<int, int> fdToIndex_{};
  std::vector<pollfd> fds_{};

public:
  void doPoll();
  void run();
  Monitor<Action> *monitor(int fd);
  void enableRead(int fd);
  void enableWrite(int fd);
  void disableRead(int fd);
  void disableWrite(int fd);
};

template <typename Action> void Monitor<Action>::onRead(Action f) {
  if (readReady_) {
    throw std::logic_error{"read already pending"};
  }
  readReady_ = std::move(f);
  mp_->enableRead(fd_);
}
template <typename Action> void Monitor<Action>::onWrite(Action f) {
  if (writeReady_) {
    throw std::logic_error{"write already pending"};
  }
  writeReady_ = std::move(f);
  mp_->enableWrite(fd_);
}
template <typename Action> void Monitor<Action>::completeRead() {
  mp_->disableRead(fd_);
  readReady_ = {};
}
template <typename Action> void Monitor<Action>::completeWrite() {
  mp_->disableWrite(fd_);
  writeReady_ = {};
}

template <typename Action> void Multiplex<Action>::doPoll() {
  auto n = poll(fds_.data(), fds_.size(), -1);
  if (n <= 0) {
    return;
  }
  auto N = std::ssize(fds_);
  for (auto i = 0; i < N; ++i) {
    auto &fd = fds_[i];
    auto monitor = sockets_.at(fd.fd);
    if (fd.revents & POLLIN) {
      if (!monitor.readReady_ || !(*monitor.readReady_)()) {
        fd.events &= ~POLLIN;
      }
    }
    if (fd.revents & POLLOUT) {
      if (!monitor.writeReady_ || !(*monitor.writeReady_)()) {
        fd.events &= ~POLLOUT;
      }
    }
  }
}

template <typename Action> void Multiplex<Action>::run() {
  for (;;) {
    doPoll();
  }
}

template <typename Action> Monitor<Action> *Multiplex<Action>::monitor(int fd) {
  sockets_.emplace(fd, Monitor<Action>{fd, this});
  fds_.emplace_back(fd, 0, 0);
  fdToIndex_.emplace(fd, fds_.size() - 1);
  return &sockets_.at(fd);
}

template <typename Action> void Multiplex<Action>::enableRead(int fd) {
  fds_[fdToIndex_[fd]].events |= POLLIN;
}

template <typename Action> void Multiplex<Action>::enableWrite(int fd) {
  fds_[fdToIndex_[fd]].events |= POLLOUT;
}

template <typename Action> void Multiplex<Action>::disableRead(int fd) {
  fds_[fdToIndex_[fd]].events &= ~POLLIN;
}

template <typename Action> void Multiplex<Action>::disableWrite(int fd) {
  fds_[fdToIndex_[fd]].events &= ~POLLOUT;
}

class TcpAsio {
  using ReadFunction = std::function<void(std::string &)>;
  using WriteFunction = std::function<void()>;
  using ReadyFunction = std::function<bool()>;

  Multiplex<ReadyFunction> multiplex;
  class Reader {
    ReadFunction f;
    std::shared_ptr<TcpConnection> conn;

  public:
    Reader(ReadFunction f, std::shared_ptr<TcpConnection> conn)
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
    std::string_view remaining;
    std::shared_ptr<TcpConnection> conn;

  public:
    Writer(WriteFunction f, std::string data,
           std::shared_ptr<TcpConnection> conn)
        : f{std::move(f)}, data_{std::move(data)}, remaining{data},
          conn{std::move(conn)} {}
    bool operator()() {
      auto written = ::write(conn->fd(), remaining.data(), remaining.size());
      if (written < 0) {
        throw errno;
      }
      if (written < std::ssize(remaining)) {
        remaining = remaining.substr(written);
        return true;
      }
      f();
      return false;
    }
  };

public:
  class Conn {
    Monitor<ReadyFunction> *mon;
    std::shared_ptr<TcpConnection> conn;

  public:
    Conn(Monitor<ReadyFunction> *mon, std::shared_ptr<TcpConnection> conn)
        : mon{mon}, conn{conn} {}
    void read(ReadFunction f) { mon->onRead(Reader(std::move(f), conn)); }
    void write(std::string data, WriteFunction f) {
      mon->onWrite(Writer(std::move(f), std::move(data), conn));
    }
  };

  class Server {
    std::unique_ptr<TcpServer> server_;
    Multiplex<ReadyFunction> mp;
    Monitor<ReadyFunction> *serverMonitor_;

  public:
    Server(std::unique_ptr<TcpServer> server,
           std::function<void(Conn)> onAccept)
        : server_{std::move(server)}, serverMonitor_{mp.monitor(server->fd())} {
      serverMonitor_->onRead([&]() {
        auto conn = server->accept();
        auto mon = mp.monitor(conn->fd());
        auto conn2 = Conn{mon, conn};
        onAccept(std::move(conn2));
        return true;
      });
    }
    void run() { mp.run(); }
  };
};
