module;

#include <asio/awaitable.hpp>

#include <expected>
#include <string>

export module ki_module_check;

export namespace ki_module_check {

using Result = std::expected<int, std::string>;

asio::awaitable<Result> answer() { co_return 42; }

}  // namespace ki_module_check
