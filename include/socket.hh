#pragma once

#include "sockets_raii.hh"

#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <netdb.h>
#include <poll.h>
#include <print>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

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
  TcpServer(std::string &address, int port);
  int fd() const { return socket_; }
  std::pair<std::error_code, std::shared_ptr<Socket>> accept() const;
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
  Monitor(const Monitor &) = delete;
  Monitor(Monitor &&) = default;
  Monitor &operator=(const Monitor &) = delete;
  Monitor &operator=(Monitor &&) = default;
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

class TcpAsio {
public:
  class Conn;

private:
  using ReadFunction = std::function<void(std::error_code, std::string &)>;
  using WriteFunction = std::function<void(std::error_code)>;
  using ReadyFunction = std::function<bool()>;
  using AcceptFunction =
      std::function<void(std::error_code, std::shared_ptr<Conn>)>;

  Multiplex multiplex;

  class Reader {
    ReadFunction f;
    std::shared_ptr<Socket> conn;

  public:
    Reader(ReadFunction f, std::shared_ptr<Socket> conn)
        : f{std::move(f)}, conn{std::move(conn)} {}
    bool operator()();
  };

  class Writer {
    WriteFunction f;
    std::string data_;
    int index{};
    std::shared_ptr<Socket> conn;

  public:
    Writer(WriteFunction f, std::string data, std::shared_ptr<Socket> conn)
        : f{std::move(f)}, data_{std::move(data)}, conn{std::move(conn)} {}
    bool operator()();
  };

  class Acceptor {
    AcceptFunction f_;
    TcpServer *conn_;
    Multiplex *mp_;

  public:
    Acceptor(AcceptFunction f, TcpServer *conn, Multiplex *mp)
        : f_{std::move(f)}, conn_{conn}, mp_{mp} {}
    bool operator()();
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

  class ReactorServer {
    Multiplex *mp_;
    Monitor *mon_;
    TcpServer server_;

  public:
    ReactorServer(Multiplex *mp, Monitor *mon, TcpServer server)
        : mp_{mp}, mon_{mon}, server_{std::move(server)} {}
    void accept(AcceptFunction f) {
      mon_->onRead(Acceptor(std::move(f), &server_, mp_));
    }
  };

  class Server {
    TcpServer server_;
    Multiplex mp;
    Monitor *serverMonitor_;
    std::function<void(Conn)> onAccept_;

  public:
    Server(TcpServer server, std::function<void(Conn)> onAccept);
    void run() { mp.run(); }
  };
};
