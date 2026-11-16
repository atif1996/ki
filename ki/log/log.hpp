#pragma once

#include <ki/log/export.hpp>

#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <variant>
#include <vector>

// ki::log is structured logging modelled on Go's log/slog: a Logger hands Records to a Sink, which formats
// and writes them. Logging never throws; a sink that cannot write reports an error code and the record is
// lost.
namespace ki::log {

enum class Level : std::uint8_t { debug, info, warn, error };

[[nodiscard]] KI_LOG_EXPORT std::string_view to_string(Level level) noexcept;

// Value is an attribute value. Strings are borrowed for the duration of the logging call.
class Value {
public:
    using Storage = std::variant<std::string_view, std::int64_t, std::uint64_t, double, bool>;

    Value(std::string_view v) noexcept : storage_{v} {}
    Value(const char* v) noexcept : storage_{std::string_view{v}} {}
    Value(const std::string& v) noexcept : storage_{std::string_view{v}} {}
    Value(bool v) noexcept : storage_{v} {}
    template <std::signed_integral T>
    Value(T v) noexcept : storage_{static_cast<std::int64_t>(v)} {}
    template <std::unsigned_integral T>
        requires(!std::same_as<T, bool>)
    Value(T v) noexcept : storage_{static_cast<std::uint64_t>(v)} {}
    template <std::floating_point T>
    Value(T v) noexcept : storage_{static_cast<double>(v)} {}

    [[nodiscard]] const Storage& get() const noexcept { return storage_; }

private:
    Storage storage_{};
};

struct Attr {
    std::string_view key{};
    Value value;
};

namespace detail {

// OwnedAttr is an Attr that owns its strings, for attributes kept beyond the logging call.
struct OwnedAttr {
    std::string key{};
    std::variant<std::string, std::int64_t, std::uint64_t, double, bool> value{};
};

}  // namespace detail

// Record is one log event. Its strings and attributes are borrowed: a sink that keeps a record past write
// must copy it.
struct Record {
    std::chrono::system_clock::time_point time{};
    Level level = Level::info;
    std::string_view message{};
    std::span<const Attr> attrs{};
};

// Sinks are exported whole so their vtables are visible to code that creates or derives from them.
class KI_LOG_EXPORT Sink {
public:
    Sink() = default;
    Sink(const Sink&) = delete;
    Sink& operator=(const Sink&) = delete;
    Sink(Sink&&) = delete;
    Sink& operator=(Sink&&) = delete;
    virtual ~Sink() = default;

    [[nodiscard]] virtual bool enabled(Level /*level*/) const noexcept { return true; }
    virtual std::error_code write(const Record& record) noexcept = 0;
    virtual std::error_code flush() noexcept { return {}; }
};

// Logger is a cheap, copyable handle: a sink, a minimum level and attributes bound with with(). A
// default-constructed Logger discards everything.
class Logger {
public:
    Logger() = default;
    KI_LOG_EXPORT explicit Logger(std::shared_ptr<Sink> sink, Level min_level = Level::info);

    // with returns a logger that adds attrs to every record; this logger is unchanged.
    [[nodiscard]] KI_LOG_EXPORT Logger with(std::initializer_list<Attr> attrs) const;

    // enabled is checked before any record is built, so a disabled call costs one comparison.
    [[nodiscard]] bool enabled(Level level) const noexcept {
        return sink_ && level >= min_level_ && sink_->enabled(level);
    }

    KI_LOG_EXPORT void log(Level level, std::string_view message,
                           std::initializer_list<Attr> attrs = {}) const noexcept;
    void debug(std::string_view message, std::initializer_list<Attr> attrs = {}) const noexcept {
        log(Level::debug, message, attrs);
    }
    void info(std::string_view message, std::initializer_list<Attr> attrs = {}) const noexcept {
        log(Level::info, message, attrs);
    }
    void warn(std::string_view message, std::initializer_list<Attr> attrs = {}) const noexcept {
        log(Level::warn, message, attrs);
    }
    void error(std::string_view message, std::initializer_list<Attr> attrs = {}) const noexcept {
        log(Level::error, message, attrs);
    }

private:
    std::shared_ptr<Sink> sink_{};
    Level min_level_ = Level::info;
    std::shared_ptr<const std::vector<detail::OwnedAttr>> bound_{};
};

// Writer delivers formatted bytes; text and JSON sinks write one complete line per call.
using Writer = std::function<std::error_code(std::string_view)>;

// fd_writer writes to a file descriptor, such as 1 for stdout or 2 for stderr.
[[nodiscard]] KI_LOG_EXPORT Writer fd_writer(int fd);

class NullSink final : public Sink {
public:
    [[nodiscard]] bool enabled(Level /*level*/) const noexcept override { return false; }
    std::error_code write(const Record& /*record*/) noexcept override { return {}; }
};

// TextSink writes slog-style "time=... level=INFO msg=hello key=value" lines. Values that are empty or
// contain spaces, quotes, '=' or control characters are quoted, with control characters escaped.
class KI_LOG_EXPORT TextSink final : public Sink {
public:
    explicit TextSink(Writer writer);
    std::error_code write(const Record& record) noexcept override;

private:
    Writer writer_{};
};

// JsonSink writes one JSON object per line: {"time":...,"level":"INFO","msg":...,"key":value}.
class KI_LOG_EXPORT JsonSink final : public Sink {
public:
    explicit JsonSink(Writer writer);
    std::error_code write(const Record& record) noexcept override;

private:
    Writer writer_{};
};

// AsyncSink hands records to a background thread, so a slow destination never blocks the caller. write
// copies the record into a bounded queue; when the queue is full the record is dropped and counted.
// Destruction writes every queued record before returning.
class KI_LOG_EXPORT AsyncSink final : public Sink {
public:
    explicit AsyncSink(std::shared_ptr<Sink> inner, std::size_t capacity = 8192);
    ~AsyncSink() override;
    AsyncSink(const AsyncSink&) = delete;
    AsyncSink& operator=(const AsyncSink&) = delete;
    AsyncSink(AsyncSink&&) = delete;
    AsyncSink& operator=(AsyncSink&&) = delete;

    [[nodiscard]] bool enabled(Level level) const noexcept override { return inner_->enabled(level); }
    std::error_code write(const Record& record) noexcept override;
    // flush waits until every record queued so far has been written and the inner sink flushed.
    std::error_code flush() noexcept override;
    [[nodiscard]] std::uint64_t dropped() const noexcept;

private:
    struct OwnedRecord {
        std::chrono::system_clock::time_point time{};
        Level level = Level::info;
        std::string message{};
        std::vector<detail::OwnedAttr> attrs{};
    };

    void run(const std::stop_token& stop);
    void deliver(const OwnedRecord& record) noexcept;

    std::shared_ptr<Sink> inner_{};
    std::size_t capacity_{};
    mutable std::mutex mutex_;
    std::condition_variable_any ready_{};
    std::condition_variable_any idle_{};
    std::deque<OwnedRecord> queue_{};
    std::uint64_t dropped_ = 0;
    bool writing_ = false;
    std::jthread worker_{};
};

}  // namespace ki::log
