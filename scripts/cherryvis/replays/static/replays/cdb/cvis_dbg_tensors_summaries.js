/*
 * Copyright (c) 2019-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

"use strict";

function cvis_dbg_tensors_summaries_init(global_data, cvis_state) {
  var DOM_LIST = $('#cdbTensorsSummariesList');
  var SUMMARIES_SEARCH = $('#cdbTensorsSummariesSearch');

  cvis_state.tensors_summaries = {
    expanded_names: new Set(),
  };

  function create_tensor_summary_entry(summary) {
    var summary_html = DOM_LIST.children().filter('.template').clone();
    summary_html.removeClass('collapse template');
    summary_html.find('.tensor-append-summary').append($('#tensor-summary-items').clone().children());
    fill_summary_html_values(summary_html, summary);
    summary_html.find('.see-more').click(function() {
      summary_html.find('.see-more-target').toggleClass('collapse');
      var hidden = summary_html.find('.see-more-target').hasClass('collapse');
      if (hidden) {
        cvis_state.tensors_summaries.expanded_names.delete(summary.name);
      }
      else {
        cvis_state.tensors_summaries.expanded_names.add(summary.name);
      }
    });
    if (cvis_state.tensors_summaries.expanded_names.has(summary.name)) {
      summary_html.find('.see-more-target').removeClass('collapse');
    }
    return summary_html;
  }

  function fill_summary_html_values(html, summary) {
    html.find('.summary-name').text(summary.name);
    var setFloat = function(selector, value) {
      html.find(selector).text(parseFloat(value).toPrecision(4));
    };
    html.find('.summary-shape').text('[' + summary.shape.join(', ') + ']');
    setFloat('.summary-min', summary.min);
    setFloat('.summary-max', summary.max);
    setFloat('.summary-mean', summary.mean);
    setFloat('.summary-stddev', summary.std);
    setFloat('.summary-median', summary.median);
    setFloat('.summary-absmedian', summary.absmedian);
    html.find('.summary-histogram').each(function() {
      summary_create_histogram(summary.hist, summary, this);
    });
  }

  function summary_create_histogram(histogram, summary, element) {
    $(element).empty();
    if ($(element).width() <= 0) {
      $(element).width(300);
    }
    $(element).height(200);
    var rows = [];
    var bucket_width = (histogram.max - histogram.min) / histogram.num_buckets;
    $.each(histogram.values, function(i, v) {
      rows.push({ bucket: histogram.min + (i + 0.5) * bucket_width, count: v});
    });
    var mean_bucket = (summary.mean - histogram.min) / bucket_width;
    var median_bucket = (summary.median - histogram.min) / bucket_width;
    new Morris.Area({
      element: element,
      data: rows,
      xkey: 'bucket',
      ykeys: ['count'],
      labels: ['bucket size'],
      axes: false,
      parseTime: false,
      smooth: false,

      // Display mean on the graph
      //events: [mean_bucket],
      //eventStrokeWidth: 2,

      hoverCallback: function (index, options, content, row) {
        var h = $('<div>').append($(content));
        var min = parseFloat(histogram.min + index * bucket_width).toPrecision(3);
        var max = parseFloat(histogram.min + (index + 1) * bucket_width).toPrecision(3);
        h.find('.morris-hover-row-label').text('[' + min + ', ' + max + ']');
        return h.html();
      }
    });
  }

  cvis_state.functions.create_tensor_summary_entry = create_tensor_summary_entry;
  cvis_state.functions.fill_summary_html_values = fill_summary_html_values;
  cvis_state.functions.summary_create_histogram = summary_create_histogram;
  cvis_state.functions.has_tensor_summaries = function() {
    return global_data.tensors_summaries !== undefined && Object.keys(global_data.tensors_summaries).length;
  };


  SUMMARIES_SEARCH.val('').removeAttr('disabled');
  DOM_LIST.find('.cvis-hide-when-disabled').show();
}

function cvis_dbg_tensors_summaries_update(global_data, cvis_state) {
  var DOM_LIST = $('#cdbTensorsSummariesList');
  var SUMMARIES_SEARCH = $('#cdbTensorsSummariesSearch');

  if (!cvis_state.functions.has_tensor_summaries()) {
    SUMMARIES_SEARCH.val('No tensor summary available').attr('disabled', 'disabled');
    DOM_LIST.find('.cvis-hide-when-disabled').hide();
    return;
  }

  // Find frame to display
  var display_frame = cvis_state.current_frame;
  // Account for lag
  display_frame += (cvis_state.current_frame - cvis_state.previous_update_frame) / 2;
  var matchf = cvis_state.functions.get_matching_frames(
    Object.keys(global_data.tensors_summaries),
    display_frame
  );
  DOM_LIST.find('.summaries-current-frame').text('f' + matchf.closest);

  var data = global_data.tensors_summaries[matchf.closest];
  data.forEach(function(d, i) {
    d['id'] = matchf.closest + "_" + i;
  });
  var matching = data.filter(d => d.name.includes(SUMMARIES_SEARCH.val()));
  var total_count = matching.length;
  var matching_entries = {
    'total_count': total_count,
    'entries': matching.slice(0, 20),
  };

  DOM_LIST.find('.cvis-hide-when-disabled').show();
  DOM_LIST.find('.cvis-num-results').text(matching_entries.total_count);
  matching_entries = matching_entries.entries;
  DOM_LIST.find('.cvis-num-results-displayed').text(matching_entries.length);

  update_dom_list(DOM_LIST,
    matching_entries,
    'data-tensor-summary-id',
    cvis_state.functions.create_tensor_summary_entry);
}
