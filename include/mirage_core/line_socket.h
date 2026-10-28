// mirage_core/line_socket.h
//
// Non-blocking, line-oriented TCP on localhost. Used for editor <-> player.
// One message = one line ("\n" terminated), normally a JSON object.
//
//   Player:  LineSocket s;  s.Connect("127.0.0.1", port);
//   Editor:  LineListener l; l.Listen(0);  port = l.Port();  l.Accept(s);
//   Both:    s.Send("{\"type\":\"log\"}");   for (auto& line : s.Poll()) ...
//
// Rules this class enforces:
//   - nothing here ever blocks (after Connect), so a slow peer cannot stall a frame
//   - the send buffer is capped: when full, Send() drops the message and counts it
//   - Poll() only returns complete lines, however the bytes were split
//
// No OS headers here. The implementation (line_socket.cpp) must not include raylib.h.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class LineSocket {
public:
    static constexpr std::size_t kMaxSendBuffer = 256 * 1024;   // bytes queued before dropping
    static constexpr std::size_t kMaxLine       = 1024 * 1024;  // longest accepted incoming line

    LineSocket() = default;
    ~LineSocket() { Close(); }
    LineSocket(const LineSocket&) = delete;
    LineSocket& operator=(const LineSocket&) = delete;

    // Blocking connect (use for 127.0.0.1 only: it returns at once), then the
    // socket is switched to non-blocking for good.
    bool Connect(const std::string& host, int port, std::string* error = nullptr);

    bool Connected() const { return fd_ != kInvalid; }
    void Close();

    // Queues `line` + '\n' and tries to send. Returns false if the message was
    // dropped (not connected, or the send buffer is full).
    bool Send(const std::string& line);

    // Sends what is pending, reads what has arrived, and returns every complete
    // line received. If the peer closed the connection, Connected() turns false
    // (lines already received are still returned).
    std::vector<std::string> Poll();

    // How many messages Send() dropped since the last call (and resets it).
    std::size_t TakeDropped() { std::size_t d = dropped_; dropped_ = 0; return d; }

private:
    friend class LineListener;
    static constexpr std::intptr_t kInvalid = -1;

    void Adopt(std::intptr_t fd);       // take over an accepted socket
    void Flush();

    std::intptr_t fd_ = kInvalid;
    std::string   sendBuf_;
    std::size_t   sendOff_ = 0;         // bytes of sendBuf_ already sent
    std::string   recvBuf_;
    std::size_t   dropped_ = 0;
};

class LineListener {
public:
    LineListener() = default;
    ~LineListener() { Close(); }
    LineListener(const LineListener&) = delete;
    LineListener& operator=(const LineListener&) = delete;

    // Listens on 127.0.0.1. port 0 = let the OS pick a free one (see Port()).
    bool Listen(int port, std::string* error = nullptr);

    bool Listening() const { return fd_ != -1; }
    int  Port() const { return port_; }
    void Close();

    // Non-blocking. If a client is waiting, hands it to `out` (closing whatever
    // `out` held before) and returns true.
    bool Accept(LineSocket& out);

private:
    std::intptr_t fd_ = -1;
    int port_ = 0;
};
