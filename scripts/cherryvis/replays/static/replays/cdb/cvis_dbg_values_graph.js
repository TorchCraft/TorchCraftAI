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
        var ret = this.handleEvent_parent(event);
        if ("click" === event.type) {
          var elem = this.chart.getElementsAtEventForMode(event, 'nearest', { intersect: false });
          if (elem.length) {
            var frame = parseInt(elem[0]._xScale.ticks[elem[0]._index]);
            cvis_state.functions.openbw_set_current_frame(frame);
            this.chart.lastDraw = 0;
          }
        }
        return ret;
      },
  });

  cvis_state.functions.linechart_create = linechart_create;
  cvis_state.functions.linechart_destroy = linechart_destroy;

  var DOM_ROOT = $('#game-values');
  var chartPlaceholder = DOM_ROOT.find('.chart-placeholder-canvas')[0];
  chartPlaceholder = linechart_destroy(chartPlaceholder);
  cvis_state.linecharts = {
    charts_with_frame_marker: [],
  };
  var c = linechart_create(chartPlaceholder, 'frame', global_data.game_values, 'LineWithFrameNum');
  if (c !== null) {
    DOM_ROOT.find('.when-no-chart').hide();
  }
  else {
    DOM_ROOT.find('.when-no-chart').show();
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
