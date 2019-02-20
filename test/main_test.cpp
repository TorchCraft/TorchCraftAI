/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "cherrypi.h"
#include "common/assert.h"
#include "common/rand.h"
#include "test.h"
#include "utils.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#ifndef WITHOUT_POSIX
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#endif // !WITHOUT_POSIX

#include <condition_variable>
#include <thread>

// gflags options
DECLARE_bool(help);
DECLARE_bool(helpshort);
DECLARE_string(helpmatch);
DECLARE_double(rtfactor);
DEFINE_double(
    rtfactor,
    -1,
    "real-time factor (speed) (-1: unlimitted/fastest)");

// lest options
DEFINE_bool(abort, false, "abort at first failure");
DEFINE_bool(count, false, "count selected tests");
DEFINE_bool(list_tags, false, "list tags of selected tests");
DEFINE_bool(list_tests, false, "list selected tests");
DEFINE_bool(pass, false, "also report passing tests");
DEFINE_bool(time, false, "list duration of selected tests");
DEFINE_string(
    order,
    "declared",
    "test order ('declared', 'lexical' or 'random')");
DEFINE_uint64(seed, 0, "use n for random generator seed");
DEFINE_int32(repeat, 1, "repeat selected tests n times (-1: indefinite)");

DEFINE_bool(logsinktostderr, true, "Log sink to stderror.");
DEFINE_string(logsinkdir, "", "Optional directory to write sink log files");
DEFINE_int32(
    j,
    0,
    "run this many tests in parallel (0 uses the vanilla lest tests runner)");

DEFINE_string(junit_xml_dump, "", "Dump test results as JUnit-like XML file");

namespace {

std::string encode_xml(std::string const& data) {
  std::string buffer;
  buffer.reserve(data.size());
  for (size_t pos = 0; pos != data.size(); ++pos) {
    switch (data[pos]) {
      case '&':
        buffer.append("&amp;");
        break;
      case '\"':
        buffer.append("&quot;");
        break;
      case '\'':
        buffer.append("&apos;");
        break;
      case '<':
        buffer.append("&lt;");
        break;
      case '>':
        buffer.append("&gt;");
        break;
      default:
        buffer.append(&data[pos], 1);
        break;
    }
  }
  return buffer;
}

struct testresult {
  testresult(std::string j) : job_name(std::move(j)) {}
  std::string job_name;
  std::unique_ptr<lest::message> fail_message;
};

class testresultsdumper : public std::vector<testresult> {
  std::ostream& os;

 public:
  testresultsdumper(std::ostream& o) : os(o) {}
  void dumpMaybe() {
    if (FLAGS_junit_xml_dump.empty()) {
      return;
    }
    try {
      std::ofstream f(FLAGS_junit_xml_dump);
      f << "<testsuite tests='" << size() << "'>" << std::endl;
      for (auto& r : *this) {
        auto split = cherrypi::utils::stringSplit(r.job_name, '/', 1);
        auto class_name = encode_xml(split[0]);
        auto test_name = split.size() == 2 ? encode_xml(split[1]) : "";
        f << "<testcase classname=\"" << class_name << "\" name=\"" << test_name
          << "\"";
        if (r.fail_message) {
          std::string fail_str = r.fail_message->where.file + ":" +
              std::to_string(r.fail_message->where.line) + ": " +
              r.fail_message->what();
          f << "><failure type=\"" << encode_xml(r.fail_message->kind) << "\">"
            << encode_xml(fail_str) << "</failure></testcase>";
        } else {
          f << "/>";
        }
        f << std::endl;
      }
      f << "</testsuite>" << std::endl;
    } catch (std::exception& e) {
      os << "Exception while writing test JUnit XML file: " << e.what();
    }
  }
};

// Parallel test runner for lest
// It's not super pretty but should do the job.
struct prun : lest::action {
  struct job {
    int pid;
    lest::text name;
    int spipe;
  };

  struct jobresult {
    job j;
    bool finished = false;
    int status;
    lest::message* msg = nullptr;
    std::string out;
  };

  lest::env output;
  lest::options option;
  int njobs = 1;
  int selected = 0;
  int failures = 0;
  bool is_parent = true;
  std::atomic<int> running{0};
  std::atomic<bool> stop{false};
  std::mutex jobMutex;
  std::condition_variable jobFinished;
  std::list<jobresult> jobs;
  testresultsdumper testresults;
  std::thread mth;

  prun(std::ostream& os, lest::options option, int njobs)
      : action(os),
        output(os, option.pass),
        option(option),
        njobs(njobs),
        testresults(os),
        mth(&prun::monitor, this) {}

  ~prun() {
    stop.store(true);
    mth.join();
    if (failures > 0) {
      os << failures << " out of " << selected << " selected "
         << lest::pluralise("test", selected) << " "
         << lest::colourise("failed.\n");
    } else if (option.pass) {
      os << "All " << selected << " selected "
         << lest::pluralise("test", selected) << " "
         << lest::colourise("passed.\n");
    }
    // This dtor is called several times (because we fork)
    if (is_parent) {
      testresults.dumpMaybe();
    }
  }

  operator int() {
    return failures;
  }
  bool abort() {
    return option.abort && failures > 0;
  }

  prun& operator()(lest::test testing) {
    selected++;

    {
      // Wait if too many jobs are currently running
      std::unique_lock<std::mutex> lock(jobMutex);
      jobFinished.wait(lock, [this] { return running < njobs; });
    }

    int sp[2];
    if (::pipe(sp) != 0) {
      throw std::system_error(errno, std::system_category());
    }

    int pid = fork();
    if (pid < 0) {
      close(sp[0]);
      close(sp[1]);
      throw std::system_error(errno, std::system_category());
    } else if (pid == 0) {
      // child
      is_parent = false;
      close(sp[0]);
      int fd = sp[1];

      auto reportTestFailureAndExit = [&](lest::message const& e) {
        // We can't communicate the exit code to the monitor thread since
        // CherryPi installs a global signal handler for reaping child
        // processes.
        dprintf(fd, "F%c", 0);
        dprintf(fd, "%s%c", e.kind.c_str(), 0);
        dprintf(fd, "%s%c", e.what(), 0);
        dprintf(fd, "%s%c", e.where.file.c_str(), 0);
        dprintf(fd, "%d%c", e.where.line, 0);
        dprintf(fd, "%s%c", e.note.info.c_str(), 0);
        std::_Exit(1);
      };
      try {
        testing.behaviour(output(testing.name));
      } catch (lest::message const& e) {
        reportTestFailureAndExit(e);
      } catch (std::exception const& e) {
        reportTestFailureAndExit(lest::message(
            "exception" /* kind */, lest::location("unknown", 0), e.what()));
      } catch (...) {
        reportTestFailureAndExit(lest::message(
            "exception" /* kind */,
            lest::location("unknown", 0),
            "Unknown exception caught"));
      }
      dprintf(fd, "S%c", 0);
      std::_Exit(0);
    }

    // parent
    close(sp[1]);

    job j;
    j.pid = pid;
    j.name = testing.name;
    j.spipe = sp[0];
    // Make pipe non-blocking
    int flags = fcntl(j.spipe, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(j.spipe, F_SETFL, flags);

    {
      std::unique_lock<std::mutex> lock(jobMutex);
      jobs.emplace_back();
      jobs.back().j = j;
      running++;
    }
    return *this;
  }

  // Monitoring thread
  void monitor() {
    std::array<char, 256> buf;

    while (true) {
      if (running == 0 && stop.load()) {
        break;
      }

      std::lock_guard<std::mutex> lock(jobMutex);
      for (auto& result : jobs) {
        if (result.finished) {
          continue;
        }
        auto& job = result.j;
        auto cleanup = [&] {
          running--;
          result.finished = true;
          close(job.spipe);
          jobFinished.notify_all();
        };

        // Attempt to read output
        // In theory we could poll all processes at once, but they will only
        // write to the pipe once when they're done anyway, and this is not
        // exactly a low-latency setup.
        struct pollfd pfd;
        pfd.fd = job.spipe;
        pfd.events = POLLIN;

        auto ret = poll(&pfd, 1, 0);
        if (ret > 0 && (pfd.events & POLLIN)) {
          // Data!
          ssize_t nread = 0;
          while (true) {
            nread = read(job.spipe, buf.data(), buf.size());
            if (nread <= 0) {
              break;
            }
            result.out.append(std::string(buf.data(), nread));
          }
        }

        // Still alive? A signal value of 0 will just check if the process
        // actually exists.
        ret = kill(job.pid, 0);
        if (ret != 0 && errno != ESRCH) {
          std::cerr << "Test runner error: can't check for " << job.pid << " ("
                    << job.name << "): " << google::StrError(errno)
                    << std::endl;
          failures++;
          cleanup();
        } else if (ret != 0 && errno == ESRCH) {
          // Done - parse result
          auto outs = cherrypi::utils::stringSplit(result.out, '\0');
          testresults.emplace_back(job.name);
          if (outs[0] == "S") {
            // success
          } else if (outs[0] == "F") {
            failures++;
            auto loc = lest::location(outs[3], std::stoi(outs[4]));
            auto msg =
                std::make_unique<lest::message>(outs[1], loc, outs[2], outs[5]);
            lest::report(os, *msg, job.name);
            testresults.back().fail_message = std::move(msg);
          } else {
            std::cerr << "Test runner error: unknown status '" << outs[0]
                      << "' for " << job.pid << "(" << job.name << ")";
          }
          cleanup();
        }
      }

      std::this_thread::yield();
    }
  }
};

struct seqrun : lest::confirm {
  testresultsdumper testresults;
  seqrun(std::ostream& os, lest::options option)
      : lest::confirm(os, option), testresults(os) {}

  ~seqrun() {
    testresults.dumpMaybe();
  }

  seqrun& operator()(lest::test testing) {
    testresults.emplace_back(testing.name);
    auto reportTestFailure = [&](lest::message const& e) {
      testresults.back().fail_message = std::make_unique<lest::message>(
          e.kind, e.where, e.what(), e.note.info);
      ++failures;
      report(os, e, testing.name);
    };

    try {
      ++selected;
      testing.behaviour(output(testing.name));
    } catch (lest::message const& e) {
      reportTestFailure(e);
    } catch (std::exception const& e) {
      reportTestFailure(lest::message(
          "exception" /* kind */, lest::location("unknown", 0), e.what()));
    } catch (...) {
      reportTestFailure(lest::message(
          "exception" /* kind */,
          lest::location("unknown", 0),
          "Unknown exception caught"));
    }
    return *this;
  }
};

// Set up lest options from gflags
std::tuple<lest::options, lest::texts> parseLestArguments(
    int argc,
    char** argv) {
  lest::options option;
  option.abort = FLAGS_abort;
  option.count = FLAGS_count;
  option.tags = FLAGS_list_tags;
  option.list = FLAGS_list_tests;
  option.pass = FLAGS_pass;
  option.time = FLAGS_time;
  option.seed = FLAGS_seed;
  option.repeat = FLAGS_repeat;
  if (FLAGS_order == "lexical") {
    option.lexical = true;
  } else if (FLAGS_order == "random") {
    option.random = true;
  } else if (FLAGS_order != "declared") {
    throw std::runtime_error("Unknown test order " + FLAGS_order);
  }

  lest::texts in;
  for (int i = 1; i < argc; i++) {
    in.push_back(argv[i]);
  }

  return std::make_tuple(option, in);
}

// Port of lest::run for explicit arguments
int runLest(
    lest::tests specification,
    std::tuple<lest::options, lest::texts> opts) {
  std::ostream& os = std::cout;
  try {
    auto option = std::get<0>(opts);
    auto in = std::get<1>(opts);

    common::Rand::setSeed(option.seed);
    if (option.lexical) {
      lest::sort(specification);
    }
    if (option.random) {
      lest::shuffle(specification, option);
    }

    if (option.count) {
      return lest::for_test(specification, in, lest::count(os));
    }
    if (option.list) {
      return lest::for_test(specification, in, lest::print(os));
    }
    if (option.tags) {
      return lest::for_test(specification, in, lest::ptags(os));
    }
    if (option.time) {
      return lest::for_test(specification, in, lest::times(os, option));
    }

    if (FLAGS_j == 0) {
      return lest::for_test(
          specification, in, seqrun(os, option), option.repeat);
    } else {
      return lest::for_test(
          specification, in, prun(os, option, FLAGS_j), option.repeat);
    }
  } catch (std::exception const& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}

} // namespace

lest::tests& specification() {
  static lest::tests tests;
  return tests;
}

int main(int argc, char* argv[]) {
  cherrypi::init();
  FLAGS_minloglevel = google::ERROR;
  FLAGS_continue_on_assert = true;
  google::InitGoogleLogging(argv[0]);

  gflags::SetUsageMessage(
      "[options] [test-spec ...]\n\n"
      "  Test specification:\n"
      "    \"@\", \"*\" all tests, unless excluded\n"
      "    empty    all tests, unless tagged [hide] or [.optional-name]\n"
#if lest_FEATURE_REGEX_SEARCH
      "    \"re\"     select tests that match regular expression\n"
      "    \"!re\"    omit tests that match regular expression"
#else
      "    \"text\"   select tests that contain text (case insensitive)\n"
      "    \"!text\"  omit tests that contain text (case insensitive)"
#endif
  );

  // Limit help output to relevant flags only
  gflags::ParseCommandLineNonHelpFlags(&argc, &argv, true);
  if (FLAGS_help) {
    if (FLAGS_helpmatch.empty()) {
      FLAGS_helpmatch = "main_test";
      FLAGS_help = false;
    }
  }
  gflags::HandleCommandLineHelpFlags();
  cherrypi::initLogging(argv[0], FLAGS_logsinkdir, FLAGS_logsinktostderr);

  // Setup lest options
  int status = runLest(specification(), parseLestArguments(argc, argv));

  cherrypi::shutdown(FLAGS_logsinktostderr);
  return status;
}
