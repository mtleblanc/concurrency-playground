#pragma once

#include "sockets_raii.hh"

#include <cstring>
#include <expected>
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

namespace Asio {

template <typename T> using Result = std::expected<T, std::error_code>;

class TcpServer {
public:
  TcpServer(std::string &address, int port);

  int fd() const { return socket_; }
  Result<std::shared_ptr<Socket>> accept() const;

  Socket socket_;
};

class Monitor;
class Multiplex;

class Monitor {
public:
  Monitor(const Monitor &) = delete;
  Monitor(Monitor &&) = default;
  Monitor &operator=(const Monitor &) = delete;
  Monitor &operator=(Monitor &&) = default;
  ~Monitor() = default;

  using Action = std::function<bool()>;
  void onReadReady(Action f);
  void onWriteReady(Action f);
  void read();
  void write();

private:
  friend class Multiplex;
  Monitor(int fd, Multiplex *mp) : fd_{fd}, mp_{mp} {}

  int fd_;
  Multiplex *mp_;
  std::optional<Action> onReadReady_;
  std::optional<Action> onWriteReady_;
};

class Multiplex {
public:
  void doPoll();
  void run();
  std::shared_ptr<Monitor> monitor(int fd);
  void enableRead(int fd);
  void enableWrite(int fd);
  void disableRead(int fd);
  void disableWrite(int fd);

private:
  std::map<int, std::shared_ptr<Monitor>> sockets_{};
  std::map<int, int> fdToIndex_{};
  std::vector<pollfd> fds_{};
};

class ReactiveAsio {
  class Conn;
  using ReadFunction = std::function<void(std::error_code, std::string &)>;
  using WriteFunction = std::function<void(std::error_code)>;
  using ReadyFunction = std::function<bool()>;
  using AcceptFunction =
      std::function<void(std::error_code, std::shared_ptr<Conn>)>;

  class Conn {
  public:
    Conn(std::shared_ptr<Monitor> mon, std::shared_ptr<Socket> socket)
        : mon_{mon}, socket_{socket} {}

    void read(ReadFunction f);
    void write(std::string data, WriteFunction f);

  private:
    std::shared_ptr<Monitor> mon_;
    std::shared_ptr<Socket> socket_;
  };

  class ReactorServer {
  public:
    ReactorServer(Multiplex *mp, std::shared_ptr<Monitor> mon, TcpServer server)
        : mp_{mp}, mon_{mon}, server_{std::move(server)} {}

    void accept(AcceptFunction f);

  private:
    Multiplex *mp_;
    std::shared_ptr<Monitor> mon_;
    TcpServer server_;
  };

private:
  struct Reader;
  struct Writer;
  struct Acceptor;

  Multiplex multiplex_;
};

class TcpAsio {
public:
  class Conn;
  using ReadFunction = std::function<void(std::error_code, std::string &)>;
  using WriteFunction = std::function<void(std::error_code)>;
  using ReadyFunction = std::function<bool()>;
  using AcceptFunction =
      std::function<void(std::error_code, std::shared_ptr<Conn>)>;

  class Conn {
  public:
    Conn(std::shared_ptr<Monitor> mon, std::shared_ptr<Socket> socket)
        : mon_{mon}, socket_{socket} {}

    void read(ReadFunction f);
    void write(std::string data, WriteFunction f);

  private:
    std::shared_ptr<Monitor> mon_;
    std::shared_ptr<Socket> socket_;
  };

  class ReactorServer {
  public:
    ReactorServer(Multiplex *mp, std::shared_ptr<Monitor> mon, TcpServer server)
        : mp_{mp}, mon_{mon}, server_{std::move(server)} {}

    void accept(AcceptFunction f);

  private:
    Multiplex *mp_;
    std::shared_ptr<Monitor> mon_;
    TcpServer server_;
  };

  class Server {
  public:
    Server(TcpServer server, std::function<void(Conn)> onAccept);

    void run() { mp.run(); }

  private:
    TcpServer server_;
    Multiplex mp;
    std::shared_ptr<Monitor> serverMonitor_;
    std::function<void(Conn)> onAccept_;
  };

private:
  struct Reader;
  struct Writer;
  struct Acceptor;

  Multiplex multiplex_;
};
} // namespace Asio
