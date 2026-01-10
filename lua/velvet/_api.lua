error("Cannot require meta file")
---@meta
---@class velvet.api
local api = {}
--- Spawn a process in a new window. Returns the window ID.
---@param cmd string The process to spawn.
function api.spawn(cmd) end
--- Set the tags of the specified window.
function api.get_tags() end
--- Set the tags of the specified window.
---@param tags integer Bitmask of tags where <winid> should be shown.
function api.set_tags(tags) end
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

