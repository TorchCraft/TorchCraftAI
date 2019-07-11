/*
 * Copyright (c) 2019-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

"use strict";

history.replaceState({page: 'list_replays'}, "", "");
if ($('#searchReplaysPattern').val() == '') {
  // If the user didn't write anything during page loading
  $('#searchReplaysPattern').val(urlutils_dict_value('replays', 'pattern', ''));
}
jQuery(document).ready(function($) {
  refresh_replays_list();
  $('.cvis-reload-current-replay').click(function() {
    e.preventDefault();
    if (history.state.page == 'replay') {
      do_send_replay_when_ready(history.state.path, history.state.cvis_path);
    }
  });
});

function create_new_replay_element(entry, template) {
  var new_replay_html = $(template).clone();
  new_replay_html.removeClass('selectReplayMenuTemplate');
  new_replay_html.find('a.cvis-replay-link')
    .attr('href', '?rep=' + encodeURIComponent(entry['path']))
    .click((function(d) { return function(event) {
      event.preventDefault();
      do_send_replay_when_ready(d['abspath'], d['cvis']);
    }})(entry));
  new_replay_html.find('.cvis-replay-short').text(entry['name']);
  new_replay_html.find('.cvis-replay-long').text(entry['path']);
  new_replay_html.find('.cvis-replay-displayname').text(entry['display_name']);
  new_replay_html.find('.cvis-replay-time'
    ).attr('data-time', 1000*entry['last_change']
    ).attr('data-time-local-diff', (new Date()).getTime() - (new Date(1000*entry['local_time'])).getTime()
  ).addClass('dynamic-time-change');
  if (entry['has_cvis_data']) {
    new_replay_html.find('.cvis-replay-has-debug-infos').text(
      'With CVis debug infos');
    if (new_replay_html.find('button').hasClass('list-group-item'))
      new_replay_html.find('button').addClass('list-group-item-success');
  }
  else {
    new_replay_html.find('.cvis-replay-has-debug-infos').text(
      'No CVis debug infos found');
    if (new_replay_html.find('button').hasClass('list-group-item'))
      new_replay_html.find('button').addClass('list-group-item-light');
  }
  return new_replay_html.show();
}

function refresh_replays_list_html(data, html) {
  var replays_loaded = [];
  var template = html.find('.selectReplayMenuTemplate');
  if (!template)
    return;
  $.each(data, function(i, d) {
    // Order by date DESC
    d['id'] = (10000000000 - parseInt(d['last_change'])) + '_PATH=' + d['path'] + '_HASCVIS=' + d['has_cvis_data'];
  });
  $('.cvis-replays-total').text(data.length);
  data = data.slice(0, 100);
  $('.cvis-replays-display-count').text(data.length);

  update_dom_list(
    html,
    data,
    'data-replay',
    e => create_new_replay_element(e, template));
}

function timeago(timeStamp, now) {
  var secondsPast = (now.getTime() - timeStamp.getTime()) / 1000;
  if (secondsPast < 60){
    return parseInt(secondsPast) + ' seconds ago';
  }
  if (secondsPast < 3600){
    return parseInt(secondsPast/60) + ' minutes ago';
  }
  if (secondsPast <= 86400){
    return parseInt(secondsPast/3600) + ' hours and ' + parseInt(secondsPast/60 % 60) + ' minutes ago';
  }
  if (secondsPast > 86400){
    var day = timeStamp.getDate();
    var month = timeStamp.toDateString().match(/ [a-zA-Z]*/)[0].replace(" ","");
    var year = timeStamp.getFullYear() == now.getFullYear() ? "" :  " "+timeStamp.getFullYear();
    var hm = timeStamp.getHours() + ':' + timeStamp.getMinutes();
    return day + " " + month + year + ' ' + hm;
  }
}

function refresh_replays_list() {
  if (cvis_autostart_replay != '') {
    do_send_replay_when_ready(
      cvis_autostart_replay,
      cvis_autostart_replay + '.cvis');
    window.setTimeout(refresh_replays_list, 5000);
    cvis_autostart_replay = '';
    return;
  }

  var pattern = $('#searchReplaysPattern').val();
  $.get( "list/?pattern=" + encodeURIComponent(pattern), function(data) {
    $('.replay-remove-me-after-initial-load').remove();
    data['replays'].sort((a, b) => b['last_change'] - a['last_change']);

    // Load specified replay if exists (/?rep=...)
    if (cvis_autostart_replay != '' && openbw_ready_to_start) {
      do_send_replay_when_ready(
        cvis_autostart_replay,
        cvis_autostart_replay + '.cvis',
      );
      cvis_autostart_replay = '';
    }

    $('.selectReplayMenu').each(function() {
      refresh_replays_list_html(data['replays'], $(this));
    });
    $('.cvis-search-pattern').text(data['pattern']);
    urlutils_dict_setvalue('replays', 'pattern', data['pattern']);

    // Update timers
    $('.dynamic-time-change').each(function(){
      var now = new Date(new Date().getTime() - parseInt($(this).attr('data-time-local-diff')));
      var time = new Date(parseInt($(this).attr('data-time')));
      $(this).text(timeago(time, now));
    });
    window.setTimeout(refresh_replays_list, 1000);
  }, "json" ).fail(function(){
    window.setTimeout(refresh_replays_list, 10000);
  });
}

$(window).on('popstate', function(e){
  var new_state = e.originalEvent.state;
  console.log('onPopState: new state', new_state);
  if (new_state.page == 'replay') {
    cvis_autostart_replay = new_state.path;
  }
  else if (new_state.page == 'list_replays') {
    var url = new URL(window.location.href);
    url.searchParams.delete('rep');
    history.replaceState(history.state, "", "?" + url.searchParams.toString());
  }
});

var is_loading_replay = false;
function do_send_replay_when_ready(path, cvis_path, config) {
  if (config == undefined) {
    config = {};
  }
  if (is_loading_replay) {
    console.log('Already loading a replay - not loading', path);
    return;
  }
  is_loading_replay = true;
  var wait_openbw = wait_openbw_ready_to_start_replay();
  var wait_stop_all_cvis = cvis_stop_all();
  var load_rep_promise = load_replay_url('get/sc/?rep=' + encodeURIComponent(path));
  var load_cvis_promise = cvis_dbg_init(path, cvis_path);
  Promise.all([load_rep_promise, load_cvis_promise, wait_openbw, wait_stop_all_cvis]).then(function(values) {
    var rep_data = values[0];
    var cvis_data = values[1];

    console.log('Replaying: ', path);
    console.log('CVis path: ', cvis_path);
    $('.list-of-replays-main-page').hide();
    cvis_reset_dom_state();
    $('.cvis-dbg-show-when-running').hide();
    $('.cvis-dbg-show-when-loading').show();
    $('.cvis-dbg-show-when-loading-failed').hide();
    $('.cvis-loading-file-path').text(cvis_path);
    $('.cvis-dbg-show-when-matching-units').hide();

    start_replay(rep_data);
    cvis_init_with_data(path, cvis_data, config);

    if (history.state.page != 'replay' || history.state.path != path) {
      history.pushState({
        page: 'replay',
        path: path,
        cvis_path: cvis_path,
      }, "", "?rep=" + encodeURIComponent(path));
    }
    var fname = path.split('/');
    fname = fname[fname.length - 1];
    document.title = fname + " - CherryVis";
    is_loading_replay = false;
  }).catch(function(e){
    console.log('Unable to setup replay: ', e);
    is_loading_replay = false;
  });
}
