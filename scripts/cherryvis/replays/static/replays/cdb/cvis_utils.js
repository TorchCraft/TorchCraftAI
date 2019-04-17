/*
 * Copyright (c) 2019-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// ========== COMPUTE UNIT VALUES GIVEN INCREMENTAL UPDATES
function _cvis_apply_updates(info, current_frame, updates) {
  if (updates === undefined) {
    return;
  }
  Object.keys(updates).map(function(frame, _) {
    var update = updates[frame];
    if (frame <= current_frame) {
      Object.assign(info, update);
    }
  });
}

function poll_update_fn(fn, interval) {
  function call_fn() {
    fn();
    setTimeout(call_fn, interval);
  }

  setTimeout(call_fn, interval);
}

function splitStringQuoteAware(s) {
  if (!s) {
    return [];
  }

  var splitRegexp = /[^\s"]+|"([^"]*)"/gi;
  var res = [];
  var match;

  do {
      //Each call to exec returns the next regex match as an array
      match = splitRegexp.exec(s);
      if (match != null)
        res.push(match[1] ? match[1] : match[0]);
  } while (match != null);
  return res;
}

function search_near_frames(data, search_pattern, current_frame, is_match, limit) {
  // 1. Find matching search pattern
  var search_terms = splitStringQuoteAware(search_pattern);
  data.forEach(function(d, i) {
    d['id'] = i;
  });
  var matching = data.filter(d => is_match(d, search_terms));

  matching.sort(function(a, b) {
    var a_dist = Math.abs(a['frame'] - current_frame);
    var b_dist = Math.abs(b['frame'] - current_frame);
    if (a_dist != b_dist)
      return a_dist - b_dist;
    if (a['frame'] < current_frame)
      return b['id'] - a['id'];
    return a['id'] - b['id'];
  });

  var total_count = matching.length;
  if (matching.length > limit) {
    // Always display all the logs from the current frame
    if (matching[limit - 1].frame == current_frame) {
      matching = matching.filter(d => d.frame == current_frame);
    }
    else {
      matching = matching.slice(0, limit);
    }
  }
  return {
    'total_count': total_count,
    'entries': matching,
  };
}

// ========== LOGS HANDLING
function cvis_preprocess_logs(logs) {
  // Merge logs occuring at the same frame
  var new_logs = [];
  logs.forEach(function(log, _) {
    if (log.attachments === undefined) {
      log.attachments = [];
    }

    var last = new_logs[new_logs.length - 1];
    if (new_logs.length == 0 ||
        log.attachments.length ||
        last.attachments.length||
        last.message != log.message ||
        last['frame'] != log['frame'] ||
        last['file'] != log['file'] ||
        last['sev'] != log['sev']) {
      new_logs.push(log);
    }
    else { // Merge logs
      last['message'] += "\n" + log['message'];
    }
  });
  return new_logs;
}

function cvis_logs_handler_init(all_logs) {
  return {
    all_logs: cvis_preprocess_logs(all_logs),
    previous_results: null,
    search_pattern: '',
    current_frame: 0,
    prev_search_pattern: '',
    prev_frame: -1,
    MAX_LOGS_TO_DISPLAY: 15,
  };
}

function cvis_logs_handler_update(state, query) {
  function log_is_a_match(log_infos, search_terms) {
    for (var i = 0; i < search_terms.length; ++i) {
      if (search_terms[i] == '')
        continue;
      if (log_infos.message.includes(search_terms[i]))
        continue;
      if (log_infos.file && log_infos.file.includes(search_terms[i]))
        continue;
      if (log_infos['frame'] == search_terms[i])
        continue;
      if (('f' + log_infos['frame']) == search_terms[i])
        continue;
      return false;
    }
    return true;
  }

  var current_frame = query.current_frame;
  var search_pattern = query.search_pattern;
  if (search_pattern == state.prev_search_pattern && current_frame == state.prev_frame) {
    return state.previous_results;
  }
  state.prev_frame = current_frame;
  state.prev_search_pattern = search_pattern;
  state.previous_results = search_near_frames(
    state.all_logs, search_pattern, current_frame,
    log_is_a_match, state.MAX_LOGS_TO_DISPLAY);
  return state.previous_results;
}
