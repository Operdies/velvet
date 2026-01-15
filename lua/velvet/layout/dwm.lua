local dwm = {}
local vv = require('velvet')

local r_left = 0
local r_top = 0
local r_bottom = 0
local r_right = 0

local function win_stack(left, top, width, height, lst)
  local offset = top
  for i, id in ipairs(lst) do
    local height_left = height - offset
    local num_items_left = 1 + #lst - i
    local win_height = math.floor(height_left / num_items_left)
    if win_height < 3 then win_height = 3 end
    local geom = { width = width, height = win_height, left = left, top = offset }
    vv.api.window_set_geometry(id, geom)
    geom = vv.api.window_get_geometry(id)
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

local function visible(win)
  local win_tags = tags[win] or {}
  for i, v in ipairs(view) do
    if v and win_tags[i] then return true end
  end
end

local nmaster = 1
local function arrange()
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
        vv.api.window_set_z_index(id, 1)
        if #master < nmaster then
          table.insert(master, id)
        else
          table.insert(stack, id)
        end
      end
    else
      vv.api.window_set_z_index(id, 2)
    end
  end

  local master_width = #stack > 0 and math.floor(term.width / 2) or term.width

  local left = r_left
  local top = r_top
  win_stack(left, top, master_width, term.height, master)
  if #stack > 0 then
    win_stack(master_width + left, top, term.width - master_width, term.height, stack)
  end
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
  arrange()
end

function dwm.toggle_view(num)
  view[num] = (view[num] and false) or true
  arrange()
end

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
  tags[win][tag] = tags[win][tag] and false or true
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
  for _, id in ipairs(lst) do
    table.insert(windows, id)
    tags[id] = view
  end
  local grp = e.create_group(vv.arrange_group_name, true)
  e.subscribe(grp, e.screen.resized, arrange)
  e.subscribe(grp, e.window.created, add_window)
  e.subscribe(grp, e.window.removed, remove_window)
  arrange()
end

function dwm.set_layer(win, layer)
  layers[win] = layer
  arrange()
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
