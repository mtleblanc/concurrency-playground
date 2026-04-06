#include "proactor.hh"

namespace Asio {
void ConnectedSocket::read(char *buffer, int bufferSize,
                           Callback<int> callback) {
  ctx_->readable(handle_, [=]([[maybe_unused]] auto &ctx,
                              [[maybe_unused]] auto handle, auto &socket) {
    callback(socket.read(buffer, bufferSize));
  });
}

void ConnectedSocket::write(const char *buffer, int bufferSize,
                            Callback<int> callback) {
  ctx_->writeable(handle_, [=]([[maybe_unused]] auto &ctx,
                               [[maybe_unused]] auto handle, auto &socket) {
    callback(socket.write(buffer, bufferSize));
  });
}

void ListeningSocket::accept([[maybe_unused]] sockaddr *sockaddr,
                             [[maybe_unused]] socklen_t *socklen,
                             Callback<ConnectedSocket> callback) {
  ctx_->readable(handle_,
                 [=](auto &ctx, [[maybe_unused]] auto handle, auto &socket) {
                   auto res = socket.accept();
                   auto maybeConnected = res.transform([&ctx](auto &newSocket) {
                     auto newHandle = ctx.watch(std::move(newSocket));
                     return ConnectedSocket{ctx, newHandle};
                   });
                   callback(std::move(maybeConnected));
                 });
}
} // namespace Asio
