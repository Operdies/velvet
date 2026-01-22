error("Cannot require meta file")
--- @meta
--- @class velvet.options
local options = {}
--- Automatically focus a window when the mouse moves over it.
--- @type boolean
options.focus_follows_mouse = true

--- Time in milliseconds before pending keybinds time out
--- @type integer
options.key_repeat_timeout = 500

--- Enable damage tracking when the screen is updated. (debugging feature)
--- @type boolean
options.display_damage = false

--- The 16 numbered terminal colors.
--- @type velvet.api.theme
options.theme = {
  background = "#1e1e2e",
  black = "#45475a",
  blue = "#89b4fa",
  bright_black = "#585b70",
  bright_blue = "#89b4fa",
  bright_cyan = "#94e2d5",
  bright_green = "#a6e3a1",
  bright_magenta = "#f5c2e7",
  bright_red = "#f38ba8",
  bright_white = "#a6adc8",
  bright_yellow = "#f9e2af",
  cursor_background = "#f5e0dc",
  cursor_foreground = "#1e1e2e",
  cyan = "#94e2d5",
  foreground = "#cdd6f4",
  green = "#a6e3a1",
  magenta = "#f5c2e7",
  red = "#f38ba8",
  white = "#bac2de",
  yellow = "#f9e2af"
}

return options
