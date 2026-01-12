error("Cannot require meta file")
--- @meta
--- @class velvet.api
local api = {}

--- @class velvet.api.window.geometry
--- @field left? integer The leftmost cell of the window.
--- @field top? integer The topmost cell of the window.
--- @field width? integer The width of the window
--- @field height? integer The height of the window

--- @class velvet.api.terminal.geometry
--- @field width? integer The width of the terminal
--- @field height? integer The height of the terminal

--- Send keys to the window specified by |winid|. Unlike |window_send_text|, the input to this function will parse and send escaped keys using the same syntax as keymaps.
--- @param winid integer The window receiving the keys
--- @param keys string The keys to send
--- @return nil 
function api.window_send_keys(winid, keys) end

--- Send text to the window specified by |winid|
--- @param winid integer The window receiving the text
--- @param text string The text to send
--- @return nil 
function api.window_send_text(winid, text) end

--- Remap the modifier |from| to the modifier |to|. This is a one-way mapping. To swap two modifiers, you must also apply the inverse mapping. Shift is not supported.
--- @param from string The modifier to remap.
--- @param to string The new modifier emitted when the remapped modifier is used.
--- @return nil 
function api.keymap_remap_modifier(from, to) end

--- Delete the mapping associated with |keys|
--- @param keys string The mapping to remove
--- @return nil 
function api.keymap_del(keys) end

--- @class velvet.api.keymap_set.Opts
--- @field repeatable? boolean If set, this mapping may be repeated within the interval defined by |velvet.options.key_repeat_timeout|

--- Creates a mapping of |keys| to |function|
--- @param keys string Left hand side of the mapping
--- @param func function Right hand side of the mapping
--- @param opts? velvet.api.keymap_set.Opts
--- @return nil 
function api.keymap_set(keys, func, opts) end

--- Get the size of the terminal.
--- @return velvet.api.terminal.geometry The geometry of the terminal window.
function api.get_terminal_geometry() end

--- Spawn a process in a new window. Returns the window ID.
--- @param cmd string The process to spawn.
--- @return integer The ID of the new window
function api.spawn(cmd) end

--- Check whether the given window ID is valid.
--- @param winid integer Window ID
--- @return boolean Bool indicating whether the window ID is valid.
function api.is_window_valid(winid) end

--- Schedule |function| to run after |delay| ms
--- @param delay integer delay in miliseconds
--- @param func function function to schedule
--- @return nil 
function api.schedule_after(delay, func) end

--- Get the number of milliseconds elapsed since startup
--- @return integer milliseconds elapsed since startup
function api.get_current_tick() end

--- Swap the two windows. This affects the layout of tiled windows.
--- @param first integer Window ID
--- @param second integer Window ID
--- @return nil 
function api.swap_windows(first, second) end

--- Focus the window specified by |winid|
--- @param winid integer Window ID
--- @return nil 
function api.set_focused_window(winid) end

--- Get the ID of the currently focused window.
--- @return integer TThe ID of the currently focused window.
function api.get_focused_window() end

--- Get the IDs of all windows.
--- @return integer[] List of window IDs
function api.list_windows() end

--- Get the geometry of ths specified window.
--- @param winid integer Window ID
--- @return velvet.api.window.geometry 
function api.get_window_geometry(winid) end

--- Set the geometry of ths specified window.
--- @param winid integer Window ID
--- @param geometry velvet.api.window.geometry Window geometry
--- @return nil 
function api.set_window_geometry(winid, geometry) end

--- Set the tags of the specified window.
--- @param winid integer Window ID
--- @return integer The tags of the specified window.
function api.get_tags(winid) end

--- set the layer of the specified window.
--- @param winid integer Window ID
--- @return string The current layer
function api.get_layer(winid) end

--- set the layer of the specified window.
--- @param winid integer Window ID
--- @param layer string The new layer
--- @return nil 
function api.set_layer(winid, layer) end

--- Set the tags of the specified window.
--- @param tags integer Bitmask of tags where <winid> should be shown.
--- @param winid integer Window ID
--- @return integer The tags of the window after the set operation.
function api.set_tags(tags, winid) end

--- @class velvet.api.detach.Opts
--- @field session_id? integer Session ID

--- Detach the current session from the server.
--- @param opts? velvet.api.detach.Opts
--- @return nil 
function api.detach(opts) end

--- Close the specified window, killing the associated process.
--- @param winid integer The window to close (default: current window)
--- @return nil 
function api.close_window(winid) end

--- 
--- @param winid integer Window ID
--- @return string New title
function api.window_get_title(winid) end

--- 
--- @param winid integer Window ID
--- @param title string New title
--- @return nil 
function api.window_set_title(winid, title) end

--- Get focus_follows_mouse
--- @return boolean The current value
function api.get_focus_follows_mouse() end

--- Set focus_follows_mouse. Returns the new value.
--- @param new_value boolean Automatically focusa window when the mouse moves over it.
--- @return boolean The value after the update
function api.set_focus_follows_mouse(new_value) end

--- Get key_repeat_timeout
--- @return integer The current value
function api.get_key_repeat_timeout() end

--- Set key_repeat_timeout. Returns the new value.
--- @param new_value integer Time in milliseconds before pending keybinds time out
--- @return integer The value after the update
function api.set_key_repeat_timeout(new_value) end

--- Get view
--- @return integer The current value
function api.get_view() end

--- Set view. Returns the new value.
--- @param new_value integer Bitmask of the currently visible tags.
--- @return integer The value after the update
function api.set_view(new_value) end

--- Get display_damage
--- @return boolean The current value
function api.get_display_damage() end

--- Set display_damage. Returns the new value.
--- @param new_value boolean Enable damage tracking when the screen is updated. (debugging feature)
--- @return boolean The value after the update
function api.set_display_damage(new_value) end

