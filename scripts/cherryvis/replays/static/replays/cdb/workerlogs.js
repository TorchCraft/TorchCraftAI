/*
 * Copyright (c) 2019-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

importScripts('cvis_utils.js');

var state = null;
var query = null;
const UPDATE_INTERVAL = 500;

onmessage = function(e) {
  if (e.data.init_data) {
    state = cvis_logs_handler_init(e.data.init_data);
    poll_update_fn(update, UPDATE_INTERVAL);
    return;
  }
  query = e.data;
};

function update() {
  if (query)
    postMessage(cvis_logs_handler_update(state, query));
}
