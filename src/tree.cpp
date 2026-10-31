#include "detail/tree.hpp"

#include "detail/strings.hpp"

#include <re2/re2.h>

#include <algorithm>
#include <utility>

namespace ki::detail {
namespace {

// Segment describes the next parameter or wildcard in a pattern, after chi's patNextSegment.
struct Segment {
    NodeType type = NodeType::static_text;
    std::string_view key{};
    std::string regex{};
    char tail = '/';
    std::size_t start = 0;
    std::size_t end = 0;
};

std::unexpected<Error> invalid(std::string message) {
    return std::unexpected(Error{.status = Status::internal_server_error, .message = std::move(message)});
}

// next_segment parses the next segment of a pattern; patterns that fail validate never reach it.
std::expected<Segment, Error> next_segment(std::string_view pattern) {
    const auto brace = pattern.find('{');
    const auto star = pattern.find('*');
    if (brace == std::string_view::npos && star == std::string_view::npos) {
        return Segment{.type = NodeType::static_text, .tail = 0, .start = 0, .end = pattern.size()};
    }
    if (brace != std::string_view::npos && star != std::string_view::npos && star < brace) {
        return invalid("wildcard '*' must be the last pattern in a route, otherwise use a '{param}'");
    }
    if (brace != std::string_view::npos) {
        int depth = 0;
        std::size_t close = brace;
        for (std::size_t i = brace; i < pattern.size(); ++i) {
            if (pattern[i] == '{') {
                ++depth;
            } else if (pattern[i] == '}' && --depth == 0) {
                close = i;
                break;
            }
        }
        if (close == brace) {
            return invalid("route param closing delimiter '}' is missing");
        }
        Segment segment{.type = NodeType::param, .start = brace, .end = close + 1};
        if (segment.end < pattern.size()) {
            segment.tail = pattern[segment.end];
        }
        const auto key = sub(pattern, brace + 1, close - brace - 1);
        const auto colon = key.find(':');
        segment.key = sub(key, 0, colon);
        if (colon != std::string_view::npos) {
            segment.type = NodeType::regexp;
            segment.regex = std::string{sub(key, colon + 1)};
            if (!segment.regex.empty()) {
                if (segment.regex.front() != '^') {
                    segment.regex.insert(0, 1, '^');
                }
                if (segment.regex.back() != '$') {
                    segment.regex.push_back('$');
                }
            }
        }
        return segment;
    }
    if (star < pattern.size() - 1) {
        return invalid("wildcard '*' must be the last value in a route; trim trailing text or use a '{param}' instead");
    }
    return Segment{.type = NodeType::catch_all, .key = "*", .tail = 0, .start = star, .end = pattern.size()};
}

// segment_of is next_segment for validated patterns.
Segment segment_of(std::string_view pattern) {
    auto segment = next_segment(pattern);
    return segment ? std::move(*segment) : Segment{.type = NodeType::static_text, .end = pattern.size()};
}

std::shared_ptr<const re2::RE2> compile(const std::string& regex) {
    re2::RE2::Options options{};
    options.set_log_errors(false);
    auto compiled = std::make_shared<const re2::RE2>(regex, options);
    return compiled->ok() ? compiled : nullptr;
}

std::vector<std::string> param_keys(std::string_view pattern) {
    std::vector<std::string> keys{};
    for (;;) {
        const auto segment = segment_of(pattern);
        if (segment.type == NodeType::static_text) {
            return keys;
        }
        keys.emplace_back(segment.key);
        pattern = sub(pattern, segment.end);
    }
}

std::size_t longest_prefix(std::string_view a, std::string_view b) noexcept {
    const auto [end_a, end_b] = std::ranges::mismatch(a, b);
    return static_cast<std::size_t>(end_a - a.begin());
}

void sort_children(std::vector<std::unique_ptr<Node>>& nodes) {
    std::ranges::sort(nodes, {}, [](const std::unique_ptr<Node>& n) { return n->label; });
    // Parameter nodes ending at '/' are tried last, so more specific delimiters win; chi's tailSort.
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        if ((*it)->type != NodeType::static_text && (*it)->tail == '/') {
            std::iter_swap(it, nodes.rbegin());
            return;
        }
    }
}

// find_edge is chi's binary search over children sorted by label.
NodeRef find_edge(const std::vector<std::unique_ptr<Node>>& nodes, char label) {
    if (nodes.empty()) {
        return std::nullopt;
    }
    std::size_t index = 0;
    std::ptrdiff_t low = 0;
    auto high = static_cast<std::ptrdiff_t>(nodes.size()) - 1;
    while (low <= high) {
        index = static_cast<std::size_t>(low + (high - low) / 2);
        if (label > nodes[index]->label) {
            low = static_cast<std::ptrdiff_t>(index) + 1;
        } else if (label < nodes[index]->label) {
            high = static_cast<std::ptrdiff_t>(index) - 1;
        } else {
            break;
        }
    }
    if (nodes[index]->label != label) {
        return std::nullopt;
    }
    return *nodes[index];
}

NodeRef find_typed_edge(const Node& node, NodeType type, char label) {
    const auto& nodes = node.children[static_cast<std::size_t>(type)];
    if (type == NodeType::catch_all) {
        return nodes.empty() ? NodeRef{} : NodeRef{*nodes.front()};
    }
    return find_edge(nodes, label);
}

std::optional<std::reference_wrapper<Node>> get_edge(Node& node, NodeType type, char label, char tail,
                                                     std::string_view prefix) {
    for (const auto& child : node.children[static_cast<std::size_t>(type)]) {
        if (child->label == label && child->tail == tail) {
            if (type == NodeType::regexp && child->prefix != prefix) {
                continue;
            }
            return *child;
        }
    }
    return std::nullopt;
}

void set_endpoint(Node& node, const Methods& methods, std::string_view pattern, const Endpoint& endpoint) {
    if (!node.endpoints) {
        node.endpoints.emplace();
    }
    auto& endpoints = *node.endpoints;
    Endpoint value = endpoint;
    value.pattern = std::string{pattern};
    value.param_keys = param_keys(pattern);
    if (methods.stub) {
        auto& stub = endpoints[std::string{stub_method}];
        stub.handler = endpoint.handler;
        stub.endpoint = endpoint.endpoint;
        stub.id = endpoint.id;
    }
    if (methods.all) {
        endpoints[std::string{any_method}] = value;
    }
    for (const auto& name : methods.names) {
        endpoints[name] = value;
    }
}

Node& add_child(Node& parent, std::unique_ptr<Node> child, std::string_view prefix) {
    std::string_view search = prefix;
    std::reference_wrapper<Node> leaf = *child;
    auto segment = segment_of(search);

    if (segment.type != NodeType::static_text) {
        if (segment.type == NodeType::regexp) {
            child->prefix = segment.regex;
            child->regex = compile(segment.regex);
        }
        if (segment.start == 0) {
            child->type = segment.type;
            const auto rest = segment.type == NodeType::catch_all ? search.size() : segment.end;
            child->tail = segment.tail;
            if (rest != search.size()) {
                search = sub(search, rest);
                auto next = std::make_unique<Node>();
                next->label = search.front();
                next->prefix = std::string{search};
                leaf = add_child(*child, std::move(next), search);
            }
        } else {
            child->type = NodeType::static_text;
            child->prefix = std::string{sub(search, 0, segment.start)};
            child->regex = nullptr;
            search = sub(search, segment.start);
            auto next = std::make_unique<Node>();
            next->type = segment.type;
            next->label = search.front();
            next->tail = segment.tail;
            leaf = add_child(*child, std::move(next), search);
        }
    }

    auto& siblings = parent.children[static_cast<std::size_t>(child->type)];
    siblings.push_back(std::move(child));
    sort_children(siblings);
    return leaf;
}

std::unique_ptr<Node> replace_child(Node& parent, char label, char tail, std::unique_ptr<Node> child) {
    auto& siblings = parent.children[static_cast<std::size_t>(child->type)];
    for (auto& existing : siblings) {
        if (existing->label == label && existing->tail == tail) {
            child->label = label;
            child->tail = tail;
            return std::exchange(existing, std::move(child));
        }
    }
    return nullptr;
}

bool leaf_handles(const Node& node, std::string_view method, FindState& state) {
    if (!node.endpoints) {
        return false;
    }
    const auto& endpoints = *node.endpoints;
    if (const auto it = endpoints.find(method); it != endpoints.end() && it->second.handler) {
        state.keys.insert(state.keys.end(), it->second.param_keys.begin(), it->second.param_keys.end());
        return true;
    }
    for (const auto& [name, endpoint] : endpoints) {
        if (name != any_method && name != stub_method && !std::ranges::contains(state.methods_allowed, name)) {
            state.methods_allowed.push_back(name);
        }
    }
    state.method_not_allowed = true;
    return false;
}

// find_route is chi's findRoute: try static, regexp, param and catch-all children in turn, backtracking
// and unwinding recorded parameter values when a branch fails.
NodeRef find_route(const Node& node, std::string_view method, std::string_view search, FindState& state) {
    for (std::size_t t = 0; t < node_type_count; ++t) {
        const auto& nodes = node.children[t];
        if (nodes.empty()) {
            continue;
        }
        const auto type = static_cast<NodeType>(t);
        NodeRef next{};
        std::string_view rest = search;
        const char label = search.empty() ? '\0' : search.front();

        switch (type) {
            case NodeType::static_text:
                next = find_edge(nodes, label);
                if (!next || !rest.starts_with(next->get().prefix)) {
                    continue;
                }
                rest = sub(rest, next->get().prefix.size());
                break;

            case NodeType::param:
            case NodeType::regexp: {
                if (rest.empty()) {
                    continue;
                }
                for (const auto& child : nodes) {
                    next = *child;
                    auto end = rest.find(child->tail);
                    if (end == std::string_view::npos) {
                        if (child->tail != '/') {
                            continue;
                        }
                        end = rest.size();
                    } else if (type == NodeType::regexp && end == 0) {
                        continue;
                    }
                    const auto value = sub(rest, 0, end);
                    if (type == NodeType::regexp && child->regex) {
                        if (!re2::RE2::PartialMatch(value, *child->regex)) {
                            continue;
                        }
                    } else if (value.find('/') != std::string_view::npos) {
                        continue;  // a parameter never spans path segments
                    }

                    const auto recorded = state.values.size();
                    state.values.push_back(value);
                    rest = sub(rest, end);
                    if (rest.empty() && leaf_handles(*child, method, state)) {
                        return *child;
                    }
                    if (auto found = find_route(*child, method, rest, state)) {
                        return found;
                    }
                    state.values.resize(recorded);
                    rest = search;
                }
                state.values.emplace_back();
                break;
            }

            case NodeType::catch_all:
                state.values.push_back(search);
                next = *nodes.front();
                rest = {};
                break;
        }

        if (!next) {
            continue;
        }
        const Node& child = next->get();
        if (rest.empty() && leaf_handles(child, method, state)) {
            return child;
        }
        if (auto found = find_route(child, method, rest, state)) {
            return found;
        }
        if (child.type != NodeType::static_text && !state.values.empty()) {
            state.values.pop_back();
        }
    }
    return std::nullopt;
}

bool find_pattern_in(const Node& node, std::string_view pattern) {
    for (const auto& nodes : node.children) {
        if (nodes.empty() || pattern.empty()) {
            continue;
        }
        const auto edge = find_typed_edge(node, nodes.front()->type, pattern.front());
        if (!edge) {
            continue;
        }
        const Node& child = edge->get();
        std::size_t consumed = 0;
        switch (child.type) {
            case NodeType::static_text:
                consumed = longest_prefix(pattern, child.prefix);
                if (consumed < child.prefix.size()) {
                    continue;
                }
                break;
            case NodeType::param:
            case NodeType::regexp: consumed = std::min(pattern.find('}'), pattern.size()) + 1; break;
            case NodeType::catch_all: consumed = longest_prefix(pattern, "*"); break;
        }
        const auto rest = sub(pattern, consumed);
        if (rest.empty()) {
            return true;
        }
        return find_pattern_in(child, rest);
    }
    return false;
}

}  // namespace

Result Tree::validate(std::string_view pattern) {
    std::vector<std::string_view> keys{};
    std::string_view rest = pattern;
    for (;;) {
        auto segment = next_segment(rest);
        if (!segment) {
            return std::unexpected(Error{.message = segment.error().message + " in '" + std::string{pattern} + "'"});
        }
        if (segment->type == NodeType::static_text) {
            return {};
        }
        if (segment->type == NodeType::regexp && !compile(segment->regex)) {
            return invalid("invalid regexp pattern '" + segment->regex + "' in route param in '" + std::string{pattern} +
                           "'");
        }
        if (std::ranges::contains(keys, segment->key)) {
            return invalid("routing pattern '" + std::string{pattern} + "' contains duplicate param key, '" +
                           std::string{segment->key} + "'");
        }
        keys.push_back(segment->key);
        rest = sub(rest, segment->end);
    }
}

Node& Tree::insert(const Methods& methods, std::string_view pattern, const Endpoint& endpoint) {
    std::reference_wrapper<Node> node = root_;
    std::string_view search = pattern;

    for (;;) {
        if (search.empty()) {
            set_endpoint(node, methods, pattern, endpoint);
            return node;
        }

        const char label = search.front();
        Segment segment{.tail = 0};
        if (label == '{' || label == '*') {
            segment = segment_of(search);
        }
        const std::string_view regex_prefix = segment.type == NodeType::regexp ? segment.regex : std::string_view{};

        Node& parent = node;
        const auto edge = get_edge(parent, segment.type, label, segment.tail, regex_prefix);

        if (!edge) {
            auto child = std::make_unique<Node>();
            child->label = label;
            child->tail = segment.tail;
            child->prefix = std::string{search};
            Node& leaf = add_child(parent, std::move(child), search);
            set_endpoint(leaf, methods, pattern, endpoint);
            return leaf;
        }
        node = *edge;

        if (node.get().type != NodeType::static_text) {
            search = sub(search, segment.end);
            continue;
        }

        const auto common = longest_prefix(search, node.get().prefix);
        if (common == node.get().prefix.size()) {
            search = sub(search, common);
            continue;
        }

        // Split the static node at the common prefix.
        auto split = std::make_unique<Node>();
        split->prefix = std::string{sub(search, 0, common)};
        Node& split_node = *split;
        auto existing = replace_child(parent, search.front(), segment.tail, std::move(split));
        existing->label = existing->prefix[common];
        existing->prefix.erase(0, common);
        const std::string existing_prefix = existing->prefix;
        add_child(split_node, std::move(existing), existing_prefix);

        search = sub(search, common);
        if (search.empty()) {
            set_endpoint(split_node, methods, pattern, endpoint);
            return split_node;
        }
        auto rest = std::make_unique<Node>();
        rest->label = search.front();
        rest->prefix = std::string{search};
        Node& leaf = add_child(split_node, std::move(rest), search);
        set_endpoint(leaf, methods, pattern, endpoint);
        return leaf;
    }
}

NodeRef Tree::find(std::string_view method, std::string_view path, FindState& state) const {
    return find_route(root_, method, path, state);
}

bool Tree::find_pattern(std::string_view pattern) const { return find_pattern_in(root_, pattern); }

}  // namespace ki::detail
