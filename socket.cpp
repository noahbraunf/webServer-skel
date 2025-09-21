#include "socket.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <format>
#include <memory>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>

namespace wnet {
FileDescriptor::~FileDescriptor() {
  if (is_valid())
    ::close(_fd);
}

FileDescriptor::FileDescriptor(FileDescriptor &&other) noexcept
    : _fd(other._fd) {
  other._fd = -1;
}

FileDescriptor &FileDescriptor::operator=(FileDescriptor &&other) noexcept {
  if (this != &other) {
    if (is_valid()) {
      ::close(_fd);
    }
    _fd = other._fd;
    other._fd = -1;
  }
  return *this;
}

constexpr void FileDescriptor::reset(int fd) {
  if (is_valid()) {
    ::close(_fd);
  }
  _fd = fd;
}

struct SocketAddr::Impl {
  sockaddr_in addr{};

  Impl() {
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; // ipv4
  }
};

SocketAddr::SocketAddr() noexcept
    : _impl(std::make_unique<SocketAddr::Impl>()) {}

SocketAddr::SocketAddr(std::string_view ip, uint16_t port)
    : _impl(std::make_unique<SocketAddr::Impl>()) {
  _impl->addr.sin_port =
      htons(port); // must convert endianess to network endianess
  if (inet_pton(AF_INET, ip.data(), &_impl->addr.sin_addr) != 1) {
    throw std::invalid_argument(
        std::format("Invalid IPv4 address supplied: {}", ip));
  }
}

const sockaddr *SocketAddr::asCType() const noexcept {
  return reinterpret_cast<const sockaddr *>(&_impl->addr);
}

sockaddr *SocketAddr::asCType() noexcept {
  return reinterpret_cast<sockaddr *>(&_impl->addr);
}

std::size_t SocketAddr::size() const noexcept { return sizeof(_impl->addr); }

uint16_t SocketAddr::port() const noexcept {
  return ntohs(_impl->addr.sin_port); // convert to host endianess
}

std::string SocketAddr::ip() const {
  char buf[INET_ADDRSTRLEN];
  if (inet_ntop(AF_INET, &_impl->addr.sin_addr, buf, sizeof(buf)) == NULL) {
    // EAFNOSUPPORT cannot happen because AF_INET is always supported
    throw new std::length_error(
        std::format("Buffer is too small (something is really wrong because "
                    "INET_ADDRSTRLEN is incorrect). INET_ADDRSTRLEN: {}",
                    INET_ADDRSTRLEN));
  }
  return buf;
}

uint32_t SocketAddr::ipValue() const noexcept {
  return ntohl(_impl->addr.sin_addr.s_addr);
}

bool SocketAddr::operator==(const SocketAddr &other) const noexcept {
  return _impl->addr.sin_family == other._impl->addr.sin_family &&
         _impl->addr.sin_port == _impl->addr.sin_port &&
         _impl->addr.sin_addr.s_addr == other._impl->addr.sin_addr.s_addr;
}

std::string SocketAddr::to_string() const {
  return std::format("{}:{}", ip(), port());
}

struct Socket::Impl {
  FileDescriptor fd;
  Type type{Type::TCP};
  State state; // TODO: figure out starting state, probably closed or
               // uninitialized
  mutable error_code last_error;

  explicit Impl(Type t) : type(t) {}
  [[nodiscard]] bool setOption(int level, int option_name,
                               const void *option_value,
                               socklen_t option_length) {
    if (setsockopt(fd.get(), level, option_name, option_value, option_length) !=
        0) { // error occured if not zero
      last_error = std::error_code(errno, std::system_category());
      return false;
    }
    return true;
  }

  template <typename T>
  [[nodiscard]] bool setOption(int level, int option_name, const T &value) {
    return setOption(level, option_name, &value, sizeof(value));
  }
};

Socket::Socket(Socket::Type type) : _impl(std::make_unique<Impl>(type)) {
  int fd = socket(AF_INET, SOCK_STREAM, 0); // only TCP supported for now
  if (fd < 0) {
    if (_impl->last_error.has_value()) {
      _impl->last_error = std::error_code(errno, std::system_category());
      throw std::runtime_error(std::format("Unable to create socket: {}",
                                           _impl->last_error.value().what()));
    } else {
      throw std::runtime_error(
          "Unable to create socket: no error message given");
    }
  }

  _impl->fd.reset(fd);
}

Socket::~Socket() noexcept {
  if (_impl && _impl->fd.is_valid()) {
    ::close(_impl->fd.get());
  }
}

std::optional<Socket> Socket::create(Socket::Type type) {
  try {
    return std::optional<Socket>{type};
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<Socket> Socket::create_bind(const SocketAddr &addr,
                                          Socket::Type type) {
  auto s = create(type);
  if (!s)
    return std::nullopt;
  if (!s->bind(addr))
    return std::nullopt;

  return s;
}

bool Socket::bind(const SocketAddr &addr) {
  if (::bind(_impl->fd.get(), addr.asCType(), addr.size()) != 0) {
    _impl->last_error = std::error_code(errno, std::system_category());
    return false;
  }
  _impl->state = State::Bind;
  return true;
}

bool Socket::listen(int backlog) {
  if (_impl->type != Type::TCP) { // connection must be TCP
    _impl->last_error = std::error_code(EOPNOTSUPP, std::system_category());
    return false;
  }

  if (::listen(_impl->fd.get(), backlog) != 0) {
    _impl->last_error = std::error_code(errno, std::system_category());
    return false;
  }

  _impl->state = State::Listen;
  return true; // success
}

std::optional<std::pair<Socket, SocketAddr>> Socket::accept() {
  if (_impl->state != State::Listen) {
    _impl->last_error = std::error_code(EINVAL, std::system_category());
    return std::nullopt;
  }

  sockaddr_in client_addr{};
  socklen_t client_len = sizeof(client_addr);

  int cfd = ::accept(_impl->fd.get(),
                     reinterpret_cast<sockaddr *>(&client_addr), &client_len);

  if (cfd < 0) {
    _impl->last_error = std::error_code(errno, std::system_category());
    return std::nullopt;
  }

  Socket client(_impl->type);
  client._impl->fd.reset(cfd);
  client._impl->state = State::Connect;

  SocketAddr addr;
  addr._impl->addr = client_addr;

  auto pair =
      std::make_pair<Socket, SocketAddr>(std::move(client), std::move(addr));
  return pair;
}

bool Socket::connect(const SocketAddr &addr) {
  int result = ::connect(_impl->fd.get(), addr.asCType(), addr.size());

  if (result < 0) {
    _impl->last_error = std::error_code(errno, std::system_category());
    return false;
  }

  _impl->state = State::Connect;
  return true;
}

std::optional<std::size_t> Socket::send(std::span<const std::byte> data) {
  ssize_t sent =
      ::send(_impl->fd.get(), data.data(), data.size(), MSG_NOSIGNAL);
  if (sent < 0) {
    _impl->last_error = std::error_code(errno, std::system_category());
    return std::nullopt;
  }

  return static_cast<std::size_t>(sent);
}

std::optional<std::size_t> Socket::send(std::string_view data) {
  return send(
      std::span{reinterpret_cast<const std::byte *>(data.data()), data.size()});
}
} // namespace wnet
