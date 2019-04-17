/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "visdom.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <thread>

using namespace visdom;

int main(int argc, char** argv) {
  FLAGS_logtostderr = true;
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  Visdom viz(ConnectionParams("localhost", 8097));
  auto win1 = viz.text("Hello world1", makeOpts({{"title", "My Window"}}));
  auto win2 = viz.text("Hello world2");
  viz.text("Hello world3", win1);
  viz.close(win2); // TODO: Not working?

  auto random = torch::rand({8, 5});
  viz.heatmap(
      random,
      makeOpts(
          {{"title", "Random heatmap"},
           {"columnnames", {"a", "b", "c", "d", "e"}}}));

  viz.text("It's bar", {}, "foo");
  viz.save({"main", "foo"});

  auto Y = torch::rand({100});
  viz.scatter(
      torch::rand({100, 2}),
      (Y + 1.5).to(torch::kI32),
      makeOpts(
          {{"legend", {"Apples", "Pears"}},
           {"xtickmin", -5},
           {"xtickmax", 5},
           {"xtickstep", 0.5},
           {"ytickmin", -5},
           {"ytickmax", 5},
           {"ytickstep", 0.5},
           {"markersymbol", "cross-thin-open"}}));

  auto colors = (torch::rand({2, 3}) * 255).to(torch::kI32);
  auto win = viz.scatter(
      torch::rand({255, 2}),
      (torch::rand({255}) + 1.5).to(torch::kI32),
      makeOpts(
          {{"markersize", 10},
           {"markercolor", colors},
           {"legend", {"1", "2"}}}));

  // Line plots
  viz.line(torch::rand({10}));

  Y = torch::linspace(-5, 5, 100);
  viz.line(
      at::stack({Y * Y, (Y + 5).sqrt()}).t(),
      at::stack({Y, Y}).t(),
      makeOpts({{"markers", false}}));

  // Line updates
  win = viz.line(
      at::stack({torch::linspace(5, 10, 10), torch::linspace(5, 10, 10) + 5})
          .t(),
      at::stack({torch::arange(0, 10), torch::arange(0, 10)}).t());
  viz.line(
      at::stack({torch::linspace(5, 10, 10), torch::linspace(5, 10, 10) + 5})
          .t(),
      at::stack({torch::arange(10, 20), torch::arange(10, 20)}).t(),
      win,
      {},
      UpdateMethod::Append);
  viz.line(
      torch::arange(1, 10),
      torch::arange(21, 30),
      win,
      {},
      "2",
      {},
      UpdateMethod::Append);
  viz.line(
      torch::arange(11, 20),
      torch::arange(1, 10),
      win,
      {},
      "delete this",
      {},
      UpdateMethod::Append);
  viz.line(
      torch::arange(11, 20),
      torch::arange(1, 10),
      win,
      {},
      "4",
      {},
      UpdateMethod::Insert);
  viz.line(torch::Tensor(), win, {}, "delete this", {}, UpdateMethod::Remove);

  Y = torch::linspace(0, 4, 200);
  viz.line(
      at::stack({Y.sqrt(), Y.sqrt() + 2}).t(),
      at::stack({Y, Y}).t(),
      makeOpts(
          {{"fillarea", true},
           {"showlegend", false},
           {"width", 400},
           {"height", 400},
           {"xtitle", "Time"},
           {"ytitle", "Volume"},
           {"ytype", "log"},
           {"title", "Stacked area plot"},
           {"marginleft", 30},
           {"marginright", 30},
           {"marginbottom", 80},
           {"margintop", 30}}));

  viz.line(
      at::stack({torch::linspace(5, 10, 10), torch::linspace(5, 10, 10) + 5})
          .t(),
      torch::arange(0, 10));


  return EXIT_SUCCESS;
}
