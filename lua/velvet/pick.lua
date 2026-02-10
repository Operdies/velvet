local vv = require('velvet')
local pick = {}

--- @class pick.item
--- @field text string

--- @class pick.mapping
--- @field keys string
--- @field action fun(pick.item) function
--- @field description string

--- @class pick.options
--- @field prompt? string
--- @field on_choice fun(pick.item): nil
--- @field on_cancel? fun(): nil
--- @field mappings? pick.mapping[]
--- @field initial_selection? integer initial selection

--- @type velvet.window | nil
local picker = nil

--- @type pick.item[]
local current_items = {}

--- @param items pick.item[]
function pick.update_items(items)
  current_items = items
end

--- Create a new picker with |opts|
--- @param items pick.item[]
--- @param opts pick.options
function pick.select(items, opts)
  if not picker or not picker:valid() then
    picker = require('velvet.window').create()
  end
  current_items = items

  local winlist = vv.api.get_windows()
  local sz = vv.api.get_screen_geometry()
  local width = 50
  local height = #winlist
  local geom = { left = sz.width // 2 - width // 2, width = width, height = height, top = sz.height // 2 - height // 2 }
  picker:set_geometry(geom)
  picker:set_frame_enabled(true)
  picker:set_frame_color('red')
  picker:set_z_index(vv.layers.popup)
  picker:clear_background_color()
  picker:set_opacity(0.8)
  picker:set_transparency_mode('all')
  picker:set_cursor_visible(false)
  picker:set_visibility(true)

  local prev_focus = vv.api.get_focused_window()
  picker:focus()

  local index = opts.initial_selection or 1
  local snapshot = {}
  local filter = ''
  local prompt = opts.prompt or "Pick: "

  local function draw()
    if not picker:valid() then return end
    picker:set_title(prompt .. filter)
    snapshot = {}
    picker:set_cursor(1, 1)
    picker:clear_background_color()
    picker:clear()
    for _, item in ipairs(current_items) do
      local display = item.text
      local case_sensitive = filter:lower() ~= filter
      local search = case_sensitive and display or display:lower()
      if search:find(filter, 1, true) then
        if #display > width then width = #display end
        snapshot[#snapshot + 1] = item
      end
    end

    height = #snapshot
    local geom2 = { left = sz.width // 2 - width // 2, width = width, height = height, top = sz.height // 2 - height // 2 }
    picker:set_geometry(geom2)
    picker:set_foreground_color('blue')
    if index > #snapshot then index = #snapshot end
    if index < 1 then index = 1 end
    for idx, _ in ipairs(snapshot) do
      if idx == index then
        picker:set_foreground_color('red')
      else
        picker:set_foreground_color('blue')
      end
      picker:set_cursor(1, idx)
      picker:draw(("\x1b[K%s"):format(snapshot[idx].text))
    end
  end

  local did_submit = false
  local tmp = vv.options.focus_follows_mouse
  vv.options.focus_follows_mouse = false
  local function dispose(no_restore_focus)
    vv.options.focus_follows_mouse = tmp
    picker:set_visibility(false)
    if not no_restore_focus and vv.api.window_is_valid(prev_focus) then vv.api.set_focused_window(prev_focus) end
    if not did_submit and opts.on_cancel then
      opts.on_cancel()
    end
  end

  local function submit()
    did_submit = true
    dispose()
    if index > 0 and index <= #snapshot then
      opts.on_choice(snapshot[index])
    end
  end

  --- @param args velvet.api.window.on_key.event_args
  picker:on_window_on_key(function(args)
    local keynames = require('velvet.keymap.named_keys')
    local k = args.key
    local ch = utf8.char(k.codepoint)
    local m = k.modifiers
    if k.event_type == 'press' or k.event_type == 'repeat' then
      if k.name == keynames.ESCAPE or (ch == 'c' and m.control) then
        dispose()
        return
      end
      if k.name == keynames.DOWN or (ch == 'n' and m.control) then
        index = 1 + (index % #snapshot)
      elseif k.name == keynames.UP or (ch == 'p' and m.control) then
        index = index - 1
        if index == 0 then index = #snapshot end
      elseif k.name == keynames.ENTER then
        submit()
        return
      elseif k.name == keynames.BACKSPACE then
        if #filter > 0 then
          filter = filter:sub(1, -2)
        end
      elseif ch == 'w' and m.control then
        filter = ''
      else
        local cp = k.alternate_codepoint > 0 and k.alternate_codepoint or k.codepoint
        -- cp 32 is space, and it is the first printable character which makes sense in a filter.
        -- 57358 chosen kind of arbitrarily. This is the keycode kitty maps caps lock to,
        -- and it is the first special key with a dedicated keycode. A better way to do this
        -- would be to use utf8proc to get the unicode category of the symbol and reject PUA / unknwon.
        if cp >= 32 and cp < 57358 then
          filter = filter .. utf8.char(cp)
        end
      end
      draw()
    end
  end)

  picker:on_mouse_click(function(_, args)
    if args.mouse_button == 'left' and args.event_type == 'mouse_down' then
      if args.pos.row <= #snapshot then
        index = args.pos.row
        submit()
      end
    end
  end)
  picker:on_mouse_move(function(_, args)
    if args.pos.row <= #snapshot then
      index = args.pos.row
      draw()
    end
  end)

  picker:on_focus_changed(function (_, args)
    if not args.focused then
      dispose(true)
    end
  end)

  draw()

end

return pick
