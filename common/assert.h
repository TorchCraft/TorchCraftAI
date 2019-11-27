#pragma once

#include <backward/backward.hpp>
#include <gflags/gflags.h>
#include <stdexcept>
#include <string>
#include <string_view>

DECLARE_bool(continue_on_assert);

namespace common {

class Exception : public std::runtime_error {
 public:
  Exception(
      std::string_view what,
      const char* file = "unknown_file",
      int line = 0);
  Exception(
      std::string_view what,
      const char* file,
      int line,
      backward::StackTrace st);

  const char* file;
  int line;
  backward::StackTrace stackTrace;

  void print();

 protected:
  static std::string
  formatErrorMessage(std::string_view message, const char* file, int line);
};

class AssertionFailure : public Exception {
 public:
  AssertionFailure(
      std::string_view condition,
      std::string_view message,
      const char* file,
      int line,
      backward::StackTrace const& st);

  std::string_view condition;

 protected:
  static std::string formatErrorMessage(
      std::string_view condition,
      std::string_view message);
};

// Returns a StackTrace that starts in the caller function
backward::StackTrace createStackTrace();

} // namespace common

#define ASSERT_MAKE_STR(s) #s
#define ASSERT_2(condition, message)   \
  do {                                 \
    if (!(condition)) {                \
      throw common::AssertionFailure(  \
          ASSERT_MAKE_STR(condition),  \
          message,                     \
          __FILE__,                    \
          __LINE__,                    \
          common::createStackTrace()); \
    }                                  \
  } while (false)
#define ASSERT_1(condition) ASSERT_2(condition, "")

// The trick below allows the user to specify either 1 or 2 arguments
// to the ASSERT macro, without having to use explicitely ASSERT_1 or ASSERT_2
// https://stackoverflow.com/questions/3046889/optional-parameters-with-c-macros
#define GET_3RD_ARG(arg1, arg2, arg3, ...) arg3
#define ASSERT_MACRO_CHOOSER(...) GET_3RD_ARG(__VA_ARGS__, ASSERT_2, ASSERT_1, )

// MSVC preprocessor is broken and doesn't like our smart trick to overload
// macros See
// https://stackoverflow.com/questions/52537699/preprocessor-inconsistencies-in-visual-studio
#ifdef _MSC_VER
#define ASSERT(cond, ...) ASSERT_1(cond)
#else
// Takes 1 or 2 arguments: the condition, and an optional message
#define ASSERT(...) ASSERT_MACRO_CHOOSER(__VA_ARGS__)(__VA_ARGS__)
#endif

#ifndef NDEBUG
// Debug mode
#define DASSERT ASSERT
#else
// Suppress debug asserts, but avoid "unused" warnings and other bugs
// See http://cnicholson.net/2009/02/stupid-c-tricks-adventures-in-assert/
#define DASSERT_2(x, y) \
  do {                  \
    (void)sizeof(x);    \
    (void)sizeof(y);    \
  } while (false)
#define DASSERT_1(x) \
  do {               \
    (void)sizeof(x); \
  } while (false)
#define DASSERT_MACRO_CHOOSER(...) \
  GET_3RD_ARG(__VA_ARGS__, DASSERT_2, DASSERT_1, )
#ifdef _MSC_VER
#define DASSERT(cond, ...) DASSERT_1(cond)
#else
#define DASSERT(...) DASSERT_MACRO_CHOOSER(__VA_ARGS__)(__VA_ARGS__)
#endif

#endif