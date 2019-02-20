#include "assert.h"
#include <fmt/format.h>
#include <glog/logging.h>

DEFINE_bool(continue_on_assert, false, "Don't abort when an assert fails");

namespace common {

Exception::Exception(std::string_view what, const char* file, int line)
    : Exception(what, file, line, createStackTrace()) {}

Exception::Exception(
    std::string_view what,
    const char* file,
    int line,
    backward::StackTrace st)
    : std::runtime_error(Exception::formatErrorMessage(what, file, line)),
      file(file),
      line(line),
      stackTrace(st) {}

std::string Exception::formatErrorMessage(
    std::string_view what,
    const char* file,
    int line) {
  return fmt::format("{} ({}:{})", what, file, line);
}

void Exception::print() {
  backward::Printer p;
  p.print(stackTrace, stderr);
  LOG(ERROR) << what();
}

AssertionFailure::AssertionFailure(
    std::string_view condition,
    std::string_view message,
    const char* file,
    int line,
    backward::StackTrace const& st)
    : Exception(
          AssertionFailure::formatErrorMessage(condition, message),
          file,
          line,
          st),
      condition(condition) {
  print();
  LOG_IF(FATAL, !FLAGS_continue_on_assert)
      << "Aborting after exception failure. Use -continue_on_assert to throw"
      << " an exception instead";
}

std::string AssertionFailure::formatErrorMessage(
    std::string_view condition,
    std::string_view message) {
  return fmt::format("Assertion \"{}\" {}", condition, message);
}

backward::StackTrace createStackTrace() {
  using namespace backward;

  StackTrace st;
  st.load_here();

  TraceResolver resolver;
  uint64_t i;
  for (i = 0; i < st.size(); ++i) {
    ResolvedTrace resolved = resolver.resolve(st[i]);
    // Skip call to "load_here" and below
    if (resolved.source.filename.find("backward/backward.hpp") !=
        std::string::npos) {
      continue;
    }
    // Skip this function
    if (resolved.source.function == __FUNCTION__) {
      continue;
    }
    break;
  }
  if (i > 0 && i < st.size()) {
    st.load_from(st[i].addr, 32);
  }
  return st;
}

} // namespace common
