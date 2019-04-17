/*
 * Copyright (c) 2019-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

"use strict";

// Match cherrypi ids with internal ids
function cvis_dbg_match_units(global_data, cvis_state) {
  function get_all_units_to_match() {
    var all_units_to_match = [];
    $.each(global_data['units_first_seen'], function(frame, v) {
      $.each(v, function(_, first_seen_infos) {
        all_units_to_match.push({
          'id': first_seen_infos['id'],
          'type': first_seen_infos['type'],
          'x': first_seen_infos['x'],
          'y': first_seen_infos['y'],
          'frame': frame,
        });
      });
    });
    return all_units_to_match;
  }

  $('.cvis-dbg-show-when-matching-units').show();
  var pbar = $('.cvis-matching-units-progress-bar');
  if (cvis_state.units_matching.start == null) {
    cvis_state.units_matching.start = new Date();
    cvis_state.units_first_seen_frame = {};
    var matcher = Module.get_units_matcher();
    var all_units_to_match = get_all_units_to_match();
    $.each(all_units_to_match, function(_, first_seen_infos) {
      cvis_state.units_first_seen_frame[first_seen_infos.id] = first_seen_infos.frame;
      matcher.add_unit(
        parseInt(first_seen_infos.frame), first_seen_infos.id,
        first_seen_infos.type, first_seen_infos.x,
        first_seen_infos.y
      );
    });
    pbar.width('0%');
    return null;
  }
  if (cvis_state.units_matching.skipped) {
    cvis_state.id2bw = {};
    cvis_state.bw2id = {};
    Module.enable_main_update_loop();
    $('.cvis-dbg-show-when-matching-units').hide();
    $('.cvis-dbg-show-when-loading').hide();
    return true;
  }

  const MATCHING_STEP_NUM_FRAMES = 1000;
  var progress = Module.get_units_matcher().do_matching(MATCHING_STEP_NUM_FRAMES);
  if (!progress.done) {
    pbar.width(((progress.current_frame / progress.end_frame) * 100) + '%');
    return null;
  }
  var timeDiff = (new Date() - cvis_state.units_matching.start) / 1000;
  console.log("Matching done in " + timeDiff + " secs");

  var all_units_to_match = get_all_units_to_match();
  var matching = Module.get_units_matcher().get_matching();
  console.log(
    '[BW<->CP Matching] Unable to match',
    (all_units_to_match.length - Object.values(matching['cp2internal']).length),
    'units');
  // Creates a map build_type => number_of_no_match
  console.log(new Map(Object.entries(global_data['types_names']).map(function(a){
    var no_match = all_units_to_match.filter(u => u['type'] == a[0]
      ).filter(u => matching['cp2internal'][u['id']] === undefined);
    return [
      no_match.length,
      a[1],
      no_match[0],
    ];
  }).sort((a, b) => a[0] - b[0]).filter(a => a[0] >= 1).map(
    a => [a[1], {'count': a[0], 'sample': a[2]}])));
  cvis_state.id2bw = matching['cp2internal'];
  cvis_state.bw2id = matching['internal2cp'];
  Module.enable_main_update_loop();
  $('.cvis-dbg-show-when-matching-units').hide();
  $('.cvis-dbg-show-when-loading').hide();
  return true;
}

function cvis_dbg_matching_update(global_data, cvis_state) {
  if (cvis_state.units_matching.skipped) {
    var new_matching = Module.get_units_matcher().get_matching();
    if (new_matching.updated) {
      cvis_state.id2bw = new_matching['cp2internal'];
      cvis_state.bw2id = new_matching['internal2cp'];
    }
  }
}
