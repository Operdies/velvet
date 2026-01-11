error("Cannot require meta file")
--- @meta
--- @class velvet.api
local api = {}

--- @class velvet.api.spawn.Opts
--- @field left? integer Leftmost column of the new window.
--- @field top? integer Topmost row of the new window.
--- @field width? integer Width of the new window.
--- @field height? integer Height of the new window.
--- @field layer? string The initial layer of the window. FLOATING|TILED

--- Spawn a process in a new window. Returns the window ID.
--- @param cmd string Spawn a process in a new window. Returns the window ID.
--- 
--- @param opts? velvet.api.spawn.Opts
--- @return integer The ID of the new window
function api.spawn(cmd, opts) end

--- @class velvet.api.get_tags.Opts
--- @field winid? integer Window ID

--- Set the tags of the specified window.
--- @param opts? velvet.api.get_tags.Opts
--- @return integer The tags of the specified window.
function api.get_tags(opts) end

--- @class velvet.api.set_tags.Opts
--- @field winid? integer Window ID

--- Set the tags of the specified window.
--- @param tags integer Set the tags of the specified window.
--- 
--- @param opts? velvet.api.set_tags.Opts
--- @return integer The tags of the window after the set operation.
function api.set_tags(tags, opts) end

--- @class velvet.api.detach.Opts
--- @field session_id? integer Session ID

--- Detach the current session from the server.
--- @param opts? velvet.api.detach.Opts
--- @return nil 
function api.detach(opts) end

--- @class velvet.api.close_window.Opts
--- @field winid? integer The window to close (default: current window)
--- @field force? boolean Forcefully close the window. (default: false)

--- Close the specified window, killing the associated process.
--- @param opts? velvet.api.close_window.Opts
--- @return nil 
function api.close_window(opts) end

--- Get key_repeat_timeout
--- @return integer The current value
function api.get_key_repeat_timeout() end

--- Set key_repeat_timeout. Returns the new value.
--- @param new_value integer Set key_repeat_timeout. Returns the new value.
--- 
--- @return integer The value after the update
function api.set_key_repeat_timeout(new_value) end

--- Get view
--- @return integer The current value
function api.get_view() end

--- Set view. Returns the new value.
--- @param new_value integer Set view. Returns the new value.
--- 
--- @return integer The value after the update
function api.set_view(new_value) end

