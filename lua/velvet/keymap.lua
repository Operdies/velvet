--- @class velvet.keymap
local keymap = {}

--- @class keymap.set.Opts
--- @field repeatable? boolean
--- The mapping can be retriggered by repeating the last key within the period defined by key_repeat_timeout


--- Creates a mapping of |keys| to the function |action|.
--- @param keys string Left hand side of the mapping
--- @param action function Right hand side of the mapping
--- @param opts? keymap.set.Opts
function keymap.set(keys, action, opts)
end

function keymap.del(keys, opts)
end

return keymap
