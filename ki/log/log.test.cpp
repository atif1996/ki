// T1.10 - T1.18

#include <ki/log/log.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using ki::log::Attr;
using ki::log::Level;
using ki::log::Record;

namespace {

// Lines collects what a sink writes, safely from any thread.
struct Lines {
    std::mutex mutex;
    std::string text;

    ki::log::Writer writer() {
        return [this](std::string_view bytes) -> std::error_code {
            const std::scoped_lock lock{mutex};
            text.append(bytes);
            return {};
        };
    }
    std::vector<std::string> split() {
        const std::scoped_lock lock{mutex};
        std::vector<std::string> out;
        for (auto line : std::views::split(text, '\n')) {
            if (!line.empty()) {
                out.emplace_back(line.begin(), line.end());
            }
        }
        return out;
    }
};

// Captured stores copies of every record it is given, so tests can inspect them after the call.
class CapturingSink final : public ki::log::Sink {
public:
    explicit CapturingSink(std::chrono::milliseconds delay = 0ms) : delay_{delay} {}

    std::error_code write(const Record& record) noexcept override {
        if (delay_ > 0ms) {
            std::this_thread::sleep_for(delay_);
        }
        std::string line{record.message};
        for (const auto& attr : record.attrs) {
            line += ' ';
            line += attr.key;
            line += '=';
            if (const auto* text = std::get_if<std::string_view>(&attr.value.get())) {
                line += *text;
            }
        }
        const std::scoped_lock lock{mutex_};
        records_.push_back(std::move(line));
        return {};
    }

    std::vector<std::string> records() {
        const std::scoped_lock lock{mutex_};
        return records_;
    }

private:
    std::chrono::milliseconds delay_;
    std::mutex mutex_;
    std::vector<std::string> records_;
};

const auto fixed_time = std::chrono::sys_days{std::chrono::year{2026} / 10 / 31} + 12h + 123ms;

Record record(Level level, std::string_view message, std::span<const Attr> attrs) {
    return Record{.time = fixed_time, .level = level, .message = message, .attrs = attrs};
}

}  // namespace

// T1.10
TEST_CASE("records below the minimum level never reach the sink") {
    auto sink = std::make_shared<CapturingSink>();
    const ki::log::Logger logger{sink, Level::warn};
    logger.debug("dropped");
    logger.info("dropped");
    logger.warn("kept");
    logger.error("kept");
    REQUIRE(sink->records().size() == 2);
    REQUIRE_FALSE(logger.enabled(Level::info));

    const ki::log::Logger discard;
    REQUIRE_FALSE(discard.enabled(Level::error));
    discard.error("nowhere");
}

TEST_CASE("the null sink disables every level") {
    const ki::log::Logger logger{std::make_shared<ki::log::NullSink>(), Level::debug};
    REQUIRE_FALSE(logger.enabled(Level::error));
}

// T1.11
TEST_CASE("text sink writes slog-style key=value lines") {
    Lines out;
    ki::log::TextSink sink{out.writer()};
    const std::vector<Attr> attrs = {{"user", "ada"}, {"id", 42},
                                     {"delta", -3},   {"ok", true},
                                     {"ratio", 0.5},  {"bytes", std::uint64_t{18446744073709551615ULL}}};
    REQUIRE_FALSE(sink.write(record(Level::info, "hello", attrs)));
    REQUIRE(out.text ==
            "time=2026-10-31T12:00:00.123Z level=INFO msg=hello user=ada id=42 delta=-3 ok=true "
            "ratio=0.5 bytes=18446744073709551615\n");
}

// T1.11
TEST_CASE("json sink writes one object per line") {
    Lines out;
    ki::log::JsonSink sink{out.writer()};
    const std::vector<Attr> attrs = {{"user", "ada"}, {"id", 42}, {"ok", false}, {"ratio", 0.25}};
    REQUIRE_FALSE(sink.write(record(Level::warn, "hello", attrs)));
    REQUIRE(out.text == R"({"time":"2026-10-31T12:00:00.123Z","level":"WARN","msg":"hello","user":"ada",)"
                        R"("id":42,"ok":false,"ratio":0.25})"
                        "\n");
}

TEST_CASE("json sink writes non-finite numbers as strings") {
    Lines out;
    ki::log::JsonSink sink{out.writer()};
    const std::vector<Attr> attrs = {{"nan", std::numeric_limits<double>::quiet_NaN()},
                                     {"inf", std::numeric_limits<double>::infinity()}};
    REQUIRE_FALSE(sink.write(record(Level::info, "m", attrs)));
    REQUIRE(out.text.find(R"("nan":"NaN","inf":"+Inf")") != std::string::npos);
}

// T1.12
TEST_CASE("text sink quotes and escapes control characters, quotes and spaces") {
    Lines out;
    ki::log::TextSink sink{out.writer()};
    const std::vector<Attr> attrs = {
        {"path", "/a\r\nInjected: yes"}, {"quote", "say \"hi\""}, {"esc", "\x1b[31m"}, {"empty", ""}, {"bad key", "v"}};
    REQUIRE_FALSE(sink.write(record(Level::error, "line1\nline2", attrs)));
    REQUIRE(out.text ==
            "time=2026-10-31T12:00:00.123Z level=ERROR msg=\"line1\\nline2\" "
            "path=\"/a\\r\\nInjected: yes\" quote=\"say \\\"hi\\\"\" esc=\"\\x1b[31m\" empty=\"\" "
            "\"bad key\"=v\n");
    REQUIRE(out.split().size() == 1);
}

// T1.12
TEST_CASE("json sink escapes control characters and quotes") {
    Lines out;
    ki::log::JsonSink sink{out.writer()};
    const std::vector<Attr> attrs = {{"path", "/a\r\nb"}, {"esc", "\x1b"}, {"q", "\"\\"}};
    REQUIRE_FALSE(sink.write(record(Level::info, "tab\there", attrs)));
    REQUIRE(out.text == R"({"time":"2026-10-31T12:00:00.123Z","level":"INFO","msg":"tab\there",)"
                        R"("path":"/a\r\nb","esc":"\u001b","q":"\"\\"})"
                        "\n");
}

// T1.13
TEST_CASE("attributes bound with with() appear on every record and not on the parent") {
    auto sink = std::make_shared<CapturingSink>();
    const ki::log::Logger base{sink};
    const auto request = base.with({{"request_id", "r1"}});
    const auto nested = request.with({{"route", "/users/{id}"}});

    request.info("first", {{"k", "v"}});
    nested.info("second");
    base.info("third");

    const auto records = sink->records();
    REQUIRE(records ==
            std::vector<std::string>{"first request_id=r1 k=v", "second request_id=r1 route=/users/{id}", "third"});
}

TEST_CASE("bound attributes own their strings") {
    auto sink = std::make_shared<CapturingSink>();
    ki::log::Logger logger;
    {
        std::string id = "r42";
        logger = ki::log::Logger{sink}.with({{"request_id", id}});
        id.assign("XXXX");
    }
    logger.info("later");
    REQUIRE(sink->records() == std::vector<std::string>{"later request_id=r42"});
}

// T1.14
TEST_CASE("a sink that cannot write reports an error and logging does not throw") {
    auto sink = std::make_shared<ki::log::TextSink>(ki::log::fd_writer(-1));
    const std::vector<Attr> attrs = {{"k", "v"}};
    const auto error = sink->write(record(Level::info, "m", attrs));
    REQUIRE(error);
    REQUIRE(error == std::errc::bad_file_descriptor);

    const ki::log::Logger logger{sink};
    REQUIRE_NOTHROW(logger.error("still fine", {{"k", "v"}}));
}

// T1.15
TEST_CASE("an async sink does not block callers when its destination stalls") {
    auto slow = std::make_shared<CapturingSink>(100ms);
    constexpr int calls = 10;
    std::uint64_t dropped = 0;
    {
        ki::log::AsyncSink async{slow, 4};
        const ki::log::Logger logger{std::shared_ptr<ki::log::Sink>(&async, [](ki::log::Sink*) {})};
        const auto started = std::chrono::steady_clock::now();
        for (int i = 0; i < calls; ++i) {
            logger.info("burst");
        }
        REQUIRE(std::chrono::steady_clock::now() - started < 50ms);
        // How many records the worker takes before the stall depends on scheduling, so only overflow itself
        // is asserted; the total below accounts for every record.
        dropped = async.dropped();
        REQUIRE(dropped > 0);
    }
    REQUIRE(slow->records().size() + dropped == calls);
}

// T1.16
TEST_CASE("an async sink copies borrowed strings before write returns") {
    auto inner = std::make_shared<CapturingSink>();
    ki::log::AsyncSink async{inner};
    {
        std::string message = "original";
        std::string value = "value";
        const std::vector<Attr> attrs = {{"k", value}};
        REQUIRE_FALSE(async.write(record(Level::info, message, attrs)));
        message.assign("XXXXXXXX");
        value.assign("XXXXX");
    }
    REQUIRE_FALSE(async.flush());
    REQUIRE(inner->records() == std::vector<std::string>{"original k=value"});
}

// T1.17
TEST_CASE("destroying an async sink writes every queued record") {
    auto inner = std::make_shared<CapturingSink>();
    {
        ki::log::AsyncSink async{inner, 2000};
        const std::vector<Attr> none;
        for (int i = 0; i < 1000; ++i) {
            REQUIRE_FALSE(async.write(record(Level::info, "m", none)));
        }
    }
    REQUIRE(inner->records().size() == 1000);
}

// T1.18
TEST_CASE("concurrent logging produces whole lines") {
    Lines out;
    constexpr int threads = 8;
    constexpr int per_thread = 500;
    {
        auto async = std::make_shared<ki::log::AsyncSink>(std::make_shared<ki::log::TextSink>(out.writer()),
                                                          threads * per_thread);
        const ki::log::Logger logger{async};
        std::vector<std::jthread> workers;
        for (int t = 0; t < threads; ++t) {
            workers.emplace_back([&logger, t] {
                const auto mine = logger.with({{"thread", t}});
                for (int i = 0; i < per_thread; ++i) {
                    mine.info("hello world", {{"i", i}});
                }
            });
        }
    }
    const auto lines = out.split();
    REQUIRE(lines.size() == threads * per_thread);
    for (const auto& line : lines) {
        REQUIRE(line.starts_with("time="));
        REQUIRE(line.find("level=INFO msg=\"hello world\" thread=") != std::string::npos);
    }
}

TEST_CASE("level names") {
    REQUIRE(ki::log::to_string(Level::debug) == "DEBUG");
    REQUIRE(ki::log::to_string(Level::error) == "ERROR");
}
