/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "test.h"

#include "basetypes.h"

using namespace cherrypi;

CASE("basetypes/rect/empty_null") {
  Rect r;
  EXPECT(r.null());
  EXPECT(r.empty());

  r = Rect(10, 10, 0, 0);
  EXPECT(r.null());
  EXPECT(r.empty());

  r = Rect(10, 10, -1, -1);
  EXPECT(!r.null());
  EXPECT(r.empty());

  r = Rect(10, 10, 1, 1);
  EXPECT(!r.null());
  EXPECT(!r.empty());
}

CASE("basetypes/rect/union") {
  Rect r;

  r = Rect(0, 0, 10, 10).united(Rect(20, 20, 10, 10));
  EXPECT(r.top() == 0);
  EXPECT(r.left() == 0);
  EXPECT(r.bottom() == 30);
  EXPECT(r.right() == 30);

  r = Rect(20, 20, 10, 10).united(Rect(0, 0, 10, 10));
  EXPECT(r.top() == 0);
  EXPECT(r.left() == 0);
  EXPECT(r.bottom() == 30);
  EXPECT(r.right() == 30);

  r = Rect(0, 0, 10, 10).united(Rect(1, 1, 5, 5));
  EXPECT(r.top() == 0);
  EXPECT(r.left() == 0);
  EXPECT(r.bottom() == 10);
  EXPECT(r.right() == 10);

  r = Rect(1, 1, 5, 5).united(Rect(0, 0, 10, 10));
  EXPECT(r.top() == 0);
  EXPECT(r.left() == 0);
  EXPECT(r.bottom() == 10);
  EXPECT(r.right() == 10);

  r = Rect(0, 0, 10, 10).united(Rect(0, 0, 10, 10));
  EXPECT(r.top() == 0);
  EXPECT(r.left() == 0);
  EXPECT(r.bottom() == 10);
  EXPECT(r.right() == 10);

  r = Rect(0, 0, 10, 10).united(Rect(5, 5, 10, 10));
  EXPECT(r.top() == 0);
  EXPECT(r.left() == 0);
  EXPECT(r.bottom() == 15);
  EXPECT(r.right() == 15);

  r = Rect(5, 5, 10, 10).united(Rect(0, 0, 10, 10));
  EXPECT(r.top() == 0);
  EXPECT(r.left() == 0);
  EXPECT(r.bottom() == 15);
  EXPECT(r.right() == 15);
}

CASE("basetypes/rect/intersection") {
  Rect r;

  r = Rect(0, 0, 10, 10).intersected(Rect(20, 20, 10, 10));
  EXPECT(r.top() == 0);
  EXPECT(r.left() == 0);
  EXPECT(r.bottom() == 0);
  EXPECT(r.right() == 0);

  r = Rect(20, 20, 10, 10).intersected(Rect(0, 0, 10, 10));
  EXPECT(r.top() == 0);
  EXPECT(r.left() == 0);
  EXPECT(r.bottom() == 0);
  EXPECT(r.right() == 0);

  r = Rect(0, 0, 10, 10).intersected(Rect(1, 1, 5, 5));
  EXPECT(r.top() == 1);
  EXPECT(r.left() == 1);
  EXPECT(r.bottom() == 6);
  EXPECT(r.right() == 6);

  r = Rect(1, 1, 5, 5).intersected(Rect(0, 0, 10, 10));
  EXPECT(r.top() == 1);
  EXPECT(r.left() == 1);
  EXPECT(r.bottom() == 6);
  EXPECT(r.right() == 6);

  r = Rect(0, 0, 10, 10).intersected(Rect(0, 0, 10, 10));
  EXPECT(r.top() == 0);
  EXPECT(r.left() == 0);
  EXPECT(r.bottom() == 10);
  EXPECT(r.right() == 10);

  r = Rect(0, 0, 10, 10).intersected(Rect(5, 5, 10, 10));
  EXPECT(r.top() == 5);
  EXPECT(r.left() == 5);
  EXPECT(r.bottom() == 10);
  EXPECT(r.right() == 10);

  r = Rect(5, 5, 10, 10).intersected(Rect(0, 0, 10, 10));
  EXPECT(r.top() == 5);
  EXPECT(r.left() == 5);
  EXPECT(r.bottom() == 10);
  EXPECT(r.right() == 10);
}
