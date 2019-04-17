/*
 * Copyright (c) 2019-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

"use strict";


// ========== TASKS LIST MANAGER
function cvis_dbg_tasks_init(global_data, cvis_state) {
  function task_display_name(task_id) {
    var task = global_data.tasks[task_id];
    if (task === undefined) {
      return 't' + task_id;
    }
    return 't' + task_id + ' ' + task['owner'] + '.' + task['name'];
  }

  function task_istracked(task_id) {
    return cvis_state.tasks.tracked.findIndex(e => e.task_id == task_id) != -1;
  }

  function task_track(task_id) {
    if (!task_istracked(task_id)) {
      var task_info = global_data.tasks[task_id];
      if (!task_info) {
        return;
      }
      cvis_state.tasks.tracked.push({
        id: 'task_tracked_cp' + task_id,
        name: task_display_name(task_id),
        task_id: task_id,
        task_info: task_info,
      });
      $('.track-task-btn[data-task-id=' + task_id + ']').hide();
    }
  }

  function task_untrack(task_id) {
    $('.track-task-btn[data-task-id=' + task_id + ']').show();
    cvis_state.tasks.tracked = cvis_state.tasks.tracked.filter(i => i.task_id != task_id);
  }

  function task_html_init(html, task_info) {
    html.find('.task-id').text('t' + task_info.id);
    html.find('.task-module-owner').text(task_info.owner);
    html.find('.task-name').text(task_info.name);
    html.find('.task-upc-id').text(task_info.upc_id);
    html.find('.task-posted-frame-link').addClass('cvis-link-frame posted-frame').attr(
      'data-frame-id',
      task_info.creation_frame);
    html.find('.posted-frame').text('f' + task_info.creation_frame);
    cvis_state.functions.parse_cvis_links(html);
  }

  function task_html_update(html, task_info) {
    var task_units = task_list_units(task_info.id);
    cvis_state.functions.unitslist_update(
      html.find('.task-units-list'),
      task_units,
      u => (u.id + '__' + u.type)
    );
  }

  function task_list_units(task_id) {
    // List unit IDs
    var task_units = [];
    $.each(global_data['units_updates'], function(unit_id, unit_updates) {
      var unit_info = {'task': -1};
      _cvis_apply_updates(unit_info, cvis_state.current_frame, unit_updates);
      if (unit_info['task'] == task_id)
        task_units.push(parseInt(unit_id));
    });
    // Find info about these units
    return task_units.map(u => cvis_state.functions.get_unit(u));
  }

  cvis_state.functions.task_display_name = task_display_name;
  cvis_state.functions.task_istracked = task_istracked;
  cvis_state.functions.task_track = task_track;
  cvis_state.functions.task_untrack = task_untrack;
  cvis_state.functions.task_list_units = task_list_units;
  cvis_state.functions.task_html_init = task_html_init;
  cvis_state.functions.task_html_update = task_html_update;

  cvis_state.tasks = {
    tracked: [],
  };
  $.each(global_data.tasks, function(task_id, task_info) {
    task_info.id = task_id;
  });
}

function cvis_dbg_tasks_update(global_data, cvis_state) {
  var TASKS_TABS_NAV = $('#cherrypi-state-tabs-nav');
  var TASKS_TABS = $('#cherrypi-state-tabs');

  update_dom_list(TASKS_TABS,
    cvis_state.tasks.tracked,
    'data-task-id',
    function(e) {
      var copy = TASKS_TABS.children().filter('.template-tracked-task').clone();
      copy.attr('id', e.id);
      copy.removeClass('collapse template-tracked-task');
      copy.addClass('delete-on-reset-cvis');
      cvis_state.functions.task_html_init(copy, e.task_info);
      cvis_state.functions.parse_cvis_links(copy);
      return copy;
    },
  );

  update_dom_list(TASKS_TABS_NAV,
    cvis_state.tasks.tracked,
    'data-task-id',
    function(e) {
      var closeButton = $('<button type="button" class="btn btn-sm btn-danger" aria-label="Close">x</button>');
      closeButton.click(function() {
        cvis_state.functions.task_untrack(e.task_id);
      });
      return $('<li class="nav-item btn-group delete-on-reset-cvis">').append(
        $('<a class="nav-link" data-toggle="tab" role="tab" aria-controls="tensors-details" aria-selected="false">'
          ).attr('aria-controls', e.id
          ).attr('href', '#' + e.id
          ).append(
            $('<i class="fas fa-flag-checkered"></i>'),
            $('<span>').text(e.name)
          ),
        closeButton
      );
    },
  );

  // Update tracked trasks
  $.each(cvis_state.tasks.tracked, function(_, tracked_info) {
    var html = $('#' + tracked_info.id);
    cvis_state.functions.task_html_update(html, tracked_info.task_info);
  });
}
