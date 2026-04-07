#pragma once

#include "proactor.hh"
#include "socket.hh"
#include <coroutine>
#include <system_error>
#include <utility>

namespace Asio {
class AsioCoroutine {
public:
  struct Promise {
    AsioCoroutine get_return_object() { return AsioCoroutine{}; }
    void unhandled_exception() noexcept {}
    void return_void() noexcept {}
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
  };
  using promise_type = Promise;
};

class ProactorReadAwaitable {
public:
  ProactorReadAwaitable(std::shared_ptr<ConnectedSocket> socket, char *data,
                        ssize_t dataSize)
      : socket_{std::move(socket)}, data_{data}, dataSize_{dataSize} {}
  auto operator co_await() {
    struct Awaiter {
      ProactorReadAwaitable &awaitable;
      std::expected<int, std::error_code> result{};

      bool await_ready() const noexcept { return false; }
      void await_suspend(std::coroutine_handle<> handle) noexcept {
        awaitable.socket_->read(
            awaitable.data_, awaitable.dataSize_,
            [this, handle](std::expected<int, std::error_code> res) {
              result = res;
              handle.resume();
            });
      }
      std::expected<int, std::error_code> await_resume() { return result; }
    };
    return Awaiter{*this};
  }

private:
  std::shared_ptr<ConnectedSocket> socket_;
  char *data_;
  ssize_t dataSize_;
};

class ProactorWriteAwaitable {
public:
  ProactorWriteAwaitable(std::shared_ptr<ConnectedSocket> socket, char *data,
                         ssize_t dataSize)
      : socket_{std::move(socket)}, data_{data}, dataSize_{dataSize} {}
  auto operator co_await() {
    struct Awaiter {
      ProactorWriteAwaitable &awaitable;
      std::expected<int, std::error_code> result{};

      bool await_ready() const noexcept { return false; }
      void await_suspend(std::coroutine_handle<> handle) noexcept {
        awaitable.socket_->write(
            awaitable.data_, awaitable.dataSize_,
            [this, handle](std::expected<int, std::error_code> res) {
              result = res;
              handle.resume();
            });
      }
      std::expected<int, std::error_code> await_resume() { return result; }
    };
    return Awaiter{*this};
  }

private:
  std::shared_ptr<ConnectedSocket> socket_;
  char *data_;
  ssize_t dataSize_;
};

class ProactorAcceptAwaitable {
public:
  ProactorAcceptAwaitable(std::shared_ptr<ListeningSocket> socket)
      : socket_{std::move(socket)} {}
  auto operator co_await() {
    struct Awaiter {
      ProactorAcceptAwaitable &awaitable;
      std::expected<ConnectedSocket, std::error_code> result{};

      bool await_ready() const noexcept { return false; }
      void await_suspend(std::coroutine_handle<> handle) noexcept {
        awaitable.socket_->accept(
            nullptr, nullptr,
            [this,
             handle](std::expected<ConnectedSocket, std::error_code> res) {
              result = std::move(res);
              handle.resume();
            });
      }
      std::expected<ConnectedSocket, std::error_code> await_resume() {
        return std::move(result);
      }
    };
    return Awaiter{*this};
  }

private:
  std::shared_ptr<ListeningSocket> socket_;
};

class ReadAwaitable {
public:
  ReadAwaitable(std::shared_ptr<TcpAsio::Conn> conn) : conn{std::move(conn)} {}
  auto operator co_await() {
    struct Awaiter {
      ReadAwaitable &awaitable;
      std::pair<std::error_code, std::string> result{};

      bool await_ready() const noexcept { return false; }
      void await_suspend(std::coroutine_handle<> handle) noexcept {
        awaitable.conn->read([this, handle](auto ec, auto &data) {
          result = {ec, std::move(data)};
          handle.resume();
        });
      }

      std::pair<std::error_code, std::string> await_resume() const noexcept {
        return result;
      }
    };
    return Awaiter{*this};
  }

private:
  std::shared_ptr<TcpAsio::Conn> conn;
};

class WriteAwaitable {
public:
  WriteAwaitable(std::shared_ptr<TcpAsio::Conn> conn, std::string data)
      : conn{std::move(conn)}, data{std::move(data)} {}
  auto operator co_await() {
    struct Awaiter {
      WriteAwaitable &awaitable;
      std::error_code result{};

      bool await_ready() const noexcept { return false; }
      void await_suspend(std::coroutine_handle<> handle) noexcept {
        awaitable.conn->write(std::move(awaitable.data),
                              [this, handle](auto ec) {
                                result = std::move(ec);
                                handle.resume();
                              });
      }

      std::error_code await_resume() const noexcept { return result; }
    };
    return Awaiter{*this};
  }

private:
  std::shared_ptr<TcpAsio::Conn> conn;
  std::string data;
};

class AcceptAwaitable {
public:
  AcceptAwaitable(std::shared_ptr<TcpAsio::ReactorServer> conn)
      : conn_{std::move(conn)} {}
  auto operator co_await() {
    struct Awaiter {
      using Result = std::pair<std::error_code, std::shared_ptr<TcpAsio::Conn>>;
      AcceptAwaitable &awaitable;
      Result result{};

      bool await_ready() const noexcept { return false; }
      void
      await_suspend([[maybe_unused]] std::coroutine_handle<> handle) noexcept {
        awaitable.conn_->accept([this, handle](auto ec, auto conn) {
          result = {ec, conn};
          handle.resume();
        });
      }

      Result await_resume() const noexcept { return result; }
    };
    return Awaiter{*this};
  }

private:
  std::shared_ptr<TcpAsio::ReactorServer> conn_;
};
} // namespace Asio
