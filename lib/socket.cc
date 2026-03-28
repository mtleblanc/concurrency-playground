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

TcpServer::TcpServer(std::string &address, int port)
    : socket_{AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP} {
  auto addressInfo_ = AddressInfo{address, port, &hints};
  if (::bind(socket_, addressInfo_->ai_addr, addressInfo_->ai_addrlen)) {
    throw std::bad_alloc{};
  }
  if (::listen(socket_, 0)) {
    throw std::bad_alloc{};
  }
}

Result<std::shared_ptr<Socket>> TcpServer::accept() const {
  auto newFd = ::accept(socket_, nullptr, nullptr);
  if (newFd < 0) {
    return std::unexpected{std::error_code{errno, std::system_category()}};
  }
  return std::make_shared<Socket>(newFd);
}

void Monitor::onRead(Action f) {
  if (readReady_) {
    throw std::logic_error{"read already pending"};
  }
  readReady_ = std::move(f);
  mp_->enableRead(fd_);
}
void Monitor::onWrite(Action f) {
  if (writeReady_) {
    throw std::logic_error{"write already pending"};
  }
  writeReady_ = std::move(f);
  mp_->enableWrite(fd_);
}
void Monitor::doRead() {
  mp_->disableRead(fd_);
  if (!readReady_) {
    return;
  }
  auto f = std::move(*readReady_);
  readReady_ = {};
  if (f()) {
    onRead(std::move(f));
  }
}
void Monitor::doWrite() {
  mp_->disableWrite(fd_);
  if (!writeReady_) {
    return;
  }
  auto f = std::move(*writeReady_);
  writeReady_ = {};
  if (f()) {
    onWrite(std::move(f));
  }
}

void Multiplex::doPoll() {
  auto n = poll(fds_.data(), fds_.size(), -1);
  if (n <= 0) {
    return;
  }
  auto N = std::ssize(fds_);
  for (auto i = 0; i < N; ++i) {
    auto &fd = fds_[i];
    auto &monitor = sockets_.at(fd.fd);
    if (fd.revents & POLLIN) {
      monitor->doRead();
    }
    if (fd.revents & POLLOUT) {
      monitor->doWrite();
    }
  }
}

void Multiplex::run() {
  for (;;) {
    doPoll();
  }
}

std::shared_ptr<Monitor> Multiplex::monitor(int fd) {
  sockets_.emplace(fd, std::make_shared<Monitor>(Monitor{fd, this}));
  fds_.emplace_back(fd, 0, 0);
  fdToIndex_.emplace(fd, fds_.size() - 1);
  return sockets_.at(fd);
}

void Multiplex::enableRead(int fd) { fds_[fdToIndex_[fd]].events |= POLLIN; }

void Multiplex::enableWrite(int fd) { fds_[fdToIndex_[fd]].events |= POLLOUT; }

void Multiplex::disableRead(int fd) { fds_[fdToIndex_[fd]].events &= ~POLLIN; }

void Multiplex::disableWrite(int fd) {
  fds_[fdToIndex_[fd]].events &= ~POLLOUT;
}

struct TcpAsio::Reader {
  ReadFunction f;
  std::shared_ptr<Socket> conn;
  Reader(ReadFunction f, std::shared_ptr<Socket> conn)
      : f{std::move(f)}, conn{std::move(conn)} {}
  bool operator()();
};

struct TcpAsio::Writer {
  WriteFunction f;
  std::string data_;
  int index{};
  std::shared_ptr<Socket> conn;
  Writer(WriteFunction f, std::string data, std::shared_ptr<Socket> conn)
      : f{std::move(f)}, data_{std::move(data)}, conn{std::move(conn)} {}
  bool operator()();
};

struct TcpAsio::Acceptor {
  AcceptFunction f_;
  TcpServer *conn_;
  Multiplex *mp_;
  Acceptor(AcceptFunction f, TcpServer *conn, Multiplex *mp)
      : f_{std::move(f)}, conn_{conn}, mp_{mp} {}
  bool operator()();
};

bool TcpAsio::Reader::operator()() {
  std::string data(512, '\0');
  auto read = ::recv(conn->fd(), data.data(), data.size(), MSG_DONTWAIT);
  if (read < 0) {
    f(std::error_code{errno, std::system_category()}, data);
    return false;
  }
  data.resize(read);
  f(std::error_code{}, data);
  return false;
}

bool TcpAsio::Writer::operator()() {
  auto remaining = std::string_view{data_};
  remaining = remaining.substr(index);
  auto written =
      ::send(conn->fd(), remaining.data(), remaining.size(), MSG_DONTWAIT);
  if (written < 0) {
    f(std::error_code{errno, std::system_category()});
    return false;
  }
  if (written < std::ssize(remaining)) {
    index += written;
    return true;
  }
  f(std::error_code{});
  return false;
}

bool TcpAsio::Acceptor::operator()() {
  auto res = conn_->accept().transform([this](auto conn) {
    return std::make_shared<Conn>(mp_->monitor(conn->fd()), conn);
  });
  f_(res.error_or({}), res.value_or(std::shared_ptr<Conn>{}));
  return false;
}

void TcpAsio::Conn::read(ReadFunction f) {
  mon_->onRead(Reader(std::move(f), conn_));
}
void TcpAsio::Conn::write(std::string data, WriteFunction f) {
  mon_->onWrite(Writer(std::move(f), std::move(data), conn_));
}
void TcpAsio::ReactorServer::accept(AcceptFunction f) {
  mon_->onRead(Acceptor(std::move(f), &server_, mp_));
}

TcpAsio::Server::Server(TcpServer server, std::function<void(Conn)> onAccept)
    : server_{std::move(server)}, serverMonitor_{mp.monitor(server_.fd())},
      onAccept_{std::move(onAccept)} {
  serverMonitor_->onRead([&]() {
    auto res = server_.accept().transform([this](auto conn) {
      auto conn2 = Conn{mp.monitor(conn->fd()), conn};
      return Conn{mp.monitor(conn->fd()), conn};
    });
    if (res) {
      onAccept_(res.value());
    }
    return true;
  });
}
} // namespace Asio
