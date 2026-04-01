#include "sockets_raii.hh"
#include <cassert>
#include <expected>
#include <functional>
#include <optional>
#include <poll.h>
#include <ranges>
#include <system_error>

namespace Asio {

class MoveOnly {
protected:
  MoveOnly(const MoveOnly &) = delete;
  MoveOnly(MoveOnly &&) = default;
  MoveOnly &operator=(const MoveOnly &) = delete;
  MoveOnly &operator=(MoveOnly &&) = default;
  ~MoveOnly() = default;
};

template <typename T>
using Callback = std::function<void(std::expected<T, std::error_code>)>;

class IOContext : MoveOnly {
public:
  using ReadyAction = std::function<void(Socket &)>;
  struct Handle {
    int fd;
    int generation;
  };

  void run();
  void readable(Handle, ReadyAction);
  void writeable(Handle, ReadyAction);
  std::optional<std::reference_wrapper<Socket>> get(Handle);
  Handle watch(Socket);

private:
  void doRead(int);
  void doWrite(int);

  struct Slot {
    Socket socket{};
    ReadyAction readable{};
    ReadyAction writeable{};
    bool occupied{};
    int generation{};
    int fdsIndex;
  };

  std::vector<pollfd> pollFds_{};
  std::unordered_map<int, Slot> socketIndices{};
};

class ConnectedSocket : MoveOnly {
public:
  ConnectedSocket(IOContext &context, IOContext::Handle handle);
  void read(char *buffer, int bufferSize, Callback<int>);
  void write(char *buffer, int bufferSize, Callback<int>);
};

class ListeningSocket : MoveOnly {
public:
  ListeningSocket(IOContext &context, IOContext::Handle handle);
  void accept(sockaddr *, socklen_t *, Callback<Socket>);
};
} // namespace Asio

namespace Asio {
void IOContext::run() {
  for (;;) {
    auto n = ::poll(pollFds_.data(), pollFds_.size(), -1);
    if (n < 0) {
      throw std::make_error_code(static_cast<std::errc>(errno));
    }
    if (n == 0) {
      continue;
    }
    // copy so that callbacks can modify without invalidating ready set
    auto ready = std::ranges::to<std::vector>(
        pollFds_ | std::views::filter(
                       [](const auto &pollfd) { return pollfd.events != 0; }));
    for (const auto &pollfd : ready) {
      if (pollfd.events & POLLOUT) {
        doWrite(pollfd.fd);
      }
      if (pollfd.events & POLLIN) {
        doRead(pollfd.fd);
      }
    }
  }
}

void IOContext::readable(Handle handle, ReadyAction action) {
  auto &slot = socketIndices.at(handle.fd);
  if (!slot.occupied || slot.generation != handle.generation) {
    throw std::invalid_argument{"bad handle"};
  }
  slot.readable = action;
  pollFds_[slot.fdsIndex].revents |= POLLIN;
}

void IOContext::writeable(Handle handle, ReadyAction action) {
  auto &slot = socketIndices.at(handle.fd);
  if (!slot.occupied || slot.generation != handle.generation) {
    throw std::invalid_argument{"bad handle"};
  }
  slot.writeable = action;
  pollFds_[slot.fdsIndex].revents |= POLLOUT;
}

std::optional<std::reference_wrapper<Socket>> IOContext::get(Handle h) {
  if (!socketIndices.contains(h.fd)) {
    return {};
  }
  auto &socket = socketIndices.at(h.fd);
  if (!socket.occupied || h.generation != socket.generation) {
    return {};
  }
  return socket.socket;
}

void IOContext::doRead(int fd) {
  assert(socketIndices.contains(fd));
  auto &slot = socketIndices.at(fd);
  auto &pollFd = pollFds_[slot.fdsIndex];
  assert(pollFd.fd == fd);
  pollFd.revents &= ~POLLIN;
  auto readable = std::exchange(slot.readable, nullptr);
  if (readable != nullptr) {
    readable(slot.socket);
  }
}

void IOContext::doWrite(int fd) {
  assert(socketIndices.contains(fd));
  auto &slot = socketIndices.at(fd);
  auto &pollFd = pollFds_[slot.fdsIndex];
  assert(pollFd.fd == fd);
  pollFd.revents &= ~POLLOUT;
  auto writeable = std::exchange(slot.writeable, nullptr);
  if (writeable != nullptr) {
    writeable(slot.socket);
  }
}

} // namespace Asio
