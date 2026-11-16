// import ki.log: structured logging as a module. The declarations come from <ki/log/log.hpp>.
module;

#include <ki/log/log.hpp>

export module ki.log;

export namespace ki::log {
using ki::log::AsyncSink;
using ki::log::Attr;
using ki::log::fd_writer;
using ki::log::JsonSink;
using ki::log::Level;
using ki::log::Logger;
using ki::log::NullSink;
using ki::log::Record;
using ki::log::Sink;
using ki::log::TextSink;
using ki::log::to_string;
using ki::log::Value;
using ki::log::Writer;
}  // namespace ki::log
