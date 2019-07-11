/*
 * Copyright (c) 2019-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

"use strict";

function cvis_dbg_heatmaps_init(global_data, cvis_state) {
  var DOM_LIST = $('#cdbHeatmapsView');
  var HEATMAPS_SEARCH = $('#cdbHeatmapsSearch');
  var TENSORS_TABS = $('#tensors-tabs');

  DOM_LIST.find('.cvis-hide-when-disabled').show();
  HEATMAPS_SEARCH.val('').removeAttr('disabled');

  cvis_state.heatmaps = {
    loading: false,
    loaded: new Map(),
    current_filename: null,
    loaded_count: 0,
  };

  // HTML RENDERING
  function create_heatmap_entry(info) {
    var html = DOM_LIST.find('.template').clone().removeClass('collapse template').addClass('delete-on-reset-cvis');
    html.attr('data-frame-id', info['first_frame']);
    html.find('.heatmap-multi-name').text(info.multi_name);
    html.find('.tensor-name').text(info['name']);
    html.find('.load-heatmap-button').click(function() {
      cvis_state.functions.load_heatmap(info, $(this));
    });
    if (cvis_state.functions.heatmap_is_loaded(info.filename, info.cvis_path)) {
      html.find('button').remove();
    }
    return html;
  }

  function get_c_buffer_for_frame(overlay, frame) {
    if (overlay.buf) {
      if (overlay.buf_frame == frame) {
        return overlay.buf;
      }
      _free(overlay.buf);
    }
    // Convert data to emcripten
    var arr = new Float32Array(overlay.data[frame].data);
    var nDataBytes = arr.length * arr.BYTES_PER_ELEMENT;
    var dataPtr = Module._malloc(nDataBytes);
    var dataHeap = new Uint8Array(Module.HEAPU8.buffer, dataPtr, nDataBytes);
    dataHeap.set(new Uint8Array(arr.buffer));

    overlay.buf = dataHeap.byteOffset;
    overlay.buf_frame = frame;
    return overlay.buf;
  }

  function update_summary_zeros_masked_out(summary, data) {
    if (summary.zero_masked_out_summary != undefined) {
      return summary.zero_masked_out_summary;
    }
    var zmos = $.extend({}, summary);
    summary.zero_masked_out_summary = zmos;
    // Recompute min / max / mean / std
    var min = undefined;
    var max = undefined;
    var total = 0;
    var totalSq = 0;
    var count = 0;
    for (var i = 0; i < data.length; ++i) {
      var v = parseFloat(data[i]);
      if (v == 0) {
        continue;
      }
      if (min == undefined) {
        min = v;
        max = v;
      }
      min = Math.min(min, v);
      max = Math.max(max, v);
      total += v;
      totalSq += v*v;
      count += 1;
    }
    if (count == 0) {
      min = NaN;
      max = NaN;
    }
    zmos.mean = total / count;
    zmos.std = Math.sqrt(totalSq / count - (zmos.mean * zmos.mean));
    zmos.min = min;
    zmos.max = max;
    return zmos;
  }

  function heatmap_update(overlay) {
    if (!overlay) {
      return;
    }
    var html = $('#' + overlay.id);
    // Find frame to display
    var display_frame = cvis_state.current_frame;
    // Account for lag
    display_frame += (cvis_state.current_frame - cvis_state.previous_update_frame) / 2;

    var matchf = cvis_state.functions.get_matching_frames(
      Object.keys(overlay.data),
      display_frame
    );
    html.find('.prev-frame').attr('data-frame-id', matchf.previous).text('f' + matchf.previous).parent().attr('data-frame-id', matchf.previous);
    html.find('.current-frame').attr('data-frame-id', matchf.closest).text('f' + matchf.closest).parent().attr('data-frame-id', matchf.closest);
    html.find('.next-frame').attr('data-frame-id', matchf.next).text('f' + matchf.next).parent().attr('data-frame-id', matchf.next);

    var data = overlay.data[matchf.closest];
    var currentHash = html.find('.overlay-mask-out-zeros').is(':checked');
    var previousHash = overlay.last_hash;
    overlay.last_hash = currentHash;
    var summary = data.summary;
    if (html.find('.overlay-mask-out-zeros').is(':checked')) {
      summary = update_summary_zeros_masked_out(summary, data.data);
    }
    var dimX = summary.shape[1];
    var dimY = summary.shape[0];

    // Value at cursor location
    var cursor_info = Module.get_screen_info();
    var overlay_y = (cursor_info.cursor_y + cursor_info.screen_y - data.top_left_pixel[1]) / data.scaling[1];
    var overlay_x = (cursor_info.cursor_x + cursor_info.screen_x - data.top_left_pixel[0]) / data.scaling[0];
    var cursor_val = '?';
    if (overlay_x >= 0 && overlay_x < dimX &&
        overlay_y >= 0 && overlay_y < dimY) {
      overlay_x = parseInt(overlay_x);
      overlay_y = parseInt(overlay_y);
      cursor_val = data.data[parseInt(overlay_x) + dimX * parseInt(overlay_y)];
    }
    if (overlay.buf_frame != matchf.closest || currentHash != previousHash) {
      cvis_state.functions.fill_summary_html_values(html, summary);
    }
    html.find('.overlay-cursorpxx').text(cursor_info.cursor_x + cursor_info.screen_x);
    html.find('.overlay-cursorpxy').text(cursor_info.cursor_y + cursor_info.screen_y);
    html.find('.overlay-cursorval').text(overlay.info.name + '[' + overlay_x + ', ' + overlay_y + '] = ' + cursor_val);

    // Data normalization
    var meanshift = parseFloat(html.find('.overlay-meanshift').val());
    meanshift *= (summary.max - summary.min);
    meanshift = parseFloat(parseFloat(meanshift).toPrecision(2));
    var stddevm = parseFloat(parseFloat(html.find('.overlay-stddevm').val()).toPrecision(2));
    var mean = summary.mean + meanshift;
    var std = summary.std * stddevm;
    if (!html.find('.overlay-substract-mean').is(':checked')) {
      mean = 0.5;
    }
    if (!html.find('.overlay-divide-stddev').is(':checked')) {
      std = 0.5;
    }
    html.find('.overlay-stddevm-val').text(stddevm);
    html.find('.overlay-meanshift-val').text(meanshift);

    // Rendering options
    var fast_rendering = html.find('.overlay-fast-rendering').is(':checked');
    if (fast_rendering) {
      html.find('.disable-if-fast-rendering').attr('disabled', 'disabled');
    }
    else {
      html.find('.disable-if-fast-rendering').removeAttr('disabled');
    }
    var alpha = parseInt(html.find('.overlay-alpha').val()) * 0.01;
    var saturation = parseInt(html.find('.overlay-saturation').val()) * 0.01;

    _js_add_screen_overlay(
      get_c_buffer_for_frame(overlay, matchf.closest),
      dimX,
      dimY,
      data.top_left_pixel[0],
      data.top_left_pixel[1],
      data.scaling[0],
      data.scaling[1],
      1 - alpha,
      saturation,
      mean,
      Math.max(std, 0.000000001),
      fast_rendering
    );
  }

  function load_callback(data, info, btn) {
    cvis_state.heatmaps.loading = false;
    DOM_LIST.find('button').removeAttr('disabled');
    var overlay = {
      info: info,
      data: data,
      buf: null,
      buf_frame: -1,
      id: 'heatmap' + (cvis_state.heatmaps.loaded_count++),
    };
    cvis_state.heatmaps.loaded.set(overlay.id, overlay);
    btn.remove();
  }

  function heatmap_is_loaded(filename, cvis_path) {
    return Array.from(cvis_state.heatmaps.loaded.values())
      .some(e => e.info.filename == filename && e.info.cvis_path == cvis_path);
  }

  cvis_state.functions.load_heatmap = function(info, btn) {
    if (heatmap_is_loaded(info.filename, info.cvis_path)) {
      btn.remove();
      return;
    }
    cvis_state.heatmaps.loading = true;
    DOM_LIST.find('button').attr('disabled', 'disabled');
    cvis_state.functions.load_cvis_json_file(
      info.cvis_path,
      info.filename,
      d => load_callback(d, info, btn),
      function() {
          console.log('Tensor loading failed', info);
          cvis_state.heatmaps.loading = false;
          DOM_LIST.find('button').removeAttr('disabled');
      }
    );
  }

  cvis_state.functions.has_heatmap = function() {
    return global_data.heatmaps !== undefined && global_data.heatmaps.length;
  }
  cvis_state.functions.heatmap_update = heatmap_update;
  cvis_state.functions.heatmap_is_loaded = heatmap_is_loaded;
  cvis_state.functions.create_heatmap_entry = create_heatmap_entry;

  cvis_state.heatmaps.loading = false;
  DOM_LIST.find('button').removeAttr('disabled');
  $.each(global_data.heatmaps, function(_, v) {
    v['cvis_path'] = cvis_state.cvis_path;
    v['multi_name'] = '';
  });
  // Sort heatmaps by name
  if (cvis_state.functions.has_heatmap()) {
    global_data.heatmaps.sort((a, b) => (a.name < b.name) ? -1 : 1);
  }
}

function cvis_dbg_heatmaps_merge(global_data, cvis_state, other) {
  $.each(other.data.heatmaps, function(_, v) {
    v['cvis_path'] = other.cvis_path;
    v['multi_name'] = other.name;
  });
  global_data.heatmaps = global_data.heatmaps.concat(other.data.heatmaps);
  // Sort heatmaps by name
  if (cvis_state.functions.has_heatmap()) {
    global_data.heatmaps.sort((a, b) => (a.name < b.name) ? -1 : 1);
  }
}

function cvis_dbg_heatmaps_update(global_data, cvis_state) {
  var DOM_LIST = $('#cdbHeatmapsView');
  var HEATMAPS_SEARCH = $('#cdbHeatmapsSearch');
  var TENSORS_TABS_NAV = $('#tensors-tabs-nav');
  var TENSORS_TABS = $('#tensors-tabs');

  if (!cvis_state.functions.has_heatmap()) {
    HEATMAPS_SEARCH.val('No heatmap available').attr('disabled', 'disabled');
    DOM_LIST.find('.cvis-hide-when-disabled').hide();
    return;
  }

  function is_a_match(info, search_terms) {
    for (var i = 0; i < search_terms.length; ++i) {
      if (info['name'].includes(search_terms[i]))
        continue;
      if (info['multi_name'].includes(search_terms[i]))
        continue;
      return false;
    }
    return true;
  }

  var current_heatmap = TENSORS_TABS_NAV.find('.active').parent().attr('data-tensor-id');
  if (current_heatmap) {
    cvis_state.functions.heatmap_update(
      cvis_state.heatmaps.loaded.get(current_heatmap)
    );
  }
  else {
    Module.remove_screen_overlay();
  }

  if (cvis_state.heatmaps.loading) {
    return;
  }

  var matching_entries = search_near_frames(
    global_data.heatmaps, HEATMAPS_SEARCH.val(), cvis_state.current_frame,
    is_a_match, 15,
  );
  DOM_LIST.find('.cvis-hide-when-disabled').show();
  DOM_LIST.find('.cvis-num-results').text(matching_entries.total_count);
  matching_entries = matching_entries.entries;
  DOM_LIST.find('.cvis-num-results-displayed').text(matching_entries.length);

  update_dom_list(DOM_LIST,
    matching_entries,
    'data-tensor-id',
    cvis_state.functions.create_heatmap_entry);
  update_dom_list(TENSORS_TABS,
    Array.from(cvis_state.heatmaps.loaded.values()),
    'data-tensor-id',
    function(e) {
      var copy = TENSORS_TABS.children().filter('.template').clone();
      copy.attr('id', e.id);
      copy.removeClass('collapse template');
      copy.addClass('delete-on-reset-cvis');
      copy.find('.tensor-append-summary').append($('#tensor-summary-items').clone().children());
      cvis_state.functions.parse_cvis_links(copy);
      return copy;
    },
  );
  function multi_prefix_maybe(multi_name) {
    if (multi_name == '') {
      return '';
    }
    return multi_name + '/';
  }
  update_dom_list(TENSORS_TABS_NAV,
    Array.from(cvis_state.heatmaps.loaded.values()),
    'data-tensor-id',
    function(e) {
      return $('<li class="nav-item delete-on-reset-cvis">').append(
        $('<a class="nav-link" data-toggle="tab" href="#tensors-details" role="tab" aria-controls="tensors-details" aria-selected="false">'
          ).attr('aria-controls', e.id
          ).attr('href', '#' + e.id
        ).text(multi_prefix_maybe(e.info.multi_name) + e.info.name)
      );
    },
  );
}
