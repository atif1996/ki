#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ki {

namespace detail {

// type_fingerprint identifies T by its compiler-generated function signature. Unlike typeid, it compares
// equal across shared libraries built by the same compiler.
template <typename T>
consteval std::string_view type_fingerprint() noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
    return __FUNCSIG__;
#else
    return __PRETTY_FUNCTION__;
#endif
}

consteval std::uint64_t fnv1a(std::string_view text) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

}  // namespace detail

// ContextKey names a typed per-request value. Keys are identified by name rather than address, so a key
// declared inline in a header is the same key in every shared library; prefix the name with the owning
// project ("myapp.user") to avoid collisions. ki's own keys use the "ki." prefix.
template <typename T>
class ContextKey {
    static_assert(std::is_object_v<T> && !std::is_const_v<T>, "context values must be non-const object types");

public:
    consteval explicit ContextKey(std::string_view name) : name_{name}, hash_{detail::fnv1a(name)} {}

    [[nodiscard]] constexpr std::string_view name() const noexcept { return name_; }
    [[nodiscard]] constexpr std::uint64_t hash() const noexcept { return hash_; }

private:
    std::string_view name_;
    std::uint64_t hash_;
};

// Context holds the typed values attached to one request. Reading a value with a key of a different type than
// it was stored with reports it as absent; the value is never reinterpreted.
class Context {
private:
    struct Holder {
        Holder() = default;
        Holder(const Holder&) = delete;
        Holder& operator=(const Holder&) = delete;
        Holder(Holder&&) = delete;
        Holder& operator=(Holder&&) = delete;
        virtual ~Holder() = default;
    };

    template <typename T>
    struct ValueHolder final : Holder {
        explicit ValueHolder(T v) : value(std::move(v)) {}
        T value;
    };

    struct Entry {
        std::uint64_t hash;
        std::string_view name;
        std::string_view type;
        std::unique_ptr<Holder> value;
    };

    struct Matches {
        std::uint64_t hash;
        std::string_view name;
        bool operator()(const Entry& e) const noexcept { return e.hash == hash && e.name == name; }
    };

    template <typename T>
    static Matches matches(const ContextKey<T>& key) noexcept {
        return {key.hash(), key.name()};
    }

public:
    template <typename T, typename U = T>
        requires std::constructible_from<T, U&&>
    void set(const ContextKey<T>& key, U&& value) {
        Entry entry{key.hash(), key.name(), detail::type_fingerprint<T>(),
                    std::make_unique<ValueHolder<T>>(T(std::forward<U>(value)))};
        const auto it = std::ranges::find_if(entries_, matches(key));
        if (it != entries_.end()) {
            *it = std::move(entry);
        } else {
            entries_.push_back(std::move(entry));
        }
    }

    template <typename T>
    [[nodiscard]] std::optional<std::reference_wrapper<const T>> get(const ContextKey<T>& key) const noexcept {
        const auto it = std::ranges::find_if(entries_, matches(key));
        if (it == entries_.end() || it->type != detail::type_fingerprint<T>()) {
            return std::nullopt;
        }
        return std::cref(static_cast<const ValueHolder<T>&>(*it->value).value);
    }

    template <typename T>
    [[nodiscard]] T value_or(const ContextKey<T>& key, std::type_identity_t<T> fallback) const {
        if (auto value = get(key)) {
            return value->get();
        }
        return fallback;
    }

    // with calls f with the value for key if one is present, and reports whether it did.
    template <typename T, std::invocable<const T&> F>
    bool with(const ContextKey<T>& key, F&& f) const {
        if (auto value = get(key)) {
            std::invoke(std::forward<F>(f), value->get());
            return true;
        }
        return false;
    }

    template <typename T>
    bool erase(const ContextKey<T>& key) noexcept {
        return std::erase_if(entries_, matches(key)) > 0;
    }

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    std::vector<Entry> entries_;
};

}  // namespace ki
