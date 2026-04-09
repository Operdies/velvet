--- This spec file is used to auto-generate C / Lua bindings, and the default config.
--- Reading and assigning options from the options table triggers a lookup in a meta table
--- which reads or updates a backing field in C.
--- A C header file is generated with getters/setters for all options, and functions corresponding to each api function.
--- An implementation file is generated with lua bindings, which will read the parameters specified in this document,
--- and pass them as normal C parameters according to the generated header file.
--- The C header is manually implemented elsewhere.

--- @alias spec_doc string|string[]

--- @class spec_option
--- @field name string
--- @field type string
--- @field default any default value of the option. Automatically assigned at startup
--- @field doc spec_doc

--- @class spec_enum_value
--- @field name string
--- @field value integer
--- @field doc? string

--- @class spec_enum enumeration type
--- @field name string
--- @field values spec_enum_value[]
--- @field doc? string
--- @field flags boolean flag indicating if this is a discrete value or a list of flags

--- @class struct_field
--- @field name string member name
--- @field type string the name of the member type
--- @field doc? spec_doc member description
--- @field optional? boolean if true, this member can be omitted

--- @class spec_type
--- @field name string the name of the type
--- @field fields struct_field[] the members of the type

--- @class spec_parameter
--- @field name string
--- @field type string
--- @field doc spec_doc

--- @class spec_return
--- @field type string
--- @field doc spec_doc

--- @class spec_function
--- @field name string function name
--- @field doc spec_doc
--- @field params? spec_parameter[]
--- @field returns? spec_return

--- @class spec_event
--- @field name string
--- @field doc spec_doc
--- @field args string

--- @class spec
--- @field options spec_option[]
--- @field types spec_type[]
--- @field enums spec_enum[]
--- @field api spec_function[]
--- @field events spec_event[]

--- @type spec
return {
  --- options {{{1
  options = {
    {
      name = "scrollback_scroll_multiplier",
      type = "int",
      default = 3,
      doc = "The number of lines scrolled per scroll wheel tick.",
    },
    {
      name = 'theme',
      type = 'theme',
      doc = 'The 16 numbered terminal colors.',
      default = {
        black             = "#45475a",
        red               = "#f38ba8",
        green             = "#a6e3a1",
        yellow            = "#f9e2af",
        blue              = "#89b4fa",
        magenta           = "#f5c2e7",
        cyan              = "#94e2d5",
        white             = "#bac2de",
        bright_black      = "#585b70",
        bright_red        = "#f38ba8",
        bright_green      = "#a6e3a1",
        bright_yellow     = "#f9e2af",
        bright_blue       = "#89b4fa",
        bright_magenta    = "#f5c2e7",
        bright_cyan       = "#94e2d5",
        bright_white      = "#a6adc8",
        foreground        = "#cdd6f4",
        background        = "#1e1e2e",
        cursor_foreground = "#1e1e2e",
        cursor_background = "#f5e0dc",
      },
    },
  },

  --- enums {{{1
  enums = {
    {
      name = "severity",
      doc = "The severity level of a message",
      flags = false,
      values = {
        { name = "debug",   value = 0 },
        { name = "info",    value = 1 },
        { name = "warning", value = 2 },
        { name = "error",   value = 3 },
      },
    },
    {
      name = "mouse_reporting",
      doc = "Mouse reporting mode.",
      flags = false,
      values = {
        { name = "off",         value = 0,    doc = "Never report mouse events." },
        { name = "click",       value = 1000, doc = "Report mouse click events." },
        { name = "cell_motion", value = 1002, doc = "Report mouse events on move while a mouse button is down." },
        { name = "all_motion",  value = 1003, doc = "Report mouse events on move." },
      },
    },
    {
      name = "mouse_protocol",
      doc = "The protocol to use for mouse reporting",
      flags = false,
      values = {
        { name = "off",       value = 0,    doc = "No mouse reporting." },
        { name = "sgr",       value = 1006, doc = "Coordinates encoded as SGR sequences." },
        -- Other protocols included for completeness -- not supported
        { name = "utf8",      value = 1005, doc = "Coordinates encoded as utf8." },
        { name = "urxvt",     value = 1015, doc = "urxvt encoding" },
        { name = "sgr_pixel", value = 1016, doc = "Coordinates encoded as SGR sequences. Encoded as pixel positions instead of cell coordinates." },
      },
    },
    {
      name = "brush",
      doc = "A named brush",
      flags = false,
      values = {
        { name = "background", value = 0 },
        { name = "foreground", value = 1 },
      },
    },
    {
      name = "transparency_mode",
      flags = false,
      doc = "The transparency mode of a window. This affects the behavior of the |alpha| setting.",
      values = {
        { name = "none",  value = 0, doc = 'Disable alpha blending' },
        { name = "clear", value = 1, doc = 'alpha blending applies to cells with no background color' },
        { name = "all",   value = 2, doc = 'alpha blending applies to all cells' },
      }
    },
    {
      name = "key_event_type",
      flags = false,
      values = {
        { name = "press",   value = 1 },
        { name = "repeat",  value = 2 },
        { name = "release", value = 3 },
      },
    },
    {
      name = "key_modifier",
      flags = true,
      values = {
        { name = "shift",     value = 1 << 0 },
        { name = "alt",       value = 1 << 1 },
        { name = "control",   value = 1 << 2 },
        { name = "super",     value = 1 << 3 },
        { name = "hyper",     value = 1 << 4 },
        { name = "meta",      value = 1 << 5 },
        { name = "caps_lock", value = 1 << 6 },
        { name = "num_lock",  value = 1 << 7 },
      }
    },
    {
      name = "scroll_direction",
      flags = false,
      values = {
        { name = "up",    value = 0 },
        { name = "down",  value = 1 },
        { name = "left",  value = 2 },
        { name = "right", value = 3 },
      },
    },
    {
      name = "mouse_button",
      flags = false,
      values = {
        { name = "left",   value = 0 },
        { name = "middle", value = 1 },
        { name = "right",  value = 2 },
        { name = "none",   value = 3 },
      },
    },
    {
      name = "mouse_event_type",
      flags = false,
      values = {
        { name = "mouse_down", value = 1 },
        { name = "mouse_up",   value = 2 },
      },
    },
  },

  --- types {{{1
  types = {
    {
      name = "mouse_settings",
      doc = "The mouse settings used by a specific window.",
      fields = {
        { name = "reporting", type = "mouse_reporting" },
        { name = "protocol",  type = "mouse_protocol" },
      },
    },
    {
      name = "rgb_color",
      doc = "rgb color with colors values in the range 0 < |col| < 1",
      fields = {
        { name = "red",   type = "float" },
        { name = "green", type = "float" },
        { name = "blue",  type = "float" },
        { name = "alpha", type = "float", optional = true },
      },
    },
    {
      name = "theme",
      fields = {
        -- 30-37 / 40-47
        { name = "black",             type = "rgb_color", doc = "Palette color 0" },
        { name = "red",               type = "rgb_color", doc = "Palette color 1" },
        { name = "green",             type = "rgb_color", doc = "Palette color 2" },
        { name = "yellow",            type = "rgb_color", doc = "Palette color 3" },
        { name = "blue",              type = "rgb_color", doc = "Palette color 4" },
        { name = "magenta",           type = "rgb_color", doc = "Palette color 5" },
        { name = "cyan",              type = "rgb_color", doc = "Palette color 6" },
        { name = "white",             type = "rgb_color", doc = "Palette color 7" },
        --  90-97 / 100-107
        { name = "bright_black",      type = "rgb_color", doc = "Palette color 8" },
        { name = "bright_red",        type = "rgb_color", doc = "Palette color 9" },
        { name = "bright_green",      type = "rgb_color", doc = "Palette color 10" },
        { name = "bright_yellow",     type = "rgb_color", doc = "Palette color 11" },
        { name = "bright_blue",       type = "rgb_color", doc = "Palette color 12" },
        { name = "bright_magenta",    type = "rgb_color", doc = "Palette color 13" },
        { name = "bright_cyan",       type = "rgb_color", doc = "Palette color 14" },
        { name = "bright_white",      type = "rgb_color", doc = "Palette color 15" },

        -- Additional named colors
        { name = "foreground",        type = "rgb_color", doc = "The default text color" },
        { name = "background",        type = "rgb_color", doc = "The default background color" },
        { name = "cursor_foreground", type = "rgb_color", doc = "The foreground color of the cell containing the cursor", optional = true },
        { name = "cursor_background", type = "rgb_color", doc = "The background color of the cell containing the cursor", optional = true },
      },
    },
    {
      name = "window.create_options",
      fields = {
        { name = "working_directory", type = "string", doc = "The initial working directory of the new window.", optional = true },
        { name = "parent_window",     type = "int",    doc = "The parent window of this window. If set, this window will close with the parent.", optional = true },
      },
    },
    {
      name = "rect",
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
      fields = { { name = "win_id", type = "int", doc = "The id of the newly created window." } }
    },
    {
      name = "window.closed.event_args",
      fields = { { name = "win_id", type = "int", doc = "The id of the closed window." } }
    },
    {
      name = "window.output.event_args",
      fields = { { name = "win_id", type = "int", doc = "The id of the window that received output." } }
    },
    {
      name = "window.moved.event_args",
      fields = {
        { name = "win_id",   type = "int",             doc = "The id of the resized window." },
        { name = "old_size", type = "rect", doc = "The old geometry of |id|." },
        { name = "new_size", type = "rect", doc = "The new geometry of |id|." },
      }
    },
    {
      name = "window.resized.event_args",
      fields = {
        { name = "win_id",   type = "int",             doc = "The id of the resized window." },
        { name = "old_size", type = "rect", doc = "The old geometry of |id|." },
        { name = "new_size", type = "rect", doc = "The new geometry of |id|." },
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
        { name = "modifiers",           type = "key_modifier",   doc = "Key modifier such as super, shift, control, alt" },
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
      name = "line",
      doc = "A line of text extracted from a window",
      fields = {
        { name = "text", type = "string", doc = "Text content" },
        { name = "wraps", type = "bool", doc = "Set if the line continues on the next row" },
        { name = "truncated", type = "bool", doc = "Set if the first cell is a continuation of a wide character. If set, the first character of |text| will be a space." },
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
      name = "session.key.event_args",
      fields = {
        { name = "key",    type = "window.key_event", doc = "The key which generated the event." },
      },
    },
    {
      name = "mouse.move.event_args",
      fields = {
        { name = "win_id",       type = "int",          doc = "The id of the topmost visible window at the coordinates." },
        { name = "pos",          type = "coordinate",   doc = "1-indexed screen coordinate of the mouse when the event was raised." },
        { name = "mouse_button", type = "mouse_button", doc = "Mouse button state when the event was raised." },
        { name = "modifiers",    type = "key_modifier", doc = "The keyboard modifier which were held when the event was raised." },
      },
    },
    {
      name = "mouse.click.event_args",
      fields = {
        { name = "win_id",       type = "int",              doc = "The id of the topmost visible window at the coordinates." },
        { name = "pos",          type = "coordinate",       doc = "1-indexed screen coordinate of the mouse when the event was raised." },
        { name = "mouse_button", type = "mouse_button",     doc = "The mouse button which was clicked." },
        { name = "event_type",   type = "mouse_event_type", doc = "Flag indicating if the button was pressed or released." },
        { name = "modifiers",    type = "key_modifier",     doc = "The keyboard modifier which were held when the event was raised." },
      },
    },
    {
      name = "mouse.scroll.event_args",
      fields = {
        { name = "win_id",    type = "int",              doc = "The id of the topmost visible window at the coordinates." },
        { name = "pos",       type = "coordinate",       doc = "1-indexed screen coordinate of the mouse when the event was raised." },
        { name = "direction", type = "scroll_direction", doc = "The scroll direction which raised the event." },
        { name = "modifiers", type = "key_modifier",     doc = "The keyboard modifier which were held when the event was raised." },
      },
    },
    {
      name = "pre_render.event_args",
      fields = {
        { name = "time",  type = "int",    doc = "The number of milliseconds elapsed since startup" },
        { name = "cause", type = "string", doc = "The reason for the render, such as 'io_idle' or 'io_max_exceeded'" },
      },
    },
    {
      name = "system_message.event_args",
      fields = {
        { name = "message", type = "string",   doc = "The message" },
        { name = "level",   type = "severity", doc = "Error level" },
      },
    },
    {
      name = "pre_reload.event_args",
      fields = {
        { name = "time", type = "int", doc = "The number of milliseconds elapsed since startup" },
      },
    },
    {
      name = "session_options",
      doc = "session specific options of an attached client.",
      fields = {
        { name = "x_pixel",                                 type = "int",  doc = "The number of horizontal pixels." },
        { name = "y_pixel",                                 type = "int",  doc = "The number of vertical pixels." },
        { name = "lines",                                   type = "int",  doc = "The number of lines." },
        { name = "columns",                                 type = "int",  doc = "The number of columns." },
        { name = "supports_repeating_multibyte_characters", type = "bool", doc = "Some terminals do not support CSI REP for multibyte characters.", optional = true },
      },
    },
  },

  --- {{{1 events
  events = {
    { name = "session_on_key",       doc = "Raised when a key is pressed.",                args = "session.key.event_args" },
    { name = "window_created",       doc = "Raised after a new window is created.",        args = "window.created.event_args" },
    { name = "window_closed",        doc = "Raised after a window is closed.",             args = "window.closed.event_args" },
    { name = "window_output",        doc = "Raised when a window produces output.",        args = "window.output.event_args" },
    { name = "window_moved",         doc = "Raised after a window is moved.",              args = "window.moved.event_args" },
    { name = "window_resized",       doc = "Raised after a window is resized.",            args = "window.resized.event_args" },
    { name = "window_on_key",        doc = "Raised when a key is sent to a lua window.",   args = "window.on_key.event_args" },
    { name = "window_focus_changed", doc = "Raised after focus changes.",                  args = "window.focus_changed.event_args" },
    { name = "screen_resized",       doc = "Raised after the screen is resized.",          args = "screen.resized.event_args" },
    { name = "mouse_move",           doc = "Raised when the mouse moves.",                 args = "mouse.move.event_args" },
    { name = "mouse_click",          doc = "Raised when the mouse is clicked.",            args = "mouse.click.event_args" },
    { name = "mouse_scroll",         doc = "Raised when the mouse scrolls.",               args = "mouse.scroll.event_args" },
    { name = "system_message",       doc = "Raised when the system logs an error message", args = "system_message.event_args", },
    {
      name = "pre_render",
      doc = "Raised right before content is rendered. This is useful for applying updates just-in-time.",
      args = "pre_render.event_args",
    },
    {
      name = "pre_reload",
      doc = "Raised before reloading. This event can be used to store state.",
      args = "pre_reload.event_args",
    },
  },

  --- api {{{1
  api = {
    --- screen {{{2
    {
      name = "get_screen_geometry",
      doc = "Get the size of the screen.",
      returns = { name = "geometry", type = "screen.geometry", doc = "The geometry of the screen window." },
    },
    --- timing {{{2
    {
      name = "schedule_cancel",
      params = {
        { name = "cancellation_id", type = "int", doc = "Schedule cancellation token returned by |schedule_after|." },
      },
      returns = { type = "bool", doc = "Boolean value indicating if a schedule was cancelled." },
      doc = "Cancel the schedule with id |cancellation_id|.",
    },
    {
      name = "schedule_after",
      params = {
        { name = "delay", type = "int",      doc = "delay in milliseconds" },
        { name = "func",  type = "function", doc = "function to run" },
      },
      returns = { type = "int", doc = "Schedule cancellation token. Pass to |schedule_cancel| to cancel this schedule." },
      doc = "Schedule oneshot |function| to run after at least |delay| ms. Returns a token which can be passed to schedule_cancel.",
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
      doc = "Set the active session to the session with id |session_id|",
      params = { { name = "session_id", type = "int", doc = "Session id" } },
    },
    {
      name = "get_active_session",
      doc = "Get the id of the active session",
      returns = { name = "session_id", type = "int", doc = "Session id" },
    },
    {
      name = "session_detach",
      doc = "Detach session |id| from the server.",
      params = { { name = "id", type = "int", doc = "Session id" } },
    },
    {
      name = "session_set_options",
      doc = "Update session options for the session id id |session_id|",
      params = {
        { name = "session_id", type = "int",             doc = "Session id" },
        { name = "options",    type = "session_options", doc = "The new session options." },
      },
    },
    {
      name = "quit",
      doc = "Quit velvet with no warning"
    },
    {
      name = "reload",
      doc = "Reload configuration. This will close all lua windows, remove all keybinds, cancel any schedules, and then load your config.",
    },
    {
      name = "get_startup_directory",
      doc = "Get the directory whcih velvet was started in.",
      returns = { type = "string", doc = "startup directory" },
    },
    {
      name = "get_mouse_position",
      doc = "Get the last recorded mouse position",
      returns = { type = "coordinate", doc = "The last recorded mouse position" },
    },
    --- Windows {{{2
    {
      name = "get_windows",
      doc = "Get the IDs of all windows.",
      returns = { type = "int[]", doc = "List of window IDs" }
    },
    {
      name = "window_set_z_index",
      doc = "Set the z index of |win| to |z|",
      params = {
        { name = "win_id", type = "int", doc = "Window id" },
        { name = "z",      type = "int", doc = "New z index of |win|" }
      },
    },
    {
      name = "window_get_z_index",
      doc = "Get the z index of |win|",
      params = { { name = "win_id", type = "int", doc = "Window id" } },
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
      name = "window_get_alpha",
      doc = "Get window alpha",
      params = { { name = "win_id", type = "int", doc = "Window id" } },
      returns = { type = "float", doc = "The new window alpha." },
    },
    {
      name = "window_get_text",
      doc = "Get window text contained in |region|. 1-indexed.",
      params = {
        { name = "win_id",  type = "int",   doc = "Window id" },
        { name = "region", type = "rect", doc = "The region to get text from" },
      },
      returns = { type = "line[]", doc = "The text in the specified region." },
    },
    {
      name = "window_get_mouse_settings",
      doc = "Get the mouse settings of |win_id|",
      params = {
        { name = "win_id",  type = "int",   doc = "Window id" },
      },
      returns = { type = "mouse_settings", doc = "The mouse settings of |win_id|." },
    },
    {
      name = "window_set_alpha",
      doc = "Set window alpha. The effect of this depends on the value of |window_get_transparency_mode|",
      params = {
        { name = "win_id",  type = "int",   doc = "Window id" },
        { name = "alpha", type = "float", doc = "The new window alpha." },
      },
    },
    {
      name = "window_get_transparency_mode",
      doc = "Get window transparency mode.",
      params = { { name = "win_id", type = "int", doc = "Window id" } },
      returns = { type = "transparency_mode", doc = "Set transparency mode." },
    },
    {
      name = "window_set_transparency_mode",
      doc = "Set window transparency mode.",
      params =
      {
        { name = "win_id", type = "int",               doc = "Window id" },
        { name = "mode",   type = "transparency_mode", doc = "Set transparency mode." },
      },
    },
    {
      name = "window_get_dim_factor",
      doc = "Get the current dim factor for |win|",
      params = {
        { name = "win_id", type = "int", doc = "Window id" },
      },
      returns = { name = "factor", type = "float", doc = "Dim factor between 0.0 and 1.0" },

    },
    {
      name = "window_set_dim_factor",
      doc = "Dim the window content of |win| by a constant factor. A larger value means more dimming (0.0 - 1.0)",
      params = {
        { name = "win_id", type = "int",   doc = "Window id" },
        { name = "factor", type = "float", doc = "Dim factor between 0.0 and 1.0" },
      },
    },
    {
      name = "get_focused_window",
      doc = "Get the id of the currently focused window.",
      returns = { type = "int", doc = "The id of the focused window." }
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
      returns = { name = "geometry", type = "rect", doc = "The geometry of the window with id |win_id|" },
    },
    {
      name = "window_set_geometry",
      doc = "Set the geometry of the specified window.",
      params = {
        { name = "win_id",   type = "int",             doc = "Window id" },
        { name = "geometry", type = "rect", doc = "rect" },
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
      name = "window_send_raw_key",
      doc =
      "Send |key| to the window with id |win_id|.",
      params = {
        { name = "win_id", type = "int",              doc = "The window receiving the keys" },
        { name = "key",    type = "window.key_event", doc = "Low level key event" },
      },
    },
    {
      name = "window_send_keys",
      doc = "Send |keys| to the window with id |win_id|. Unlike |window_paste_text|, keys such as <C-x> will be encoded .",
      params = {
        { name = "win_id", type = "int",    doc = "The window receiving the keys" },
        { name = "keys",   type = "string", doc = "The keys to send" },
      },
    },
    {
      name = "window_paste_text",
      doc = "Send |text| to the window with id |win_id|. If the recipeint has bracketed paste enabled (mode ?2004), the text will be escaped accordingly.",
      params = {
        { name = "win_id", type = "int",    doc = "The window receiving the text" },
        { name = "text",   type = "string", doc = "The text to send" },
      },
    },
    {
      name = "window_send_mouse_move",
      doc =
      "Send mouse move event to window with id |win_id|. The event will be encoded according to window emulator's options if applicable.",
      params = { { name = "mouse_move", doc = "Mouse move event args", type = "mouse.move.event_args" } }
    },
    {
      name = "window_send_mouse_click",
      doc =
      "Send mouse click event to window with id |win_id|. The event will be encoded according to window emulator's options if applicable.",
      params = { { name = "mouse_click", doc = "Mouse click event args", type = "mouse.click.event_args" } }
    },
    {
      name = "window_send_mouse_scroll",
      doc =
      "Send mouse scroll event to window with id |win_id|. The event will be encoded according to window emulator's options if applicable. If the window does not handle scrolling, and it has content in its scrollback buffer, this scrolls the window content.",
      params = { { name = "mouse_scroll", doc = "Mouse scroll event args", type = "mouse.scroll.event_args" } }
    },
    {
      name = "window_create",
      doc =
      "Create a naked window with no backing process. This window can be controlled through the lua API. Returns the window id.",
      params = {
        { name = "options", type = "window.create_options", doc = "Options for the created window." },
      },
      returns = { type = "int", doc = "The id of the new window" }
    },
    {
      name = "window_create_process",
      doc =
      "Create a new window with the process |cmd|. If |cmd| is |string|, the process is started as { 'sh', '-c', |cmd| }. Returns the window id.",
      params = {
        { name = "cmd",     type = "string|string[]",       doc = "The process to spawn." },
        { name = "options", type = "window.create_options", doc = "Options for the created window." },
      },
      returns = { type = "int", doc = "The id of the new window" }
    },
    {
      name = "window_get_working_directory",
      doc = "Get the current working directory of |win_id|. If |win_id| is hosting a process, the process may update the working directory.",
      params = { { name = "win_id", type = "int", doc = "Window id" } },
      returns = { type = "string", doc = "The current working directory of the window" }
    },
    {
      name = "window_get_foreground_process_name",
      doc = "Get the name of the foreground_process of |win_id|. Does not apply to lua windows.",
      params = { { name = "win_id", type = "int", doc = "Window id" } },
      returns = { type = "string", doc = "The name of the foreground_process" }
    },
    {
      name = "window_get_parent",
      doc = "Returns the id of the parent of |win_id| or 0 if the window does not have a parent.",
      params = { { name = "win_id", type = "int", doc = "Window id" } },
      returns = { type = "int", doc = "The window id of the parent window." }
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
    {
      name = "window_get_scrollback_size",
      doc = "Get the number of lines in the scrollback of the window with id |win_id|.",
      params = { { name = "win_id", type = "int", doc = "Window id" } },
      returns = { type = "int", doc = "The number of lines in scrollback, not counting the current screen buffer." }
    },
    {
      name = "window_get_scroll_offset",
      doc = "Get the scroll offset of the window with id |win_id|",
      params = { { name = "win_id", type = "int", doc = "Window id" } },
      returns = { type = "int", doc = "The number of lines below the bottom line of the window." }
    },
    {
      name = "window_set_scroll_offset",
      doc = "Set the scroll offset of the window with id |win_id|",
      params = { 
        { name = "win_id", type = "int", doc = "Window id" },
        { name = "scroll_offset", type = "int", doc = "The number of lines below the bottom line of the window." }
      },
    },
    --- drawing {{{1
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
      name = "window_set_drawing_color",
      doc =
      "Set the drawing color of |win_id|. This is equivalent to setting an rgb color with SGR 38/48, but is much faster because it skips formatting and parsing. Useful for tight render loops.",
      params = {
        { name = "win_id", type = "int",       doc = "Window id" },
        { name = "brush",  type = "brush",     doc = "Foreground or background brush" },
        { name = "color",  type = "rgb_color", doc = "The new color" },
      },
    },
    {
      name = "window_get_cursor_position",
      doc = "Get the position of the cursor local to |win_id|.",
      params = {
        { name = "win_id", type = "int",        doc = "Window id" },
      },
      returns = { type = "coordinate", doc = "The current cursor position" },
    },
    {
      name = "window_set_cursor_position",
      doc =
      "Set the cursor position of |win_id|. This is equivalent to moving the cursor with CUP, but is much faster because it skips formatting and parsing. Useful for tight render loops.",
      params = {
        { name = "win_id", type = "int",        doc = "Window id" },
        { name = "pos",    type = "coordinate", doc = "The new cursor position" },
      },
    },
    --- clipboard {{{1
    {
      name = "clipboard_set",
      doc = "Copy text to the system clipboard.",
      params = { { name = "text", type = "string", doc = "The text to copy." } },
    },
    --- unicode {{{1
    {
      name = "string_display_width",
      doc = "Get the display width of a string, taking into account multi-width and 0-width characters.",
      params = { { name = "string", type = "string", doc = "Input string." } },
      returns = { type = "int", doc = "The display width of |string|" },
    },
    {
      name = "string_lower",
      doc = "Get a copy of |string| converted to lower case. Unlike |string.lower|, this function correctly maps the entire unicode range.",
      params = { { name = "string", type = "string", doc = "Input string." } },
      returns = { type = "string", doc = "Lower case string." },
    },
    {
      name = "string_upper",
      doc = "Get a copy of |string| converted to upper case. Unlike |string.upper|, this function correctly maps the entire unicode range.",
      params = { { name = "string", type = "string", doc = "Input string." } },
      returns = { type = "string", doc = "Upper case string." },
    },
    {
      name = "session_store_value",
      doc =
      "Store a named value in the current session. Session values are preserved after reloading, but lost when the session ends.",
      params = {
        { name = "name",  type = "string", doc = "The name of the stored value." },
        { name = "value", type = "any",    doc = "The value to store." },
      },
    },
    {
      name = "session_load_value",
      doc = "Load a value from the current session by name.",
      params = { { name = "name", type = "string", doc = "The name of the session value." } },
      returns = { type = "any", doc = "The loaded value." },
    },
    {
      name = "debug_set_display_damage",
      doc = "Enable damage tracking when the screen is updated. (debugging feature)",
      params = { { name = "mode", type = "bool", doc = "Damage display mode" } },
    },
    -- {
    --   name = "disk_store_value",
    --   doc = "Store a named value on the disk. Values saved on the disk are shared between sessions.",
    --   params = {
    --     { name = "name",  type = "string", doc = "The name of the stored value." },
    --     { name = "value", type = "any",    doc = "The value to store." },
    --   },
    -- },
    -- {
    --   name = "disk_load_value",
    --   doc = "Load a value from disk by name.",
    --   params = { { name = "name", type = "string", doc = "The name of the disk value." } },
    --   returns = { type = "any", doc = "The loaded value." },
    -- },
  },
}

-- Modeline {{{1
-- vim: fdm=marker shiftwidth=2
