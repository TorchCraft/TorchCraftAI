/*
 * Copyright (c) 2019-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

"use strict";

function cvis_dbg_values_graph_init(global_data, cvis_state) {
  var CHART_COLORS = {
    green: 'rgb(73, 183, 91)',
    blue: 'rgb(62, 187, 209)',
    red: 'rgb(216, 72, 36)',
    grey: 'rgb(198, 197, 196)',
    yellow: 'rgb(239, 235, 100)',
    purple: 'rgb(181, 100, 239)',
    orange: 'rgb(247, 196, 66)'
  };

  function chartjs_create_datasets(xlabel, draw_data) {
    var keys = new Set();
    $.each(draw_data, function(x, d) {
      for (let key of Object.keys(d)) {
        keys.add(key);
      }
    });
    keys = Array.from(keys);
    var datasets = {};
    var colorsLoop = Object.values(CHART_COLORS);
    for (let key of keys) {
      datasets[key] = {
        label: key,
        fill: false,
        backgroundColor: colorsLoop[Object.keys(datasets).length % colorsLoop.length],
        borderColor: colorsLoop[Object.keys(datasets).length % colorsLoop.length],
        spanGaps: true,
        tension: 0, // No interpolation
        data: [],
      };
    }
    for (let x of Object.keys(draw_data)) {
      for (let k of keys) {
        var v = draw_data[x][k] === undefined ? null : draw_data[x][k];
        datasets[k].data.push({
          x: x,
          y: v,
        });
      }
    }
    for (let key of keys) {
      if (datasets[key].data.length > 20) {
        datasets[key].pointRadius = 0;
      }
    }
    return Object.values(datasets);
  }

  function linechart_create(html_canvas, xlabel, draw_data, lineType, datasets, xvalues) {
    if (lineType === undefined) {
      lineType = 'Line';
    }
    if (datasets === undefined) {
      if (draw_data === undefined || !Object.keys(draw_data).length) {
        return null;
      }
      datasets = chartjs_create_datasets(xlabel, draw_data);
      xvalues = Object.keys(draw_data);
    }
    if (lineType == 'LineWithFrameNum') {
      xvalues = [];
    }
    var config = {
      type: lineType,
      data: {
        labels: xvalues,
        datasets: datasets,
      },
      options: {
        responsive: true,
        title: {
          display: true,
          text: 'Game values'
        },
        tooltips: {
          mode: 'nearest',
          intersect: false,
        },
        hover: {
          mode: 'nearest',
          intersect: false
        },
        scales: {
          xAxes: [{
            display: true,
            type: 'linear',
            scaleLabel: {
              display: true,
              labelString: xlabel
            }
          }],
          yAxes: [{
            display: true,
            scaleLabel: {
              display: true,
              labelString: 'Value'
            }
          }],
        },
      }
    };
    return new Chart(html_canvas.getContext('2d'), config);
  }

  function linechart_destroy(element) {
    element = $(element);
    var parent = element.parent();
    var c = element.clone();
    c.removeAttr('style');
    c.removeAttr('height');
    c.removeAttr('width');
    element.remove();
    parent.append(c);
    return c[0];
  }

  Chart.defaults.LineWithFrameNum = Chart.defaults.line;
  Chart.controllers.LineWithFrameNum = Chart.controllers.line.extend({
      initialize: function(chart, index) {
        Chart.controllers.line.prototype.initialize.call(this, chart, index);
        cvis_state.linecharts.charts_with_frame_marker.push(this.chart);
        if (!this.chart.handleEvent_parent) {
          this.chart.handleEvent_parent = this.chart.handleEvent;
          this.chart.handleEvent = this.handleEvent;
        }
      },
      draw: function(ease) {
        this.drawAtFrame(cvis_state.current_frame, ease);
      },
      drawAtFrame: function(frame, ease) {
        Chart.controllers.line.prototype.draw.call(this, ease);

        var ctx = this.chart.ctx;
        var xaxis = this.chart.scales['x-axis-0'];
        var x = (frame - parseFloat(xaxis.min)) / (parseFloat(xaxis.max) - parseFloat(xaxis.min));
        x = Math.max(0, Math.min(x, 1))
        x = (x * xaxis.width) + xaxis.left;
        var topY = this.chart.scales['y-axis-0'].top;
        var bottomY = this.chart.scales['y-axis-0'].bottom;

        // draw line
        ctx.save();
        ctx.beginPath();
        ctx.moveTo(x, topY);
        ctx.lineTo(x, bottomY);
        ctx.lineWidth = 1;
        ctx.strokeStyle = '#07C';
        ctx.stroke();
        ctx.restore();
        this.chart.lastDraw = (new Date()).getTime();
      },
      handleEvent: function(event) {
        var changed = this.handleEvent_parent(event);
        if ("click" === event.type) {
          var elem = this.chart.getElementsAtEventForMode(event, 'nearest', { intersect: false });
          if (elem.length) {
            var withinChart = event.y > elem[0]._yScale.margins.top;
            if (withinChart) {
              var frame = parseInt(this.chart.data.datasets[elem[0]._datasetIndex].data[elem[0]._index].x);
              cvis_state.functions.openbw_set_current_frame(frame);
              this.chart.lastDraw = 0;
            }
          }
        }
        return changed;
      },
  });

  var TEMPLATE_TITLE = $('#game-values-template-title');
  var TEMPLATE_BODY = $('#game-values-template');
  var idCounter = 0;
  function add_game_values_graph(name, game_values, show_active) {
    var id = '_game_value_' + (idCounter++);
    var tabTitle = TEMPLATE_TITLE.clone().removeClass('collapse').addClass('delete-on-reset-cvis').removeAttr('id');
    var tabContent = TEMPLATE_BODY.clone().removeClass('collapse').addClass('delete-on-reset-cvis').attr('id', id);
    tabTitle.find('a').attr('href', '#' + id).attr('aria-selected', false).removeClass('show active').text(name);
    if (show_active) {
      tabTitle.find('a').attr('aria-selected', 'false').addClass('show active');
      tabContent.addClass('active show');
    }
    TEMPLATE_TITLE.parent().append(tabTitle);
    TEMPLATE_BODY.parent().append(tabContent);
    var chartPlaceholder = tabContent.find('.chart-placeholder-canvas')[0];
    chartPlaceholder = linechart_destroy(chartPlaceholder);
    var c = linechart_create(chartPlaceholder, 'frame', game_values, 'LineWithFrameNum');
    if (c !== null) {
      tabContent.find('.when-no-chart').hide();
    }
    else {
      tabContent.find('.when-no-chart').show();
    }
  };

  function add_player_values(name, game_values) {
    var dataByPrefix = {};
    $.each(game_values, function(frame, frame_data) {
      $.each(frame_data, function(key, value) {
        var parts = key.split('.');
        if (parts.length < 2) {
          parts = [''].concat(parts);
        }
        if (!dataByPrefix.hasOwnProperty(parts[0])) {
          dataByPrefix[parts[0]] = {};
        }
        if (!dataByPrefix[parts[0]].hasOwnProperty(frame)) {
          dataByPrefix[parts[0]][frame] = {};
        }
        dataByPrefix[parts[0]][frame][parts[1]] = value;
      });
    });
    var e = Object.entries(dataByPrefix);
    e.sort(function(a, b) { return a[0].length - b[0].length; });
    $.each(e, function(_, entry) {
      const prefix = entry[0];
      const values = entry[1];
      var show_active = false;
      var graph_name = prefix;
      if (prefix == '' && name == '') {
        show_active = true;
      }
      if (graph_name == '') {
        graph_name = name == '' ? 'Game values' : '';
      }
      if (name != '') {
        if (graph_name == '') {
          graph_name = name;
        }
        else {
          graph_name = name + '/' + graph_name;
        }
      }
      add_game_values_graph(graph_name, values, show_active);
    });
  };

  cvis_state.functions.linechart_create = linechart_create;
  cvis_state.functions.linechart_destroy = linechart_destroy;
  cvis_state.functions.add_game_values_graph = add_game_values_graph;
  cvis_state.functions.add_player_values = add_player_values;
  cvis_state.linecharts = {
    charts_with_frame_marker: [],
  };

  add_player_values("", global_data.game_values);
}

function cvis_dbg_values_graph_merge(global_data, cvis_state, other) {
  if (Object.keys(other.data.game_values).length > 0) {
    cvis_state.functions.add_player_values(other.name, other.data.game_values);
  }
}

function cvis_dbg_values_graph_update(global_data, cvis_state) {
  $.each(cvis_state.linecharts.charts_with_frame_marker, function(_, chart) {
    // Force charts redraw, so it can update the current frame line
    if (chart.lastDraw + 300 < (new Date()).getTime()) {
      chart.draw();
    }
  });
}
