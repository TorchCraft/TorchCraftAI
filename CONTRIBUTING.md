# Contributing to CherryPi
We want to make contributing to this project as easy and transparent as
possible.

## Our Development Process
We develop CherryPi on an internal repository. We intend to periodically
publish updates to this public repository with the latest features that are
known to work well and are ready for you to use.

## Pull Requests
Because this repository is a periodic publication of an upstream repository, we
may accept pull requests but can not guarantee that we can do so or that changes
will persist across versions. We recommend that you fork CherryPi and apply
changes to your forked repository.

For changes that you would like to apply, we recommend opening an issue and
discussing it before opening a pull request. To make changes, you will need to
complete the Facebook Contributor License Agreement (see below).

To make a change after discussing it:
1. Fork the repo and create your branch from `master`.
2. If you've added code that should be tested, add tests.
3. Update the documentation.
4. Ensure the test suite passes.
5. Apply clang-format (see below).
6. If you haven't already, complete the Contributor License Agreement ("CLA").

## Contributor License Agreement ("CLA")
In order to accept your pull request, we need you to submit a CLA. You only need
to do this once to work on any of Facebook's open source projects.

Complete your CLA here: <https://code.facebook.com/cla>

## Issues
We use GitHub issues to track public bugs. Please ensure your description is
clear and has sufficient instructions to be able to reproduce the issue.

## Coding Style
Use clang-format. The top-level directory of the repository contains a
`.clang-format` style file.

We're using the [Google Logging Library
(glog)](http://rpg.ifi.uzh.ch/docs/glog.html) for logging with the following
conventions:
- `LOG(WARNING)` for things that indicate that there's something seriously
  wrong. In the case of unrecoverable errors, rather throw an exception.
- `VLOG(0)` for abnormal conditions in modules that are not that serious but
  that should be shown in non-verbose mode.
- `VLOG(<level>)` for all other logging.

`VLOG(<level>)` messages are logged to stderr if the `-verbose` value specified
on the command-line is greater or equal to `<level>`. You can use `-vmodule` to
turn up the verbosity level for specific source files only, or to suppress
non-critical logging from a specific module (by specifying a negative level).

## License
By contributing to CherryPi, you agree that your contributions will be licensed
under the LICENSE file in the root directory of this source tree.

