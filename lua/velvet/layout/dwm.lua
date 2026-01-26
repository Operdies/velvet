local dwm = { }
--- @enum dwm.layer
dwm.layers = {
  tiled = 1, floating = 2,
}

local vv = require('velvet')
local window = require('velvet.window')

local r_left = 0
local r_top = 0
local r_bottom = 0
local r_right = 0

local move_duration = 0
local single_monocle = true

--- @alias arrange string arrange mode
---| 'monocle' single window visible at a time
---|'tiled' dwm-style master-stack tiling

--- @type arrange
local layout_name = 'tiled'

--- @param win velvet.window
--- @param to velvet.api.window.geometry
local function win_move(win, to)
  local client_area = to
  if win:get_frame_enabled() then
    -- if this window has a frame, shrink the client area to accomodate it
    client_area = {
      left = to.left + 1,
      top = to.top + 1,
      width = to.width - 2,
      height = to.height - 2,
    }
  end
  if move_duration > 0 then
    local a = require('velvet.stdlib.animation')
    a.animate(win.id, client_area, move_duration, { easing_function = a.easing.spring, ms_per_frame = 10 })
  else
    win:set_geometry(client_area)
  end
end

--- @type velvet.window
local taskbar = nil
--- @param left integer leftmost column of stacking area
--- @param top integer topmost row of stacking area
--- @param width integer width of stacking area
--- @param height integer height of stacking area
--- @param lst velvet.window[] windows to stack
local function win_stack(left, top, width, height, lst)
  local offset = top
  for i, win in ipairs(lst) do
    local height_left = height - offset
    local num_items_left = 1 + #lst - i
    local win_height = math.floor(height_left / num_items_left)
    if win_height < 3 then win_height = 3 end
    local geom = { width = width, height = win_height, left = left, top = offset }
    win_move(win, geom)
    offset = offset + geom.height
  end
end

--- @return boolean[]
local function get_tagsset()
  local tags = {}
  for i = 1, 9 do
    tags[i] = false
  end
  return tags
end


--- @type velvet.window[]
local windows = {}
--- @type velvet.window[]
local focus_order = {}

local view = get_tagsset()
view[1] = true
local prev_view = view

--- @type table<velvet.window, boolean[]> 
local tags = {}

--- @type table<velvet.window, dwm.layer> 
local layers = {}

--- @return nil
local function table_swap(tbl, i1, i2)
  if i1 and i2 and i1 ~= i2 then
    tbl[i1], tbl[i2] = tbl[i2], tbl[i1]
  end
end

--- @param tbl velvet.window[]
--- @param val velvet.window
--- @return integer?
local function table_index(tbl, val)
  for i, v in ipairs(tbl) do
    if v == val then return i end
  end
  return nil
end

--- @param win velvet.window
--- @return boolean
local function visibleontags(win)
  local win_tags = tags[win] or {}
  for i, v in ipairs(view) do
    if v and win_tags[i] then return true end
  end
  return false
end

--- @param win velvet.window
local function set_focus(win)
  if vv.api.window_is_lua(vv.api.get_focused_window()) then return end
  if win == nil then return end
  if win == taskbar.id then return end
  local current_index = table_index(focus_order, win)
  if current_index ~= nil then table.remove(focus_order, current_index) end
  table.insert(focus_order, win)
  win:focus()
end

--- @return velvet.window?
local function get_focus()
  if #focus_order == 0 then return nil end
  return focus_order[#focus_order]
end

--- Set the focus to the most recently focused visible item
--- @return nil
local function ensure_focus_visible()
  for i=#focus_order,1,-1 do
    if visibleontags(focus_order[i]) then
      focus_order[i]:focus()
      local rem = table.remove(focus_order, i)
      table.insert(focus_order, rem)
      return
    end
  end
end

--- @param id velvet.window
local function tiled(id)
  return layers[id] == dwm.layers.tiled
end

-- arbitrarily decide where floating and tiled windows begin
local tiled_z = vv.layers.tiled
local floating_z = vv.layers.floating

local nmaster = 1
local mfact = 0.5
local dim_inactive = 0

--- @return velvet.window
local function create_status_window()
  taskbar = window.create()
  -- taskbar is below tiled windows, but the tiling
  -- area does not overlap with the taskar area.
  taskbar:set_z_index(floating_z - 1)
  taskbar:set_cursor_visible(false)
  taskbar:set_line_wrapping(false)
  return taskbar
end

--- @param tag integer
--- @return boolean
local function tag_occupied(tag)
  for _, set in pairs(tags) do
    if set[tag] then return true end
  end
  return false
end

local show_status = true

--- @param layout arrange
--- @return nil
local function status_update(layout)
  taskbar:set_visibility(show_status)
  if not show_status then return end
  local sz = vv.api.get_screen_geometry()
  taskbar:set_geometry({ left = 0, top = sz.height - 1, width = sz.width, height = 1 })
  taskbar:clear_background_color()
  taskbar:set_transparency_mode('clear')
  taskbar:set_opacity(0)
  taskbar:clear()
  taskbar:set_foreground_color('black')
  taskbar:set_cursor(3, 1)

  local views = {}
  for i=1,9 do 
    if view[i] or tag_occupied(i) then
      views[#views+1] = i
      taskbar:set_background_color(view[i] and 'red' or 'blue')
      taskbar:draw((" %d "):format(i))
    end
  end

  if layout == 'monocle' then
    local focus = 0
    local out_of = 0
    local fid = vv.api.get_focused_window()
    for _, win in ipairs(windows) do
      if visibleontags(win) then out_of = out_of + 1 end
      if win.id == fid then focus = out_of end
    end
    local segment = (' %d of %d '):format(focus, out_of)
    taskbar:set_cursor(sz.width // 2 - #segment // 2, 1)
    taskbar:set_background_color('red')
    taskbar:draw(segment)
  elseif layout == 'tiled' then
    local visible = 0
    local out_of = #windows
    for _, win in ipairs(windows) do
      if visibleontags(win) then visible = visible + 1 end
    end
    local segment = (' %d of %d shown '):format(visible, out_of)
    taskbar:set_cursor(sz.width // 2 - #segment // 2, 1)
    taskbar:set_background_color('red')
    taskbar:draw(segment)
  end

  local function view_mouse_hit(_, args)
    if args.mouse_button == 'left' then
      local col = args.pos.col
      local hit = col // 3
      if hit >= 1 and hit <= #views then
        dwm.set_view(views[hit])
      end
    end
  end
  taskbar:on_mouse_move(view_mouse_hit)
  taskbar:on_mouse_click(view_mouse_hit)
end

local function monocle()
  local ok, err = pcall(status_update, 'monocle')
  if not ok then dbg({ status_update = err }) end
  local term = vv.api.get_screen_geometry()

  local focused_id = vv.api.get_focused_window()
  -- don't steal focus from lua windows since they are not managed here
  if not vv.api.window_is_lua(focused_id) then
    if focused_id ~= nil and focused_id ~= get_focus() and focused_id ~= 0 then
      -- if focus was changed outside of this module, update internal focus order tracking
      set_focus(window.from_handle(focused_id))
    end
  end
  term.width = term.width - (r_left + r_right)
  term.height = term.height - (r_top + r_bottom)
  focused_id = vv.api.get_focused_window()
  local focus_is_lua = vv.api.window_is_lua(focused_id)
  local first = true

  for _, win in ipairs(windows) do
    if visibleontags(win) and (win.id == focused_id or (focus_is_lua and first)) then
      first = false
      win:set_visibility(true)
      win:set_dimming(0)
      win:set_opacity(0.8)
      win:set_frame_color('red')
      win:set_z_index(tiled_z)
      win_stack(r_left - 1, r_top - 1, term.width + 2, term.height, { win })
    else
      win:set_visibility(false)
    end
  end

  ensure_focus_visible()
end

local function tile()
  local num_visible = 0
  for _, win in ipairs(windows) do
    if visibleontags(win) then num_visible = num_visible + 1 end
    if num_visible > 1 then break end
  end
  if num_visible == 1 and single_monocle then
    monocle()
    return
  end

  local ok, err = pcall(status_update, 'tiled')
  if not ok then dbg({ status_update = err }) end
  local term = vv.api.get_screen_geometry()

  local focused_id = vv.api.get_focused_window()
  -- don't steal focus from lua windows since they are not managed here
  if not vv.api.window_is_lua(focused_id) then
    if focused_id ~= nil and focused_id ~= get_focus() and focused_id ~= 0 then
      -- if focus was changed outside of this module, update internal focus order tracking
      set_focus(window.from_handle(focused_id))
    end
  end

  term.width = term.width - (r_left + r_right)
  term.height = term.height - (r_top + r_bottom)

  focused_id = vv.api.get_focused_window()
  local master = {}
  local stack = {}
  for _, win in ipairs(windows) do
    local vis = visibleontags(win)
    win:set_visibility(vis)
    local floating = layers[win] == dwm.layers.floating
    if floating then
      win:set_transparency_mode('all')
      win:set_opacity(0.8)
    else
      win:set_transparency_mode('all')
      win:set_opacity(0.8)
    end
    if vis then
      if win.id == focused_id then
        win:set_dimming(0)
        win:set_frame_color('red')
      else
        win:set_dimming(dim_inactive)
        win:set_frame_color('blue')
      end
      if not floating then
        win:set_z_index(tiled_z)
        if #master < nmaster then
          table.insert(master, win)
        else
          table.insert(stack, win)
        end
      end
    end
  end

  for i, win in ipairs(focus_order) do
    local z = tiled(win) and (tiled_z + i) or (floating_z + i)
    win:set_z_index(z)
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
end

local function arrange()
  if layout_name == 'monocle' then monocle() else tile() end
end

local function add_window(id, init)
  if vv.api.window_is_lua(id) then return end
  local win = window.from_handle(id)
  win:set_frame_enabled(true)
  layers[win] = dwm.layers.tiled
  table.insert(windows, win)
  table.insert(focus_order, 1, win)
  tags[win] = table.move(view, 1, #view, 1, {})
  if not init then
    arrange()
  end
end

local function remove_window()
  for i, win in ipairs(windows) do
    if not win:valid() then table.remove(windows, i) end
  end
  for i, win in ipairs(focus_order) do
    if not win:valid() then table.remove(focus_order, i) end
  end
  for win, _ in pairs(layers) do
    if not win:valid() then layers[win] = nil end
  end
  for win, _ in pairs(tags) do
    if not win:valid() then tags[win] = nil end
  end
  arrange()
end


--- Toggle visibility of workspace #num. Multiple workspaces can be visible
function dwm.toggle_view(num)
  prev_view = table.move(view, 1, #view, 1, {})
  if view[num] then view[num] = false else view[num] = true end
  arrange()
end

--- Set the currently visible workspaces to |view_tags| (table) or { view_tags } (integer)
--- @param view_tags integer|integer[]
function dwm.set_view(view_tags)
  prev_view = table.move(view, 1, #view, 1, {})
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

function dwm.select_previous_view()
  local new_view = table.move(prev_view, 1, #prev_view, 1, {})
  prev_view = view
  view = new_view
  arrange()
end

--- Set tag |win_tags| on window |win|.
function dwm.set_tags(id, win_tags)
  local win = window.from_handle(id)
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

function dwm.toggle_tag(id, tag)
  local win = window.from_handle(id)
  if tags[win][tag] then tags[win][tag] = false else tags[win][tag] = true end
  arrange()
end

local e = vv.events
function dwm.activate()
  tags = {}
  windows = {}
  layers = {}
  view = get_tagsset()
  view[1] = true
  local lst = vv.api.get_windows()
  focus_order = {}
  for _, id in ipairs(lst) do
    focus_order[#focus_order + 1] = window.from_handle(id)
    add_window(id, true)
  end
  local event_handler = e.create_group(vv.arrange_group_name, true)
  taskbar = create_status_window()
  -- dwm.reserve(0, 0, 1, 0)
  dwm.reserve(0, 0, 0, 0)
  event_handler.screen_resized = arrange
  event_handler.window_created = function(args) add_window(args.win_id, false) end
  event_handler.window_closed = function(args) 
    remove_window() 
  end
  event_handler.window_focus_changed = function(args) 
    if vv.api.window_is_lua(args.new_focus) then return end
    arrange() 
  end
  if #windows > 0 then
    local f = window.from_handle(lst[1])
    set_focus(f)
  end
  arrange()
end

--- @param v number
--- @param lo number
--- @param hi number
--- @return number
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

function dwm.set_layer(id, layer)
  local win = window.from_handle(id)
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

local function get_next_matching(win, match)
  local pivot = table_index(windows, win)
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
  local fid = vv.api.get_focused_window()
  local focus = window.from_handle(fid)
  local p = get_prev_matching(focus, visibleontags)
  if p then set_focus(p) end
  arrange()
end

--- Focus the next visible window
function dwm.focus_next()
  local fid = vv.api.get_focused_window()
  local focus = window.from_handle(fid)
  local n = get_next_matching(focus, visibleontags)
  if n then set_focus(n) end
  arrange()
end

--- Swap the focused window with the next visible tiled window
function dwm.swap_next() 
  local fid = vv.api.get_focused_window()
  local focus = window.from_handle(fid)
  if not tiled(focus) then return end
  local n = get_next_matching(focus, function(id) return visibleontags(id) and tiled(id) end)
  if n and n ~= focus then
    local i1 = table_index(windows, focus)
    local i2 = table_index(windows, n)
    table_swap(windows, i1, i2)
    arrange()
  end
end

--- Swap the focused window with the previous visible tiled window
function dwm.swap_prev() 
  local fid = vv.api.get_focused_window()
  local focus = window.from_handle(fid)
  if not tiled(focus) then return end
  local n = get_prev_matching(focus, function(id) return visibleontags(id) and tiled(id) end)
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
  local focus = window.from_handle(focus_id)
  local focus_index = table_index(windows, focus)
  local next_index = get_first_matching(windows, function(win) return win ~= focus and visibleontags(win) and tiled(win) end)
  if next_index then
    local next_win = windows[next_index]
    table_swap(windows, focus_index, next_index)
    local new_focus = get_first_matching(windows, function(win) return win == focus or win == next_win end)
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

--- @param win? integer window id
function dwm.toggle_border(win)
  win = win or vv.api.get_focused_window()
  local w = window.from_handle(win)
  w:set_frame_enabled(not w:get_frame_enabled())
  arrange()
end

--- @param mode arrange arrange mode
function dwm.set_arrange(mode)
  layout_name = mode
  arrange()
end

function dwm.toggle_arrange()
  if layout_name == 'monocle' then dwm.set_arrange('tiled') else dwm.set_arrange('monocle') end
end

--- @param mode? boolean if true, show the status bar. Otherwise hide it
function dwm.set_status(mode)
  show_status = mode or not show_status
  -- dwm.reserve(0, 0, show_status and 1 or 0, 0)
  arrange()
end

--- @param vis velvet.window
function dwm.make_visible(vis)
  if tags[vis] then
    if not visibleontags(window) then
      local tbl = {}
      for i, b in ipairs(tags[vis]) do
        if b then table.insert(tbl, i) end
      end
      dwm.set_view(tbl)
      arrange()
    end
  end
end

return dwm
