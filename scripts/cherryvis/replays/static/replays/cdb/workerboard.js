/*
 * Copyright (c) 2019-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

importScripts('cvis_utils.js');

var board_updates = null;

var search_pattern = '';
var current_frame = 0;

var prev_search_pattern = '';
var prev_frame = -1;

const UPDATE_INTERVAL = 500;
var MAX_ENTRIES_TO_DISPLAY = 15;

onmessage = function(e) {
  if (e.data.init_data) {
    board_updates = e.data.init_data;
    poll_update_fn(update, UPDATE_INTERVAL);
    return;
  }
  search_pattern = e.data.search;
  current_frame = e.data.current_frame;
};

function update() {
  if (search_pattern == prev_search_pattern && current_frame == prev_frame) {
    return;
  }
  prev_frame = current_frame;
  prev_search_pattern = search_pattern;
  var matching = _cvis_board_matched_entries();
  var total_count = matching.length;
  matching = matching.slice(0, MAX_ENTRIES_TO_DISPLAY);
  postMessage({
    'total_count': total_count,
    'entries': matching,
  });
}

function _cvis_board_is_a_match(log_infos, search_terms) {
  for (var i = 0; i < search_terms.length; ++i) {
    if (log_infos['value'].includes(search_terms[i]))
      continue;
    if (log_infos['id'].includes(search_terms[i]))
      continue;
    return false;
  }
  return true;
}

function _cvis_board_matched_entries() {
  // Gather all board values at current frame
  var board_values = {};
  _cvis_apply_updates(board_values, current_frame, board_updates);
  var board_entries = [];
  Object.keys(board_values).map(function(k, _) {
    var v = board_values[k];
    board_entries.push({
      'id': k + '____' + v,
      'key': k,
      'value': v,
    });
  });
  var search_terms = search_pattern.split(' ');
  var board_entries = board_entries.filter(log => _cvis_board_is_a_match(log, search_terms));
  return board_entries;
}
