/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <map>
#include <string>

namespace cherrypi {

/// Attempts to find StarCraft maps which approximately match a given map name
class MapMatcher {
 public:
  /// Attempt to find a map which matches the given name.
  ///
  /// Aims to forgive differences like:
  /// * Different versions
  /// * Having branding ("ICCUP")
  /// * Different "observer" status (ie. "Lost Temple" vs. "Lost Temple OBS")
  /// * *.scx <-> *.scm
  ///
  /// Returns an empty string if no match is found.
  std::string tryMatch(const std::string& mapName);
  /// This is used to be in consistence with the train_micro script where a map
  /// path prefix is required.
  void setMapPrefix(const std::string& prefix) {
    prefix_ = prefix;
  }

 protected:
  void load_();
  std::map<std::string, std::string> mapByFuzzyName_;
  std::string prefix_;
};

} // namespace cherrypi
