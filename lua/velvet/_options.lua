error("Cannot require meta file")
--- @meta
--- @class velvet.options
local options = {}
--- Time in milliseconds before pending keybinds time out
--- @type integer
options.key_repeat_timeout = 500

--- Bitmask of the currently visible tags.
--- @type integer
options.view = 1

--- Enable damage tracking when the screen is updated. (debugging feature)
--- @type boolean
options.display_damage = false

return options
