error("Cannot require meta file")
--- @meta
--- @class velvet.options
local options = {}
--- The number of lines scrolled per scroll wheel tick.
--- @type integer
options.scrollback_scroll_multiplier = 3

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

--- Set the rendering fps target. Under load, velvet dispatches frames on a timer.
--- Note that this only affects frame scheduling behavior. When the system is not under load
--- velvet will attempt to dispatch dirty frames regardless of fps target immediately after flushing all pending IO.
--- @type integer
options.fps_target = "60"

return options
