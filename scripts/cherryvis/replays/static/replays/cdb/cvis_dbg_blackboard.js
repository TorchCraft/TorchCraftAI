/*
 * Copyright (c) 2019-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

"use strict";

function cvis_dbg_blackboard_init(global_data, cvis_state) {
  $('#cdbBoardSearch').val('').removeAttr('disabled');
  $('.cvis-hide-when-board-disabled').show();
}

function cvis_dbg_blackboard_update(global_data, cvis_state) {
  if (global_data['board_updates'] === undefined || global_data['board_updates'].length == 0) {
    $('#cdbBoardSearch').val('No board entry available').attr('disabled', 'disabled');
    $('.cvis-hide-when-board-disabled').hide();
    return;
  }

  cvis_state.workers.board.worker.postMessage({
    'search': $('#cdbBoardSearch').val(),
    'current_frame': cvis_state.current_frame,
  });
  var matching = cvis_state.workers.board.last_result;
  if (matching == null) {
    return;
  }

  function render_board_entry(board_entry) {
    var html = DOM_LIST.find('.template').clone();
    html.removeClass('template collapse').addClass('delete-on-reset-cvis');
    html.find('.cvis-board-entry-key').text(board_entry['key']);
    html.find('.cvis-board-entry-val').text(board_entry['value']);
    return html;
  }


  $('.cvis-hide-when-board-disabled').show();
  var DOM_LIST = $('#cdbBoardView');
  $('.cvis-board-num-results').text(matching.total_count);
  $('.cvis-board-num-results-displayed').text(matching.entries.length);

  update_dom_list(DOM_LIST,
    matching.entries,
    'data-board-key',
    render_board_entry);
}
