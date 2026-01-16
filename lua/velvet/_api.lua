error("Cannot require meta file")
--- @meta
--- @class velvet.api
local api = {}

--- @class velvet.api.window.geometry
--- @field left integer The leftmost cell of the window.
--- @field top integer The topmost cell of the window.
--- @field width integer The width of the window
--- @field height integer The height of the window

--- @class velvet.api.terminal.geometry
--- @field width integer The width of the terminal
--- @field height integer The height of the terminal

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

--- Schedule |function| to run after at least |delay| ms
--- @param delay integer delay in miliseconds
--- @param func function function to run
--- @return nil 
function api.schedule_after(delay, func) end

--- Get the number of milliseconds elapsed since startup
--- @return integer milliseconds elapsed since startup
function api.get_current_tick() end

--- Get the IDs of all sessions.
--- @return integer[] List of session IDs
function api.get_sessions() end

--- Get the ID of the active session
--- @param session_id integer Session ID
--- @return nil 
function api.set_active_session(session_id) end

--- Get the ID of the active session
--- @return integer Session ID
function api.get_active_session() end

--- Detach |session| session from the server.
--- @param session_id integer Session ID
--- @return nil 
function api.session_detach(session_id) end

--- Kill the velvet instance. All child processes will be killed.
--- @return nil 
function api.server_kill() end

--- Get the IDs of all windows.
--- @return integer[] List of window IDs
function api.get_windows() end

--- Swap two windows. This affects the layout of tiled windows.
--- @param first integer Window ID
--- @param second integer Window ID
--- @return nil 
function api.swap_windows(first, second) end

--- Set the z index of |win| to |z|
--- @param win integer Window ID
--- @param z integer New z index of |win|
--- @return nil 
function api.window_set_z_index(win, z) end

--- Get the z index of |win|
--- @param win integer Window ID
--- @return integer The z index of |win|
function api.window_get_z_index(win) end

--- Set window hidden flag. A hidden window will not be rendered.
--- @param win_id integer Window ID
--- @param hidden boolean New hidden state of |win_id|
--- @return nil 
function api.window_set_hidden(win_id, hidden) end

--- Get window hidden flag. A hidden window will not be rendered.
--- @param win_id integer Window ID
--- @return boolean Bool indicating if the window is hidden.
function api.window_get_hidden(win_id) end

--- Get window opacity
--- @param win integer Window ID
--- @return number The new window opacity.
function api.window_get_opacity(win) end

--- Set window opacity. The effect of this depends on the value of |window_get_transparency_mode|
--- @param win integer Window ID
--- @param opacity number The new window opacity.
--- @return nil 
function api.window_set_opacity(win, opacity) end

--- Get window transparency mode.
--- @param win integer Window ID
--- @return string Set transparency mode. Valid values are 'off', 'clear', 'all'
function api.window_get_transparency_mode(win) end

--- Set window transparency mode.
--- @param win integer Window ID
--- @param mode string Set transparency mode. Valid values are 'off', 'clear', 'all'
--- @return nil 
function api.window_set_transparency_mode(win, mode) end

--- Get the ID of the currently focused window.
--- @return integer TThe ID of the currently focused window.
function api.get_focused_window() end

--- Focus the window with id |win_id|
--- @param win_id integer Window ID
--- @return nil 
function api.set_focused_window(win_id) end

--- Get the geometry of the specified window.
--- @param win_id integer Window ID
--- @return velvet.api.window.geometry 
function api.window_get_geometry(win_id) end

--- Set the geometry of the specified window.
--- @param win_id integer Window ID
--- @param geometry velvet.api.window.geometry Window geometry
--- @return nil 
function api.window_set_geometry(win_id, geometry) end

--- Close the specified window, killing the associated process.
--- @param win_id integer The window to close
--- @return nil 
function api.window_close(win_id) end

--- Get the title of the window with id |win_id|
--- @param win_id integer Window ID
--- @return string Window title
function api.window_get_title(win_id) end

--- Set the title of the window with id |win_id|
--- @param win_id integer Window ID
--- @param title string New title
--- @return nil 
function api.window_set_title(win_id, title) end

--- Send |keys| to the window with id |win_id|. Unlike |window_send_text|, keys such as <C-x> will be encoded .
--- @param win_id integer The window receiving the keys
--- @param keys string The keys to send
--- @return nil 
function api.window_send_keys(win_id, keys) end

--- Send |text| to the window with id |win_id|
--- @param win_id integer The window receiving the text
--- @param text string The text to send
--- @return nil 
function api.window_send_text(win_id, text) end

--- Create a new window with the process |cmd|, executed with 'sh -c'. Returns the window ID.
--- @param cmd string The process to spawn.
--- @return integer The ID of the new window
function api.window_create_process(cmd) end

--- Returns true if a window exists with id |win_id|.
--- @param win_id integer Window ID
--- @return boolean Bool indicating whether the window ID is valid.
function api.window_is_valid(win_id) end

--- Get focus_follows_mouse
--- @return boolean The current value
function api.get_focus_follows_mouse() end

--- Set focus_follows_mouse. Returns the new value.
--- @param new_value boolean Automatically focus a window when the mouse moves over it.
--- @return boolean The value after the update
function api.set_focus_follows_mouse(new_value) end

--- Get key_repeat_timeout
--- @return integer The current value
function api.get_key_repeat_timeout() end

--- Set key_repeat_timeout. Returns the new value.
--- @param new_value integer Time in milliseconds before pending keybinds time out
--- @return integer The value after the update
function api.set_key_repeat_timeout(new_value) end

--- Get display_damage
--- @return boolean The current value
function api.get_display_damage() end

--- Set display_damage. Returns the new value.
--- @param new_value boolean Enable damage tracking when the screen is updated. (debugging feature)
--- @return boolean The value after the update
function api.set_display_damage(new_value) end

