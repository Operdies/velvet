--- This spec file is used to auto-generate C / Lua bindings, and the default config.
--- Reading and assigning options from the options table triggers a lookup in a meta table
--- which reads or updates a backing field in C.
--- A C header file is generated with getters/setters for all options, and functions corresponding to each api function.
--- An implementation file is generated with lua bindings, which will read the parameters specified in this document,
--- and pass them as normal C parameters according to the generated header file.
--- The C header is manually implemented elsewhere.
return {
  meta = {
    name = "velvet",
    api_version = 1,
  },

  options = {
    {
      name = "focus_follows_mouse",
      type = "bool",
      default = true,
      doc = "Automatically focusa window when the mouse moves over it."
    },
    {
      name = "key_repeat_timeout",
      type = "int",
      default = 500,
      doc = "Time in milliseconds before pending keybinds time out",
    },
    {
      name = "view",
      type = "int",
      default = 1,
      doc = "Bitmask of the currently visible tags.",
    },
    {
      name = "display_damage",
      type = "bool",
      default = false,
      doc = "Enable damage tracking when the screen is updated. (debugging feature)",
    },
  },

  types = {
    ["window.geometry"] = {
      { name = "left",   type = "int", doc = "The leftmost cell of the window." },
      { name = "top",    type = "int", doc = "The topmost cell of the window." },
      { name = "width",  type = "int", doc = "The width of the window" },
      { name = "height", type = "int", doc = "The height of the window" },
      type = "struct",
    },
    ["terminal.geometry"] = {
      { name = "width",  type = "int", doc = "The width of the terminal" },
      { name = "height", type = "int", doc = "The height of the terminal" },
    },
  },

  -- types we know that we cannot automatically marshal. Such functions must be implemented by hand.
  manual_types = { ["int[]"] = true },

  api = {
    {
      name = "window_send_keys",
      doc = "Send keys to the window specified by |winid|. Unlike |window_send_text|, the input to this function will parse and send escaped keys using the same syntax as keymaps.",
      params = { 
        { name = "winid", type = "int", doc = "The window receiving the keys" },
        { name = "keys", type = "string", doc = "The keys to send" },
      },
    },
    {
      name = "window_send_text",
      doc = "Send text to the window specified by |winid|",
      params = { 
        { name = "winid", type = "int", doc = "The window receiving the text" },
        { name = "text", type = "string", doc = "The text to send" },
      },
    },
    {
      name = "keymap_remap_modifier",
      doc = "Remap the modifier |from| to the modifier |to|. This is a one-way mapping. To swap two modifiers, you must also apply the inverse mapping. Shift is not supported.",
      params = {
        { name = "from", type = "string", doc = "The modifier to remap." },
        { name = "to", type = "string", doc = "The new modifier emitted when the remapped modifier is used." },
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
    {
      name = "get_terminal_geometry",
      doc = "Get the size of the terminal.",
      returns = { name = "geometry", type = "terminal.geometry", doc = "The geometry of the terminal window." },
    },
    {
      name = "spawn",
      doc = "Spawn a process in a new window. Returns the window ID.",
      params = {
        { name = "cmd", type = "string", doc = "The process to spawn." },
      },
      optional = {
        { name = "left",   type = "int",    doc = "Leftmost column of the new window." },
        { name = "top",    type = "int",    doc = "Topmost row of the new window." },
        { name = "width",  type = "int",    doc = "Width of the new window." },
        { name = "height", type = "int",    doc = "Height of the new window." },
        { name = "layer",  type = "string", doc = "The initial layer of the window. FLOATING|TILED" },
      },
      returns = { type = "int", doc = "The ID of the new window" }
    },
    {
      name = "is_window_valid",
      doc = "Check whether the given window ID is valid.",
      params = { { name = "winid", type = "int", doc = "Window ID" } },
      returns = { type = "bool", doc = "Bool indicating whether the window ID is valid." }
    },
    {
      name = "schedule_after",
      params = {
        { name = "delay",  type = "int", doc = "delay in miliseconds" },
        { name = "func", type = "function", doc = "function to schedule" },
      },
      doc = "Schedule |function| to run after |delay| ms",
    },
    {
      name = "get_current_tick",
      doc = "Get the number of milliseconds elapsed since startup",
      returns = { type = "int", doc = "milliseconds elapsed since startup" }
    },
    {
      name = "swap_windows",
      params = {
        { name = "first",  type = "int", doc = "Window ID" },
        { name = "second", type = "int", doc = "Window ID" },
      },
      doc = "Swap the two windows. This affects the layout of tiled windows.",
    },
    {
      name = "set_focused_window",
      params = { { name = "winid", type = "int", doc = "Window ID" } },
      doc = "Focus the window specified by |winid|",
    },
    {
      name = "get_focused_window",
      doc = "Get the ID of the currently focused window.",
      returns = { type = "int", doc = "TThe ID of the currently focused window." }
    },
    {
      name = "list_windows",
      doc = "Get the IDs of all windows.",
      returns = { type = "int[]", doc = "List of window IDs" }
    },
    {
      name = "get_window_geometry",
      doc = "Get the geometry of ths specified window.",
      params = { { name = "winid", type = "int", doc = "Window ID" } },
      returns = { name = "geometry", type = "window.geometry", "Window geometry" },
    },
    {
      name = "set_window_geometry",
      doc = "Set the geometry of ths specified window.",
      params = {
        { name = "winid",    type = "int",             doc = "Window ID" },
        { name = "geometry", type = "window.geometry", doc = "Window geometry" },
      },
    },
    {
      name = "get_tags",
      doc = "Set the tags of the specified window.",
      params = { { name = "winid", type = "int", doc = "Window ID" } },
      returns = { type = "int", doc = "The tags of the specified window." }
    },
    {
      name = "get_layer",
      doc = "set the layer of the specified window.",
      params = {
        { name = "winid", type = "int", doc = "Window ID" },
      },
      returns = { name = "layer", type = "string", doc = "The current layer" },
    },
    {
      name = "set_layer",
      doc = "set the layer of the specified window.",
      params = {
        { name = "winid", type = "int",    doc = "Window ID" },
        { name = "layer", type = "string", doc = "The new layer" },
      },
    },
    {
      name = "set_tags",
      doc = "Set the tags of the specified window.",
      params = {
        { name = "tags",  type = "int", doc = "Bitmask of tags where <winid> should be shown." },
        { name = "winid", type = "int", doc = "Window ID" },
      },
      returns = { type = "int", doc = "The tags of the window after the set operation." }
    },
    {
      name = "detach",
      doc = "Detach the current session from the server.",
      optional = { { name = "session_id", type = "int", doc = "Session ID" } },
    },
    {
      name = "close_window",
      doc = "Close the specified window, killing the associated process.",
      params = {
        { name = "winid", type = "int",  doc = "The window to close (default: current window)" },
        { name = "force", type = "bool", doc = "Forcefully close the window. (default: false)" },
      },
    },
  },
}
