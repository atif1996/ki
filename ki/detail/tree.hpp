#pragma once

#include <ki/error.hpp>
#include <ki/handler.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace re2 {
class RE2;
}

// The routing tree is a port of chi's tree.go (itself based on armon/go-radix): the same node types,
// insertion splits and matching order, so chi's tree tests apply unchanged. Patterns are validated before
// insertion, so insertion itself cannot fail and a rejected pattern leaves the tree untouched.
namespace ki::detail {

struct RouterState;

enum class NodeType : std::uint8_t { static_text, regexp, param, catch_all };
inline constexpr std::size_t node_type_count = 4;

// Endpoint keys besides real method names: one for every method, and the stub chi installs for mounts.
inline constexpr std::string_view any_method = "*";
inline constexpr std::string_view stub_method = "#stub";

struct Endpoint {
    Handler handler{};                      // with inline middlewares applied
    Handler endpoint{};                     // as registered, for walk()
    std::vector<Middleware> middlewares{};  // inline middlewares, for walk()
    std::uint64_t id = 0;                   // identifies the registered handler; a mount's stub shares the mount's id
    std::string pattern{};
    std::vector<std::string> param_keys{};
};

using Endpoints = std::map<std::string, Endpoint, std::less<>>;

struct Node {
    NodeType type = NodeType::static_text;
    char label = 0;
    char tail = 0;
    std::string prefix{};
    std::shared_ptr<const re2::RE2> regex{};
    std::optional<Endpoints> endpoints{};  // engaged on leaves
    std::array<std::vector<std::unique_ptr<Node>>, node_type_count> children{};
    std::shared_ptr<RouterState> subroutes{};
};

using NodeRef = std::optional<std::reference_wrapper<const Node>>;

// Methods selects which endpoint keys an insertion sets: one method, or every method in names plus
// any_method; stub also sets the mount stub.
struct Methods {
    std::vector<std::string> names{};
    bool all = false;
    bool stub = false;
};

// FindState accumulates what a lookup learns: parameter keys and values (views into the searched path),
// and on a method mismatch the methods the path does allow.
struct FindState {
    std::vector<std::string> keys{};
    std::vector<std::string_view> values{};
    std::vector<std::string> methods_allowed{};
    bool method_not_allowed = false;
};

// Paths and patterns are matched in a routing form: percent-escapes are decoded, as Go's ServeMux does per
// segment, and then the characters that structure a route ('%', '/', '{', '}', '*') are re-escaped so a
// decoded "%2F" never splits a segment and a decoded "%7B" never starts a parameter.

// routing_path converts a raw request path to routing form.
[[nodiscard]] std::string routing_path(std::string_view raw_path);

// routing_pattern converts a validated pattern to routing form; "{name...}" becomes "*".
[[nodiscard]] std::string routing_pattern(std::string_view pattern);

// pattern_shape is a routing pattern with parameter names removed, so patterns that differ only in their
// parameter names compare equal.
[[nodiscard]] std::string pattern_shape(std::string_view routing_pattern);

// decode_value turns a value captured from a routing-form path back into the text it stands for.
[[nodiscard]] std::string decode_value(std::string_view value);

class Tree {
public:
    // parse checks a pattern's braces, wildcard position, regexes and parameter names, and returns the
    // parameter names in order.
    [[nodiscard]] static std::expected<std::vector<std::string>, Error> parse(std::string_view pattern);

    // insert adds an endpoint at a routing-form pattern and returns the leaf node. The endpoint carries the
    // pattern as written and its parameter names.
    Node& insert(const Methods& methods, std::string_view routing_pattern, const Endpoint& endpoint);

    [[nodiscard]] NodeRef find(std::string_view method, std::string_view path, FindState& state) const;

    // find_pattern reports whether a registered route has the same shape as pattern.
    [[nodiscard]] bool find_pattern(std::string_view pattern) const;

    [[nodiscard]] const Node& root() const noexcept { return root_; }

private:
    Node root_{};
};

}  // namespace ki::detail
