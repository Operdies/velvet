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
  },

  api = {
    { name = "spawn",
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
    { name = "get_tags",
      doc = "Set the tags of the specified window.",
      params = {},
      optional = {
        { name = "winid", type = "int", doc = "Window ID" } },
      returns = { type = "int", doc = "The tags of the specified window." }
    },
    { name = "set_tags",
      doc = "Set the tags of the specified window.",
      params = {
        { name = "tags", type = "int", doc = "Bitmask of tags where <winid> should be shown." },
      },
      optional = { { name = "winid", type = "int", doc = "Window ID" } },
      returns = { type = "int", doc = "The tags of the window after the set operation." }
    },
    {
      name = "detach",
      doc = "Detach the current session from the server.",
      optional = { { name = "session_id", type = "int", doc = "Session ID" } },
    },
    { name = "close_window",
      doc = "Close the specified window, killing the associated process.",
      optional = { 
        { name = "winid", type = "int", doc = "The window to close" } ,
        {
          name = "force",
          type = "bool",
          doc = [[
        Forcefully close the window.
        If this option is set, velvet will rudely kill the hosted process and remove the window.
        Otherwise, velvet will attempt to gracefully close it.
        ]]
        },
      },
    },
  },
}

