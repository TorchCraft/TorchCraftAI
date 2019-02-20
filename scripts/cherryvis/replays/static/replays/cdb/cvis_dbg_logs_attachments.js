/*
 * Copyright (c) 2019-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

"use strict";

function cvis_dbg_logs_attachments_init(global_data, cvis_state) {

  function type_possibilities(type) {
    var possibilities = {
      bar: true,
      curve: true,
      table: true,
    };
    switch (type) {
      case 'Unit*':
      case 'Position':
      case 'Dumpable':
        possibilities.bar = false;
      case 'std::string':
        possibilities.curve = false;
        break;
      case 'float':
      case 'int':
        break;
      default:
        console.log('Unsupported key/value', type);
    }
    return possibilities;
  }

  function read_value(key) {
    switch (typeof key) {
      case 'object':
        // Unit*
        if (key.type == 'unit') {
          return 'i' + key.id;
        }
        // Position
        if (key.type == 'position') {
          return '(' + key.x + ', ' + key.x + ')';
        }
      case 'number': // int or float
        return parseFloat(key);
      case 'string': return key;
    }
  }

  function read_value_html(key, frame) {
    switch (typeof key) {
      case 'object':
        // Unit*
        if (key.type == 'unit') {
          var unit_info = {'type': -1};
          _cvis_apply_updates(unit_info, frame, global_data['units_updates'][key.id]);
          return $('<a href="#" class="cvis-link-unit"></a>')
            .attr('data-unit-id', key.id)
            .text(cvis_state.functions.unittype_get_name(unit_info.type) + ' i' + key.id);
        }
        // Position
        if (key.type == 'position') {
          return $('<a href="#" class="cvis-link-position"></a>')
            .attr('data-pos-x-wt', key.x)
            .attr('data-pos-y-wt', key.y);
        }
      default:
        return $('<span>').text(read_value(key));
    }
  }

  function log_graph_render_as_table(html, log_info, frame) {
    var values = Array.isArray(log_info.map) ? log_info.map : Object.entries(log_info.map);

    // Sort by Y desc if it makes sense
    switch (log_info.value_type) {
      case 'float':
      case 'int':
        values = values.sort((a, b) => read_value(b[1]) - read_value(a[1]));
    }

    var list = $('<ul class="list-unstyled">');
    html.append(list);
    var addRow = function(key, val) {
      var li = $('<li>');
      var div = $('<div class="row sidebar-item-container-no-pad">');
      var key = $('<div class="col-md-6 no-pad-left"></div>').append(key);
      var value = $('<div class="col-md-6"></div>').append(val);
      div.append(key, value);
      li.append(div);
      list.append(li);
    };

    addRow($('<span>').text('Key'), $('<span>').text('Value'));
    $.each(values, function(i, item) {
      addRow(read_value_html(item[0], frame), read_value_html(item[1], frame));
    });
  }

  function log_graph_render_as_chart(html, log_info, chart_type) {
    var values = Array.isArray(log_info.map) ? log_info.map : Object.entries(log_info.map);
    var datapoints;
    var xlabels = undefined;
    if (chart_type == 'scatter') {
      datapoints = values.map(function(p) {
        return {
          x: read_value(p[0]),
          y: read_value(p[1]),
        };
      });
      datapoints.sort((a, b) => a.x - b.x);
    }
    else {
      // Sort by Y desc
      values = values.map(a => [read_value(a[0]), read_value(a[1])]);
      values = values.sort((a, b) => b[1] - a[1]);
      datapoints = values.map(a => a[1]);
      xlabels = values.map(a => a[0]);
    }

    var parent = $('<div style="position: relative; height: 20vh;"><canvas></canvas></div>');
    html.empty();
    html.append(parent);
    var datasets = [{
      label: 'value',
      fill: false,
      backgroundColor: 'rgb(39, 112, 51)',
      borderColor: 'rgb(73, 183, 91)',
      showLine: true,
      tension: 0, // No interpolation
      data: datapoints,
    }];
    var chart = cvis_state.functions.linechart_create(parent.find('canvas')[0], '', undefined, chart_type, datasets, xlabels);
    var gridLines = {
      color: 'rgb(130,130,130)',
      zeroLineColor: 'rgb(160,160,160)',
    };
    chart.config.options.title.display = false;
    chart.config.options.scales.yAxes[0].scaleLabel.display = false;
    chart.config.options.scales.yAxes[0].gridLines = gridLines;
    chart.config.options.scales.yAxes[0].ticks.fontColor = 'rgb(160,160,160)';
    chart.config.options.scales.xAxes[0].scaleLabel.display = false;
    chart.config.options.scales.xAxes[0].gridLines = gridLines;
    chart.config.options.scales.xAxes[0].ticks.fontColor = 'rgb(160,160,160)';
    chart.config.options.legend.display = false;
    chart.config.options.maintainAspectRatio = false;
    chart.update();
  }

  function log_attachments_render(html, log_info) {
    if (!log_info.attachments) {
      return;
    }
    log_info.attachments.forEach(function(attachment) {
      var html_sub = $('<div class="col-md-12"></div>');
      html.append(html_sub);
      var values = Array.isArray(attachment.map) ? attachment.map : Object.entries(attachment.map);
      if (!values.length) {
        html_sub.text('map<' + attachment.key_type + ', ' + attachment.value_type + '> is empty');
        return;
      }
      var renderX = type_possibilities(attachment.key_type);
      var renderY = type_possibilities(attachment.value_type);
      var curve = renderX.curve && renderY.curve;
      var bar = renderX.bar && renderY.bar;
      if (curve || bar) {
        log_graph_render_as_chart(html_sub, attachment, curve ? 'scatter' : 'bar');
        return;
      }

      log_graph_render_as_table(html_sub, attachment, log_info.frame);
    });
    cvis_state.functions.parse_cvis_links(html);
  }

  cvis_state.functions.log_attachments_render = log_attachments_render;
}
