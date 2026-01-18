local dwm = {
  layers = {
    tiled = 1, floating = 2,
  },
}
local vv = require('velvet')

--- @type velvet.window
local taskbar = nil
local r_left = 0
local r_top = 0
local r_bottom = 0
local r_right = 0

local move_duration = 0
local function win_move(win, to)
  local client_area = to
  if move_duration > 0 then
    local a = require('velvet.stdlib.animation')
    a.animate(win, client_area, move_duration, { easing_function = a.easing.spring, ms_per_frame = 10 })
  else
    vv.api.window_set_geometry(win, client_area)
  end
end

local function win_stack(left, top, width, height, lst)
  local offset = top
  for i, id in ipairs(lst) do
    local height_left = height - offset
    local num_items_left = 1 + #lst - i
    local win_height = math.floor(height_left / num_items_left)
    if win_height < 3 then win_height = 3 end
    local geom = { width = width, height = win_height, left = left, top = offset }
    win_move(id, geom)
    offset = offset + geom.height
  end
end

local function get_tagsset()
  local tags = {}
  for i = 1, 9 do
    tags[i] = false
  end
  return tags
end


local windows = {}
local view = get_tagsset()
view[1] = true
local tags = {}
local layers = {}
local focus_order = {}

local function table_swap(tbl, i1, i2)
  if i1 and i2 and i1 ~= i2 then
    tbl[i1], tbl[i2] = tbl[i2], tbl[i1]
  end
end

local function table_index(tbl, val)
  for i, v in ipairs(tbl) do
    if v == val then return i end
  end
  return nil
end

local function visible(win)
  local win_tags = tags[win] or {}
  for i, v in ipairs(view) do
    if v and win_tags[i] then return true end
  end
end

local function set_focus(win)
  if win == 0 then return end
  if win == taskbar.id then return end
  local current_index = table_index(focus_order, win)
  if current_index ~= nil then table.remove(focus_order, current_index) end
  table.insert(focus_order, win)
  vv.api.set_focused_window(win)
end

local function get_focus()
  if #focus_order == 0 then return nil end
  return focus_order[#focus_order]
end

-- Set the focus to the most recently focused visible item
local function ensure_focus_visible()
  for i=#focus_order,1,-1 do
    if visible(focus_order[i]) then
      vv.api.set_focused_window(focus_order[i])
      local rem = table.remove(focus_order, i)
      table.insert(focus_order, rem)
      return
    end
  end
end

local function tiled(win)
  return layers[win] == dwm.layers.tiled
end

-- arbitrarily decide where floating and tiled windows begin
local tiled_z = -1000
local floating_z = -100

local nmaster = 1
local mfact = 0.5
local dim_inactive = 0

local function status_update()
  local sz = vv.api.get_screen_geometry()
  taskbar:set_cursor_visible(false)
  taskbar:set_line_wrapping(false)
  taskbar:set_geometry({ left = 0, top = sz.height - 1, width = sz.width, height = 1 })
  taskbar:clear_background_color()
  taskbar:clear()
  taskbar:set_foreground_color('black')
  taskbar:set_cursor(1, 1)

  for i=1,9 do 
    taskbar:set_background_color(view[i] and 'red' or 'blue')
    taskbar:draw((" %d "):format(i))
  end

  local function view_mouse_hit(_, args)
    if args.mouse_button == vv.api.mouse_button.left then
      local col = args.pos.col
      local hit = 1 + ((col - 1) // 3)
      if hit >= 1 and hit <= 9 then
        dwm.set_view(hit)
      end
    end
  end
  taskbar:on_mouse_move(view_mouse_hit)
  taskbar:on_mouse_click(view_mouse_hit)
end

local function arrange2()
  local term = vv.api.get_screen_geometry()

  local focused_id = vv.api.get_focused_window()
  if vv.api.window_is_lua(focused_id) then return end
  if focused_id ~= nil and focused_id ~= get_focus() then
    -- if focus was changed outside of this module, update internal focus order tracking
    set_focus(focused_id)
  end

  term.width = term.width - (r_left + r_right)
  term.height = term.height - (r_top + r_bottom)

  focused_id = vv.api.get_focused_window()
  local master = {}
  local stack = {}
  for _, id in ipairs(windows) do
    local vis = visible(id)
    vv.api.window_set_hidden(id, not vis)
    local floating = layers[id] == dwm.layers.floating
    local t = vv.api.transparency_mode
    if floating then
      vv.api.window_set_transparency_mode(id, t.all)
      vv.api.window_set_opacity(id, 0.8)
    else
      vv.api.window_set_transparency_mode(id, t.clear)
      vv.api.window_set_opacity(id, 0.8)
    end
    if vis then
      if id == focused_id then
        vv.api.window_set_dim_factor(id, 0)
      else
        vv.api.window_set_dim_factor(id, dim_inactive)
      end
      if not floating then
        vv.api.window_set_z_index(id, tiled_z)
        if #master < nmaster then
          table.insert(master, id)
        else
          table.insert(stack, id)
        end
      end
    end
  end

  for i, id in ipairs(focus_order) do
    local z = tiled(id) and (tiled_z + i) or (floating_z + i)
    vv.api.window_set_z_index(id, z)
  end

  local master_width = #stack > 0 and math.floor(term.width * mfact) or term.width
  if #master == 0 then master_width = 0 end

  local left = r_left
  local top = r_top
  win_stack(left, top, master_width, term.height, master)
  if #stack > 0 then
    win_stack(master_width + left, top, term.width - master_width, term.height, stack)
  end

  ensure_focus_visible()
  status_update()
end

local function arrange()
  local ok, err = pcall(arrange2)
  if not ok then dbg({ arrange_error = err }) end
end

local function add_window(win, init)
  if vv.api.window_is_lua(win) then return end
  layers[win] = dwm.layers.tiled
  table.insert(windows, 1, win)
  tags[win] = table.move(view, 1, #view, 1, {})
  if not init then
    set_focus(win)
    arrange()
  end
end

local function remove_window(win)
  layers[win] = nil
  for i, id in ipairs(windows) do
    if id == win then
      table.remove(windows, i)
      tags[id] = nil
      break
    end
  end
  for i, id in ipairs(focus_order) do
    if id == win then
      table.remove(focus_order, i)
      break
    end
  end
  arrange()
end

--- Toggle visibility of workspace #num. Multiple workspaces can be visible
function dwm.toggle_view(num)
  if view[num] then view[num] = false else view[num] = true end
  arrange()
end

--- Set the currently visible workspaces to |view_tags| (table) or { view_tags } (integer)
function dwm.set_view(view_tags)
  local new_view = get_tagsset()
  if type(view_tags) == 'table' then
    for _, t in ipairs(view_tags) do
      new_view[t] = true
    end
  else
    new_view[view_tags] = true
  end
  view = new_view
  arrange()
end

--- Set tag |win_tags| on window |win|.
function dwm.set_tags(win, win_tags)
  local tagset = get_tagsset()
  if type(win_tags) == 'table' then
    for _, t in ipairs(win_tags) do
      tagset[t] = true
    end
  else
    tagset[win_tags] = true
  end
  tags[win] = tagset
  arrange()
end

function dwm.toggle_tag(win, tag)
  if tags[win][tag] then tags[win][tag] = false else tags[win][tag] = true end
  arrange()
end

local e = vv.events
local window = require('velvet.window')
function dwm.activate()
  tags = {}
  windows = {}
  layers = {}
  view = get_tagsset()
  view[1] = true
  local lst = vv.api.get_windows()
  focus_order = table.move(lst, 1, #lst, 1, {})
  for _, id in ipairs(lst) do
    add_window(id, true)
  end
  local event_handler = e.create_group(vv.arrange_group_name, true)
  taskbar = window.create()
  -- taskbar is below tiled windows, but the tiling
  -- area does not overlap with the taskar area.
  taskbar:set_z_index(floating_z - 1)
  dwm.reserve(0, 0, 1, 0)
  event_handler.screen_resized = arrange
  event_handler.window_created = function(args) add_window(args.id, false) end
  event_handler.window_closed = function(args) remove_window(args.id) end
  event_handler.window_focus_changed = arrange
  if #windows > 0 then
    set_focus(lst[1])
  end
  arrange()
end

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

function dwm.incmfact(v)
  mfact = clamp(mfact + v, 0.10, 0.90)
  arrange()
end

function dwm.incnmaster(v)
  nmaster = clamp(nmaster + v, 0, 10)
  arrange()
end

function dwm.set_layer(win, layer)
  layers[win] = layer
  arrange()
end

local function get_prev_matching(id, match)
  local pivot = table_index(windows, id)
  if pivot == nil then
    pivot = 0
  else
    pivot = pivot - 1
  end

  for i = #windows - 1, 0, -1 do
    local index = 1 + ((i + pivot) % (#windows))
    if match(windows[index]) then return windows[index] end
  end
  return nil
end

local function get_next_matching(id, match)
  local pivot = table_index(windows, id)
  if pivot == nil then
    pivot = 0
  else
    pivot = pivot - 1
  end

  for i = 1, #windows do
    local index = 1 + ((i + pivot) % (#windows))
    if match(windows[index]) then return windows[index] end
  end
  return nil
end

local function get_first_matching(tbl, match)
  for i, id in ipairs(tbl) do 
    if match(id) then return i end
  end
  return nil
end



--- Focus the previous visible window
function dwm.focus_prev()
  local focus = vv.api.get_focused_window()
  local p = get_prev_matching(focus, visible)
  if p then set_focus(p) end
  arrange()
end

--- Focus the next visible window
function dwm.focus_next()
  local focus = vv.api.get_focused_window()
  local n = get_next_matching(focus, visible)
  if n then set_focus(n) end
  arrange()
end

--- Swap the focused window with the next visible tiled window
function dwm.swap_next() 
  local focus = vv.api.get_focused_window()
  if not tiled(focus) then return end
  local n = get_next_matching(focus, function(id) return visible(id) and tiled(id) end)
  if n and n ~= focus then
    local i1 = table_index(windows, focus)
    local i2 = table_index(windows, n)
    table_swap(windows, i1, i2)
    arrange()
  end
end

--- Swap the focused window with the previous visible tiled window
function dwm.swap_prev() 
  local focus = vv.api.get_focused_window()
  if not tiled(focus) then return end
  local n = get_prev_matching(focus, function(id) return visible(id) and tiled(id) end)
  if n and n ~= focus then
    local i1 = table_index(windows, focus)
    local i2 = table_index(windows, n)
    table_swap(windows, i1, i2)
    arrange()
  end
end

--- Swap the selected window with the first tiled window.
--- If the selected window is the first tiled window, swap with the next tiled window.
function dwm.zoom()
  local focus_id = vv.api.get_focused_window()
  if focus_id == 0 then return end
  local focus_index = table_index(windows, focus_id)
  local next_index = get_first_matching(windows, function(id) return id ~= focus_id and visible(id) and tiled(id) end)
  if next_index then
    local next_id = windows[next_index]
    table_swap(windows, focus_index, next_index)
    local new_focus = get_first_matching(windows, function(id) return id == focus_id or id == next_id end)
    if new_focus then set_focus(windows[new_focus]) end
    arrange()
  end
end

--- reserve `n` lines/columns from top,left,bottom,right
--- For example, if left=1, then the leftmost column of cells is considered reserved,
--- and will not be used in the tiling layout.
--- bottom=1 means the last line is reserved.
--- @param top integer
--- @param left integer
--- @param bottom integer
--- @param right integer
function dwm.reserve(top, left, bottom, right)
  r_top, r_left, r_bottom, r_right = top, left, bottom, right
  arrange()
end

function dwm.set_animation_duration(v, dur)
  move_duration = dur
end

function dwm.inc_inactive_dim(inc)
  dim_inactive = clamp(dim_inactive + inc, 0, 1)
  arrange()
end

function dwm.set_inactive_dim(v)
  dim_inactive = clamp(v, 0, 1)
  arrange()
end

return dwm
