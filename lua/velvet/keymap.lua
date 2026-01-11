--- @class velvet.keymap
local keymap = {}

--- @class velvet.keymap.set.Opts
--- @field repeatable? boolean
--- The mapping can be retriggered by repeating the last key within the period defined by key_repeat_timeout


--- Creates a mapping of |keys| to the function |action|.
--- @param keys string Left hand side of the mapping
--- @param action function Right hand side of the mapping
--- @param opts? velvet.keymap.set.Opts
--- @return nil
function keymap.set(keys, action, opts)
end

--- @class velvet.keymap.del.Opts
--- The mapping can be retriggered by repeating the last key within the period defined by key_repeat_timeout


--- Deletes the mapping associated with |keys|
--- @param keys string The mapping to remove
--- @param opts? velvet.keymap.del.Opts
--- @return nil
function keymap.del(keys, opts)
end

return keymap
