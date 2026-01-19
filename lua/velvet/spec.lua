--- This spec file is used to auto-generate C / Lua bindings, and the default config.
--- Reading and assigning options from the options table triggers a lookup in a meta table
--- which reads or updates a backing field in C.
--- A C header file is generated with getters/setters for all options, and functions corresponding to each api function.
--- An implementation file is generated with lua bindings, which will read the parameters specified in this document,
--- and pass them as normal C parameters according to the generated header file.
--- The C header is manually implemented elsewhere.
return {
  --- options {{{1
  options = {
    {
      name = "focus_follows_mouse",
      type = "bool",
      default = true,
      doc = "Automatically focus a window when the mouse moves over it."
    },
    {
      name = "key_repeat_timeout",
      type = "int",
      default = 500,
      doc = "Time in milliseconds before pending keybinds time out",
    },
    {
      name = "display_damage",
      type = "bool",
      default = false,
      doc = "Enable damage tracking when the screen is updated. (debugging feature)",
    },
    {
      name = 'color_palette',
      type = 'color_palette',
      doc = 'The 16 numbered terminal colors.',
      default = {
        black          = "#45475a",
        red            = "#f38ba8",
        green          = "#a6e3a1",
        yellow         = "#f9e2af",
        blue           = "#89b4fa",
        magenta        = "#f5c2e7",
        cyan           = "#94e2d5",
        white          = "#bac2de",
        bright_black   = "#585b70",
        bright_red     = "#f38ba8",
        bright_green   = "#a6e3a1",
        bright_yellow  = "#f9e2af",
        bright_blue    = "#89b4fa",
        bright_magenta = "#f5c2e7",
        bright_cyan    = "#94e2d5",
        bright_white   = "#a6adc8",
      },
    },
  },

  enums = {
    {
      name = "transparency_mode",
      values = {
        { name = "none",  value = 1 },
        { name = "clear", value = 2 },
        { name = "all",   value = 3 },
      }
    },
    {
      name = "key_event_type",
      values = {
        { name = "press",   value = 1 },
        { name = "repeat",  value = 2 },
        { name = "release", value = 3 },
      },
    },
    {
      name = "key_modifiers",
      values = {
        { name = "shift",     value = 1 << 0 },
        { name = "alt",       value = 1 << 1 },
        { name = "ctrl",      value = 1 << 2 },
        { name = "super",     value = 1 << 3 },
        { name = "hyper",     value = 1 << 4 },
        { name = "meta",      value = 1 << 5 },
        { name = "caps_lock", value = 1 << 6 },
        { name = "num_lock",  value = 1 << 7 },
      }
    },
    {
      name = "scroll_direction",
      values = {
        { name = "up",   value = 0 },
        { name = "down", value = 1 },
        { name = "left",  value = 2 },
        { name = "right",   value = 3 },
      },
    },
    {
      name = "mouse_button",
      values = {
        { name = "left",   value = 0 },
        { name = "middle", value = 1 },
        { name = "right",  value = 2 },
        { name = "none",   value = 3 },
      },
    },
    {
      name = "mouse_event_type",
      values = {
        { name = "mouse_down", value = 1 },
        { name = "mouse_up", value = 2 },
      },
    },
  },

  --- types {{{1
  types = {
    {
      name = "rgb_color",
      fields = {
        { name = "red",   type = "int" },
        { name = "green", type = "int" },
        { name = "blue",  type = "int" },
      },
    },
    {
      name = "color_palette",
      fields = {
        -- 30-37 / 40-47
        { name = "black",          type = "rgb_color", doc = "Palette color 0" },
        { name = "red",            type = "rgb_color", doc = "Palette color 1" },
        { name = "green",          type = "rgb_color", doc = "Palette color 2" },
        { name = "yellow",         type = "rgb_color", doc = "Palette color 3" },
        { name = "blue",           type = "rgb_color", doc = "Palette color 4" },
        { name = "magenta",        type = "rgb_color", doc = "Palette color 5" },
        { name = "cyan",           type = "rgb_color", doc = "Palette color 6" },
        { name = "white",          type = "rgb_color", doc = "Palette color 7" },
        --  90-97 / 100-107
        { name = "bright_black",   type = "rgb_color", doc = "Palette color 8" },
        { name = "bright_red",     type = "rgb_color", doc = "Palette color 9" },
        { name = "bright_green",   type = "rgb_color", doc = "Palette color 10" },
        { name = "bright_yellow",  type = "rgb_color", doc = "Palette color 11" },
        { name = "bright_blue",    type = "rgb_color", doc = "Palette color 12" },
        { name = "bright_magenta", type = "rgb_color", doc = "Palette color 13" },
        { name = "bright_cyan",    type = "rgb_color", doc = "Palette color 14" },
        { name = "bright_white",   type = "rgb_color", doc = "Palette color 15" },
      },
    },
    {
      name = "window.geometry",
      fields = {
        { name = "left",   type = "int", doc = "The leftmost cell of the window." },
        { name = "top",    type = "int", doc = "The topmost cell of the window." },
        { name = "width",  type = "int", doc = "The width of the window" },
        { name = "height", type = "int", doc = "The height of the window" }
      }
    },
    {
      name = "screen.geometry",
      fields = {
        { name = "width",  type = "int", doc = "The width of the screen" },
        { name = "height", type = "int", doc = "The height of the screen" },
      }
    },
    {
      name = "window.created.event_args",
      fields = { { name = "id", type = "int", doc = "The id of the newly created window." } }
    },
    {
      name = "window.closed.event_args",
      fields = { { name = "id", type = "int", doc = "The id of the closed window." } }
    },
    {
      name = "window.moved.event_args",
      fields = {
        { name = "id",       type = "int",             doc = "The id of the resized window." },
        { name = "old_size", type = "window.geometry", doc = "The old geometry of |id|." },
        { name = "new_size", type = "window.geometry", doc = "The new geometry of |id|." },
      }
    },
    {
      name = "window.resized.event_args",
      fields = {
        { name = "id",       type = "int",             doc = "The id of the resized window." },
        { name = "old_size", type = "window.geometry", doc = "The old geometry of |id|." },
        { name = "new_size", type = "window.geometry", doc = "The new geometry of |id|." },
      }
    },
    {
      name = "window.focus_changed.event_args",
      fields = {
        { name = "old_focus", type = "int", doc = "The previous focused window." },
        { name = "new_focus", type = "int", doc = "The new focused window." },
      }
    },
    {
      name = "screen.resized.event_args",
      fields = {
        { name = "old_size", type = "screen.geometry", doc = "The old screen size" },
        { name = "new_size", type = "screen.geometry", doc = "The new screen size" },
      }
    },
    {
      name = "window.key_event",
      fields = {
        { name = "codepoint",           type = "int",            doc = "Unicode codepoint of the key generating the event." },
        { name = "alternate_codepoint", type = "int",            doc = "Shifted unicode codepoint of the key generating the event. This is only set if the key was shifted." },
        { name = "name",                type = "string",         doc = "Key name, such as 'F1'" },
        { name = "event_type",          type = "key_event_type", doc = "Event type, such as key press, repeat, and release." },
        { name = "modifiers",           type = "key_modifiers",  doc = "Key modifiers such as super, shift, ctrl, alt" },
      },
    },
    {
      name = "window.on_key.event_args",
      fields = {
        { name = "win_id", type = "int",              doc = "The id of the window the keys were sent to." },
        { name = "key",    type = "window.key_event", doc = "The key which generated the event." },
      },
    },
    {
      name = "coordinate",
      doc = "1-indexed screen coordinate",
      fields = {
        { name = "row", type = "int", doc = "row" },
        { name = "col", type = "int", doc = "column" },
      },
    },
    {
      name = "mouse.move.event_args",
      fields = {
        { name = "win_id",       type = "int",           doc = "The id of the topmost visible window at the coordinates." },
        { name = "pos",          type = "coordinate",    doc = "1-indexed screen coordinate of the mouse when the event was raised." },
        { name = "mouse_button", type = "mouse_button",  doc = "Mouse button state when the event was raised." },
        { name = "modifiers",    type = "key_modifiers", doc = "The keyboard modifiers which were held when the event was raised." },
      },
    },
    {
      name = "mouse.click.event_args",
      fields = {
        { name = "win_id",       type = "int",              doc = "The id of the topmost visible window at the coordinates." },
        { name = "pos",          type = "coordinate",       doc = "1-indexed screen coordinate of the mouse when the event was raised." },
        { name = "mouse_button", type = "mouse_button",     doc = "The mouse button which was clicked." },
        { name = "event_type",   type = "mouse_event_type", doc = "Flag indicating if the button was pressed or released." },
        { name = "modifiers",    type = "key_modifiers",    doc = "The keyboard modifiers which were held when the event was raised." },
      },
    },
    {
      name = "mouse.scroll.event_args",
      fields = {
        { name = "win_id",       type = "int",              doc = "The id of the topmost visible window at the coordinates." },
        { name = "pos",          type = "coordinate",       doc = "1-indexed screen coordinate of the mouse when the event was raised." },
        { name = "direction",    type = "scroll_direction", doc = "The scroll direction which raised the event." },
        { name = "modifiers",    type = "key_modifiers",    doc = "The keyboard modifiers which were held when the event was raised." },
      },
    },
  },

  events = {
    { name = "window_created",       doc = "Raised after a new window is created.",      args = "window.created.event_args" },
    { name = "window_closed",        doc = "Raised after a window is closed.",           args = "window.closed.event_args" },
    { name = "window_moved",         doc = "Raised after a window is moved.",            args = "window.moved.event_args" },
    { name = "window_resized",       doc = "Raised after a window is resized.",          args = "window.resized.event_args" },
    { name = "window_on_key",        doc = "Raised when a key is sent to a lua window.", args = "window.on_key.event_args" },
    { name = "window_focus_changed", doc = "Raised after focus changes.",                args = "window.focus_changed.event_args" },
    { name = "screen_resized",       doc = "Raised after the screen is resized.",        args = "screen.resized.event_args" },
    { name = "mouse_move",           doc = "Raised when the mouse moves.",               args = "mouse.move.event_args" },
    { name = "mouse_click",          doc = "Raised when the mouse is clicked.",          args = "mouse.click.event_args" },
    { name = "mouse_scroll",         doc = "Raised when the mouse scrolls.",             args = "mouse.scroll.event_args" },
  },

  -- types we know that we cannot automatically marshal. Such functions must be implemented by hand.
  manual_types = { ["int[]"] = true },

  --- api {{{1
  api = {
    --- keymap {{{2
    {
      name = "keymap_remap_modifier",
      doc =
      "Remap the modifier |from| to the modifier |to|. This is a one-way mapping. To swap two modifiers, you must also apply the inverse mapping. Shift is not supported.",
      params = {
        { name = "from", type = "key_modifiers", doc = "The modifier to remap." },
        { name = "to",   type = "key_modifiers", doc = "The new modifier emitted when the remapped modifier is used." },
      },
    },
    {
      name = "keymap_del",
      doc = "Delete the mapping associated with |keys|",
      params = {
        { name = "keys", type = "string", doc = "The mapping to remove" },
      },
    },
    {
      name = "keymap_set",
      doc = "Creates a mapping of |keys| to |function|",
      params = {
        { name = "keys", type = "string",   doc = "Left hand side of the mapping" },
        { name = "func", type = "function", doc = "Right hand side of the mapping" },
      },
      optional = {
        {
          name = "repeatable",
          type = "bool",
          doc = "If set, this mapping may be repeated within the interval defined by |velvet.options.key_repeat_timeout|"
        },
      },
    },
    --- screen {{{2
    {
      name = "get_screen_geometry",
      doc = "Get the size of the screen.",
      returns = { name = "geometry", type = "screen.geometry", doc = "The geometry of the screen window." },
    },
    --- timing {{{2
    {
      name = "schedule_after",
      params = {
        { name = "delay", type = "int",      doc = "delay in miliseconds" },
        { name = "func",  type = "function", doc = "function to run" },
      },
      doc = "Schedule |function| to run after at least |delay| ms",
    },
    {
      name = "get_current_tick",
      doc = "Get the number of milliseconds elapsed since startup",
      returns = { type = "int", doc = "milliseconds elapsed since startup" }
    },
    --- system {{{2
    {
      name = "get_sessions",
      doc = "Get the IDs of all sessions.",
      returns = { type = "int[]", doc = "List of session IDs" }
    },
    {
      name = "set_active_session",
      doc = "Get the id of the active session",
      params = { { name = "session_id", type = "int", doc = "Session id" } },
    },
    {
      name = "get_active_session",
      doc = "Get the id of the active session",
      returns = { name = "session_id", type = "int", doc = "Session id" },
    },
    {
      name = "session_detach",
      doc = "Detach |session| session from the server.",
      params = { { name = "session_id", type = "int", doc = "Session id" } },
    },
    {
      name = "quit",
      doc = "Quit velvet with no warning"
    },
    --- Windows {{{2
    {
      name = "get_windows",
      doc = "Get the IDs of all windows.",
      returns = { type = "int[]", doc = "List of window IDs" }
    },
    {
      -- TODO: deprecate this
      name = "swap_windows",
      params = {
        { name = "first",  type = "int", doc = "Window id" },
        { name = "second", type = "int", doc = "Window id" },
      },
      doc = "Swap two windows. This affects the layout of tiled windows.",
    },
    {
      name = "window_set_z_index",
      doc = "Set the z index of |win| to |z|",
      params = {
        { name = "win", type = "int", doc = "Window id" },
        { name = "z",   type = "int", doc = "New z index of |win|" }
      },
    },
    {
      name = "window_get_z_index",
      doc = "Get the z index of |win|",
      params = { { name = "win", type = "int", doc = "Window id" } },
      returns = { type = "int", doc = "The z index of |win|" }
    },
    {
      name = "window_set_hidden",
      doc = "Set window hidden flag. A hidden window will not be rendered.",
      params = {
        { name = "win_id", type = "int",  doc = "Window id" },
        { name = "hidden", type = "bool", doc = "New hidden state of |win_id|" }
      },
    },
    {
      name = "window_get_hidden",
      doc = "Get window hidden flag. A hidden window will not be rendered.",
      params = { { name = "win_id", type = "int", doc = "Window id" } },
      returns = { type = "bool", doc = "Bool indicating if the window is hidden." }
    },
    {
      name = "window_get_opacity",
      doc = "Get window opacity",
      params = { { name = "win", type = "int", doc = "Window id" } },
      returns = { type = "float", doc = "The new window opacity." },
    },
    {
      name = "window_set_opacity",
      doc = "Set window opacity. The effect of this depends on the value of |window_get_transparency_mode|",
      params = {
        { name = "win",     type = "int",   doc = "Window id" },
        { name = "opacity", type = "float", doc = "The new window opacity." },
      },
    },
    {
      name = "window_get_transparency_mode",
      doc = "Get window transparency mode.",
      params = { { name = "win", type = "int", doc = "Window id" } },
      returns = { type = "transparency_mode", doc = "Set transparency mode." },
    },
    {
      name = "window_set_transparency_mode",
      doc = "Set window transparency mode.",
      params =
      {
        { name = "win",  type = "int",               doc = "Window id" },
        { name = "mode", type = "transparency_mode", doc = "Set transparency mode." },
      },
    },
    {
      name = "window_get_dim_factor",
      doc = "Get the current dim factor for |win|",
      params = {
        { name = "win", type = "int", doc = "Window id" },
      },
      returns = { name = "factor", type = "float", doc = "Dim factor between 0.0 and 1.0" },

    },
    {
      name = "window_set_dim_factor",
      doc = "Dim the window content of |win| by a constant factor. A larger value means more dimming (0.0 - 1.0)",
      params = {
        { name = "win",    type = "int",    doc = "Window id" },
        { name = "factor", type = "float", doc = "Dim factor between 0.0 and 1.0" },
      },
    },
    {
      name = "get_focused_window",
      doc = "Get the id of the currently focused window.",
      returns = { type = "int", doc = "TThe id of the currently focused window." }
    },
    {
      name = "set_focused_window",
      params = { { name = "win_id", type = "int", doc = "Window id" } },
      doc = "Focus the window with id |win_id|",
    },
    {
      name = "window_get_geometry",
      doc = "Get the geometry of the specified window.",
      params = { { name = "win_id", type = "int", doc = "Window id" } },
      returns = { name = "geometry", type = "window.geometry", "Window geometry" },
    },
    {
      name = "window_set_geometry",
      doc = "Set the geometry of the specified window.",
      params = {
        { name = "win_id",   type = "int",             doc = "Window id" },
        { name = "geometry", type = "window.geometry", doc = "Window geometry" },
      },
    },
    {
      name = "window_close",
      doc = "Close the specified window, killing the associated process.",
      params = {
        { name = "win_id", type = "int", doc = "The window to close" },
      },
    },
    {
      name = "window_get_title",
      doc = "Get the title of the window with id |win_id|",
      params = {
        { name = "win_id", type = "int", doc = "Window id" },
      },
      returns = { name = "title", type = "string", doc = "Window title" }
    },
    {
      name = "window_set_title",
      doc = "Set the title of the window with id |win_id|",
      params = {
        { name = "win_id", type = "int",    doc = "Window id" },
        { name = "title",  type = "string", doc = "New title" },
      },
    },
    {
      name = "window_send_keys",
      doc = "Send |keys| to the window with id |win_id|. Unlike |window_send_text|, keys such as <C-x> will be encoded .",
      params = {
        { name = "win_id", type = "int",    doc = "The window receiving the keys" },
        { name = "keys",   type = "string", doc = "The keys to send" },
      },
    },
    {
      name = "window_send_text",
      doc = "Send |text| to the window with id |win_id|",
      params = {
        { name = "win_id", type = "int",    doc = "The window receiving the text" },
        { name = "text",   type = "string", doc = "The text to send" },
      },
    },
    {
      name = "window_create",
      doc =
      "Create a naked window with no backing process. This window can be controlled through the lua API. Returns the window id.",
      returns = { type = "int", doc = "The id of the new window" }
    },
    {
      name = "window_write",
      doc =
      "Write to the backing emulator of a window. This is only valid for naked windows, and will error if the |win_id| is process backed. The backing emulator acts like screen pty, and will parse ansi escapes such as \\r, \\n, color escapes, cursor movement, etc.",
      params = {
        { name = "win_id", type = "int",    doc = "Window id" },
        { name = "text",   type = "string", doc = "String which can embed any VT compatible ansi escape." },
      },
    },
    {
      name = "window_create_process",
      doc = "Create a new window with the process |cmd|, executed with 'sh -c'. Returns the window id.",
      params = {
        { name = "cmd", type = "string", doc = "The process to spawn." },
      },
      returns = { type = "int", doc = "The id of the new window" }
    },
    {
      name = "window_is_lua",
      doc = "Returns true if |win_id| exists and is a lua window.",
      params = { { name = "win_id", type = "int", doc = "Window id" } },
      returns = { type = "bool", doc = "Bool indicating if |win_id| is naked." }
    },
    {
      name = "window_is_valid",
      doc = "Returns true if a window exists with id |win_id|.",
      params = { { name = "win_id", type = "int", doc = "Window id" } },
      returns = { type = "bool", doc = "Bool indicating whether the window id is valid." }
    },
  },
}

-- Modeline {{{1
-- vim: fdm=marker shiftwidth=2
