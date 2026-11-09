#pragma once
// Force-included, via nam_core, into every C++ file that can see nlohmann::json.
//
// nlohmann guards its checks with JSON_ASSERT, which is assert() and so compiled
// out of a release build. NAM Core reads a model's fields with operator[] on a
// const json, where a missing key is one of those checks: without it, the lookup
// dereferences end() and copies whatever memory is there. A .nam file that
// parses but lacks a field (an error page saved as JSON, a truncated export)
// then crashes, or not, depending on the heap. Throwing instead makes it the
// ordinary "Invalid model" error ModelManager already reports.
//
// The other checks are nlohmann's own invariants, some inside noexcept
// functions; if one ever failed it would now terminate rather than carry on
// with a broken object. The throw is in a function of its own so the compilers
// don't warn about those call sites.
//
// It has to be the same in every file that includes json.hpp, hence the force
// include rather than a define in any one source.
#include <stdexcept>

namespace evil {
[[noreturn]] inline void jsonAssertFailed() {
  throw std::runtime_error("the file is missing a field it needs");
}
} // namespace evil

#define JSON_ASSERT(x) ((x) ? static_cast<void>(0) : ::evil::jsonAssertFailed())
