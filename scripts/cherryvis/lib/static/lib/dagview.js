/***************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2019 Swayvil <swayvil@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ***************************************************************/

/*
 * Dependencies:
 * - d3.js
 * - jquery.js
 * Initial code from:
 * https://github.com/swayvil/d3js-tree-boxes/blob/master/src/scripts/tree-boxes.js
 */
"use strict";

var dagview_root;
function dagview_render(element, jsonData, user_config)
{
  // CONFIG
  var config = {
    'horizontal': false,
    'arrow_size': 40,
    'use_flextree': true,
    'min_padding': 5,
    'node_render_fn': n => $('<p>NO RENDER FUNCTION GIVEN</p>'),
    'center_node_score_fn': n => -1,
  };
  Object.assign(config, user_config);
  config.reload_displayed_tree_fn = findDisplayTree;

  // DEFS
  const NODE_STATUS_COLLAPSED = 'NODE_STATUS_COLLAPSED';
  const NODE_STATUS_SHOW_PARTIAL = 'NODE_STATUS_SHOW_PARTIAL';
  const NODE_STATUS_SHOW_ALL_CHILDREN = 'NODE_STATUS_SHOW_ALL_CHILDREN';
  const NODE_STATUS_LEAF = 'NODE_STATUS_LEAF';

  function translate(x, y) {
    if (config.horizontal)
      return 'translate(' + y + ',' + x + ')';
    return 'translate(' + x + ',' + y + ')';
  }

  var treeHtml = d3.select(element);
  var display_root = null,  // Root of the displayed tree
    real_root = null;  // Root of the full tree

  var rectNode = { width : 80, height : 90}
  if (config.horizontal) {
    rectNode = { width : 120, height : 45};
  }
  var i = 0,
    duration = 750;

  var treeLayout, flexTreeLayout;
  var baseSvg,
    svgGroup,
    nodeGroup,
    linkGroup,
    defs;

  init(jsonData);
  findDisplayTree(config.center_node_score_fn);

  function init(jsonData) {
    treeLayout = d3.layout.tree().nodeSize([ rectNode.height, rectNode.width]);
    real_root = jsonData;
    if (config.use_flextree) {
      function nodeSize(n) {
        return [
          n.data.size[config.horizontal ? 1 : 0] + config.min_padding,
          n.data.size[config.horizontal ? 0 : 1] + config.arrow_size
        ];
      }
      flexTreeLayout = d3.flextree().nodeSize(nodeSize);
      function minSize(xy) {
        return display_root.nodes.reduce(function (min, node) {
          return Math.min(min, nodeSize(node)[xy]);
        }, Infinity);
      };
      real_root = flexTreeLayout.hierarchy(jsonData);
      flexTreeLayout.spacing(function (a, b) {
        return a.path(b).length * minSize(1) * 0.05;
      });
    }
    real_root.fixed = true;
    real_root.x0 = 0;
    real_root.y0 = 0;
    dagview_root = real_root;

    baseSvg = treeHtml.append('svg')
    .attr('class', 'flex-fill')
    .call(d3.behavior.zoom()
      .scaleExtent([0.1, 10])
      .on("zoom", on_zoom));


    svgGroup = baseSvg.append('g')
    .attr('class','drawarea')
    .attr("transform", 'translate(' + parseInt(element.clientWidth / 2) + ',0)')
    .append('g');

    nodeGroup = svgGroup.append('g')
          .attr('id', 'nodes');
    linkGroup = svgGroup.append('g')
          .attr('id', 'links');

    defs = baseSvg.append('defs');
    initArrowDef();
    initDropShadow();
  }

  function findDisplayTree(node_find_scoring)
  {
    // Fix root parent, in case we call this function again
    if (display_root && display_root._parent)
      display_root.parent = display_root._parent;

    var new_root = initAllNodes(real_root, node_find_scoring);
    console.log('Tree initialized with', i, 'nodes');
    if (new_root.score >= 0) {
      display_root = new_root.node;
      display_root._parent = display_root.parent;
      display_root.parent = null;
      console.log("Found display root node", display_root, ' score:', new_root.score);
    }
    else {
      display_root = real_root;
      console.log("No central node found");
    }
    expandNodesRecursively(display_root, 25);
    click_get_parents(null, display_root, 5, false);
    renderAllNodes(display_root, true);

    update(display_root);
  }

  function expandNodesRecursively(tree, count) {
    var done_count = 0;
    $.each(tree.descendants(), function(_, node) {
      if (done_count >= count)
        return;
      if (node.data.collapseStatus == NODE_STATUS_COLLAPSED) {
        showEverythingOrAtLeastMoreOf(node)
      }
      if (node.children)
        done_count += node.children.length;
    });
    if (done_count && done_count < count) {
      expandNodesRecursively(tree, count - done_count);
    }
  }

  function showEverythingOrAtLeastMoreOf(node) {
    const SHOW_MORE_COUNT = 15;
    node.children = node._children.slice(0, (node.children ? node.children.length : 0) + SHOW_MORE_COUNT);
    if (node.children && node.children.length == node._children.length) {
      node._children = null;
      node.data.collapseStatus = NODE_STATUS_SHOW_ALL_CHILDREN;
    }
    else {
      node.data.collapseStatus = NODE_STATUS_SHOW_PARTIAL;
    }
  }

  function on_zoom() {
    svgGroup.attr("transform", "translate(" + d3.event.translate + ")scale(" + d3.event.scale + ")");
  }

  function update(source)
  {
    // Compute the new tree layout
    var nodes = null, links = null;
    renderAllNodes(display_root, false);
    if (config.use_flextree) {
      flexTreeLayout(display_root);
      nodes = display_root.descendants();
      links = treeLayout.links(nodes);
      nodes.forEach(function(d) {
        d.x -= d.data.size[0] / 2;
      });
    }
    else {
      nodes = treeLayout.nodes(display_root).reverse();
      links = treeLayout.links(nodes);

      // Check if two nodes are in collision on the ordinates axe and move them
      breadthFirstTraversal(treeLayout.nodes(display_root), collision);

      // Normalize for fixed-depth
      nodes.forEach(function(d) {
        if (config.horizontal)
          d.y = d.depth * (rectNode.width + config.arrow_size);
        else
          d.y = d.depth * (rectNode.height + config.arrow_size);
      });
    }

    // 1) ******************* Update the nodes *******************
    update_nodes(source, nodes);
    update_links(source, links);

    // Stash the old positions for transition.
    nodes.forEach(function(d) {
      d.x0 = d.x;
      d.y0 = d.y;
    });
  }

  function update_nodes(source, nodes) {
    var node = nodeGroup.selectAll('g.node').data(nodes, function(d) {
      return d.data.id || (d.data.id = ++i);
    });

    var nodeEnter = node.enter().insert('g', 'g.node')
    .attr('class', 'node')
    .attr('transform', function(d) {
        return translate(source.x0, source.y0); });

    var fObj = nodeEnter.append('foreignObject')
    .attr('width', d => d.data.size[0])
    .attr('height', d => d.data.size[1])
    .append('xhtml')
    .each(function(d) {
      $(this).append(d.data.html);
    });

    // Transition nodes to their new position.
    var nodeUpdate = node.transition().duration(duration)
    .attr('transform', function(d) { return translate(d.x, d.y); });

    nodeUpdate.select('text').style('fill-opacity', 1);

    // Transition exiting nodes to the parent's new position
    var nodeExit = node.exit().transition().duration(duration)
      .attr('transform', function(d) { return translate(source.x, source.y);; })
      .remove();
    nodeExit.select('text').style('fill-opacity', 1e-6);
  }

  function update_links(source, links) {
    var link = linkGroup.selectAll('path').data(links, function(d) {
      return d.target.data.id;
    });

    d3.selection.prototype.moveToFront = function() {
        return this.each(function(){
            this.parentNode.appendChild(this);
          });
      };

    // Enter any new links at the parent's previous position.
      // Enter any new links at the parent's previous position.
      var linkenter = link.enter().insert('path', 'g')
      .attr('class', 'link')
      .attr('d', function(d) { return diagonal(d); })
      .attr('marker-end', 'url(#end-arrow)');

    // Transition links to their new position.
    var linkUpdate = link.transition().duration(duration)
                .attr('d', function(d) { return diagonal(d); });
    // Transition exiting nodes to the parent's new position.
    link.exit().transition()
    .remove();
  }

  // Toggle children on click.
  function click_toggle_expand(clicked_html, d) {
    if (!d.children && d._children) {
      // Completely collapsed
      console.assert(d.data.collapseStatus == NODE_STATUS_COLLAPSED);
      if (d.data.selectChildren !== undefined) {
        d.children = [d._children[d.data.selectChildren]];
        d.data.collapseStatus = NODE_STATUS_SHOW_PARTIAL;
      }
      else {
        expandNodesRecursively(d, 20);
      }
    }
    else if (d.children && d._children) {
      // Show only a few children
      console.assert(d.data.collapseStatus == NODE_STATUS_SHOW_PARTIAL);
      showEverythingOrAtLeastMoreOf(d);
    }
    else if (d.children && !d._children) {
      // Show all
      console.assert(d.data.collapseStatus == NODE_STATUS_SHOW_ALL_CHILDREN);
      d._children = d.children;
      d.children = null;
      d.data.collapseStatus = NODE_STATUS_COLLAPSED;
    }
    else {
      // This is a leaf
      return;
    }
    update(d);
    renderUpdateButton(d.data, $(clicked_html).parentsUntil('xhtml'));
  }

  function click_get_parents(clicked_html, old_root, count, update) {
    if (clicked_html != null) {
      $(clicked_html).remove();
    }
    if (count == 0 || old_root._parent == null) {
      if (update)
        update(display_root);
      return;
    }
    old_root.parent = old_root._parent;
    display_root = old_root.parent;
    display_root._parent = display_root.parent;
    display_root.parent = null;

    // Only display 'old_root' as child of 'display_root', otherwise we get too many nodes
    display_root.children = display_root._children;
    if (display_root.children.length == 1) {
      display_root.data.collapseStatus = NODE_STATUS_SHOW_ALL_CHILDREN;
    }
    else {
      display_root._children = display_root.children;
      display_root.children = [old_root];
      display_root.data.selectChildren = display_root._children.indexOf(old_root);
      display_root.data.collapseStatus = NODE_STATUS_SHOW_PARTIAL;
    }
    click_get_parents(null, display_root, count - 1, update);
  }

  // Breadth-first traversal of the tree
  // func function is processed on every node of a same level
  // return the max level
  function breadthFirstTraversal(tree, func)
  {
    var max = 0;
    if (tree && tree.length > 0)
    {
      var currentDepth = tree[0].depth;
      var fifo = [];
      var currentLevel = [];

      fifo.push(tree[0]);
      while (fifo.length > 0) {
      var node = fifo.shift();
      if (node.depth > currentDepth) {
        func(currentLevel);
        currentDepth++;
        max = Math.max(max, currentLevel.length);
        currentLevel = [];
      }
      currentLevel.push(node);
      if (node.children) {
        for (var j = 0; j < node.children.length; j++) {
          fifo.push(node.children[j]);
        }
      }
      }
      func(currentLevel);
      return Math.max(max, currentLevel.length);
    }
    return 0;
  }

  // x = ordoninates and y = abscissas
  function collision(siblings) {
    if (siblings) {
      for (var i = 0; i < siblings.length - 1; i++)
      {
        var nodeSize = config.horizontal ? rectNode.height : rectNode.width;
        if (siblings[i + 1].x - (siblings[i].x + nodeSize) < config.min_padding)
          siblings[i + 1].x = siblings[i].x + nodeSize + config.min_padding;
      }
    }
  }

  function diagonal(d) {
    var sourceDX = rectNode.width;
    var sourceDY = rectNode.height;
    var targetDX = rectNode.width;
    var targetDY = rectNode.height;
    if (config.use_flextree) {
      sourceDX = d.source.data.size[0];
      sourceDY = d.source.data.size[1];
      targetDX = d.target.data.size[0];
      targetDY = d.target.data.size[1];
    }
    if (config.horizontal) {
      // Swap
      sourceDX = [sourceDY, sourceDY = sourceDX][0];
      targetDX = [targetDY, targetDY = targetDX][0];
    }
    var p0 = {
      x : d.source.x + sourceDX / 2,
      y : (d.source.y + sourceDY)
    }, p3 = {
      x : d.target.x + targetDX / 2,
      y : d.target.y - 12
    }, m = (p0.y + p3.y) / 2, p = [ p0, {
      x : p0.x,
      y : m
    }, {
      x : p3.x,
      y : m
    }, p3 ];
    p = p.map(function(d) {
      return config.horizontal ? [ d.y, d.x ] : [ d.x, d.y ];
    });
    return 'M' + p[0] + 'C' + p[1] + ' ' + p[2] + ' ' + p[3];
  }

  function renderUpdateButton(node, html) {
    var btn = html.find('.dagview-expand-toggle');
    console.assert(btn[0]);
    btn.removeClass('btn-outline-primary btn-outline-warning');
    switch (node.collapseStatus) {
      case NODE_STATUS_COLLAPSED:
        btn.addClass('btn-outline-primary');
        btn.text('show hidden');
        btn.show();
        break;
      case NODE_STATUS_SHOW_PARTIAL:
        btn.addClass('btn-outline-primary');
        btn.text('show more');
        btn.show();
        break;
      case NODE_STATUS_SHOW_ALL_CHILDREN:
        btn.addClass('btn-outline-warning');
        btn.text('collapse');
        btn.show();
        break;
      case NODE_STATUS_LEAF:
        btn.hide();
        break;
    }
  }

  function initAllNodes(node, node_find_scoring) {
    var data = node.data;
    var bestNode = node;
    var bestNodeScore = node_find_scoring(node.data);
    if (node._children) {  // reset case
      node.children = node._children;
    }
    $.each(node.children, function(_, child) {
      var childBest = initAllNodes(child, node_find_scoring);
      if (childBest.score > bestNodeScore) {
        bestNodeScore = childBest.score;
        bestNode = childBest.node;
      }
    });

    if (data.id)
      i = Math.max(i, data.id);
    else
      data.id = ++i;

    // Initially we collapse everything
    data.collapseStatus = node.children ? NODE_STATUS_COLLAPSED : NODE_STATUS_LEAF;
    node._children = node.children;
    node.children = null;
    return {
      'score': bestNodeScore,
      'node': bestNode,
    };
  }

  function collapseAllNodes(node) {
    node._children = node.children;
    node.children = null;
    node.data.collapseStatus = NODE_STATUS_COLLAPSED;
    $.each(node._children, (_, n) => collapseAllNodes(n));
  }

  function renderAllNodes(node, force_html_render) {
    var data = node.data;
    if (force_html_render || !data.html) {
      data.html = $('<div class="node-rect">');
      if (!node.parent && node._parent) {
        data.html.append($('<button class="btn btn-outline-primary btn-light btn-lg btn-block btn-sm"/>')
        .click(e => click_get_parents(e.target, node, 5, true))
        .text('Load parents'));
      }
      data.html.append(config.node_render_fn(data, function() {
        data.size = [data.html.width(), data.html.height()];
        update(display_root);
      }));
      if (data.collapseStatus != NODE_STATUS_LEAF) {
        data.html.append($('<button class="dagview-expand-toggle btn btn-light btn-lg btn-block btn-sm"/>').click(e => click_toggle_expand(e.target, node)));
        renderUpdateButton(data, data.html);
      }
    }

    // Heuristics to determine dimensions
    var copy = data.html.clone();
    copy.hide();
    $('body').append(copy);
    data.size = [copy.width(), copy.height()];
    copy.remove();

    $.each(node.children, (_, n) => renderAllNodes(n));
  }

  function initDropShadow() {
    var filter = defs.append("filter")
        .attr("id", "drop-shadow")
        .attr("color-interpolation-filters", "sRGB");

    filter.append("feOffset")
    .attr("result", "offOut")
    .attr("in", "SourceGraphic")
      .attr("dx", 0)
      .attr("dy", 0);

    filter.append("feGaussianBlur")
        .attr("stdDeviation", 2);

    filter.append("feOffset")
        .attr("dx", 2)
        .attr("dy", 2)
        .attr("result", "shadow");

    filter.append("feComposite")
      .attr("in", 'offOut')
      .attr("in2", 'shadow')
      .attr("operator", "over");
  }

  function initArrowDef() {
    // Build the arrows definitions
    // End arrow
    defs.append('marker')
    .attr('id', 'end-arrow')
    .attr('viewBox', '0 -5 10 10')
    .attr('refX', 0)
    .attr('refY', 0)
    .attr('markerWidth', 6)
    .attr('markerHeight', 6)
    .attr('orient', 'auto')
    .attr('class', 'arrow')
    .append('path')
    .attr('d', 'M0,-5L10,0L0,5');

    // End arrow selected
    defs.append('marker')
    .attr('id', 'end-arrow-selected')
    .attr('viewBox', '0 -5 10 10')
    .attr('refX', 0)
    .attr('refY', 0)
    .attr('markerWidth', 6)
    .attr('markerHeight', 6)
    .attr('orient', 'auto')
    .attr('class', 'arrowselected')
    .append('path')
    .attr('d', 'M0,-5L10,0L0,5');

    // Start arrow
    defs.append('marker')
    .attr('id', 'start-arrow')
    .attr('viewBox', '0 -5 10 10')
    .attr('refX', 0)
    .attr('refY', 0)
    .attr('markerWidth', 6)
    .attr('markerHeight', 6)
    .attr('orient', 'auto')
    .attr('class', 'arrow')
    .append('path')
    .attr('d', 'M10,-5L0,0L10,5');

    // Start arrow selected
    defs.append('marker')
    .attr('id', 'start-arrow-selected')
    .attr('viewBox', '0 -5 10 10')
    .attr('refX', 0)
    .attr('refY', 0)
    .attr('markerWidth', 6)
    .attr('markerHeight', 6)
    .attr('orient', 'auto')
    .attr('class', 'arrowselected')
    .append('path')
    .attr('d', 'M10,-5L0,0L10,5');
  }
  return config;
}
