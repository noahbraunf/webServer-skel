#ifndef SOCKET_H_
#define SOCKET_H_
#include <concepts>
#include <cstring>
#include <netinet/in.h>
#include <optional>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

struct sockaddr;
struct sockaddr_in;
struct pollfd;

namespace wnet { // wrapper for network utilities
using error_code = std::optional<std::system_error>;

class SocketAddr;
class Socket;

template <typename T>
concept SocketLike = requires(T t) {
  { t.fd() } -> std::convertible_to<int>;
  { t.isValid() } -> std::convertible_to<bool>;
};

class FileDescriptor {
public:
  constexpr FileDescriptor() noexcept = default;
  constexpr explicit FileDescriptor(int fd) noexcept : _fd(fd) {}

  [[nodiscard]] constexpr int get() const noexcept { return _fd; }
  [[nodiscard]] constexpr int isValid() const noexcept { return _fd >= 0; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return isValid();
  }

  constexpr void reset(int fd = -1) { _fd = fd; }

private:
  int _fd{-1};
};

class SocketAddr {};

} // namespace wnet
#endif
