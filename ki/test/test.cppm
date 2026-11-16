// import ki.test: the test helpers as a module. The declarations come from <ki/test/recorder.hpp>.
module;

#include <ki/test/recorder.hpp>

export module ki.test;

export namespace ki::test {
using ki::test::make_request;
using ki::test::Recorded;
using ki::test::Recorder;
}  // namespace ki::test
