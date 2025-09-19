#ifndef SOCKET_H_
#define SOCKET_H_
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <netinet/in.h>
#include <optional>

#include <arpa/inet.h>
#include <fcntl.h>
#include <span>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>

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

  FileDescriptor(const FileDescriptor &) = delete;
  FileDescriptor &operator=(const FileDescriptor &) = delete;

  FileDescriptor(FileDescriptor &&other) noexcept;
  FileDescriptor &operator=(FileDescriptor &&other) noexcept;

  ~FileDescriptor();

  [[nodiscard]] constexpr int get() const noexcept { return _fd; }
  [[nodiscard]] constexpr int isValid() const noexcept { return _fd >= 0; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return isValid();
  }

  constexpr void reset(int fd = -1);

private:
  int _fd{-1};
};

class SocketAddr {
public:
  static inline const std::string LOCALHOST = "127.0.0.1";
  SocketAddr() noexcept;
  SocketAddr(std::string_view ip, uint16_t port);

  [[nodiscard]] const sockaddr *asCType() const noexcept;
  [[nodiscard]] sockaddr *asCType() noexcept;
  [[nodiscard]] std::size_t size() const noexcept;

  [[nodiscard]] uint16_t port() const noexcept;
  [[nodiscard]] std::string ip() const;
  [[nodiscard]] uint32_t ipValue() const noexcept;

  [[nodiscard]] bool operator==(const SocketAddr &otherAddr) const noexcept;
  [[nodiscard]] bool
  operator<=>(const SocketAddr &otherAddr) const noexcept = default;

  [[nodiscard]] std::string toString() const;

private:
  struct Impl; // the underlying socket from libc
  std::unique_ptr<Impl> _impl;
  friend class Socket;
};

struct SocketOptions {
  // TODO: figure out which options I need to add;
};

class Socket {
public:
  enum class Type {
    TCP,
    UDP, // TODO: add udp support (not needed for this lab)
  };

  enum class State { Listen, Bind, Recv, Accept };

  explicit Socket(Type type = Type::TCP);
  Socket(Socket &&other) noexcept = default;
  Socket &operator=(Socket &&other) noexcept = default;
  ~Socket() noexcept;

  Socket(const Socket &) = delete;
  Socket &operator=(const Socket &) = delete;

  [[nodiscard]] static std::optional<Socket> create(Type type = Type::TCP);
  [[nodiscard]] static std::optional<Socket> createBind(const SocketAddr &addr,
                                                        Type type = Type::TCP);

  // TODO: add support for configuration with SocketOptions

  [[nodiscard]] bool bind(const SocketAddr &addr);
  [[nodiscard]] bool listen(int backlog = 128);
  [[nodiscard]] std::optional<std::pair<Socket, SocketAddr>> accept();
  [[nodiscard]] bool connect(const SocketAddr &addr);

  [[nodiscard]] std::optional<std::size_t>
  send(std::span<const std::byte> data);
  [[nodiscard]] std::optional<std::string>
  send(std::size_t maxStringSize = 4096);
  [[nodiscard]] std::optional<std::size_t>
  sendTo(std::span<const std::byte> data, const SocketAddr &addr);

  [[nodiscard]] std::optional<std::size_t>
  recv(std::span<const std::byte> buffer);
  [[nodiscard]] std::optional<std::size_t> recv(std::string_view data);
  [[nodiscard]] std::optional<std::pair<std::size_t, SocketAddr>>
  recvFrom(std::span<std::byte> buffer);

  [[nodiscard]] bool isValid() const noexcept;
  [[nodiscard]] int fd() const noexcept;
  [[nodiscard]] State state() const noexcept;
  [[nodiscard]] Type type() const noexcept;

  [[nodiscard]] std::optional<SocketAddr> localAddress() const;
  [[nodiscard]] std::optional<SocketAddr> remoteAddress() const;

  bool shutdown();
  void close() noexcept;

  [[nodiscard]] error_code lastError() const noexcept;

private:
  struct Impl; // socket underlying implementation (so I can work on both my mac
               // device and ubuntu)
  std::unique_ptr<Impl> _impl;
};

class Poll {};

} // namespace wnet
#endif
