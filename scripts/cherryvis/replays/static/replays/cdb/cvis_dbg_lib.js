/*
 * Copyright (c) 2019-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

"use strict";

const XYPixelsPerWalktile = 8;

function cvis_dbg_lib_init(global_data, cvis_state) {
  function get_unit_at_frame(unit_id, frame) {
    var info = {};
    if (frame == cvis_state.current_frame) {
      var bw_id = cvis_state.id2bw[unit_id];
      if (bw_id === undefined)
        return null;
      info = Module.lookup_unit(bw_id);
      if (!info) {
        return null;
      }
    }
    info.task = -1;
    info.id = unit_id;
    info.found = true;
    _cvis_apply_updates(info, frame, global_data['units_updates'][unit_id]);
    return info;
  }

  function get_unit(unit_id) {
    return get_unit_at_frame(unit_id, cvis_state.current_frame);
  }

  function get_unit_by_bw_id(unit_bw_id, no_updates) {
    var unit = unit_bw_id ? Module.lookup_unit(unit_bw_id) : null;
    if (!unit) {
      return {
        'id': -unit_bw_id,
        'bw_id': unit_bw_id,
        'x': 0,
        'y': 0,
        'type': 0,
        'found': false,
      };
    }
    unit['id'] = cvis_state.bw2id[unit_bw_id];
    if (unit['id'] === undefined)
      unit['id'] = -unit_bw_id;
    unit['task'] = -1;
    unit['seen'] = false;
    unit['found'] = true;
    if (no_updates) {
      return unit;
    }
    _cvis_apply_updates(unit, cvis_state.current_frame, global_data['units_updates'][unit['id']]);
    return unit;
  }

  function get_unit_by_cp_id(unit_cp_id) {
    return get_unit_by_bw_id(cvis_state.id2bw[unit_cp_id]);
  }

  function update_highlights() {
    Module.set_highlighted_units(cvis_state.lib.highlighted_units);
    Module.set_highlighted_rects(cvis_state.lib.highlighted_rectangles);
  }

  function get_walk_tile_rect(x, y) {
    x *= XYPixelsPerWalktile;
    y *= XYPixelsPerWalktile;
    return {
      from: [x, y],
      to: [x + XYPixelsPerWalktile, y + XYPixelsPerWalktile],
    };
  }

  function screen_force_see_tmp(x, y) {
    var si = Module.get_screen_info();
    if (si.screen_x < x && x < (si.screen_x + si.screen_width) &&
      si.screen_y < y && y < (si.screen_y + si.screen_height)) {
      // Already visible
      if (cvis_state.lib.screen_force_see_tmp != null) {
        cvis_state.lib.screen_force_see_tmp.reset = false;
      }
      return;
    }

    if (cvis_state.lib.screen_force_see_tmp == null) {
      var orig = [
        si.screen_x + si.screen_width / 2,
        si.screen_y + si.screen_height / 2
      ];
      cvis_state.lib.screen_force_see_tmp = {
        orig: orig,
      }
    }
    cvis_state.lib.screen_force_see_tmp.to = [x, y];
    cvis_state.lib.screen_force_see_tmp.reset = false;
    Module.set_screen_center_position(x, y);
  }

  function screen_force_see_tmp_reset() {
    if (cvis_state.lib.screen_force_see_tmp == null) {
      return;
    }
    cvis_state.lib.screen_force_see_tmp.reset = true;
  }

  function openbw_set_current_frame(frame) {
    var newFrame = parseInt(frame);
    _replay_set_value(3, newFrame);
    cvis_state.previous_update_frame = newFrame;
    cvis_state.current_frame = newFrame;
    cvis_state.moving_to_frame = newFrame;
  }

  function parse_cvis_links(html) {
    var get_bw_id_from_html = function(e) {
      if (e.attr('data-unit-bw-id')) {
        return parseInt(e.attr('data-unit-bw-id'));
      }
      return parseInt(cvis_state.id2bw[e.attr('data-unit-id')]);
    };

    html.find('a.cvis-link-unit').click(function(e) {
      e.preventDefault();
      _cvis_select_unit_bw_id(get_bw_id_from_html($(this)));
      cvis_state.lib.screen_force_see_tmp = null;
      cvis_state.lib.highlighted_units = [];
      update_highlights();
    }).on('mouseover', function() {
      var unit = get_unit_by_bw_id(get_bw_id_from_html($(this)));
      if (!unit || !unit.found) {
        return;
      }
      cvis_state.lib.highlighted_units = [unit.bw_id];
      update_highlights();
      screen_force_see_tmp(unit.x, unit.y);
    }).on('mouseout', function() {
      cvis_state.lib.highlighted_units = [];
      screen_force_see_tmp_reset();
    });

    html.find('a.cvis-link-position').click(function(e) {
      e.preventDefault();
      var rect = get_walk_tile_rect(parseInt($(this).attr('data-pos-x-wt')), parseInt($(this).attr('data-pos-y-wt')));
      Module.set_screen_center_position(parseInt((rect.from[0] + rect.to[0]) / 2), parseInt((rect.from[1] + rect.to[1]) / 2));
      cvis_state.lib.screen_force_see_tmp = null;
      cvis_state.lib.highlighted_rectangles = [];
      update_highlights();
    }).on('mouseover', function() {
      var rect = get_walk_tile_rect(parseInt($(this).attr('data-pos-x-wt')), parseInt($(this).attr('data-pos-y-wt')));
      var rect2 = {
        from: [Math.max(0, rect.from[0] - 30), Math.max(0, rect.from[1] - 30)],
        to: [rect.to[0] + 30, rect.to[1] + 30],
      };
      var rect3 = {
        from: [Math.max(0, rect.from[0] - 90), Math.max(0, rect.from[1] - 90)],
        to: [rect.to[0] + 90, rect.to[1] + 90],
      };
      cvis_state.lib.highlighted_rectangles = [rect, rect2, rect3];
      update_highlights();
      screen_force_see_tmp(parseInt((rect.from[0] + rect.to[0]) / 2), parseInt((rect.from[1] + rect.to[1]) / 2));
    }).on('mouseout', function() {
      cvis_state.lib.highlighted_rectangles = [];
      screen_force_see_tmp_reset();
    }).each(function() {
      var x = parseInt($(this).attr('data-pos-x-wt'));
      var y = parseInt($(this).attr('data-pos-y-wt'));
      $(this).text('(' + x + ', ' + y + ')');
    });

    html.find('a.cvis-link-frame').each(function() {
      $(this).text('f' + $(this).attr('data-frame-id'));
      $(this).attr('href', '#');
    }).click(function(e){
      e.preventDefault();
      openbw_set_current_frame($(this).attr('data-frame-id'))
    });
    html.find('a.cvis-link-upc').click(function(e) {
      e.preventDefault();
      var g = cvis_state.functions.find_tree('gameupcs');
      if (g !== null)
        cvis_state.functions.load_tree(g, 'u' + $(this).attr('data-upc-id'));
    });
    html.find('button.track-task-btn').click(function(e) {
      e.preventDefault();
      var task_id = $(this).attr('data-task-id');
      if (task_id)
        cvis_state.functions.task_track(parseInt(task_id));
    });
    html.find('button.track-unit-btn').click(function(e) {
      e.preventDefault();
      var unit_id = $(this).attr('data-unit-id');
      if (unit_id)
        cvis_state.functions.unit_track(parseInt(unit_id));
    });
  }

  function load_cvis_json_file(cvis, filename, success_fn, fail_fn) {
    $.get(
      "get/cvis/?cvis=" + encodeURIComponent(cvis) + "&f=" + encodeURIComponent(filename),
      g => success_fn(g),
      "json"
    ).fail(fail_fn);
  }

  /**
    Given a list of frames, and a goal frame, returns the nearest frame (closest),
      along with the previous and next frames.
  */
  function get_matching_frames(all_frames, goal) {
    all_frames = all_frames.map(f => parseInt(f)).sort((a, b) => a - b);
    var i = 0;
    for (i = 0; i < all_frames.length && all_frames[i] < goal; ++i) {}
    var previous;
    var next;
    var closest;
    if (i >= all_frames.length) {
      next = closest = all_frames[all_frames.length-1];
      previous = all_frames.length > 1 ? all_frames[all_frames.length - 2] : next;
    }
    else {
      var prev_i = i > 0 ? i - 1 : i;
      var closest_i = (all_frames[i] - goal) < (goal - all_frames[prev_i]) ? i : prev_i;
      closest = all_frames[closest_i];
      next = all_frames[closest_i + 1 < all_frames.length ? closest_i + 1 : closest_i];
      previous = all_frames[closest_i - 1 >= 0 ? closest_i - 1 : closest_i];
    }
    return {
      previous: previous,
      closest: closest,
      next: next
    };
  }

  function change_perspective(persp) {
    do_send_replay_when_ready(cvis_state.rep_path, cvis_state.cvis_path, {
      'multi': persp,
      'frame': _cvis_current_frame(),
    });
  }

  // Jump to frame
  $('#cvisSetFrame').off('keyup').keyup(function(e){
    if(e.keyCode == 13) // ENTER
      openbw_set_current_frame(parseInt($(this).val()));
  });
  $('#cvisSetFrameBtn').off('click').click(function(){
    openbw_set_current_frame(parseInt($('#cvisSetFrame').val()));
  });
  $('.cvis-jump-frame-relative').off('click').click(function(){
    var jump_to = _cvis_current_frame();
    jump_to += parseInt($(this).attr('data-jump-frame'));
    openbw_set_current_frame(jump_to);
    $('#cvisSetFrame').val(jump_to);
  });
  var selectPerspective = $('#cvisPerspective').off('change');
  selectPerspective.empty();
  $('.cvisPerspectivePanel').addClass('collapse');
  $.each(cvis_state.multi, function(other_key, other) {
    $('.cvisPerspectivePanel').removeClass('collapse');
    selectPerspective.append($('<option>').attr('value', other_key).text(other.name));
  });
  selectPerspective.val(cvis_state.current_multi);
  selectPerspective.change(function() {
    $('.cvisPerspectivePanel').addClass('collapse');
    change_perspective(selectPerspective.val());
  });


  cvis_state.functions.get_unit_at_frame = get_unit_at_frame;
  cvis_state.functions.get_unit = get_unit;
  cvis_state.functions.get_unit_by_bw_id = get_unit_by_bw_id;
  cvis_state.functions.get_unit_by_cp_id = get_unit_by_cp_id;
  cvis_state.functions.parse_cvis_links = parse_cvis_links;
  cvis_state.functions.load_cvis_json_file = load_cvis_json_file;
  cvis_state.functions.get_matching_frames = get_matching_frames;
  cvis_state.functions.update_highlights = update_highlights;
  cvis_state.functions.openbw_set_current_frame = openbw_set_current_frame;
  cvis_state.functions.change_perspective = change_perspective;
  cvis_state.lib = {
    highlighted_units: [],
    highlighted_rectangles: [],
    screen_force_see_tmp: null,
  };
}

function cvis_dbg_lib_update_end(global_data, cvis_state) {
  // Update displayed logs
  var current_frame = cvis_state.current_frame;
  $('.cvis-has-frame-id').each(function(){
    var f = $(this).attr('data-frame-id');
    if (f > current_frame) {
      $(this).addClass('cvis-frame-future').removeClass('cvis-frame-past');
    }
    else {
      $(this).addClass('cvis-frame-past').removeClass('cvis-frame-future');
    }
  });
  cvis_state.functions.update_highlights();

  // Reset screen pos if needed:
  if (cvis_state.lib.screen_force_see_tmp && cvis_state.lib.screen_force_see_tmp.reset) {
    Module.set_screen_center_position(
      cvis_state.lib.screen_force_see_tmp.orig[0],
      cvis_state.lib.screen_force_see_tmp.orig[1]
    );
    cvis_state.lib.screen_force_see_tmp = null;
  }
}
