// Winsock / POSIX sockets. Do NOT include raylib.h in this file.
#include "mirage_core/line_socket.h"

#include <algorithm>
#include <cstring>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using SockLen = int;
  using Sock = SOCKET;
  static bool WouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
  static void CloseSock(std::intptr_t s) { closesocket((SOCKET)s); }
  static bool SetNonBlocking(std::intptr_t s) { u_long on = 1; return ioctlsocket((SOCKET)s, FIONBIO, &on) == 0; }
  static int  LastError() { return WSAGetLastError(); }
  static constexpr int kSendFlags = 0;
  static void NetInit() {
      static bool done = [] { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }();
      (void)done;
  }
#else
  #include <arpa/inet.h>
  #include <cerrno>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <sys/socket.h>
  #include <unistd.h>
  using SockLen = socklen_t;
  using Sock = int;
  static bool WouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
  static void CloseSock(std::intptr_t s) { close((int)s); }
  static bool SetNonBlocking(std::intptr_t s) {
      int f = fcntl((int)s, F_GETFL, 0);
      return f >= 0 && fcntl((int)s, F_SETFL, f | O_NONBLOCK) == 0;
  }
  static int  LastError() { return errno; }
  #ifdef MSG_NOSIGNAL
    static constexpr int kSendFlags = MSG_NOSIGNAL;
  #else
    static constexpr int kSendFlags = 0;
  #endif
  static void NetInit() {}
#endif

namespace {
void SetError(std::string* e, const std::string& what) {
    if (e) *e = what + " (error " + std::to_string(LastError()) + ")";
}
void NoDelay(std::intptr_t s) {
    int on = 1;
    setsockopt((Sock)s, IPPROTO_TCP, TCP_NODELAY, (const char*)&on, sizeof on);
}
}  // namespace

// ---------------------------------------------------------------------------
// LineSocket
// ---------------------------------------------------------------------------
bool LineSocket::Connect(const std::string& host, int port, std::string* error) {
    Close();
    NetInit();

    std::intptr_t s = (std::intptr_t)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kInvalid) { SetError(error, "socket() failed"); return false; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((unsigned short)port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        CloseSock(s);
        if (error) *error = "bad address: " + host;
        return false;
    }
    if (connect((Sock)s, (sockaddr*)&addr, sizeof addr) != 0) {
        SetError(error, "connect to " + host + ":" + std::to_string(port) + " failed");
        CloseSock(s);
        return false;
    }
    Adopt(s);
    return true;
}

void LineSocket::Adopt(std::intptr_t fd) {
    Close();
    fd_ = fd;
    SetNonBlocking(fd_);
    NoDelay(fd_);
}

void LineSocket::Close() {
    if (fd_ != kInvalid) CloseSock(fd_);
    fd_ = kInvalid;
    sendBuf_.clear(); sendOff_ = 0;
    recvBuf_.clear();
}

bool LineSocket::Send(const std::string& line) {
    if (fd_ == kInvalid) { ++dropped_; return false; }
    if (sendBuf_.size() - sendOff_ + line.size() + 1 > kMaxSendBuffer) { ++dropped_; return false; }

    sendBuf_ += line;
    sendBuf_ += '\n';
    Flush();
    return true;
}

void LineSocket::Flush() {
    while (fd_ != kInvalid && sendOff_ < sendBuf_.size()) {
        const int n = (int)send((Sock)fd_, sendBuf_.data() + sendOff_,
                                (int)std::min<std::size_t>(sendBuf_.size() - sendOff_, 1 << 16),
                                kSendFlags);
        if (n > 0) { sendOff_ += (std::size_t)n; continue; }
        if (n < 0 && WouldBlock()) break;        // peer is slow: keep the rest queued
        Close();                                  // real error / closed
        return;
    }
    if (sendOff_ == sendBuf_.size()) { sendBuf_.clear(); sendOff_ = 0; }
    else if (sendOff_ > kMaxSendBuffer / 2) { sendBuf_.erase(0, sendOff_); sendOff_ = 0; }
}

std::vector<std::string> LineSocket::Poll() {
    std::vector<std::string> lines;
    if (fd_ == kInvalid) return lines;

    Flush();

    bool peerClosed = false;
    char buf[4096];
    while (fd_ != kInvalid) {
        const int n = (int)recv((Sock)fd_, buf, sizeof buf, 0);
        if (n > 0) { recvBuf_.append(buf, (std::size_t)n); if (recvBuf_.size() > kMaxLine * 2) break; continue; }
        if (n < 0 && WouldBlock()) break;
        peerClosed = true;                        // 0 = orderly close, <0 = error
        break;
    }

    std::size_t start = 0, nl;
    while ((nl = recvBuf_.find('\n', start)) != std::string::npos) {
        std::size_t end = nl;
        if (end > start && recvBuf_[end - 1] == '\r') --end;
        if (end - start <= kMaxLine) lines.emplace_back(recvBuf_, start, end - start);
        start = nl + 1;
    }
    recvBuf_.erase(0, start);

    // A "line" that never ends is a broken peer: cut it off.
    if (recvBuf_.size() > kMaxLine) peerClosed = true;

    if (peerClosed) Close();
    return lines;
}

// ---------------------------------------------------------------------------
// LineListener
// ---------------------------------------------------------------------------
bool LineListener::Listen(int port, std::string* error) {
    Close();
    NetInit();

    std::intptr_t s = (std::intptr_t)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == -1) { SetError(error, "socket() failed"); return false; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((unsigned short)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);   // localhost only, never the network

    if (bind((Sock)s, (sockaddr*)&addr, sizeof addr) != 0 ||
        listen((Sock)s, 4) != 0) {
        SetError(error, "bind/listen failed");
        CloseSock(s);
        return false;
    }

    SockLen len = sizeof addr;
    if (getsockname((Sock)s, (sockaddr*)&addr, &len) != 0) {
        SetError(error, "getsockname failed");
        CloseSock(s);
        return false;
    }

    SetNonBlocking(s);
    fd_   = s;
    port_ = ntohs(addr.sin_port);
    return true;
}

void LineListener::Close() {
    if (fd_ != -1) CloseSock(fd_);
    fd_ = -1;
    port_ = 0;
}

bool LineListener::Accept(LineSocket& out) {
    if (fd_ == -1) return false;
    std::intptr_t c = (std::intptr_t)accept((Sock)fd_, nullptr, nullptr);
    if (c == -1) return false;                 // nobody waiting (or a transient error)
    out.Adopt(c);
    return true;
}
