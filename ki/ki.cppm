// import ki: the main library as a module. The declarations come from <ki/ki.hpp>; macros such as
// KI_VERSION_STRING are not part of a module and still need <ki/version.hpp>. Asio is not re-exported: code
// that writes handlers includes the Asio headers it uses, since a module keeps only the parts of a header it
// names itself and coroutine code needs the rest.
module;

#include <ki/ki.hpp>

export module ki;

export namespace ki {
using ki::chain;
using ki::Context;
using ki::ContextKey;
using ki::Error;
using ki::FormOptions;
using ki::Handler;
using ki::Headers;
using ki::HostRouter;
using ki::Middleware;
using ki::ParsedForm;
using ki::ParsedQuery;
using ki::QueryOptions;
using ki::QueryValues;
using ki::reason_phrase;
using ki::redirect;
using ki::Request;
using ki::Response;
using ki::Result;
using ki::Route;
using ki::RouteContext;
using ki::RouteEndpoint;
using ki::RouteParams;
using ki::Router;
using ki::Status;
using ki::strip_prefix;
using ki::valid_header_name;
using ki::valid_header_value;
using ki::version;
using ki::walk;
using ki::WalkFn;
}  // namespace ki

export namespace ki::url {
using ki::url::Component;
using ki::url::needs_unescape;
using ki::url::parse_target;
using ki::url::path_escape;
using ki::url::query_escape;
using ki::url::Target;
using ki::url::TargetForm;
using ki::url::unescape;
}  // namespace ki::url
