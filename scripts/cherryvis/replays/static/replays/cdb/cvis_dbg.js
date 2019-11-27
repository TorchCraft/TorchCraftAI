/*
 * Copyright (c) 2019-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

"use strict";


// =============== ACCESS TO OPENBW
function _cvis_current_frame() {
  return _replay_get_value(2);
}

function _cvis_selected_units() {
  return Module.get_selected_units();
}

function _cvis_select_unit_bw_id(bw_id) {
  Module.clear_selection();
  var unit = Module.lookup_unit(bw_id);
  if (unit) {
    Module.select_unit_by_bw_id(bw_id);
    Module.set_screen_center_position(unit['x'], unit['y']);
  }
  else {
    console.log('BW Unit with id', bw_id, 'not found.');
  }
}

var _cvis_all_states = [];

$(document).ready(function(){
  // Expand & collapse
  const EXPAND_WIDTH = '80vw';
  $('.cvis-collapse-button').hide();
  $('.cvis-expand-collapse').click(function() {
    $('.cvis-expand-collapse').toggle();
    $('.infobar').toggle();
    if ($('.sidebar-cdb').attr('style') === undefined || !$('.sidebar-cdb').attr('style').includes(EXPAND_WIDTH))
      $('.sidebar-cdb').css('min-width', EXPAND_WIDTH);
    else
      $('.sidebar-cdb').css('min-width', '');
  });

  // Back to list of replays
  $(window).on('popstate', function(e){
  	var new_state = e.originalEvent.state;
    if (new_state.page == 'replay') {
      return; // Will be handled in available_replays.js
    }
    cvis_stop_all().then(cvis_reset_dom_state);
  });
});

function cvis_reset_dom_state() {
  $('.cvis-dbg-show-when-loading-failed').show();
  $('.cvis-dbg-show-when-loading').hide();
  $('.cvis-dbg-show-when-running').hide();
  $('.cvis-dbg-show-when-loading').hide();
  $('.cvis-dbg-show-when-loading-failed').hide();
  $('.cvis-dbg-show-when-matching-units').hide();
  $('.list-of-replays-main-page').show();
  $('.hide-when-game-on').show();
  $('.show-when-game-on').addClass('collapse');
  $('.sidebar-cdb').show();  // But has collapse class
  $('.cvis-matching-units-progress-bar').width('0%');
  $('.delete-on-reset-cvis').remove();
}

function cvis_stop_all() {
  return new Promise(function(resolve, reject) {
    function _check_ready() {
      var all_stopped = true;
      $.each(_cvis_all_states, function(_, s) {
        all_stopped = all_stopped && s['killed'];
      });
      if (all_stopped) {
        Module.reset_replay();
        resolve();
      }
      else
        window.setTimeout(_check_ready, 300);
    }
    var all_stopped = true;
    $.each(_cvis_all_states, function(_, s) {
      all_stopped = all_stopped && s['killed'];
    });
    if (all_stopped) {
      resolve();
      return;
    }
    $.each(_cvis_all_states, function(_, s) {
      s['kill'] = true;
    });
    _check_ready();
  });
}

function cvis_dbg_init(rep, cvis_path) {
  return new Promise(function(resolve, reject) {
    $.get("get/replay_info?rep=" + encodeURIComponent(rep), function(replay_info) {
        // Load the multi dumps
        var proms = [];
        $.each(replay_info.multi, function(multi_name, multi_cvis_file) {
          proms.push(new Promise(function(res, rej) {
            $.get("get/cvis/?cvis=" + encodeURIComponent(multi_cvis_file), d => res({name: multi_name, cvis_path: multi_cvis_file, data: d}),
                "json").fail(function() {
                  res(null);
            });
          }));
        });
        // Load the main dump
        $.get("get/cvis/?cvis=" + encodeURIComponent(cvis_path), function(d) {
          Promise.all(proms).then(function(multi_datas) {
            var resolution = {data: d, cvis_path: cvis_path, replay_info: replay_info, multi: {}};
            $.each(multi_datas, function(_, multi_data) {
              if (multi_data != null) {
                resolution.multi[multi_data.name] = multi_data;
              }
            });
            resolve(resolution);
          });
        }, "json").fail(function() {
              resolve(null);
        });
    }, "json").fail(function() {
        resolve(null);
    });
  });
}

function create_worker(file, initial_data) {
  var w = new Worker('/static/replays/cdb/' + file);
  var s = {
    'worker': w,
    'last_result': null,
    'post': w.postMessage,
  };
  w.postMessage({'init_data': initial_data});
  w.onmessage = function(m) {
    s['last_result'] = m.data;
  };
  return s;
}

function cvis_init_with_data(path, init_data, config) {
  if (init_data === null) {
    $('.cvis-dbg-show-when-loading-failed').show();
    $('.cvis-dbg-show-when-loading').hide();
    // Still play the game, but without the cherryvis sidebar
    $('.sidebar-cdb').hide();
    Module.enable_main_update_loop();
    return;
  }
  var cvis_state = {
    'multi': init_data.multi,
    'rep_path': path,
    'cvis_path': init_data.cvis_path,
    'current_multi': config['multi'] !== undefined ? config['multi'] : '',
    'moving_to_frame': -1,
    'previous_update_frame': 0,
    'current_frame': 0,
    'previously_selected_ids': [],
    'selected_units': [],
    'tasks_tracked': [],
    'kill': false,
    'killed': false,
    'id2bw': null,
    'bw2id': null,
    'units_matching': {
      'start': null,
      'skipped': false,
    },
    'functions': {},
  };

  var global_data = init_data.data;
  init_data.multi[''] = {
    'data': init_data.data,
    'cvis_path': init_data.cvis_path,
    'name': 'Default',
  };
  if (cvis_state.current_multi != '') {
    global_data = init_data.multi[cvis_state.current_multi].data;
    cvis_state.cvis_path = init_data.multi[cvis_state.current_multi].cvis_path;
  }
  cvis_state['workers'] = {
    'logs': create_worker('workerlogs.js', global_data['logs'], {
      'total_count': 0,
      'entries': [],
    }),
    'board': create_worker('workerboard.js', global_data['board_updates'], {
      'total_count': 0,
      'entries': [],
    }),
  };
  _cvis_all_states.push(cvis_state);
  $('.cvis-empty-on-reset').html('');
  $('.cvis-dbg-show-when-running').show();
  $('.cvis-matching-units-skip').click(function() {
    cvis_state.units_matching.skipped = true;
  });

  // ========== MAIN UPDATE
  function _cvis_update() {
    if (cvis_state['kill']) {
      $.each(cvis_state['workers'], function(_, w) {
        w.worker.terminate();
      });
      cvis_state['killed'] = true;
      return;
    }

    if (!Module.has_replay_loaded()) { // Replay still loading
      window.setTimeout(_cvis_update, 300);
      return;
    }

    if (cvis_state.id2bw == null) {
      var matching = cvis_dbg_match_units(global_data, cvis_state);
      if (!matching) {
        window.setTimeout(_cvis_update, 5);
        return;
      }
      cvis_dbg_lib_init(global_data, cvis_state);
      cvis_dbg_unitslist_init(global_data, cvis_state);
      cvis_dbg_logs_init(global_data, cvis_state);
      cvis_dbg_logs_attachments_init(global_data, cvis_state);
      cvis_dbg_blackboard_init(global_data, cvis_state);
      cvis_dbg_tasks_init(global_data, cvis_state);
      cvis_dbg_trees_init(global_data, cvis_state);
      cvis_dbg_heatmaps_init(global_data, cvis_state);
      cvis_dbg_tensors_summaries_init(global_data, cvis_state);
      cvis_dbg_values_graph_init(global_data, cvis_state);

      $.each(cvis_state.multi, function(other_name, other) {
        if (other_name == cvis_state.current_multi) {
          return;
        }
        cvis_dbg_heatmaps_merge(global_data, cvis_state, other);
        cvis_dbg_trees_merge(global_data, cvis_state, other);
        cvis_dbg_tensors_summaries_merge(global_data, cvis_state, other);
        cvis_dbg_values_graph_merge(global_data, cvis_state, other);
      });


      if (config['frame'] !== undefined) {
        cvis_state.functions.openbw_set_current_frame(config['frame']);
      }
    }

    cvis_state.current_frame = _cvis_current_frame();
    if (cvis_state.current_frame < cvis_state.moving_to_frame) {
      // OpenBW is moving to given frame, let's render cvis at the destination
      // frame right now
      cvis_state.current_frame = cvis_state.moving_to_frame;
    }
    else {
      cvis_state.moving_to_frame = -1;
    }
    $('.cvis-dbg-current-frame').text(cvis_state.current_frame);
    cvis_dbg_matching_update(global_data, cvis_state);
    cvis_dbg_unitslist_update_selected(global_data, cvis_state);
    cvis_dbg_unitslist_update_global(global_data, cvis_state);
    cvis_dbg_tasks_update(global_data, cvis_state);
    cvis_dbg_logs_update(global_data, cvis_state);
    cvis_dbg_blackboard_update(global_data, cvis_state);
    cvis_dbg_drawcommands_update(global_data, cvis_state);
    cvis_dbg_trees_update(global_data, cvis_state);
    cvis_dbg_heatmaps_update(global_data, cvis_state);
    cvis_dbg_tensors_summaries_update(global_data, cvis_state);
    cvis_dbg_values_graph_update(global_data, cvis_state);
    cvis_dbg_lib_update_end(global_data, cvis_state);
    cvis_state.previous_update_frame = cvis_state.current_frame;

    window.setTimeout(_cvis_update, 500);
  }

  _cvis_update();
}
