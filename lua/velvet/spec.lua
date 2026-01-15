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
  },

  --- types {{{1
  types = {
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
      name = "terminal.geometry",
      fields = {
        { name = "width",  type = "int", doc = "The width of the terminal" },
        { name = "height", type = "int", doc = "The height of the terminal" },
      }
    },
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
        { name = "from", type = "string", doc = "The modifier to remap." },
        { name = "to",   type = "string", doc = "The new modifier emitted when the remapped modifier is used." },
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
      name = "get_terminal_geometry",
      doc = "Get the size of the terminal.",
      returns = { name = "geometry", type = "terminal.geometry", doc = "The geometry of the terminal window." },
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
    {
      name = "get_sessions",
      doc = "Get the IDs of all sessions.",
      returns = { type = "int[]", doc = "List of session IDs" }
    },
    {
      name = "set_active_session",
      doc = "Get the ID of the active session",
      params = {{ name = "session_id", type = "int", doc = "Session ID" }},
    },
    {
      name = "get_active_session",
      doc = "Get the ID of the active session",
      returns = { name = "session_id", type = "int", doc = "Session ID" },
    },
    {
      name = "session_detach",
      doc = "Detach |session| session from the server.",
      params = { { name = "session_id", type = "int", doc = "Session ID" } },
    },
    {
      name = "server_kill",
      doc = "Kill the velvet instance. All child processes will be killed."
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
        { name = "first",  type = "int", doc = "Window ID" },
        { name = "second", type = "int", doc = "Window ID" },
      },
      doc = "Swap two windows. This affects the layout of tiled windows.",
    },
    {
      name = "window_set_z_index",
      doc = "Set the z index of |win| to |z|",
      params = {
        { name = "win", type = "int", doc = "Window ID" },
        { name = "z",   type = "int", doc = "New z index of |win|" }
      },
    },
    {
      name = "window_get_z_index",
      doc = "Get the z index of |win|",
      params = { { name = "win", type = "int", doc = "Window ID" } },
      returns = { type = "int", doc = "The z index of |win|" }
    },
    {
      name = "window_set_hidden",
      doc = "Set window hidden flag. A hidden window will not be rendered.",
      params = {
        { name = "win_id", type = "int",  doc = "Window ID" },
        { name = "hidden", type = "bool", doc = "New hidden state of |win_id|" }
      },
    },
    {
      name = "window_get_hidden",
      doc = "Get window hidden flag. A hidden window will not be rendered.",
      params = { { name = "win_id", type = "int", doc = "Window ID" } },
      returns = { type = "bool", doc = "Bool indicating if the window is hidden." }
    },
    {
      name = "window_get_opacity",
      doc = "Get window opacity",
      params = { { name = "win", type = "int", doc = "Window ID" } },
      returns = { type = "float", doc = "The new window opacity." },
    },
    {
      name = "window_set_opacity",
      doc = "Set window opacity. The effect of this depends on the value of |window_get_transparency_mode|",
      params = {
        { name = "win",     type = "int",   doc = "Window ID" },
        { name = "opacity", type = "float", doc = "The new window opacity." },
      },
    },
    {
      name = "window_get_transparency_mode",
      doc = "Get window transparency mode.",
      params = { { name = "win", type = "int", doc = "Window ID" } },
      returns = { type = "string", doc = "Set transparency mode. Valid values are 'off', 'clear', 'all'" },
    },
    {
      name = "window_set_transparency_mode",
      doc = "Set window transparency mode.",
      params =
      { { name = "win", type = "int",   doc = "Window ID" },
        { name = "mode", type = "string", doc = "Set transparency mode. Valid values are 'off', 'clear', 'all'" },
      },
    },
    {
      name = "get_focused_window",
      doc = "Get the ID of the currently focused window.",
      returns = { type = "int", doc = "TThe ID of the currently focused window." }
    },
    {
      name = "set_focused_window",
      params = { { name = "win_id", type = "int", doc = "Window ID" } },
      doc = "Focus the window with id |win_id|",
    },
    {
      name = "window_get_geometry",
      doc = "Get the geometry of the specified window.",
      params = { { name = "win_id", type = "int", doc = "Window ID" } },
      returns = { name = "geometry", type = "window.geometry", "Window geometry" },
    },
    {
      name = "window_set_geometry",
      doc = "Set the geometry of the specified window.",
      params = {
        { name = "win_id",   type = "int",             doc = "Window ID" },
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
        { name = "win_id", type = "int", doc = "Window ID" },
      },
      returns = { name = "title", type = "string", doc = "Window title" }
    },
    {
      name = "window_set_title",
      doc = "Set the title of the window with id |win_id|",
      params = {
        { name = "win_id", type = "int",    doc = "Window ID" },
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
      name = "window_create_process",
      doc = "Create a new window with the process |cmd|, executed with 'sh -c'. Returns the window ID.",
      params = {
        { name = "cmd", type = "string", doc = "The process to spawn." },
      },
      returns = { type = "int", doc = "The ID of the new window" }
    },
    {
      name = "window_is_valid",
      doc = "Returns true if a window exists with id |win_id|.",
      params = { { name = "win_id", type = "int", doc = "Window ID" } },
      returns = { type = "bool", doc = "Bool indicating whether the window ID is valid." }
    },
  },
}

-- Modeline {{{1
-- vim: fdm=marker shiftwidth=2
