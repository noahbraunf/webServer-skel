#ifndef SOCKET_H_
#define SOCKET_H_
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <regex>
#include <stdexcept>
#include <string>

#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

class SocketAddr;
class Socket {
public:
  enum Type { // eventually we will probably have to implement UDP
    TCP = SOCK_STREAM
  };

  Socket(Type type = TCP)
      : mFd(socket(AF_INET, static_cast<int>(type), 0)), mType(type) {
    if (mFd == -1) {
      throw std::runtime_error("Failed creating socket");
    }
  };

  // move semantics
  Socket(Socket &&other) noexcept : mFd(other.mFd), mType(other.mType) {
    other.mFd =
        -1; // two objects cannot own the same file descriptor for a socket
  }

  Socket &operator=(Socket &&other) noexcept {
    if (this != &other) {
      mFd = other.mFd;
      mType = other.mType;
      other.mFd = -1;
    }
    return *this;
  }

  ~Socket() { close(mFd); }
  // removal of copy semantics because sockets are owned by one object
  Socket(const Socket &) = delete;
  Socket &operator=(const Socket &) = delete;

  void bind(const SocketAddr addr) {
    if (bind(mFd, addr.asCType(), addr.mAddr))
  }

private:
  Socket() = default;
  int mFd = -1;
  Type mType;
};

class SocketAddr {
public:
  SocketAddr(const std::string &ip, uint16_t port) {
    std::memset(&mAddr, 0,
                sizeof(mAddr));   // size of storage is platform dependent which
                                  // is why we have to do this
    mAddr.sin_family = AF_INET;   // ipv4
    mAddr.sin_port = htons(port); // host and client may have different layouts
                                  // of uint16_t, inet handles this

    if (inet_pton(AF_INET, ip.c_str(), &mAddr.sin_addr) != 1) {
      throw std::invalid_argument("IPv4 address is not valid.");
    }
  }

  const sockaddr *asCType() const {
    return static_cast<const sockaddr *>(static_cast<const void *>(&mAddr));
  }
  sockaddr *asCType() {
    return static_cast<sockaddr *>(static_cast<void *>(&mAddr));
  }
  bool operator==(const SocketAddr &other) const {
    return mAddr.sin_family == other.mAddr.sin_family && // address types match
           mAddr.sin_port == other.mAddr.sin_port &&     // ports match
           mAddr.sin_addr.s_addr == other.mAddr.sin_addr.s_addr; // ip match
  }

  const sockaddr_in raw() const { return mAddr; }
  sockaddr_in raw() { return mAddr; }

  socklen_t size() const { return sizeof(mAddr); }

  uint16_t portHostByteOrder() const { return ntohs(mAddr.sin_port); }
  std::string ip() const {
    char buffer[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &mAddr.sin_addr, buffer, sizeof(buffer));
    return buffer;
  }
  uint32_t ipNumber() const {
    return ntohl(mAddr.sin_addr.s_addr); // host byte order
  }

private:
  SocketAddr() = default;
  sockaddr_in mAddr; // contains info on type of address, padding, and alignment
  socklen_t mSize = 0; // size of socket

  friend class Socket;
};

#endif
