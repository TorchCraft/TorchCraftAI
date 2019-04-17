/*
 * Copyright (c) 2019-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

function _update_url(searchParams) {
  history.replaceState(history.state, "", "?" + searchParams.toString());
}

// Values list
// We can't do a=v1&a=v2 because this doesn't go through fburl ...
function urlutils_list_values(param) {
  var url = new URL(window.location.href);
  var list = [];
  for (var i = 0; url.searchParams.get(param + '[' + i + ']'); ++i) {
    list.push(url.searchParams.get(param + '[' + i + ']'));
  }
  return list;
}

function urlutils_list_values_add(param, value_to_add) {
  var num_vals = urlutils_list_values(param).length;
  var url = new URL(window.location.href);
  if ($.inArray(value_to_add, urlutils_list_values(param)) >= 0) {
    return;
  }
  url.searchParams.append(param + '[' + num_vals + ']', value_to_add);
  _update_url(url.searchParams);
}

function urlutils_list_values_remove(param, value_to_remove) {
  var url = new URL(window.location.href);
  var all_values = urlutils_list_values(param);
  for (var i = 0; i < all_values.length; ++i) {
    url.searchParams.delete(param + '[' + i + ']');
  }
  all_values = all_values.filter(u => u != value_to_remove);
  $.each(all_values, function (idx, v) {
    url.searchParams.append(param + '[' + idx + ']', v);
  });
  _update_url(url.searchParams);
}

// KV storage
function urlutils_dict_value(param, key, default_val) {
  var url = new URL(window.location.href);
  var val = url.searchParams.get(param + '.' + key);
  if (val == null) {
    return default_val;
  }
  return val;
}

function urlutils_dict_setvalue(param, key, value) {
  var url = new URL(window.location.href);
  url.searchParams.set(param + '.' + key, value);
  _update_url(url.searchParams);
}

function urlutils_dict_unset(param, key) {
  var url = new URL(window.location.href);
  url.searchParams.delete(param + '.' + key);
  _update_url(url.searchParams);
}
