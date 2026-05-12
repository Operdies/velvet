local M = {}

--- @class velvet.statusbar.segment
--- @field text string
--- @field foreground? velvet.color
--- @field background? velvet.color
--- @field italic? boolean
--- @field bold? boolean

--- @alias velvet.statusbar.update_triggers (velvet.async.event_registration|integer)[]

--- @alias velvet.statusbar.element.content
--- | nil
--- | string
--- | velvet.statusbar.segment
--- | velvet.statusbar.segment[]

--- @class velvet.statusbar.mouse_event_args
--- @field segment velvet.statusbar.segment|table the segment which was clicked. Note that if a string was returned, that string will have been converted into a segment element. Furthermore, if the segment was at the first or last index, the string content may have been padded. In other words, the text property cannot be used to identify the segment. If you need to identify this segment somehow, return a segment table with a text property and annotate it with a custom field such as `id = x`

--- @class velvet.statusbar.on_click_event_args : velvet.statusbar.mouse_event_args
--- @field click velvet.api.mouse.click.event_args the raw click event

--- @class velvet.statusbar.on_scroll_event_args : velvet.statusbar.mouse_event_args
--- @field scroll velvet.api.mouse.scroll.event_args the raw scroll event

--- @class velvet.statusbar.on_mouse_move_event_args : velvet.statusbar.mouse_event_args
--- @field move velvet.api.mouse.move.event_args the raw mouse move event

--- @class velvet.statusbar.element_definition
--- @field content fun(opt: velvet.statusbar.element.options|table, trigger?: velvet.async.event_registration, data?: velvet.async.wait.result): velvet.statusbar.element.content function returning the segments for this element. The function will be executed as a coroutine, and is thus allowed to yield with the async system. The statusbar will be updated immediately when the function returns.
--- @field update_triggers? velvet.statusbar.update_triggers|fun(opt: velvet.statusbar.element.options|table): velvet.statusbar.update_triggers when any event in this list is triggered, |content| will be called
--- @field default_options? table|velvet.statusbar.element.options|table
--- @field on_click? fun(opt: velvet.statusbar.element.options|table, args: velvet.statusbar.on_click_event_args)
--- @field on_scroll? fun(opt: velvet.statusbar.element.options|table, args: velvet.statusbar.on_scroll_event_args)
--- @field on_mouse_move? fun(opt: velvet.statusbar.element.options|table, args: velvet.statusbar.on_mouse_move_event_args)

--- @type table<string, velvet.statusbar.element_definition>
local registry = {}

--- @class velvet.statusbar.private_data
--- @field location string
--- @field update velvet.async.event_source
--- @field quit velvet.async.event_source
--- @field left velvet.statusbar.element[]
--- @field running boolean
--- @field center velvet.statusbar.element[]
--- @field right velvet.statusbar.element[]
--- @field background velvet.color
--- @field win velvet.window
--- @field bar_updating velvet.async.event_source signaled when the bar is about to update
--- @field segment_lookup table<integer, { element: velvet.statusbar.element, segment: velvet.statusbar.segment }>
--- @field element_data table<string, velvet.statusbar.thread_data>
--- @field sticky table<velvet.statusbar.element, integer> track which element received the current mouse click event.

--- @class velvet.statusbar.thread_data
--- @field co thread
--- @field segments velvet.statusbar.segment[]

---@type table<velvet.statusbar, velvet.statusbar.private_data>
local bar_data = setmetatable({}, { __mode = 'k' })

--- @type table<velvet.statusbar.element_definition, nil|velvet.statusbar.segment[]>
local element_data = setmetatable({}, { __mode = 'k' })

--- @param name string
--- @param element velvet.statusbar.element_definition
function M.register(name, element)
  registry[name] = element
  element_data[element] = {}
end

--- @class velvet.statusbar
local Bar = {}
Bar.__index = Bar
Bar.register = M.register

local default_option_fields = { 'foreground', 'background', 'bold', 'italic' }

--- @param name string
--- @param update_completed velvet.async.event_source<string>
--- @param bar_updating velvet.async.event_source<string>
--- @param elem_data table
--- @param elem_options table|velvet.statusbar.element.options
local function run_segment(name, update_completed, bar_updating, elem_data, elem_options)
  local trigger, data
  while registry[name] do
    local elem = registry[name]
    local success, result = pcall(elem.content, elem_options, trigger, data)
    update_completed:emit(trigger)

    --- @type velvet.statusbar.segment[]
    local segment_array = {}
    if success then
      if type(result) == 'string' then
        segment_array[1] = { text = result }
      elseif type(result) == 'table' and result.text then
        segment_array[1] = result
      elseif type(result) == 'table' then
        segment_array = {}
        for _, seg in ipairs(result) do
          if type(seg) == 'string' then
            segment_array[#segment_array + 1] = { text = seg }
          elseif type(seg) == 'table' and seg.text then
            segment_array[#segment_array + 1] = seg
          end
        end
      end
    else
      printerr(name .. ' error: ' .. result)
      segment_array = { { text = "error: " .. result, foreground = '#000000', background = '#ff0000' } }
    end
    elem_data.segments = segment_array

    if elem_data.segments then
      for _, seg in ipairs(elem_data.segments) do
        if seg.text then
          seg.text = tostring(seg.text):gsub('\n', '')
        end
        for _, field in ipairs(default_option_fields) do
          if seg[field] == nil then seg[field] = elem_options[field] end
        end
      end
    end

    local update_triggers = elem.update_triggers
    if type(update_triggers) == 'function' then
      update_triggers = update_triggers(elem_options)
    end

    if update_triggers == nil then
      update_triggers = { bar_updating }
    elseif type(update_triggers) ~= 'table' then
      update_triggers = {}
    end

    -- if no update triggers were specified, call content() every time the bar is updated.
    -- Hopefully this element is either dirt cheap or making use of async functions.
    if #update_triggers == 0 then
      update_triggers = { bar_updating }
    end

    local update_rate_min = 100
    for i, v in ipairs(update_triggers) do
      -- Enforce an artifical poll limit of 10 times per second.
      -- This can be cirucmvented by hammering an event source,
      -- so it's not really a limitation in any real sense.
      -- Setting such a frequent update rate is more likely to be a bug than intended behavior anyway.
      if type(v) == 'number' and v < update_rate_min then update_triggers[i] = update_rate_min end
    end

    trigger, data = vv.async.wait(table.unpack(update_triggers))
  end
end

local function bar_dispatch_mouse_event(bar, event, event_data)
  local priv = bar_data[bar]
  local win = priv.win
  local mouse_evt = event_data.data
  local target = priv and priv.segment_lookup[mouse_evt.pos.col] or {}
  local elem = target and target.element


  local sticky, count = next(priv.sticky)
  if sticky and sticky ~= elem then
    elem = sticky
    target = {}
  end

  if event == win.events.mouse.click then
    -- Track number of held mouse buttons so we can send mouse_up
    -- to the element which received mouse_down

    sticky = sticky or elem
    count = count or 0
    if sticky then
      local delta = mouse_evt.event_type == 'mouse_down' and 1 or -1
      count = count + delta
      if count > 0 then
        priv.sticky = { [sticky] = count }
      else
        priv.sticky = {}
      end
    end
  end

  local def = elem and registry[elem.name]
  if not elem or not def then return end
  local fn, event_args
  if def.on_click and event == win.events.mouse.click then
    fn, event_args = def.on_click, { click = mouse_evt, segment = target.segment }
  elseif def.on_mouse_move and event == win.events.mouse.move then
    fn, event_args = def.on_mouse_move, { move = mouse_evt, segment = target.segment }
  elseif def.on_scroll and event == win.events.mouse.scroll then
    fn, event_args = def.on_scroll, { scroll = mouse_evt, segment = target.segment }
  end
  -- the event handler could make async calls, which is fine,
  -- but it should not block the statusbar update thread
  if fn and event_args then
    local geom = win:get_geometry()
    -- window events get col/row converted to local coordinates for convenience,
    -- but this information is not relevant to segment callbacks.
    -- Convert them back to global coordinates.
    mouse_evt.pos.col = mouse_evt.pos.col + geom.left - 1
    mouse_evt.pos.row = mouse_evt.pos.row + geom.top - 1
    -- We need to defer the fn() call in order for this thread to get back to its wait() call.
    -- Otherwise the statusbar will not receive updates triggered by fn().
    vv.api.schedule_after(0, function()
      fn(elem.options, event_args)
    end)
  end
end

--- @param elems { segments: velvet.statusbar.segment[], element: velvet.statusbar.element }[]
--- @return integer width
local function get_segment_width(elems)
  local total_width = 0
  for _, elem in ipairs(elems) do
    for _, seg in ipairs(elem.segments) do
      if seg.text then
        local width = vv.api.string_display_width(seg.text)
        total_width = total_width + width
      end
    end
  end
  return total_width
end

local segment_start_fn = {
  left = function() return 1 end,
  center = function(bar_width, segment_width) return math.floor(1 + (bar_width / 2 - segment_width / 2)) end,
  right = function(bar_width, segment_width) return 1 + bar_width - segment_width end,
}

--- @param bar velvet.statusbar
local function bar_render_segments(bar)
  local priv = bar_data[bar]
  local win = priv.win
  local segment_data = priv.element_data

  if not win:valid() then return end

  local bar_width = vv.api.window_get_geometry(win.id).width

  if priv.background then
    win:set_background_color(priv.background)
  else
    win:clear_background_color()
  end
  win:clear()

  -- Track which elements occupy which columns. This is used to hit-test mouse events.
  -- Note that this implementatin is a bit naive. It actually sets a physical index for every occupied column.
  -- But this number is unlikely to get very large, and it is a very easy way to handle overlapping elements in a crowded bar;
  -- if one element overwrites another, the cell will be mapped to the last value written, which is what the user would expect.
  -- It also makes the column-to-segment lookup trivial.
  --- @type table<integer, { element: velvet.statusbar.element, segment: velvet.statusbar.segment }>
  local partition = {}

  for _, key in ipairs({ 'left', 'center', 'right' }) do
    local elements = priv[key]
    --- @type { segments: velvet.statusbar.segment[], element: velvet.statusbar.element }[]
    local elem_array = {}
    for _, element in ipairs(elements) do
      local data = segment_data[element]
      if data and data.segments and #data.segments > 0 then
        elem_array[#elem_array + 1] = { segments = data.segments, element = element }
      end
    end

    for _, elem in ipairs(elem_array) do
      local segments = elem.segments
      for i = 1, #segments do
        local segment = segments[i]
        if segment.text then
          local space = 32 -- string.byte(' ', 1)
          local text = tostring(segment.text)
          -- pad outer elements if they are not already padded
          if i == 1 and text:byte(1) ~= space then text = ' ' .. text end
          if i == #segments and text:byte(-1) ~= space then text = text .. ' ' end
          segment.text = text
        end
      end
    end

    local segment_width = get_segment_width(elem_array)
    local segment_start = segment_start_fn[key](bar_width, segment_width)
    win:set_cursor(segment_start, 1)

    for _, elem in ipairs(elem_array) do
      local segments = elem.segments
      for i = 1, #segments do
        local segment = segments[i]
        if segment.text then
          -- We don't set background/foreground colors in the CSI sequence below
          -- because the window color functions are actually doing a bit of extra work;
          -- named colors (black, bright_black, cyan, etc.) are of course converted to the
          -- appropriate index, but user defined theme colors set via e.g.
          -- ```lua
          -- vv.options.theme.my_super_red = #ff0000
          -- ```
          -- are translated to the appropriate rgb code as well.
          win:set_background_color(segment.background or priv.background or 'black')
          win:set_foreground_color(segment.foreground or 'white')
          local CSI = '\x1b['
          local SGR = 'm'
          local bold = segment.bold == true and '1' or '22'
          local italic = segment.italic == true and '3' or '23'
          local seg_start = win:get_cursor().col
          win:draw(
            CSI .. bold .. ';' .. italic .. SGR .. -- set style
            segment.text ..                        -- draw text
            CSI .. SGR                             -- reset style
          )
          local seg_end = win:get_cursor().col
          if seg_end > seg_start then
            for col = seg_start, seg_end - 1 do
              partition[col] = { element = elem.element, segment = segment }
            end
          end
        end
      end
    end
  end
  priv.segment_lookup = partition
end

--- @param bar velvet.statusbar
local function bar_dispatch_events(bar)
  local priv = bar_data[bar]
  local win = priv.win
  local draw = false

  local maybe_pre_render = nil
  ::flush_events::

  local event, event_data = vv.async.wait(
    priv.quit,     -- signaled when bar:quit() is called
    priv.update,   -- signaled when any segment updates its content
    win.events.closed, win.events.resized, win.events.mouse.click, win.events.mouse.scroll, win.events.mouse.move,
    maybe_pre_render
  )

  local m = win.events.mouse
  if event == m.click or event == m.scroll or event == m.move then
    bar_dispatch_mouse_event(bar, event, event_data)
  elseif event == priv.quit then
    priv.running = false
    if win:valid() then win:close() end
  elseif event == win.events.closed then
    priv.running = false
  elseif event == win.events.resized then
    draw = true
  elseif event == priv.update then
    draw = true
  end
  if draw and event ~= maybe_pre_render then
    maybe_pre_render = 'pre_render.late'
    -- force schedule a render by invalidating the bar.
    -- this allows us to dispatch all events until right before the next render, giving all segments an opportunity to update if they need to
    priv.win:draw(' ')
    goto flush_events
  end
  bar_render_segments(bar)
end

--- @param bar velvet.statusbar
local function bar_update_workers(bar)
  local priv = bar_data[bar]
  local segment_data = priv.element_data
  local current = {}
  for _, tbl in ipairs({ priv.left, priv.center, priv.right }) do
    for _, elem in ipairs(tbl) do
      current[elem] = true
    end
  end

  -- 1. cancel all the threads we don't care about
  for key, trd in pairs(segment_data) do
    if not current[key] then
      segment_data[key] = nil
      vv.async.cancel(trd.co)
    end
  end

  -- 2. start missing threads
  for key, _ in pairs(current) do
    if not segment_data[key] then
      local elem = registry[key.name]
      if elem then
        local thread_data = {}
        segment_data[key] = thread_data
        thread_data.co = vv.async.run(function()
          local ok, err = xpcall(run_segment, debug.traceback, key.name, priv.update, priv.bar_updating:listener(),
            thread_data, key.options)
          if not ok then printerr(key .. ': ' .. err) end

          -- This thread is about to exit. The only expected scenario this should happen
          -- is if the element type was unregistered via M.register(name, nil).
          -- When the slot for this element is cleared, the bar loop will attempt to restart it.
          -- To avoid busy loops in the present of bugs, insert an artifical delay.
          vv.async.wait(1000)
          segment_data[key] = nil
        end)
      end
    end
  end
end

--- @param bar velvet.statusbar
local function bar_run(bar)
  local priv = bar_data[bar]
  priv.running = true
  local win = priv.win
  win:set_alternate_screen(true)
  win:set_auto_return(false)
  win:set_line_wrapping(false)
  win:set_z_index(vv.z_hint.statusbar)
  win:set_cursor_visible(false)
  if not priv.background then
    win:set_transparency_mode('clear')
    win:set_alpha(0)
  end
  win:set_title('status bar')
  win:set_anchors({ left = { to = 'left' }, right = { to = 'right' }, top = { to = priv.location }, bottom = { to = priv.location } })

  -- start worker threads after the call to bar_dispatch_events()
  vv.api.schedule_after(0, function() bar_update_workers(bar) end)
  while priv.running do
    -- we need the loop to reach its wait() call before the worker threads start emitting events
    local ok, err = xpcall(bar_dispatch_events, debug.traceback, bar)
    if not ok then
      printerr(err)
    end
  end
end

--- @class velvet.statusbar.element.options
--- @field foreground? velvet.color default foreground color.
--- @field background? velvet.color default background color.
--- @field bold? boolean
--- @field italic? boolean

--- @alias velvet.statusbar.element_name string|velvet.default_config.statusbar.builtin_modules

--- @class velvet.statusbar.element
--- @field name velvet.statusbar.element_name the name used to register the element
--- @field options? table|velvet.statusbar.element.options|table configuration options for the specific element. Refer to the specific element definition for details.

--- @class velvet.statusbar.options
--- @field left? (velvet.statusbar.element_name|velvet.statusbar.element)[] left-aligned elements
--- @field center? (velvet.statusbar.element_name|velvet.statusbar.element)[] centered elements
--- @field right? (velvet.statusbar.element_name|velvet.statusbar.element)[] right-aligned elements
--- @field background? velvet.color bar background color, defaulting to black
--- @field location? 'top'|'bottom'


local parts = { 'left', 'center', 'right' }

local default_options = { left = {}, center = {}, right = {}, location = 'bottom' }

local function normalize_bar_elem(elem)
  local item = elem
  if type(item) == 'string' then
    item = { name = item, options = {} }
  end
  local def = registry[item.name]
  if def then
    item.options = vv.tbl_deep_extend('force', def.default_options or {}, item.options or {})
  end
  return item
end

--- @param options velvet.statusbar.options
--- @return velvet.statusbar
function M.create(options)
  options = vv.tbl_deep_extend('force', default_options, options)
  for _, part in ipairs(parts) do
    local elems = options[part] or {}
    for idx, elem in ipairs(elems) do
      elems[idx] = normalize_bar_elem(elem)
    end
  end
  local bar = setmetatable({}, Bar)
  bar_data[bar] = {
    update = vv.async.event_source(),
    location = options.location,
    left = options.left,
    center = options.center,
    right = options.right,
    background = options.background,
    quit = vv.async.event_source(),
    win = require('velvet.window').create({}),
    bar_updating = vv.async.event_source(),
    segment_lookup = {},
    element_data = {},
    sticky = {},
    running = false,
  }
  vv.async.run(bar_run, bar)
  return bar
end

local function bar_set(bar, side, segments)
  local elems = {}
  for idx, elem in ipairs(segments or {}) do
    elems[idx] = normalize_bar_elem(elem)
  end
  local data = bar_data[bar]
  data[side] = elems
  bar:update()
end

--- @param left (velvet.statusbar.element_name|velvet.statusbar.element)[] left-aligned segments
function Bar:set_left(left)
  bar_set(self, 'left', left or {})
end

--- @param center (velvet.statusbar.element_name|velvet.statusbar.element)[] center-aligned segments
function Bar:set_center(center)
  bar_set(self, 'center', center or {})
end

--- @param right (velvet.statusbar.element_name|velvet.statusbar.element)[] right-aligned segments
function Bar:set_right(right)
  bar_set(self, 'right', right or {})
end

local function bar_get(bar, side)
  local data = bar_data[bar]
  return vv.deepcopy(data[side])
end

--- @return velvet.statusbar.element[] left aligned elements
function Bar:get_left()
  return bar_get(self, 'left')
end

--- @return velvet.statusbar.element[] center aligned elements
function Bar:get_center()
  return bar_get(self, 'center')
end

--- @return velvet.statusbar.element[] right aligned elements
function Bar:get_right()
  return bar_get(self, 'right')
end

function Bar:quit()
  bar_data[self].quit:emit(nil)
end

--- Trigger a bar update. This is normally not required, but is convenient
--- if something has happened which is not monitored by registered update triggers.
function Bar:update()
  bar_update_workers(self)
  bar_data[self].update:emit()
end

return M
