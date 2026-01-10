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
      returns = "int",
    },
    { name = "get_tags",
      doc = "Set the tags of the specified window.",
      params = {},
      optional = {
        { name = "winid", type = "int", doc = "Window ID" } },
      returns = "int",
    },
    { name = "set_tags",
      doc = "Set the tags of the specified window.",
      params = {
        { name = "tags", type = "int", doc = "Bitmask of tags where <winid> should be shown." },
      },
      optional = {
        { name = "winid", type = "int", doc = "Window ID" } },
      returns = "int",
    },
    -- map = {
    --   doc = "map keys to an action",
    --   params = {
    --     lhs = { type = "string", doc = "keys" },
    --     rhs = { type = "function", doc = "action" },
    --   },
    --   returns = "void",
    -- },
    -- close_window = {
    --   doc = "Close a window by ID",
    --   params = { winid = { type = "int", doc = "Window ID" }, },
    --   returns = "void",
    -- },
  },
}

