--- @class dwm.options
--- @field focus_follows_mouse boolean if set, focus windows on mouseover

--- @alias dwm.layer
--- | 'tiled' window is managed in the tiling layer
--- | 'floating' window floats above tiled windows and is not managed
local dwm = {
  --- @type dwm.options
  options = {
    focus_follows_mouse = true,
  }
}

--- @param v number
--- @param lo number
--- @param hi number
--- @return number
local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function round(x)
  return math.floor(x + 0.5)
end


local window = require('velvet.window')
local rect = require('velvet.ui.rect')

local r_left = 0
local r_top = 0
local r_bottom = 0
local r_right = 0

local tiled_alpha = 0.8
local floating_alpha = 0.8

local move_duration = 0

--- @alias arrange string arrange mode
---|'tiled' dwm-style master-stack tiling

--- @type integer[]
local windows = {}

-- |state| is automatically saved and reloaded when user config is reloaded.
-- Note that it uses |integer| instead of |velvet.window| because |velvet.window| instances becomes stale after a reload.
-- And also because the restore logic cannot handle table keys.
local session_options = require('velvet.session_storage').create('velvet.dwm')
session_options.state = session_options.state or {
  --- @type table<integer, boolean[]>
  tags = {},
  --- @type table<integer, dwm.layer>
  layers = {},
  --- @type boolean[]
  view = { true, false, false, false, false, false, false, false, false },
  --- @type boolean[]
  prev_view = { true, false, false, false, false, false, false, false, false },
  --- @type integer[]
  focus_order = {},
  --- @type integer
  nmaster = 1,
  --- @type number
  mfact = 0.5,
}
local state = session_options.state

--- @param win velvet.window
--- @param to velvet.api.rect
local function win_move(win, to)
  local a = require('velvet.ui.animation')
  local client_area = to
  -- if this window has a frame, shrink the client area.
  if win:get_frame_enabled() then client_area = rect.inset(to, 1) end
  if move_duration > 0 then
    a.animate(win.id, client_area, move_duration, { easing_function = a.easing.spring, ms_per_frame = 10 })
  else
    win:set_geometry(client_area)
    a.cancel(win.id)
  end
end

--- @type velvet.window
local taskbar = nil

--- @param left integer leftmost column of stacking area
--- @param top integer topmost row of stacking area
--- @param width integer width of stacking area
--- @param height integer height of stacking area
--- @param count integer number of windows to stack
local function calc_win_stack(left, top, width, height, count)
  local geoms = {}
  local offset = top
  for i = 1, count do
    local height_left = 1 + height - offset
    local num_items_left = 1 + count - i
    local win_height = height_left // num_items_left
    if win_height < 3 then win_height = 3 end
    local geom = { width = width, height = win_height, left = left, top = offset }
    offset = offset + geom.height
    geoms[#geoms + 1] = geom
  end
  return geoms
end

--- @param geom velvet.api.rect geometry
--- @param lst velvet.window[] windows to stack
local function win_stack(geom, lst)
  local geoms = calc_win_stack(geom.left, geom.top, geom.width, geom.height, #lst)
  for i, win in ipairs(lst) do
    win_move(win, geoms[i])
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

--- @generic TKey
--- @param tbl table<TKey, any>
--- @param i1 TKey
--- @param i2 TKey
local function table_swap(tbl, i1, i2)
  if i1 and i2 and i1 ~= i2 then
    tbl[i1], tbl[i2] = tbl[i2], tbl[i1]
  end
end

--- @generic T
--- @param tbl T[]
--- @param val T
--- @return integer?
local function table_index(tbl, val)
  for i, v in ipairs(tbl) do
    if v == val then return i end
  end
end

--- @param id integer
--- @return boolean
local function visibleontags(id)
  local win_tags = state.tags[id] or {}
  for i, v in ipairs(state.view) do
    if v and win_tags[i] then return true end
  end
  return false
end

local function ignore_window(win)
  if type(win) == 'table' then
    win = win.id
  end
  if not vv.api.window_is_valid(win) then return false end
  return vv.api.window_is_lua(win) or vv.api.window_get_parent(win) > 0
end

--- @param id integer
local function set_focus(id)
  local win = window.from_handle(id)
  if ignore_window(vv.api.get_focused_window()) then return end
  if win == nil then return end
  if win == taskbar.id then return end
  local current_index = table_index(state.focus_order, win.id)
  if current_index ~= nil then table.remove(state.focus_order, current_index) end
  table.insert(state.focus_order, win.id)
  win:focus()
end

--- @return velvet.window?
local function get_focus()
  for i = #state.focus_order, 1, -1 do
    local id = state.focus_order[i]
    if id and not vv.api.window_is_valid(id) then
      state.focus_order[i] = nil
    else
      return window.from_handle(id)
    end
  end
end

--- Set the focus to the most recently focused visible item
--- @return nil
local function ensure_focus_visible()
  for i = #state.focus_order, 1, -1 do
    local id = state.focus_order[i]
    if visibleontags(id) then
      local win = window.from_handle(id)
      win:focus()
      local rem = table.remove(state.focus_order, i)
      table.insert(state.focus_order, rem)
      return
    end
  end
end

--- @param id integer
local function tiled(id)
  return state.layers[id] == 'tiled'
end

-- arbitrarily decide where floating and tiled windows begin
local tiled_z = vv.z_hint.tiled
local floating_z = vv.z_hint.floating

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
  for _, set in pairs(state.tags) do
    if set[tag] then return true end
  end
  return false
end

local show_status = true
local km = require('velvet.keymap')

local chain = nil
--- @return nil
local function status_update()
  taskbar:set_visibility(show_status)
  if not show_status then return end
  local sz = vv.api.get_screen_geometry()
  local tgeom = { left = 1, top = sz.height, width = sz.width, height = 1 }
  taskbar:set_geometry(tgeom)
  taskbar:clear_background_color()
  taskbar:set_transparency_mode('clear')
  taskbar:set_alpha(0)
  taskbar:clear()
  taskbar:set_foreground_color('black')
  taskbar:set_cursor(3, 1)

  local on_click = {}

  for i = 1, 9 do
    if state.view[i] or tag_occupied(i) then
      taskbar:set_background_color(state.view[i] and 'red' or 'blue')
      local c1, c2 = taskbar:draw((" %d "):format(i))
      on_click[#on_click+1] = { c1.col, c2.col - 1, function() dwm.set_view(i) end }
    end
  end

  if chain and #chain > 0 then 
    local offset = math.max(15, #chain + 2)
    local lcol = taskbar:get_geometry().width - offset
    taskbar:set_cursor(lcol, 1)
    taskbar:clear_background_color()
    taskbar:set_background_color(vv.options.theme.background)
    taskbar:set_foreground_color('white')
    taskbar:draw(' ' .. chain .. ' ')
  end

  local passthrough = km.get_passthrough()
  if passthrough then
    local segment = " Direct Input "
    taskbar:set_foreground_color('black')
    taskbar:set_background_color('red')
    taskbar:set_cursor(sz.width // 2 - vv.api.string_display_width(segment) // 2, 1)
    local c1, c2 = taskbar:draw(segment)
    on_click[#on_click+1] = { c1.col, c2.col - 1, function() km.set_passthrough(false) end }
  end

  --- @param args velvet.api.mouse.move.event_args|velvet.api.mouse.click.event_args
  local function view_mouse_hit(_, args)
    if args.mouse_button == 'left' then
      local col = args.pos.col
      for _, clk in ipairs(on_click) do
        local lcol, rcol, fun  = table.unpack(clk)
        if col >= lcol and col <= rcol then 
          fun()
          return nil
        end
      end
    end
    -- bubble events up if the taskbar did not handle them -- this lets us click things below the transparent sections.
    return 'passthrough'
  end

  taskbar:on_mouse_move(view_mouse_hit)
  taskbar:on_mouse_click(view_mouse_hit)
end

local left_stack = {}
local right_stack = {}

local function tile()
  local num_visible = 0
  for _, win in ipairs(windows) do
    if visibleontags(win) then num_visible = num_visible + 1 end
    if num_visible > 1 then break end
  end

  status_update()
  local term = vv.api.get_screen_geometry()

  local focused_id = vv.api.get_focused_window()
  -- don't steal focus from lua windows since they are not managed here
  if not ignore_window(focused_id) then
    if focused_id ~= nil and focused_id ~= get_focus() and focused_id ~= 0 then
      -- if focus was changed outside of this module, update internal focus order tracking
      set_focus(focused_id)
    end
  end

  term.width = term.width - (r_left + r_right)
  term.height = term.height - (r_top + r_bottom)

  focused_id = vv.api.get_focused_window()
  left_stack = {}
  right_stack = {}
  for _, id in ipairs(windows) do
    local win = window.from_handle(id)
    local vis = visibleontags(id)
    win:set_visibility(vis)
    local floating = state.layers[win.id] == 'floating'
    if floating then
      win:set_transparency_mode('all')
      win:set_alpha(floating_alpha)
    else
      win:set_transparency_mode('all')
      win:set_alpha(tiled_alpha)
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
        if #left_stack < state.nmaster then
          table.insert(left_stack, win)
        else
          table.insert(right_stack, win)
        end
      end
    end
  end

  for i, id in ipairs(state.focus_order) do
    local win = window.from_handle(id)
    local z = tiled(id) and tiled_z or floating_z
    win:set_z_index(i + z)
  end

  local master_width = #right_stack > 0 and math.floor(term.width * state.mfact) or term.width
  if #left_stack == 0 then master_width = 0 end

  local left = 1 + r_left
  local top = 1 + r_top
  local left_geom = { left = left, top = top, width = master_width, height = term.height }
  if #left_stack == 1 and #right_stack == 0 then 
    left_geom.width = master_width + 2
    left_geom.left = left - 1
    left_geom.top = top - 1
  end
  win_stack(left_geom, left_stack)
  if #right_stack > 0 then
    local g = { left = master_width + left, top = top, width = term.width - master_width, height = term.height }
    if #left_stack == 0 and #right_stack == 1 then
      g.left = g.left - 1
      g.width = g.width + 2
      g.top = g.top - 1
    end
    win_stack(g, right_stack)
  end

  ensure_focus_visible()
end

local function arrange()
  tile()
end

local drop_hint = window.create()
drop_hint:set_visibility(false)

local function drop_or_show_hint(w, args)
  local sz = vv.api.get_screen_geometry()
  sz.width = sz.width - (r_left + r_right)
  sz.height = sz.height - (r_top + r_bottom)

  local lw = #right_stack > 0 and math.floor(sz.width * state.mfact) or sz.width
  if #left_stack == 0 then lw = 0 end
  local rw = sz.width - lw

  local left_bias = round(sz.width / 3)
  local right_bias = 2 * round(sz.width / 3)

  local function get_drop_location()
    local c = args.global_pos.col
    local side = nil
    if #left_stack > 0 and #right_stack > 0 then
      side = c <= lw and 'left' or 'right'
    elseif #left_stack == 0 or #right_stack == 0 then
      if c <= sz.width // 3 then
        side = 'left'
        state.nmaster = 0
        arrange()
      elseif c >= (2 * sz.width // 3) then
        side = 'right'
        state.nmaster = #left_stack + #right_stack
        arrange()
      else
        side = #left_stack > 0 and 'left' or 'right'
      end
    elseif #left_stack > 0 then
      side = args.global_pos.col <= right_bias and 'left' or 'right'
    elseif #right_stack > 0 then
      side = args.global_pos.col <= left_bias and 'left' or 'right'
    else
      side = 'left'
    end

    local left = side == 'left' and 1 or 1 + lw
    local width = side == 'left' and lw or rw
    if #left_stack == 0 and #right_stack == 0 then
      left = 1
      width = sz.width
    elseif side == 'right' and #right_stack == 0 then
      left = 1 + sz.width // 2
      width = 1 + sz.width - left
    elseif side == 'left' and #left_stack == 0 then
      left = 1
      width = sz.width // 2
    end

    local stack_count = 1 + (side == 'left' and #left_stack or #right_stack)
    local stack = calc_win_stack(left, 1 + r_top, width, sz.height, stack_count)
    local r = args.global_pos.row
    for i, geom in ipairs(stack) do
      if r >= geom.top and r <= geom.top + geom.height then 
        return side, i, geom
      end
    end
  end

  local side, index, geom = get_drop_location()

  local function show_drop_hint()
    drop_hint:set_geometry(geom)
    drop_hint:set_visibility(true)
    drop_hint:set_alpha(0.5)
    drop_hint:set_background_color('red')
    drop_hint:set_z_index(vv.z_hint.floating - 1)
    drop_hint:clear()
  end

  local function drop_window()
    local current = table_index(windows, w.id)
    if current then table.remove(windows, current) end
    if side == 'left' then
      state.nmaster = state.nmaster + 1
    elseif side == 'right' and state.nmaster > #left_stack then
      state.nmaster = #left_stack
    end
    local before = nil
    if side == 'left' then before = left_stack[index] or right_stack[1] 
    elseif side == 'right' then before = right_stack[index] or nil end
    local idx = before and table_index(windows, before.id) or (#windows + 1)
    table.insert(windows, idx, w.id)
    state.layers[w.id] = 'tiled'
  end

  if args.type == 'move_end' then
    drop_window()
  elseif args.type == 'move_continue' then
    show_drop_hint()
  end
end

local dragging = nil
local function add_window(id, init)
  if ignore_window(id) then return end
  local win = window.from_handle(id)
  win:set_frame_enabled(true)
  win.on_drag = function(w, args)
    drop_hint:set_visibility(false)
    if args.type == 'move_end' or args.type == 'resize_end' then 
      drop_hint:set_visibility(false)
      dragging = nil 
    end
    if args.type == 'move_end' and args.modifiers.alt then
      drop_or_show_hint(w, args)
      state.layers[w.id] = 'tiled'
      arrange()
    elseif args.type == 'move_continue' then
      if not dragging then
        dragging = w
        if table_index(left_stack, w) then 
          state.nmaster = clamp(state.nmaster - 1, 0, 10)
        end
        state.layers[w.id] = 'floating'
        w:set_z_index(vv.z_hint.overlay - 1)
        arrange()
      end
      if args.modifiers.alt then drop_or_show_hint(w, args) end
    elseif args.type == 'resize_continue' then
      dragging = w
      if state.layers[w.id] ~= 'floating' then
        state.layers[w.id] = 'floating'
        arrange()
      end
    elseif args.type == 'resize_end' then
      dragging = nil
    end
  end
  windows[#windows + 1] = win.id
  if not state.tags[win.id] then
    table.insert(state.focus_order, 1, win.id)
    state.layers[win.id] = 'tiled'
    state.tags[win.id] = table.move(state.view, 1, #state.view, 1, {})
    if not init then
      arrange()
    end
  end
end

function table.remove_if(t, pred)
  local j = 1
  for i = 1, #t do
    if not pred(t[i]) then
      t[j] = t[i]
      j = j + 1
    end
  end
  for i = j, #t do
    t[i] = nil
  end
end

function table.unset_if_key(t, pred)
  for k, _ in pairs(t) do
    if pred(k) then t[k] = nil end
  end
end

local function remove_window()
  local function win_invalid(id) return not vv.api.window_is_valid(id) end
  table.remove_if(windows, win_invalid)
  table.remove_if(state.focus_order, win_invalid)
  table.unset_if_key(state.layers, win_invalid)
  table.unset_if_key(state.tags, win_invalid)
  arrange()
end

local function set_view(new_view)
  for i = 1, 9 do
    if new_view[i] ~= state.view[i] then
      state.prev_view = table.move(state.view, 1, 9, 1, {})
      state.view = new_view
      arrange()
      return
    end
  end
end

--- Toggle visibility of workspace #num. Multiple workspaces can be visible
function dwm.toggle_view(num)
  local new_view = table.move(state.view, 1, #state.view, 1, {})
  if new_view[num] then new_view[num] = false else new_view[num] = true end
  set_view(new_view)
end

--- Set the currently visible workspaces to |view_tags| (table) or { view_tags } (integer)
--- @param view_tags integer|integer[]
function dwm.set_view(view_tags)
  local new_view = get_tagsset()
  if type(view_tags) == 'table' then
    for _, t in ipairs(view_tags) do
      new_view[t] = true
    end
  else
    new_view[view_tags] = true
  end
  for i = 1, 9 do
    if new_view[i] ~= state.view[i] then
      set_view(new_view)
      if dragging and not visibleontags(dragging.id) then state.tags[dragging.id] = vv.deepcopy(new_view) end
      arrange()
      return
    end
  end
end

function dwm.select_previous_view()
  set_view(state.prev_view)
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
  state.tags[win.id] = tagset
  arrange()
end

function dwm.toggle_tag(id, tag)
  local win = window.from_handle(id)
  if state.tags[win.id][tag] then state.tags[win.id][tag] = false else state.tags[win.id][tag] = true end
  arrange()
end

local activated = false
function dwm.activate()
  if not activated then
    activated = true
    local event_handler = vv.events.create_group('dwm.arrange', true)
    local lst = vv.api.get_windows()
    for _, id in ipairs(lst) do
      add_window(id, true)
    end
    taskbar = create_status_window()
    -- dwm.reserve(0, 0, 1, 0)
    dwm.reserve(0, 0, 0, 0)
    event_handler.screen_resized = arrange
    event_handler.window_created = function(args)
      if ignore_window(args.win_id) then return end
      add_window(args.win_id, false)
    end

    event_handler.window_closed = function(_) remove_window() end
    event_handler.window_focus_changed = function(args)
      if ignore_window(args.new_focus) then return end
      arrange()
    end
    arrange()
    event_handler[km.passthrough_changed] = status_update
    event_handler[km.chain_changed] = function(new_chain)
      chain = new_chain
      status_update()
    end

    local win_under_cursor = nil
    -- TODO: dwm must manage borders for this to work properly.
    -- Since window.lua now supports forwarding mouse events, the raw handler
    -- doesn't know if a mouse event should actually go to an overlay or a window/border.
    event_handler.mouse_move = function(args)
      if args.win_id == 0 then 
        dragging = nil
        return 
      end
      if dragging then return end
      local id = args.win_id
      if not dwm.options.focus_follows_mouse then return end
      local win = window.from_handle(id)
      if win.is_border then id = win.parent.id end
      -- don't keep setting focus if the cursor hasn't moved away from the window
      if id == win_under_cursor then return end
      win_under_cursor = id
      vv.api.set_focused_window(id)
    end
  end
end

--- Increase width of the left stack by |v|
--- @param v number delta
function dwm.incmfact(v)
  state.mfact = clamp(state.mfact + v, 0.10, 0.90)
  arrange()
end

--- Increase the number of windows in the left stack
--- @param v integer delta
function dwm.incnmaster(v)
  state.nmaster = clamp(state.nmaster + v, 0, 10)
  arrange()
end

--- set the layer of window |win| to |layer|
--- @param win integer|velvet.window window
--- @param layer dwm.layer new layer
function dwm.set_layer(win, layer)
  win = type(win) == 'number' and window.from_handle(win) or win
  state.layers[win.id] = layer
  arrange()
end

--- @param id integer
--- @param match fun(integer): boolean
--- @return integer?
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

--- @param win integer
--- @param match fun(integer): boolean
--- @return integer?
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
  local p = get_prev_matching(fid, visibleontags)
  if p then set_focus(p) end
  arrange()
end

--- Focus the next visible window
function dwm.focus_next()
  local fid = vv.api.get_focused_window()
  local n = get_next_matching(fid, visibleontags)
  if n then set_focus(n) end
  arrange()
end

--- Swap the focused window with the next visible tiled window
function dwm.swap_next()
  local fid = vv.api.get_focused_window()
  if not tiled(fid) then return end
  local n = get_next_matching(fid, function(id) return visibleontags(id) and tiled(id) end)
  if n and n ~= fid then
    local i1 = table_index(windows, fid)
    local i2 = table_index(windows, n)
    table_swap(windows, i1, i2)
    arrange()
  end
end

--- Swap the focused window with the previous visible tiled window
function dwm.swap_prev()
  local fid = vv.api.get_focused_window()
  if not tiled(fid) then return end
  local n = get_prev_matching(fid, function(id) return visibleontags(id) and tiled(id) end)
  if n and n ~= fid then
    local i1 = table_index(windows, fid)
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
  local next_index = get_first_matching(windows,
    function(win) return win ~= focus_id and visibleontags(win) and tiled(win) end)
  if next_index then
    local next_win = windows[next_index]
    table_swap(windows, focus_index, next_index)
    local new_focus = get_first_matching(windows, function(win) return win == focus_id or win == next_win end)
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

function dwm.set_animation_duration(_, dur)
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

--- @param mode? boolean if true, show the status bar. Otherwise hide it
function dwm.set_status(mode)
  show_status = mode or not show_status
  -- dwm.reserve(0, 0, show_status and 1 or 0, 0)
  arrange()
end

--- @param vis velvet.window
function dwm.make_visible(vis)
  if state.tags[vis.id] then
    if not visibleontags(vis.id) then
      local tbl = {}
      for i, b in ipairs(state.tags[vis.id]) do
        if b then table.insert(tbl, i) end
      end
      dwm.set_view(tbl)
      arrange()
    end
  end
end

function dwm.is_visible(win)
  if type(win) == 'integer' then
    win = window.from_handle(win)
  end
  return visibleontags(win)
end

return dwm
