/*
 * Copyright (c) 2019-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

"use strict";

function cvis_dbg_trees_init(global_data, cvis_state) {
  var DOM_LIST = $('#cdbTreesView');
  var GRAPH_VIEW = $('#cdbLoadedTreePlaceholder');

  function destroy_graph() {
    var div = GRAPH_VIEW.find('.cvis-draw-tree');
    div.empty();
    cvis_state.tree.current = null;
    cvis_state.tree.current_info = null;
    cvis_state.tree.loading = false;
    GRAPH_VIEW.hide();
    DOM_LIST.show();
    DOM_LIST.find('button').removeAttr('disabled');
  }

  function render_distr(distr, frame, on_html_change) {
    function render_item(tr, item) {
      var th = $('<th scope="row">').text(item.type_prefix + item.id);
      var td = $('<td>');
      var prob = $('<td>').text(('' + item.proba).slice(0, 4));
      if (item.type_prefix == 'i') {  // Unit
        var unit = cvis_state.functions.get_unit_at_frame(item.id, frame);
        var td = $('<td>');
        if (unit && unit.type) {
          td.append($('<img style="height: 40px; width: 40px">').attr('src',
            "http://www.openbw.com/bw/production_icons/icon " + ("000" + unit.type).slice(-3) + ".bmp"));
        }
      }
      tr.append(th, td, prob);
    }

    var table = $('<table class="table table-borderless table-sm table-dark flex-fill">');
    table.append('<thead>'
      + '<tr>'
      + '  <th scope="col">Item</th>'
      + '  <th scope="col"></th>'
      + '  <th scope="col">Proba</th>'
      + '</tr>'
    + '</thead>');
    var tbody = $('<tbody>');
    $.each(distr, function(i, item) {
      var tr = $('<tr>');
      render_item(tr, item);
      if (i >= 10) {
        tr.addClass('see-more-rows');
        tr.hide();
      }
      tbody.append(tr);
    });
    table.append(tbody);

    var div = $('<div class="d-flex flex-column">').append(table);
    if (distr.length > 10) {
      var btn_show_more = $('<button type="button" class="btn btn-primary flex-fill">Show all ' + distr.length + ' elements</button>');
      var all_shown = false;
      btn_show_more.click(function() {
        $(this).parent().find('.see-more-rows').toggle();
        all_shown = !all_shown;
        $(this).text(all_shown ? "Show less" : 'Show all ' + distr.length + ' elements');
        on_html_change();
      });
      div.append(btn_show_more);
    }
    return div;
  }

  function render_node(n, on_html_change) {
    var div = $('<div class="cvis-graph-node">');
    div.attr('style', 'max-width: 700px');

    // Header: ID - MODULE - FRAME
    var header = $('<div class="d-flex">');
    var h_id = $('<div class="text-truncate justify-content-start mr-3"></div>');
    var h_module = $('<div class="text-truncate justify-content-center mx-3"></div>');
    var h_frame = $('<div class="text-truncate justify-content-end ml-3"></div>');
    if (n.id !== undefined) {
      var id_text = ((n.type_prefix !== undefined) ? n.type_prefix : '#') + n.id;
      h_id.text(id_text);
    }
    if (n.module !== undefined) {
      h_module.text(n.module);
    }
    if (n.frame !== undefined) {
      h_frame.text('f' + n.frame);
    }
    header.append(h_id, h_module, h_frame);
    div.append(header);

    if (n.description)
      div.append($('<p class="d-block text-truncate">').text(n.description));
    if (n.distribution)
      div.append($('<div class="d-flex">').append(render_distr(n.distribution, n.frame, on_html_change).addClass('flex-fill')));
    return div;
  }

  function score_node(search_pattern, n) {
    if (n.id && search_pattern == (n.type_prefix + n.id)) {
      return 10000;
    }
    if (n.frame && search_pattern == ('f' + n.frame)) {
      return 10000 - 1;
    }
    var score = -1;
    if (n.module && search_pattern == n.module) {
      score += 500;
    }
    if (n.frame !== undefined)
      score += 1000 - Math.abs(cvis_state.current_frame - n.frame);
    return score;
  }

  function trees_enabled() {
    return global_data['trees'] !== undefined && global_data['trees'].length;
  }

  function find_tree(graph_name) {
    if (!trees_enabled())
      return null;
    var candidates = global_data['trees'].filter(g => g.name == graph_name);
    return candidates.length ? candidates[0] : null;
  }

  function load_tree_callback(graph_data, graph_info, init_search_pattern) {
    var div = GRAPH_VIEW.find('.cvis-draw-tree');
    GRAPH_VIEW.show();
    DOM_LIST.hide();
    cvis_state.tree.current = dagview_render(div[0], graph_data, {
      'horizontal': false,
      'use_flextree': true,
      'node_render_fn': render_node,
      'center_node_score_fn': n => score_node(init_search_pattern, n),
    });
    cvis_state.tree.current_info = graph_info;
  }

  function search_current_graph(pattern) {
    cvis_state.tree.current.reload_displayed_tree_fn(
      n => score_node(pattern, n));
  }

  function load_tree(graph_info, init_search_pattern) {
    if (cvis_state.tree.current_info !== null &&
        cvis_state.tree.current_info.filename == graph_info.filename) {
      search_current_graph(init_search_pattern);
      return;
    }
    if (cvis_state.tree.current_info !== null) {
      destroy_graph();
    }
    cvis_state.tree.loading = true;
    DOM_LIST.find('button').attr('disabled', 'disabled');
    cvis_state.functions.load_cvis_json_file(
      graph_info.cvis_path,
      graph_info.filename,
      g => load_tree_callback(g, graph_info, init_search_pattern),
      function() {
          console.log('Loading of graph failed', graph_info);
          destroy_graph();
      }
    );
  }

  cvis_state.functions.trees_enabled = trees_enabled;
  cvis_state.functions.find_tree = find_tree;
  cvis_state.functions.load_tree = load_tree;

  cvis_state.tree = {
    'loading': false,
    'current': null,
    'current_info': null,
  };
  destroy_graph();
  $('#cdbLoadedTreeClose').off();
  $('#cdbLoadedTreeClose').click(destroy_graph);
  $('#cdbLoadedTreeSearch').off();
  $('#cdbLoadedTreeSearch').keyup(function(e){
    if (e.keyCode == 13) // ENTER
      search_current_graph($(this).val());
  });
  $.each(global_data.trees, function(_, v) {
    v['cvis_path'] = cvis_state.cvis_path;
    v['multi_name'] = '';
  })
  if (trees_enabled()) {
    $('#cdbTreesSearch').val('').removeAttr('disabled');
    DOM_LIST.find('.cvis-hide-when-disabled').show();
  }
  else {
    $('#cdbTreesSearch').val('No tree available').attr('disabled', 'disabled');
    DOM_LIST.find('.cvis-hide-when-disabled').hide();
  }
}

function cvis_dbg_trees_merge(global_data, cvis_state, other) {
  $.each(other.data.trees, function(_, v) {
    v['cvis_path'] = other.cvis_path;
    v['multi_name'] = other.name;
  });
  global_data.trees = global_data.trees.concat(other.data.trees);
}

function cvis_dbg_trees_update(global_data, cvis_state) {
  if (!cvis_state.functions.trees_enabled()) {
    return;
  }
  var DOM_LIST = $('#cdbTreesView');

  function create_entry(info) {
    var html = DOM_LIST.find('.template').clone().removeClass('collapse template').addClass('delete-on-reset-cvis');
    html.attr('data-frame-id', info['frame']);
    html.find('.cvis-link-frame').attr('data-frame-id', info['frame']);
    html.find('.cvis-graphs-multi').text(info.multi_name);
    html.find('.cvis-graphs-name').text(info['name']);
    html.find('.load-tree-button').click(function() {
      cvis_state.functions.load_tree(info, 'f' + cvis_state.current_frame);
    });
    cvis_state.functions.parse_cvis_links(html);
    return html;
  }

  function is_a_match(info, search_terms) {
    for (var i = 0; i < search_terms.length; ++i) {
      if (info['name'].includes(search_terms[i]))
        continue;
      if (info['multi_name'].includes(search_terms[i]))
        continue;
      if (info['frame'] == search_terms[i])
        continue;
      return false;
    }
    return true;
  }

  if (cvis_state.tree.loading) {
    return;
  }

  var matching_entries = search_near_frames(
    global_data.trees, $('#cdbTreesSearch').val(), cvis_state.current_frame,
    is_a_match, 15,
  );
  DOM_LIST.find('.cvis-num-results').text(matching_entries.total_count);
  matching_entries = matching_entries.entries;
  DOM_LIST.find('.cvis-num-results-displayed').text(matching_entries.length);

  update_dom_list(DOM_LIST,
    matching_entries,
    'data-graph-id',
    create_entry);
}
