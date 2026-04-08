#include "coroutine.hh"
#include "proactor.hh"
#include "reactor.hh"
#include "socket.hh"
#include <functional>
#include <memory>
#include <print>
#include <sys/socket.h>

using namespace Asio;
constexpr auto BUFSZ = 64 * 1024;

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
  auto buf = std::string(BUFSZ, 0);
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

  void handle_read(Result<int> res) {
    if (!res) {
      std::println("{}", res.error().message());
      return;
    }
    data.resize(*res);
    startWrite();
  }

  void handle_write(Result<int> res) {
    if (!res) {
      std::println("{}", res.error().message());
      return;
    }
    index += *res;
    startWrite();
  }
};

void proactor_accept_callback(ListeningSocket &listserv,
                              Result<ConnectedSocket> conn) {
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

Task proactor_coro_send_all(std::shared_ptr<ConnectedSocket> conn,
                            std::string &buf) {

  for (auto sv = std::string_view{buf}; !sv.empty();) {
    auto writeResult =
        co_await ProactorWriteAwaitable(conn, sv.data(), std::ssize(sv));
    if (!writeResult) {
      std::println("{}", writeResult.error().message());
    }
    sv = sv.substr(*writeResult);
  }
}

AsioCoroutine proactor_coro_echo(ConnectedSocket socket) {
  std::string buf(BUFSZ, 0);
  auto conn = std::make_shared<ConnectedSocket>(std::move(socket));
  for (;;) {
    buf.resize(BUFSZ);
    auto readResult =
        co_await ProactorReadAwaitable{conn, buf.data(), std::ssize(buf)};
    if (!readResult) {
      std::println("{}", readResult.error().message());
      continue;
    }
    if (*readResult == 0) {
      break;
    }
    buf.resize(*readResult);
    co_await proactor_coro_send_all(conn, buf);
  }
}

AsioCoroutine proactor_coro_listen(std::shared_ptr<ListeningSocket> listserv) {

  for (;;) {
    auto socket = co_await ProactorAcceptAwaitable{listserv};
    if (!socket) {
      std::println("{}", socket.error().message());
      continue;
    }
    proactor_coro_echo(std::move(*socket));
  }
}

[[maybe_unused]] void proactor_coro(TcpServer server) {
  IOContext ctx;
  auto serverHandle = ctx.watch(std::move(server.socket_));
  auto listserv = std::make_shared<ListeningSocket>(ctx, serverHandle);
  proactor_coro_listen(listserv);
  ctx.run();
}

int main() {
  // std::println("concurrency playground");
  auto address = std::string{"0.0.0.0"};
  auto server = TcpServer{address, 12345};
  proactor_coro(std::move(server));
}
