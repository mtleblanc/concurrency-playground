#include "reactor.hh"
#include <ranges>

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
                       [](const auto &pollfd) { return pollfd.revents != 0; }));
    for (const auto &pollfd : ready) {
      if (pollfd.revents & POLLOUT) {
        doWrite(pollfd.fd);
      }
      if (pollfd.revents & POLLIN) {
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
  pollFds_[slot.fdsIndex].events |= POLLIN;
}

void IOContext::writeable(Handle handle, ReadyAction action) {
  auto &slot = socketIndices.at(handle.fd);
  if (!slot.occupied || slot.generation != handle.generation) {
    throw std::invalid_argument{"bad handle"};
  }
  slot.writeable = action;
  pollFds_[slot.fdsIndex].events |= POLLOUT;
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

auto IOContext::watch(Socket s) -> Handle {
  auto fd = s.fd();
  auto &slot = socketIndices[fd];
  assert(!slot.occupied);
  slot.socket = std::move(s);
  slot.occupied = true;
  pollFds_.emplace_back(fd, 0, 0);
  slot.fdsIndex = pollFds_.size() - 1;
  return {fd, slot.generation};
}

void IOContext::doRead(int fd) {
  assert(socketIndices.contains(fd));
  auto &slot = socketIndices.at(fd);
  auto &pollFd = pollFds_[slot.fdsIndex];
  assert(pollFd.fd == fd);
  pollFd.events &= ~POLLIN;
  auto readable = std::exchange(slot.readable, nullptr);
  if (readable != nullptr) {
    readable(*this, slot.handle(), slot.socket);
  }
}

void IOContext::doWrite(int fd) {
  assert(socketIndices.contains(fd));
  auto &slot = socketIndices.at(fd);
  auto &pollFd = pollFds_[slot.fdsIndex];
  assert(pollFd.fd == fd);
  pollFd.events &= ~POLLOUT;
  auto writeable = std::exchange(slot.writeable, nullptr);
  if (writeable != nullptr) {
    writeable(*this, slot.handle(), slot.socket);
  }
}

} // namespace Asio
