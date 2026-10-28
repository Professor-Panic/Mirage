// mirage_player/cerr_tee.h
//
// ScriptSystem reports Lua errors on std::cerr. CerrTee keeps that output
// as it is and also hands every complete line to a callback, so the player
// can forward script errors to the editor.
//
//   CerrTee tee(std::cerr, [](const std::string& line) { ... });
#pragma once
#include <functional>
#include <ostream>
#include <streambuf>
#include <string>

class CerrTee : public std::streambuf {
public:
    using Sink = std::function<void(const std::string&)>;

    CerrTee(std::ostream& stream, Sink sink)
        : stream_(stream), original_(stream.rdbuf(this)), sink_(std::move(sink)) {}

    ~CerrTee() override { stream_.rdbuf(original_); }   // put the stream back

    CerrTee(const CerrTee&) = delete;
    CerrTee& operator=(const CerrTee&) = delete;

protected:
    int overflow(int c) override {
        if (c == traits_type::eof()) return traits_type::not_eof(c);
        const char ch = static_cast<char>(c);
        Write(&ch, 1);
        return c;
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        Write(s, static_cast<std::size_t>(n));
        return n;
    }
    int sync() override { return original_->pubsync(); }

private:
    void Write(const char* s, std::size_t n) {
        original_->sputn(s, static_cast<std::streamsize>(n));
        for (std::size_t i = 0; i < n; ++i) {
            if (s[i] == '\n') {
                if (!line_.empty() && line_.back() == '\r') line_.pop_back();
                if (!line_.empty() && sink_) sink_(line_);
                line_.clear();
            } else {
                line_ += s[i];
            }
        }
    }

    std::ostream&  stream_;
    std::streambuf* original_;
    Sink           sink_;
    std::string    line_;
};
