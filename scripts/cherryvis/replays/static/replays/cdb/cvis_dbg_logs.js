/*
 * Copyright (c) 2019-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

"use strict";

function cvis_dbg_logs_init(global_data, cvis_state) {
  var CVIS_BOT_GLOBAL_LOGS_VIEW = $('#cdbBotGlobalLogsView');

  function render_log_message(message_text) {
    var html_p = $('<p class="cvis-log-message"></p>');
    html_p.text(message_text);
    html_p.html(html_p.html().replace(/(i((?:\d)+) ([a-zA-Z_]+))/g,
      '<a href="#" class="cvis-link-unit" data-unit-id="$2">$1</a>'));
    // u4277
    html_p.html(html_p.html().replace(/(u((?:\d)+))/g,
      '<a href="#" class="cvis-link-upc" data-upc-id="$2">$1</a>'));
    html_p.html(html_p.html().replace(/(\((\d+),(\d+)\))/g,
      '<a href="#" class="cvis-link-position" data-pos-x-wt="$2" data-pos-y-wt="$3">$1</a>'));
    return html_p;
  }

  function render_log_content(html, log_info) {
    console.assert(html[0] !== undefined);

    var message_text = log_info.message;
    if (log_info.attachments && log_info.attachments.length) {
      var loaded = false;
      html.find('.btn-toggle-log-attachments').click(function(){
        html.find('.log-attachments-container').collapse("toggle");
        if (!loaded) {
          cvis_state.functions.log_attachments_render(html.find('.log-attachments-container'), log_info);
          loaded = true;
        }
      });
    }
    else {
      html.find('.btn-toggle-log-attachments').remove();
    }
    html.find('.log-message-container').append(render_log_message(log_info.message));
  }

  function logs_init_template(html) {
    html.append(
      CVIS_BOT_GLOBAL_LOGS_VIEW.find('.logs-search-li').clone(),
      CVIS_BOT_GLOBAL_LOGS_VIEW.find('.template').clone()
    );
    html.find('.logs-search').val('').removeAttr('disabled');
    html.find('.cvis-show-when-logs-enabled').show();
    html.find('.cvis-logs-num-results').text(0);
    html.find('.cvis-logs-num-results-displayed').text(0);
  }

  function logs_init(html, logs, use_worker) {
    if (logs.length == 0) {
      html.find('.logs-search').val('No logs available').attr('disabled', 'disabled');
      html.find('.cvis-show-when-logs-enabled').hide();
      return (q => null);
    }
    else {
      html.find('.logs-search').val('').removeAttr('disabled');
      html.find('.cvis-show-when-logs-enabled').show();
    }
    if (use_worker) {
      return function(query) {
        cvis_state.workers.logs.worker.postMessage(query);
        return cvis_state.workers.logs.last_result;
      };
    }
    else {
      var logs_state = cvis_logs_handler_init(logs);
      return function(query) {
        return cvis_logs_handler_update(logs_state, query);
      };
    }
  }

  function logs_update(html, query_logs) {
    var matching_logs = query_logs({
      'search_pattern': html.find('.logs-search').val(),
      'current_frame': cvis_state.current_frame,
    });
    if (matching_logs == null) {
      return;
    }

    html.find('.cvis-show-when-logs-enabled').show();
    html.find('.cvis-logs-num-results').text(matching_logs.total_count);
    matching_logs = matching_logs.entries;
    html.find('.cvis-logs-num-results-displayed').text(matching_logs.length);

    update_dom_list(html,
      matching_logs,
      'data-log-id',
      function(log_info) {
        var html = CVIS_BOT_GLOBAL_LOGS_VIEW.find('.template').clone();
        html.removeClass('template collapse');
        html.addClass('delete-on-reset-cvis');
        html.attr('data-frame-id', log_info.frame);
        html.find('a.log-link-frame')
          .attr('data-frame-id', log_info.frame)
          .attr('title', log_info.file + ':' + log_info.line);
        render_log_content(html, log_info);
        cvis_state.functions.parse_cvis_links(html);
        return html;
    });
  }

  cvis_state.functions.logs_init_template = logs_init_template;
  cvis_state.functions.logs_init = logs_init;
  cvis_state.functions.logs_update = logs_update;
  cvis_state.functions.logs_global_query = logs_init(CVIS_BOT_GLOBAL_LOGS_VIEW, global_data.logs, true);
}

function cvis_dbg_logs_update(global_data, cvis_state) {
  var CVIS_BOT_GLOBAL_LOGS_VIEW = $('#cdbBotGlobalLogsView');
  cvis_state.functions.logs_update(CVIS_BOT_GLOBAL_LOGS_VIEW, cvis_state.functions.logs_global_query);
}
