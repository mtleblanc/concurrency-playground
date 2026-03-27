#pragma once

#include "socket.hh"
#include <coroutine>
#include <system_error>
#include <utility>

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
  AcceptAwaitable(std::shared_ptr<TcpAsio::Conn> conn)
      : conn{std::move(conn)} {}
  auto operator co_await() {
    struct Awaiter {
      AcceptAwaitable &awaitable;
      std::error_code result{};

      bool await_ready() const noexcept { return false; }
      void
      await_suspend([[maybe_unused]] std::coroutine_handle<> handle) noexcept {}

      std::error_code await_resume() const noexcept { return result; }
    };
    return Awaiter{*this};
  }

private:
  std::shared_ptr<TcpAsio::Conn> conn;
};
