#include <ki/log.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cerrno>
#include <cmath>
#include <format>
#include <iterator>
#include <utility>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace ki::log {
namespace {

template <class... Fs>
struct Overloaded : Fs... {
    using Fs::operator()...;
};

std::string_view upper_level(Level level) noexcept {
    switch (level) {
        case Level::debug: return "DEBUG";
        case Level::info: return "INFO";
        case Level::warn: return "WARN";
        case Level::error: return "ERROR";
    }
    return "INFO";
}

bool is_control(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return u < 0x20 || u == 0x7F;
}

void append_hex_byte(std::string& out, char c) {
    constexpr std::string_view digits = "0123456789abcdef";
    const auto u = static_cast<unsigned char>(c);
    out.push_back(digits[u >> 4U]);
    out.push_back(digits[u & 0xFU]);
}

void append_time(std::string& out, std::chrono::system_clock::time_point time) {
    std::format_to(std::back_inserter(out), "{:%FT%T}Z", std::chrono::floor<std::chrono::milliseconds>(time));
}

template <typename T>
void append_number(std::string& out, T value) {
    std::array<char, 32> buffer{};
    const auto result = std::to_chars(buffer.data(), std::to_address(buffer.end()), value);
    out.append(std::string_view{buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data())});
}

// Text format, after Go's slog.TextHandler.

bool needs_quotes(std::string_view text) noexcept {
    return text.empty() || std::ranges::any_of(text, [](char c) {
               return c == ' ' || c == '"' || c == '=' || c == '\\' || is_control(c);
           });
}

void append_text_string(std::string& out, std::string_view text) {
    if (!needs_quotes(text)) {
        out.append(text);
        return;
    }
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
            case '"': out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\n': out.append("\\n"); break;
            case '\r': out.append("\\r"); break;
            case '\t': out.append("\\t"); break;
            default:
                if (is_control(c)) {
                    out.append("\\x");
                    append_hex_byte(out, c);
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

void append_text_value(std::string& out, const Value& value) {
    std::visit(Overloaded{
                   [&](std::string_view v) { append_text_string(out, v); },
                   [&](bool v) { out.append(v ? "true" : "false"); },
                   [&](double v) { std::format_to(std::back_inserter(out), "{}", v); },
                   [&](auto v) { append_number(out, v); },
               },
               value.get());
}

// JSON format, one object per line.

void append_json_string(std::string& out, std::string_view text) {
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
            case '"': out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\n': out.append("\\n"); break;
            case '\r': out.append("\\r"); break;
            case '\t': out.append("\\t"); break;
            case '\b': out.append("\\b"); break;
            case '\f': out.append("\\f"); break;
            default:
                if (is_control(c)) {
                    out.append("\\u00");
                    append_hex_byte(out, c);
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

void append_json_value(std::string& out, const Value& value) {
    std::visit(Overloaded{
                   [&](std::string_view v) { append_json_string(out, v); },
                   [&](bool v) { out.append(v ? "true" : "false"); },
                   [&](double v) {
                       // JSON has no NaN or infinity; write them as strings, as slog's JSON handler does.
                       if (std::isfinite(v)) {
                           std::format_to(std::back_inserter(out), "{}", v);
                       } else {
                           append_json_string(out, std::isnan(v) ? "NaN" : (v > 0 ? "+Inf" : "-Inf"));
                       }
                   },
                   [&](auto v) { append_number(out, v); },
               },
               value.get());
}

detail::OwnedAttr own(const Attr& attr) {
    return {.key = std::string{attr.key},
            .value = std::visit(
                [](auto v) -> decltype(detail::OwnedAttr::value) {
                    if constexpr (std::is_same_v<decltype(v), std::string_view>) {
                        return std::string{v};
                    } else {
                        return v;
                    }
                },
                attr.value.get())};
}

Attr borrow(const detail::OwnedAttr& attr) {
    return {.key = attr.key, .value = std::visit([](const auto& v) { return Value{v}; }, attr.value)};
}

}  // namespace

std::string_view to_string(Level level) noexcept { return upper_level(level); }

Logger::Logger(std::shared_ptr<Sink> sink, Level min_level) : sink_{std::move(sink)}, min_level_{min_level} {}

Logger Logger::with(std::initializer_list<Attr> attrs) const {
    auto bound = bound_ ? std::make_shared<std::vector<detail::OwnedAttr>>(*bound_)
                        : std::make_shared<std::vector<detail::OwnedAttr>>();
    for (const auto& attr : attrs) {
        bound->push_back(own(attr));
    }
    Logger out = *this;
    out.bound_ = std::move(bound);
    return out;
}

void Logger::log(Level level, std::string_view message, std::initializer_list<Attr> attrs) const noexcept {
    if (!enabled(level)) {
        return;
    }
    try {
        const auto now = std::chrono::system_clock::now();
        if (!bound_ || bound_->empty()) {
            (void)sink_->write(Record{
                .time = now, .level = level, .message = message, .attrs = std::span{attrs.begin(), attrs.size()}});
            return;
        }
        std::vector<Attr> all;
        all.reserve(bound_->size() + attrs.size());
        for (const auto& attr : *bound_) {
            all.push_back(borrow(attr));
        }
        all.insert(all.end(), attrs);
        (void)sink_->write(Record{.time = now, .level = level, .message = message, .attrs = all});
    } catch (...) {
        // Only allocation can fail here; logging drops the record rather than throw.
        return;
    }
}

Writer fd_writer(int fd) {
    return [fd](std::string_view bytes) -> std::error_code {
        while (!bytes.empty()) {
#if defined(_WIN32)
            const auto n = ::_write(fd, bytes.data(), static_cast<unsigned>(bytes.size()));
#else
            const auto n = ::write(fd, bytes.data(), bytes.size());
#endif
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return {errno, std::system_category()};
            }
            bytes.remove_prefix(static_cast<std::size_t>(n));
        }
        return {};
    };
}

TextSink::TextSink(Writer writer) : writer_{std::move(writer)} {}

std::error_code TextSink::write(const Record& record) noexcept {
    try {
        std::string line = "time=";
        append_time(line, record.time);
        line.append(" level=");
        line.append(upper_level(record.level));
        line.append(" msg=");
        append_text_string(line, record.message);
        for (const auto& attr : record.attrs) {
            line.push_back(' ');
            append_text_string(line, attr.key);
            line.push_back('=');
            append_text_value(line, attr.value);
        }
        line.push_back('\n');
        return writer_(line);
    } catch (...) {
        return std::make_error_code(std::errc::not_enough_memory);
    }
}

JsonSink::JsonSink(Writer writer) : writer_{std::move(writer)} {}

std::error_code JsonSink::write(const Record& record) noexcept {
    try {
        std::string line = R"({"time":")";
        append_time(line, record.time);
        line.append(R"(","level":")");
        line.append(upper_level(record.level));
        line.append(R"(","msg":)");
        append_json_string(line, record.message);
        for (const auto& attr : record.attrs) {
            line.push_back(',');
            append_json_string(line, attr.key);
            line.push_back(':');
            append_json_value(line, attr.value);
        }
        line.append("}\n");
        return writer_(line);
    } catch (...) {
        return std::make_error_code(std::errc::not_enough_memory);
    }
}

AsyncSink::AsyncSink(std::shared_ptr<Sink> inner, std::size_t capacity)
    : inner_{std::move(inner)},
      capacity_{std::max<std::size_t>(capacity, 1)},
      worker_{[this](const std::stop_token& stop) { run(stop); }} {}

AsyncSink::~AsyncSink() {
    worker_.request_stop();
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::error_code AsyncSink::write(const Record& record) noexcept {
    try {
        OwnedRecord owned{.time = record.time, .level = record.level, .message = std::string{record.message}, .attrs = {}};
        owned.attrs.reserve(record.attrs.size());
        for (const auto& attr : record.attrs) {
            owned.attrs.push_back(own(attr));
        }
        {
            const std::scoped_lock lock{mutex_};
            if (queue_.size() >= capacity_) {
                ++dropped_;
                return std::make_error_code(std::errc::no_buffer_space);
            }
            queue_.push_back(std::move(owned));
        }
        ready_.notify_one();
        return {};
    } catch (...) {
        return std::make_error_code(std::errc::not_enough_memory);
    }
}

std::error_code AsyncSink::flush() noexcept {
    std::unique_lock lock{mutex_};
    idle_.wait(lock, [this] { return queue_.empty() && !writing_; });
    return {};
}

std::uint64_t AsyncSink::dropped() const noexcept {
    const std::scoped_lock lock{mutex_};
    return dropped_;
}

void AsyncSink::deliver(const OwnedRecord& record) noexcept {
    try {
        std::vector<Attr> attrs;
        attrs.reserve(record.attrs.size());
        for (const auto& attr : record.attrs) {
            attrs.push_back(borrow(attr));
        }
        (void)inner_->write(Record{.time = record.time, .level = record.level, .message = record.message, .attrs = attrs});
    } catch (...) {
        return;
    }
}

void AsyncSink::run(const std::stop_token& stop) {
    std::unique_lock lock{mutex_};
    for (;;) {
        ready_.wait(lock, stop, [this] { return !queue_.empty(); });
        if (queue_.empty()) {
            return;  // stop requested and every queued record written
        }
        auto batch = std::exchange(queue_, {});
        writing_ = true;
        lock.unlock();
        for (const auto& record : batch) {
            deliver(record);
        }
        (void)inner_->flush();
        lock.lock();
        writing_ = false;
        idle_.notify_all();
    }
}

}  // namespace ki::log
