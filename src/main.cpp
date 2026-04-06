#include "coroutine.hh"
#include "proactor.hh"
#include "reactor.hh"
#include "socket.hh"
#include <functional>
#include <memory>
#include <print>
#include <sys/socket.h>

using namespace Asio;

class EchoConnection {
  TcpAsio::Conn *conn_;

  void echo(std::string data) {
    conn_->write(std::move(data), [&](auto res) {
      if (res) {
        return;
      }
      conn_->read([&](auto res, auto data) {
        if (res) {
          return;
        }
        echo(std::move(data));
      });
    });
  }

public:
  EchoConnection(TcpAsio::Conn *conn) : conn_{conn} {
    conn_->read([&](auto res, auto data) {
      if (res) {
        return;
      }
      echo(std::move(data));
    });
  }
};

[[maybe_unused]] void runProactor() {
  auto address = std::string{"0.0.0.0"};
  auto server = TcpServer{address, 12345};
  auto connections = std::vector<std::unique_ptr<TcpAsio::Conn>>{};
  auto echoConnections = std::vector<std::unique_ptr<EchoConnection>>{};
  auto multiplexServer =
      TcpAsio::Server{std::move(server), [&](auto conn) {
                        auto connp =
                            std::make_unique<TcpAsio::Conn>(std::move(conn));
                        connections.emplace_back(std::move(connp));
                        auto c = connections.back().get();
                        auto ec = std::make_unique<EchoConnection>(c);
                        echoConnections.emplace_back(std::move(ec));
                      }};
  multiplexServer.run();
}

[[maybe_unused]] AsioCoroutine echo(std::shared_ptr<TcpAsio::Conn> conn) {
  for (;;) {
    auto [ec, data] = co_await ReadAwaitable{conn};
    if (ec) {
      co_return;
    }
    ec = co_await WriteAwaitable(conn, data);
    if (ec) {
      co_return;
    }
  }
}

[[maybe_unused]] AsioCoroutine
serve(std::shared_ptr<TcpAsio::ReactorServer> server) {
  for (;;) {
    auto [ec, conn] = co_await AcceptAwaitable(server);
    if (ec) {
      continue;
    }
    echo(conn);
  }
}

[[maybe_unused]] void coroReadWrite(TcpServer server) {
  auto multiplexServer =
      TcpAsio::Server{std::move(server), [&](auto conn) {
                        auto connp =
                            std::make_shared<TcpAsio::Conn>(std::move(conn));
                        echo(std::move(connp));
                      }};
  multiplexServer.run();
}

[[maybe_unused]] void fullCoro(TcpServer server) {
  Multiplex mp;
  auto mon = mp.monitor(server.fd());
  auto rs =
      std::make_shared<TcpAsio::ReactorServer>(&mp, mon, std::move(server));
  serve(rs);
  mp.run();
}

void read_callback(IOContext &ctx, IOContext::Handle h, Socket &s);
struct Writer {
  std::string data;
  int sent{};
  void operator()(IOContext &ctx, IOContext::Handle h, Socket &s) {
    auto sv = std::string_view{data};
    sv = sv.substr(sent);
    auto res = s.write(sv.data(), sv.size());
    if (res) {
      auto n = res.value();
      // std::println("Sent: {}", sv.substr(0, n));
      if (n < std::ssize(sv)) {
        sent += n;
        ctx.writeable(h, *this);
      } else {
        ctx.readable(h, read_callback);
      }
    }
  }
};

void read_callback(IOContext &ctx, IOContext::Handle h, Socket &s) {
  auto buf = std::string(1024, 0);
  auto res = s.read(buf.data(), buf.size());
  if (res) {
    auto n = res.value();
    if (n > 0) {
      buf.resize(n);
      // std::println("Received: {}", buf);
      ctx.writeable(h, Writer{std::move(buf)});
    } else {
      // std::println("Connecion {} closed", s.fd());
      ctx.unwatch(h);
    }
  } else {
    // std::println("Read returned {}", res.error().message());
    ctx.readable(h, read_callback);
  }
}
void connect_callback(IOContext &ctx, IOContext::Handle h, Socket &s) {
  auto res = s.accept();
  if (res) {
    auto newFd = std::move(res.value());
    // std::println("Connected fd {}", newFd.fd());
    auto newHandle = ctx.watch(std::move(newFd));
    ctx.readable(newHandle, read_callback);
  }
  ctx.readable(h, connect_callback);
}

[[maybe_unused]] void reactor(TcpServer server) {
  auto socket = std::move(server.socket_);
  IOContext ctx;
  auto serverHandle = ctx.watch(std::move(socket));
  ctx.readable(serverHandle, connect_callback);
  ctx.run();
}

static constexpr auto BUFSZ = 1024;
class proactor_echo : public std::enable_shared_from_this<proactor_echo> {
  ConnectedSocket socket;
  std::string data;
  int index{};

public:
  proactor_echo(ConnectedSocket s) : socket{std::move(s)}, data(BUFSZ, 0) {}

  void startRead() {
    index = 0;
    data.resize(BUFSZ);
    socket.read(data.data(), data.size(),
                std::bind(&proactor_echo::handle_read, shared_from_this(),
                          std::placeholders::_1));
  }

  void startWrite() {
    auto sv = std::string_view{data};
    sv = sv.substr(index);
    if (sv.empty()) {
      startRead();
      return;
    }
    socket.write(sv.data(), sv.size(),
                 std::bind(&proactor_echo::handle_write, shared_from_this(),
                           std::placeholders::_1));
  }

  void handle_read(std::expected<int, std::error_code> res) {
    if (!res) {
      std::println("{}", res.error().message());
      return;
    }
    data.resize(*res);
    startWrite();
  }

  void handle_write(std::expected<int, std::error_code> res) {
    if (!res) {
      std::println("{}", res.error().message());
      return;
    }
    index += *res;
    startWrite();
  }
};

void proactor_accept_callback(
    ListeningSocket &listserv,
    std::expected<ConnectedSocket, std::error_code> conn) {
  if (!conn) {
    std::println("{}", conn.error().message());
    return;
  }
  auto echoConnection = std::make_shared<proactor_echo>(std::move(*conn));
  echoConnection->startRead();
  listserv.accept(nullptr, nullptr,
                  std::bind(proactor_accept_callback, std::ref(listserv),
                            std::placeholders::_1));
}

[[maybe_unused]] void proactor(TcpServer server) {
  auto socket = std::move(server.socket_);
  IOContext ctx;
  auto serverHandle = ctx.watch(std::move(socket));
  auto listserv = ListeningSocket{ctx, serverHandle};
  listserv.accept(nullptr, nullptr,
                  std::bind(proactor_accept_callback, std::ref(listserv),
                            std::placeholders::_1));
  ctx.run();
}

int main() {
  // std::println("concurrency playground");
  auto address = std::string{"0.0.0.0"};
  auto server = TcpServer{address, 12345};
  proactor(std::move(server));
}
