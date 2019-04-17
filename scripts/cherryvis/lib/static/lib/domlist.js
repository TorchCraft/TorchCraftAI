/*
 * Copyright (c) 2019-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

function update_dom_list(
  dom_list,
  display_entries,
  html_data_name,
  create_new_entry) {
  display_entries = display_entries.sort(function(a, b) {
    return a['id'] > b['id'] ? 1 : -1;
  });

  // Some logs are already in the DOM, let them here
  var matching_logs_ids = display_entries.map(d => d['id']);
  var dom_elements = [];
  dom_list.children().each(function() {dom_elements.push($(this));});
  var i_current_entry = 0;
  var i_dom_entry = 0;
  while (i_dom_entry < dom_elements.length || i_current_entry < display_entries.length) {
    if (i_dom_entry >= dom_elements.length) {
      dom_list.append(create_new_entry(display_entries[i_current_entry]
        ).attr(html_data_name, display_entries[i_current_entry]['id'])
      );
      ++i_current_entry;
    }
    else if (!dom_elements[i_dom_entry][0].hasAttribute(html_data_name)) {
      // Search input and other static things
      ++i_dom_entry;
    }
    else if (i_current_entry >= display_entries.length) {
      dom_elements[i_dom_entry].remove();
      ++i_dom_entry;
    }
    else {
      var dom_log_id = dom_elements[i_dom_entry].attr(html_data_name);
      var matching_log_id = display_entries[i_current_entry]['id'];
      if (dom_log_id == matching_log_id) {
        // Entry already in DOM
        ++i_dom_entry;
        ++i_current_entry;
      }
      else if (dom_log_id < matching_log_id) {
        dom_elements[i_dom_entry].remove();
        ++i_dom_entry;
      }
      else {
        create_new_entry(display_entries[i_current_entry]
          ).attr(html_data_name, display_entries[i_current_entry]['id']
          ).insertBefore(dom_elements[i_dom_entry]);
        ++i_current_entry;
      }
    }
  }
}
