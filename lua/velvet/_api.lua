error("Cannot require meta file")
---@meta
---@class velvet.api
local api = {}

---@class velvet.api.spawn.Opts
--- @field left? integer Leftmost column of the new window.
--- @field top? integer Topmost row of the new window.
--- @field width? integer Width of the new window.
--- @field height? integer Height of the new window.
--- @field layer? string The initial layer of the window. FLOATING|TILED

--- Spawn a process in a new window. Returns the window ID.
---@param cmd string The process to spawn.
---@param opts? velvet.api.spawn.Opts
function api.spawn(cmd, opts) end

---@class velvet.api.get_tags.Opts
--- @field winid? integer Window ID

--- Set the tags of the specified window.
---@param opts? velvet.api.get_tags.Opts
function api.get_tags(opts) end

---@class velvet.api.set_tags.Opts
--- @field winid? integer Window ID

--- Set the tags of the specified window.
---@param tags integer Bitmask of tags where <winid> should be shown.
---@param opts? velvet.api.set_tags.Opts
function api.set_tags(tags, opts) end

--- Get key_repeat_timeout
function api.get_key_repeat_timeout() end

--- Set key_repeat_timeout. Returns the new value.
---@param new_value integer Time in milliseconds before pending keybinds time out
function api.set_key_repeat_timeout(new_value) end

--- Get view
function api.get_view() end

--- Set view. Returns the new value.
---@param new_value integer Bitmask of the currently visible tags.
function api.set_view(new_value) end

