local dwm = {}
local vv = require('velvet')

local r_left = 0
local r_top = 0
local r_bottom = 0
local r_right = 0

local function win_move(win, new_geom)
  vv.api.window_set_geometry(win, new_geom)
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
  return layers[win] == nil or layers[win] == 'tiled'
end

-- arbitrarily decide tiled windows begin at z 10 and floating windows begin at z 100
local tiled_z = 10
local floating_z = 100

local nmaster = 1
local function arrange()
  local focused_id = vv.api.get_focused_window()
  if focused_id ~= nil and focused_id ~= get_focus() then
    -- if focus was changed outside of this module, update internal focus order tracking
    set_focus(focused_id)
  end

  local term = vv.api.get_terminal_geometry()
  term.width = term.width - (r_left + r_right)
  term.height = term.height - (r_top + r_bottom)

  local master = {}
  local stack = {}
  for _, id in ipairs(windows) do
    local vis = visible(id)
    vv.api.window_set_hidden(id, not vis)
    local floating = layers[id] == "floating"
    if floating then
      vv.api.window_set_transparency_mode(id, "all")
      vv.api.window_set_opacity(id, 0.5)
    else
      vv.api.window_set_transparency_mode(id, "clear")
      vv.api.window_set_opacity(id, 0.2)
    end
    if vis then
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
    if layers[id] == 'floating' then
      vv.api.window_set_z_index(id, floating_z + i)
    end
  end

  local master_width = #stack > 0 and math.floor(term.width / 2) or term.width

  local left = r_left
  local top = r_top
  win_stack(left, top, master_width, term.height, master)
  if #stack > 0 then
    win_stack(master_width + left, top, term.width - master_width, term.height, stack)
  end

  ensure_focus_visible()
end

local function add_window(win)
  table.insert(windows, 1, win)
  vv.api.set_focused_window(win)
  tags[win] = table.move(view, 1, #view, 1, {})
  arrange()
end

local function remove_window(win)
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
function dwm.activate()
  tags = {}
  windows = {}
  layers = {}
  view = get_tagsset()
  view[1] = true
  local lst = vv.api.get_windows()
  focus_order = table.move(lst, 1, #lst, 1, {})
  for _, id in ipairs(lst) do
    table.insert(windows, id)
    tags[id] = view
  end
  local grp = e.create_group(vv.arrange_group_name, true)
  e.subscribe(grp, e.screen.resized, arrange)
  e.subscribe(grp, e.window.created, add_window)
  e.subscribe(grp, e.window.removed, remove_window)
  if #windows > 0 then
    set_focus(lst[1])
  end
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
end

--- Focus the next visible window
function dwm.focus_next()
  local focus = vv.api.get_focused_window()
  local n = get_next_matching(focus, visible)
  if n then set_focus(n) end
end

--- Swap the focused window with the next visible tiled window
function dwm.swap_next() 
  local focus = vv.api.get_focused_window()
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
  local focus_index = table_index(windows, focus_id)
  local next_index = get_first_matching(windows, function(id) return id ~= focus_id and visible(id) and tiled(id) end)
  if next_index then
    local next_id = windows[next_index]
    table_swap(windows, focus_index, next_index)
    local new_focus = get_first_matching(windows, function(id) return id == focus_id or id == next_id end)
    set_focus(new_focus)
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

return dwm
