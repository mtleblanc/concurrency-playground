#pragma once

#include "proactor.hh"
#include <coroutine>
#include <memory>
#include <utility>

namespace Asio {

class Task {
public:
  struct FinalAwaiter {
    bool await_ready() const noexcept { return false; }
    template <typename P>
    void await_suspend(std::coroutine_handle<P> handle) noexcept {
      handle.promise().next.resume();
    }
    void await_resume() const noexcept {}
  };

  struct Promise {
    Task get_return_object() {
      return Task{std::coroutine_handle<Promise>::from_promise(*this)};
    }
    void unhandled_exception() noexcept {}
    void return_void() noexcept {}
    std::suspend_never initial_suspend() noexcept { return {}; }
    FinalAwaiter final_suspend() noexcept { return {}; }

    std::coroutine_handle<> next;
  };

  struct Awaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend([[maybe_unused]] std::coroutine_handle<> calling) {
      handle.promise().next = calling;
    }
    void await_resume() {}

    std::coroutine_handle<Promise> handle;
  };

  Awaiter operator co_await() { return {handle_}; }
  using promise_type = Promise;

  explicit Task(std::coroutine_handle<Promise> handle) : handle_{handle} {}
  ~Task() {
    if (handle_) {
      handle_.destroy();
    }
  }

private:
  std::coroutine_handle<Promise> handle_;
};

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
      Result<int> result{};

      bool await_ready() const noexcept { return false; }
      void await_suspend(std::coroutine_handle<> handle) noexcept {
        awaitable.socket_->read(awaitable.data_, awaitable.dataSize_,
                                [this, handle](Result<int> res) {
                                  result = res;
                                  handle.resume();
                                });
      }
      Result<int> await_resume() { return result; }
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
  ProactorWriteAwaitable(std::shared_ptr<ConnectedSocket> socket,
                         const char *data, ssize_t dataSize)
      : socket_{std::move(socket)}, data_{data}, dataSize_{dataSize} {}
  auto operator co_await() {
    struct Awaiter {
      ProactorWriteAwaitable &awaitable;
      Result<int> result{};

      bool await_ready() const noexcept { return false; }
      void await_suspend(std::coroutine_handle<> handle) noexcept {
        awaitable.socket_->write(awaitable.data_, awaitable.dataSize_,
                                 [this, handle](Result<int> res) {
                                   result = res;
                                   handle.resume();
                                 });
      }
      Result<int> await_resume() { return result; }
    };
    return Awaiter{*this};
  }

private:
  std::shared_ptr<ConnectedSocket> socket_;
  const char *data_;
  ssize_t dataSize_;
};

class ProactorAcceptAwaitable {
public:
  ProactorAcceptAwaitable(std::shared_ptr<ListeningSocket> socket)
      : socket_{std::move(socket)} {}
  auto operator co_await() {
    struct Awaiter {
      ProactorAcceptAwaitable &awaitable;
      Result<ConnectedSocket> result{};

      bool await_ready() const noexcept { return false; }
      void await_suspend(std::coroutine_handle<> handle) noexcept {
        awaitable.socket_->accept(nullptr, nullptr,
                                  [this, handle](Result<ConnectedSocket> res) {
                                    result = std::move(res);
                                    handle.resume();
                                  });
      }
      Result<ConnectedSocket> await_resume() { return std::move(result); }
    };
    return Awaiter{*this};
  }

private:
  std::shared_ptr<ListeningSocket> socket_;
};
} // namespace Asio
