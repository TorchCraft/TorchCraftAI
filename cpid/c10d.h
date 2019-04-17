#ifndef C10D_H
#define C10D_H

#ifdef HAVE_C10D
#include <c10d/ProcessGroup.hpp>
#include <c10d/Store.hpp>
#include <c10d/Types.hpp>
#else
// Basic c10d stub to get at least autocompletion working
#include <chrono>
#include <string>
#include <vector>

namespace c10d {
struct Store {
  virtual ~Store() = default;
  virtual void set(
      const std::string& key,
      const std::vector<uint8_t>& value) = 0;
  virtual std::vector<uint8_t> get(const std::string& key) = 0;
  virtual int64_t add(const std::string& key, int64_t value) = 0;
  virtual bool check(const std::vector<std::string>& keys) = 0;
  virtual void wait(const std::vector<std::string>& keys) = 0;
  virtual void wait(
      const std::vector<std::string>& keys,
      const std::chrono::milliseconds& timeout) = 0;
  void setTimeout(const std::chrono::seconds&) {}
};
struct ProcessGroup {
  struct Work;
};
enum ReduceOp {
  SUM,
  MIN,
  MAX,
};
}; // namespace c10d
#endif

#endif // C10D_H
