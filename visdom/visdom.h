/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <autogradpp/autograd.h>
#include <mapbox/variant.hpp>

#include <string>
#include <unordered_map>

namespace visdom {

struct ConnectionParams {
  std::string server;
  int port = 8097;
  bool ipv6 = true; // unused
  bool proxy = false; // unused

  ConnectionParams() : server("http://localhost") {}
  ConnectionParams(
      std::string server,
      int port = 8097,
      bool ipv6 = true,
      bool proxy = false)
      : server(std::move(server)), port(port), ipv6(ipv6), proxy(proxy) {}
};

using StringList = std::vector<std::string>;
using StringListMap = std::unordered_map<int, StringList>;
using OptionValue = mapbox::util::variant<bool,
                                          double,
                                          std::string,
                                          StringList,
                                          StringListMap,
                                          torch::Tensor>;
using Options = std::unordered_map<std::string, OptionValue>;

struct OptionPair {
  OptionPair(std::string key, OptionValue&& value)
      : first(std::move(key)), second(std::move(value)) {}
  OptionPair(std::string key, char const* value)
      : first(std::move(key)), second(std::string(value)) {}
  OptionPair(std::string key, bool value)
      : first(std::move(key)), second(value) {}
  OptionPair(std::string key, double value)
      : first(std::move(key)), second(value) {}
  OptionPair(std::string key, float value)
      : first(std::move(key)), second(double(value)) {}
  OptionPair(std::string key, int value)
      : first(std::move(key)), second(double(value)) {}
  OptionPair(std::string key, std::initializer_list<std::string> value)
      : first(std::move(key)), second(std::vector<std::string>(value)) {}

  std::string first;
  OptionValue second;
};

inline Options makeOpts(std::initializer_list<OptionPair> init) {
  Options opts;
  for (auto const& it : init) {
    opts[it.first] = it.second;
  }
  return opts;
}

enum class UpdateMethod {
  None,
  Append,
  Insert,
  Replace,
  Remove,
};

class VisdomImpl;
class Visdom {
 public:
  Visdom(
      ConnectionParams cparams = ConnectionParams(),
      std::string env = "main",
      bool send = true);
  ~Visdom();

  /**
   * This funciton allows the user to save envs that are alive on the Tornado
   * server.
   * The envs can be specified as a list of env ids.
   */
  std::string save(std::vector<std::string> const& envs);

  /**
   * This function closes a specific window.
   * Use `win={}` to close all windows in an env.
   */
  std::string close(
      std::string const& win = std::string(),
      std::string const& env = std::string());

  /**
   * This funciton prints text in a box.
   * It takes as input an `text` string. No specific `opts` are currently
   * supported.
   */
  std::string text(std::string const& txt, Options const& opts = Options()) {
    return text(txt, {}, {}, opts);
  }
  std::string text(
      std::string const& txt,
      std::string const& win,
      Options const& opts = Options()) {
    return text(txt, win, {}, opts);
  }
  std::string text(
      std::string const& txt,
      std::string const& win,
      std::string const& env,
      Options const& opts = Options());

  /**
   * This function draws a heatmap.
   * It takes as input an `NxM` tensor `X` that specifies the value at each
   * location in the heatmap.
   *
   * The following `opts` are supported:
   * - `opts.colormap`: colormap (string; default = "Viridis")
   * - `opts.xmin`: clip minimum value (number, default = `X:min()`)
   * - `opts.xmax`: clip maximum value (number, default = `X:max()`)
   * - `opts.columnnames`: vector of strings containing x-axis labels
   * - `opts.rownames`: vector of strings containing y-axis labels
   */
  std::string heatmap(torch::Tensor tensor, Options const& opts = Options()) {
    return heatmap(tensor, {}, {}, opts);
  }
  std::string heatmap(
      torch::Tensor tensor,
      std::string const& win,
      Options const& opts = Options()) {
    return heatmap(tensor, win, {}, opts);
  }
  std::string heatmap(
      torch::Tensor tensor,
      std::string const& win,
      std::string const& env,
      Options const& opts = Options());

  /**
   * This function draws a 2D or 3D scatter plot. It takes in an `Nx2` or `Nx3`
   * tensor `X` that specifies the locations of the `N` points in the scatter
   * plot. An optional `N` tensor `Y` containing discrete labels that range
   * between `1` and `K` can be specified as well -- the labels will be
   * reflected in the colors of the markers.
   *
   * `update` can be used to efficiently update the data of an existing line.
   * Use 'append' to append data, 'replace' to use new data.  Update data that
   * is all NaN is ignored (can be used for masking update).
   *
   * The following `opts` are supported:
   *
   * - `opts.colormap`    : colormap (`string`; default = `'Viridis'`)
   * - `opts.markersymbol`: marker symbol (`string`; default = `'dot'`)
   * - `opts.markersize`  : marker size (`number`; default = `'10'`)
   * - `opts.markercolor` : marker color (`np.array`; default = `None`)
   * - `opts.legend`      : `table` containing legend names
   */
  std::string scatter(
      torch::Tensor X,
      Options const& opts = Options(),
      UpdateMethod update = UpdateMethod::None) {
    return scatter(X, torch::Tensor(), {}, {}, {}, opts, update);
  }
  std::string scatter(
      torch::Tensor X,
      std::string const& win,
      Options const& opts = Options(),
      UpdateMethod update = UpdateMethod::None) {
    return scatter(X, torch::Tensor(), win, {}, {}, opts, update);
  }
  std::string scatter(
      torch::Tensor X,
      std::string const& win,
      std::string const& env,
      Options const& opts = Options(),
      UpdateMethod update = UpdateMethod::None) {
    return scatter(X, torch::Tensor(), win, env, {}, opts, update);
  }
  std::string scatter(
      torch::Tensor X,
      std::string const& win,
      std::string const& env,
      std::string const& name,
      Options const& opts = Options(),
      UpdateMethod update = UpdateMethod::None) {
    return scatter(X, torch::Tensor(), win, env, name, opts, update);
  }
  std::string scatter(
      torch::Tensor X,
      torch::Tensor Y,
      Options const& opts = Options(),
      UpdateMethod update = UpdateMethod::None) {
    return scatter(X, Y, {}, {}, {}, opts, update);
  }
  std::string scatter(
      torch::Tensor X,
      torch::Tensor Y,
      std::string const& win,
      Options const& opts = Options(),
      UpdateMethod update = UpdateMethod::None) {
    return scatter(X, Y, win, {}, {}, opts, update);
  }
  std::string scatter(
      torch::Tensor X,
      torch::Tensor Y,
      std::string const& win,
      std::string const& env,
      Options const& opts = Options(),
      UpdateMethod update = UpdateMethod::None) {
    return scatter(X, Y, win, env, {}, opts, update);
  }
  std::string scatter(
      torch::Tensor X,
      torch::Tensor Y,
      std::string const& win,
      std::string const& env,
      std::string const& name,
      Options const& opts = Options(),
      UpdateMethod update = UpdateMethod::None);

  /**
   * This function draws a line plot. It takes in an `N` or `NxM` tensor `Y`
   * that specifies the values of the `M` lines (that connect `N` points) to
   * plot. It also takes an optional `X` tensor that specifies the corresponding
   * x-axis values; `X` can be an `N` tensor (in which case all lines will share
   * the same x-axis values) or have the same size as `Y`.
   *
   * `update` can be used to efficiently update the data of an existing line.
   * Use 'append' to append data, 'replace' to use new data.  Update data that
   * is all NaN is ignored (can be used for masking update).
   *
   * The following `opts` are supported:
   *
   * - `opts.fillarea`    : fill area below line (`boolean`)
   * - `opts.colormap`    : colormap (`string`; default = `'Viridis'`)
   * - `opts.markers`     : show markers (`boolean`; default = `false`)
   * - `opts.markersymbol`: marker symbol (`string`; default = `'dot'`)
   * - `opts.markersize`  : marker size (`number`; default = `'10'`)
   * - `opts.legend`      : `table` containing legend names
   *
   * If `update` is specified, the figure will be updated without
   * creating a new plot -- this can be used for efficient updating.
   */
  std::string line(
      torch::Tensor Y,
      Options const& opts = Options(),
      UpdateMethod update = UpdateMethod::None) {
    return line(Y, torch::Tensor(), {}, {}, {}, opts, update);
  }
  std::string line(
      torch::Tensor Y,
      std::string const& win,
      Options const& opts = Options(),
      UpdateMethod update = UpdateMethod::None) {
    return line(Y, torch::Tensor(), win, {}, {}, opts, update);
  }
  std::string line(
      torch::Tensor Y,
      std::string const& win,
      std::string const& env,
      Options const& opts = Options(),
      UpdateMethod update = UpdateMethod::None) {
    return line(Y, torch::Tensor(), win, env, {}, opts, update);
  }
  std::string line(
      torch::Tensor Y,
      std::string const& win,
      std::string const& env,
      std::string const& name,
      Options const& opts = Options(),
      UpdateMethod update = UpdateMethod::None) {
    return line(Y, torch::Tensor(), win, env, name, opts, update);
  }
  std::string line(
      torch::Tensor Y,
      torch::Tensor X,
      Options const& opts = Options(),
      UpdateMethod update = UpdateMethod::None) {
    return line(Y, X, {}, {}, {}, opts, update);
  }
  std::string line(
      torch::Tensor Y,
      torch::Tensor X,
      std::string const& win,
      Options const& opts = Options(),
      UpdateMethod update = UpdateMethod::None) {
    return line(Y, X, win, {}, {}, opts, update);
  }
  std::string line(
      torch::Tensor Y,
      torch::Tensor X,
      std::string const& win,
      std::string const& env,
      Options const& opts = Options(),
      UpdateMethod update = UpdateMethod::None) {
    return line(Y, X, win, env, {}, opts, update);
  }
  std::string line(
      torch::Tensor Y,
      torch::Tensor X,
      std::string const& win,
      std::string const& env,
      std::string const& name,
      Options const& opts = Options(),
      UpdateMethod update = UpdateMethod::None);

 private:
  VisdomImpl* d_;
};

} // namespace visdom
