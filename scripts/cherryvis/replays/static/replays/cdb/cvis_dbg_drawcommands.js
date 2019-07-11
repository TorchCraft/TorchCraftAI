/*
 * Copyright (c) 2019-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

"use strict";

function cvis_dbg_drawcommands_update(global_data, cvis_state) {
  const COMMAND_DRAW_TEXT = 25;
  const COMMAND_DRAW_TEXT_SCREEN = 26;

  Module.clear_draw_commands();
  $('.cvis-cmd-draw').remove();
  var draw_commands = global_data['draw_commands'];
  var draw_commands_this_frame = [];

  // Find commands issued in nearest past frame
  if (draw_commands !== undefined && !$.isEmptyObject(draw_commands)) {
    var current_frame = cvis_state.current_frame;
    var nearest_frame = undefined;
    $.each(Object.keys(draw_commands), function(_, frame) {
      if (current_frame < frame) {
        return;
      }
      if (nearest_frame === undefined || Math.abs(current_frame - frame) < Math.abs(current_frame - nearest_frame)) {
        nearest_frame = frame;
        draw_commands_this_frame = draw_commands[nearest_frame];
      }
    });
  }

  // Draw commands
  var screen_info = Module.get_screen_info();
  $.each(draw_commands_this_frame, function(_, command) {
    // Convert CherryPi IDs -> OpenBW IDs in arguments
    if ('cherrypi_ids_args_indices' in command) {
      if (!command.cherrypi_ids_args_indices.every(
          i => cvis_state.id2bw[command.args[i]] !== undefined))
        return;
      $.each(command.cherrypi_ids_args_indices, function(_, idx) {
        command.args[idx] = cvis_state.id2bw[command.args[idx]];
      });
      command.cherrypi_ids_args_indices = []; // So we don't do it twice
    }
    if (!Module.add_draw_command(command)) {
      console.log('Unable to draw command: ', command);
    }
  });
}
