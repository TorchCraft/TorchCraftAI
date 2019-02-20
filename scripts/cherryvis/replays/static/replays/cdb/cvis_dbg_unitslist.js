/*
 * Copyright (c) 2019-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

"use strict";

function cvis_dbg_unitslist_init(global_data, cvis_state) {
  function unittype_get_name(type) {
    var type_name = global_data['types_names'][type];
    if (type_name !== undefined)
      return type_name;
    else
      return 'UnkType ' + type;
  }

  function unittype_get_name_short(unit_info) {
    var type_name = global_data['types_names'][unit_info.type];
    if (type_name !== undefined)
      type_name = type_name.split("_")[1];
    else
      type_name = "UnkType";
    return type_name + " i" + unit_info.id;
  }

  function unit_istracked(unit_id) {
    return cvis_state.unitslist.tracked.findIndex(e => e.unit_id == unit_id) != -1;
  }

  function unit_track(unit_id) {
    if (!unit_istracked(unit_id)) {
      var unit_info = cvis_state.functions.get_unit_by_cp_id(unit_id);
      if (!unit_info.found) {
        return;
      }
      cvis_state.unitslist.tracked.push({
        id: 'unit_tracked_cp' + unit_id,
        name: unittype_get_name_short(unit_info),
        unit_id: unit_id,
        initial_unit_info: unit_info,
      });
      $('.track-unit-btn[data-unit-id=' + unit_id + ']').hide();
    }
  }

  function unit_untrack(unit_id) {
    $('.track-unit-btn[data-unit-id=' + unit_id + ']').show();
    cvis_state.unitslist.tracked = cvis_state.unitslist.tracked.filter(i => i.unit_id != unit_id);
  }

  function unit_html_init(html, unit_info) {
    var id = unit_info.id;
    var link = $('<a href="#" class="cvis-link-unit unit-type-name">').attr('data-unit-id', id);
    if (unit_info.bw_id) {
      link.attr('data-unit-bw-id', unit_info.bw_id);
    }
    html.find('.unit-link').append(link);

    var track_btn = html.find('.track-unit-btn');
    track_btn.attr('data-unit-id', id);
    if (cvis_state.functions.unit_istracked(id)) {
      track_btn.hide();
    }

    html.find('.first-seen-frame-link').addClass('cvis-link-frame first-seen-frame').attr(
      'data-frame-id',
      cvis_state.units_first_seen_frame[id]);
    html.find('.first-seen-frame').text('f' + cvis_state.units_first_seen_frame[id]);

    html.attr('data-unit-id', id);
    cvis_state.functions.parse_cvis_links(html);
  }

  function unit_html_update(html, unit_id, unit_info) {
    if (unit_info === undefined) {
      unit_info = cvis_state.functions.get_unit_by_cp_id(unit_id);
    }

    html.find('.unit-cherrypi-id').text('i' + (unit_id >= 0 ? unit_id : '???'));
    if (unit_info && unit_info.found) {
      html.find('.show-when-unit-found').show();
      html.find('.hide-when-unit-found').hide();

      html.find('.unit-bw-id').text('bw' + (unit_info['bw_id'] ? unit_info['bw_id'] : '???'));
      html.find('.cur-hp').text(unit_info.hp);
      var can_track_task = false;
      if (unit_info.task >= 0) {
        html.find('.task-id').text('t' + unit_info.task);
        html.find('.task-name').text(cvis_state.functions.task_display_name(unit_info.task));
        can_track_task = !cvis_state.functions.task_istracked(unit_info.task);
      }
      else {
        html.find('.task-id').text('');
        html.find('.task-name').text('None');
      }

      if (can_track_task) {
        html.find('.track-task-btn').removeAttr('disabled');
        html.find('.track-task-btn').attr('data-task-id', unit_info.task);
      }
      else {
        html.find('.track-task-btn').attr('disabled', 'disabled');
      }
      var x = parseInt(unit_info.x / XYPixelsPerWalktile);
      var y = parseInt(unit_info.y / XYPixelsPerWalktile);
      html.find('.unit-position')
        .attr('data-pos-x-wt', x)
        .attr('data-pos-y-wt', y)
        .text('(' + x + ', ' + y + ') walktiles');

      var typeText = unittype_get_name(unit_info['type']);
      html.find('.unit-type-name').text(typeText);
      html.find('.unit-type-id').text(unit_info['type']);
    }
    else {
      html.find('.show-when-unit-found').hide();
      html.find('.hide-when-unit-found').show();

      html.find('.unit-bw-id').text('!NOTFOUND!');
    }
  }

  function unitslist_update(html_ul, units, hash_fn) {
    function create_unit(e) {
      var unit_info = e.unit_info;
      var html = html_ul.find('.template').clone();
      html.removeClass('template collapse');
      html.addClass('delete-on-reset-cvis');
      html.attr('data-unit-id', unit_info.id);

      cvis_state.functions.unit_html_init(html, unit_info);
      cvis_state.functions.unit_html_update(html, unit_info.id, unit_info);
      return html;
    }

    if (hash_fn === undefined) {
      hash_fn = (u) => u.id;
    }
    units = units.map(function(u) {
      return {
        unit_info: u,
        id: hash_fn(u),
      }
    });
    update_dom_list(html_ul,
      units,
      'data-unit-hash',
      create_unit);
  }


  cvis_state.unitslist = {
    tracked: [],
  };
  cvis_state.functions.unit_istracked = unit_istracked;
  cvis_state.functions.unit_track = unit_track;
  cvis_state.functions.unit_untrack = unit_untrack;
  cvis_state.functions.unit_html_init = unit_html_init;
  cvis_state.functions.unit_html_update = unit_html_update;
  cvis_state.functions.unittype_get_name = unittype_get_name;
  cvis_state.functions.unitslist_update = unitslist_update;
}


function cvis_dbg_unitslist_update_global(global_data, cvis_state) {
  var UNITS_TABS_NAV = $('#cherrypi-state-tabs-nav');
  var UNITS_TABS = $('#cherrypi-state-tabs');

  update_dom_list(UNITS_TABS,
    cvis_state.unitslist.tracked,
    'data-unit-id',
    function(e) {
      var copy = UNITS_TABS.children().filter('.template-tracked-unit').clone();
      copy.attr('id', e.id);
      copy.removeClass('collapse template-tracked-unit');
      copy.addClass('delete-on-reset-cvis');
      cvis_state.functions.unit_html_init(copy, e.initial_unit_info);

      // Unit logs
      copy.find('.show-when-unit-logs-found').hide();
      copy.find('.hide-when-unit-logs-found').show();
      var logs = global_data.units_logs ? global_data.units_logs[e.unit_id] : undefined;
      if (logs !== undefined) {
        cvis_state.functions.logs_init_template(copy.find('.unit-logs'));
        // Hackfix for alignment
        copy.find('.logs-search').parent().parent().removeClass('sidebar-item-container');
        e.logs_state = logs ? cvis_state.functions.logs_init(copy.find('.unit-logs'), logs) : undefined;
        if (e.logs_state) {
          copy.find('.show-when-unit-logs-found').show();
          copy.find('.hide-when-unit-logs-found').hide();
        }
      }
      cvis_state.functions.parse_cvis_links(copy);
      return copy;
    },
  );

  update_dom_list(UNITS_TABS_NAV,
    cvis_state.unitslist.tracked,
    'data-unit-id',
    function(e) {
      var closeButton = $('<button type="button" class="btn btn-sm btn-danger" aria-label="Close">x</button>');
      closeButton.click(function() {
        cvis_state.functions.unit_untrack(e.unit_id);
      });
      return $('<li class="nav-item btn-group delete-on-reset-cvis">').append(
        $('<a class="nav-link" data-toggle="tab" role="tab" aria-controls="tensors-details" aria-selected="false">'
          ).attr('aria-controls', e.id
          ).attr('href', '#' + e.id
          ).append(
            $('<i class="fas fa-pastafarianism"></i>'),
            $('<span>').text(e.name)
          ),
        closeButton
      );
    },
  );

  // Update units that need updating
  $.each(cvis_state.unitslist.tracked, function(_, tracked_info) {
    var html = $('#' + tracked_info.id);
    cvis_state.functions.unit_html_update(html, tracked_info.unit_id);
    if (tracked_info.logs_state)
      cvis_state.functions.logs_update(html.find('.unit-logs'), tracked_info.logs_state);
  });
}

// ========== SELECTED UNITS IN GAME
function cvis_dbg_unitslist_update_selected(global_data, cvis_state) {
  var selected_units_bw = _cvis_selected_units();
  var units_selected = [];
  for (var i = 0; i < selected_units_bw.length; ++i) {
    units_selected.push(cvis_state.functions.get_unit_by_bw_id(
        selected_units_bw[i].bw_id, true));
  }

  if (units_selected.length) {
    $('.hide-if-units-selected').hide();
  }
  else {
    $('.hide-if-units-selected').show();
  }

  cvis_state.functions.unitslist_update(
    $('#cdbSelectedUnitsView'),
    units_selected,
    u => (u.id + '__' + u.type)
  );
}
