--- @class velvet.keymap
--- @field current_chain velvet.keymap.keybind current keymap
--- @field root velvet.keymap.keybind root keymap
--- @field repeat_timeout integer The interval in ms in which mappings with { repeatable=true } will be repeated without resetting the chain state. (Default 300)
--- @field chain_unwind_timeout integer The timeout in ms before incomplete chains will start unwinding. (Default 2000)
--- @field remapped_keys table<string,string> remapped keys
--- @field on_unhandled_key? fun(self: velvet.keymap, args: velvet.api.on_key.event_args)
--- @field on_chain_changed? fun(self: velvet.keymap)
--- @field on_passthrough_changed? fun(self: velvet.keymap)
--- @field on_keymap_changed? fun(self: velvet.keymap)
--- @field last_repeat integer
--- @field passthrough boolean if set, all keys are passed through as unhandled
--- @field unwind_schedule? integer handle to scheduled unwind callback
local Keys = {}
Keys.__index = Keys

Keys.events = {
  passthrough_changed = "keymap.passthrough_changed",
  chain_changed = "keymap.chain_changed",
  keymap_changed = "keymap.keymap_changed",
}

local vk = require('velvet.keymap.named_keys')

--- Create a new keymap
--- @return velvet.keymap
function Keys.create()
  local instance = setmetatable({
    repeat_timeout = 300,
    chain_unwind_timeout = 2000,
    remapped_keys = {},
    passthrough = false,
    last_repeat = 0,
    root = { children = {}, options = {} }
  }, Keys)
  instance.current_chain = instance.root
  return instance
end

--- @class chord
--- @field mods integer mask of modifiers for the key
--- @field key string string representation of the key
--- @field alt_key string alternate string representation of the key

--- @type table<velvet.api.key_modifier, integer>
local mflags = {
  shift = 1,
  alt = 2,
  control = 4,
  super = 8,
  hyper = 16,
  meta = 32,
  caps_lock = 64,
  num_lock = 128,
}

local short_mods = {
  S = 'shift',
  M = 'alt',
  C = 'control',
  D = 'super',
  H = 'hyper',
  T = 'alt', -- treat meta as alt
  Caps = 'caps_lock',
  Num = 'num_lock',
}

--- @param flags integer
--- @return velvet.api.key_modifiers mods
local function modifiers_from_flags(flags)
  local mods = {}
  for name, flag in pairs(mflags) do
    if (flags & flag) > 0 then
      mods[name] = true
    end
  end
  return mods
end


--- @param mods velvet.api.key_modifiers
--- @return integer flags
local function flags_from_modifiers(mods)
  local flags = 0
  for name, flag in pairs(mflags) do
    if mods[name] then flags = flags | flag end
  end
  return flags
end

local modifier_keys = {
  LEFT_SHIFT = mflags.shift,
  RIGHT_SHIFT = mflags.shift,
  LEFT_CONTROL = mflags.control,
  RIGHT_CONTROL = mflags.control,
  LEFT_ALT = (mflags.alt | mflags.meta),
  RIGHT_ALT = (mflags.alt | mflags.meta),
  LEFT_META = (mflags.alt | mflags.meta),
  RIGHT_META = (mflags.alt | mflags.meta),
  LEFT_SUPER = mflags.super,
  RIGHT_SUPER = mflags.super,
  LEFT_HYPER = mflags.hyper, RIGHT_HYPER = mflags.hyper,
  CAPS_LOCK = mflags.caps_lock,
  NUM_LOCK = mflags.num_lock,
}

--- @param key velvet.api.window.key_event
--- @return chord chord
local function chord_from_key_event(key)
  local mods = flags_from_modifiers(key.modifiers)
  if vk[key.name] then
    local base_key = vk[key.name]
    -- Prevent modifiers from modifying themselves. This can only cause confusion.
    if modifier_keys[base_key] then mods = mods & ~modifier_keys[base_key] end
    return { key = base_key, mods = mods }
  end

  local base_key = utf8.char(key.codepoint)
  -- Mappings such as <S-2> which (on a us layout) can also
  -- be specified as <S-@>. By checking the alternate codepoint, we can match both mappings.
  local alt_key = key.alternate_codepoint > 0 and utf8.char(key.alternate_codepoint)
  return { key = base_key, mods = mods, alt_key = alt_key }
end

--- @param x string
--- @return integer[]
local function string_to_codepoints(x)
  local cc = {}
  for _, c in utf8.codes(x) do
    cc[#cc+1] = c
  end
  return cc
end

--- @param chord chord
--- @return velvet.api.window.key_event key
local function chord_to_key_event(chord)
  local upper, lower = vv.api.string_upper(chord.key), vv.api.string_lower(chord.key)
  local mods = modifiers_from_flags(chord.mods)
  if vk[upper] then
    return { name = upper, codepoint = 0, alternate_codepoint = 0, event_type = 'press', modifiers = mods }
  else
    local alt = 0
    if chord.key == upper and upper ~= lower then
      alt = string_to_codepoints(upper)[1]
    end
    return {
      name = lower, 
      modifiers = mods,
      codepoint = string_to_codepoints(lower)[1],
      alternate_codepoint = alt,
      event_type = 'press',
    }
  end
end

--- @param chord chord
--- @return chord clean chord with unsupported modifiers stripped
local function clean_chord(chord)
  local clean = vv.deepcopy(chord)
  clean.mods = chord.mods & ~(mflags.num_lock | mflags.caps_lock | mflags.hyper | mflags.meta)
  if (chord.mods & mflags.meta) == mflags.meta then clean.mods = clean.mods | mflags.alt end
  return clean
end

--- @param chord chord
--- @return string,string|nil chord_string string representation of |chord|, and an optional alternative representation if the key was shifted.
local function chord_to_string(chord)
  -- use an array to preserve order
  local mappings = {
    { 'control', 'C', 4 },
    { 'shift',   'S', 1 },
    { 'meta',    'M', 32 },
    { 'alt',     'M', 2 },
    { 'super',   'D', 8 },
    { 'hyper',   'H', 16 },
  }

  if chord.mods == 0 then
    local named = vk[chord.key:upper()]
    if named then return '<' .. named .. '>' end
    return chord.key, chord.alt_key
  end
  local str = '<'
  for _, pair in ipairs(mappings) do
    local flag = pair[3]
    if chord.mods & flag == flag then
      str = str .. pair[2] .. '-'
    end
  end
  local alt_key = chord.alt_key and (str .. chord.alt_key .. '>')
  local primary_key = str .. chord.key .. '>'
  return primary_key, alt_key
end

--- @class velvet.keymap.keybind
--- @field parent? velvet.keymap.keybind parent keymap
--- @field children table<string, velvet.keymap.keybind> child mappings
--- @field execute? fun(nil): nil keymap action
--- @field options velvet.keys.set.options
--- @field key? string the key of this keymap in its parent child table
--- @field trigger? velvet.api.on_key.event_args the exact event which caused this keymap to be entered

--- @param lhs string
--- @return chord[] chords
local function chords_from_string(lhs)
  local function parse_modifiers(seq)
    local sequence = seq
    local mods = 0
    local key = nil
    while true do
      local mod = seq:find("^[SMCDHT]%-")
      if mod then
        local modstring = seq:sub(1, 1)
        local flag = mflags[short_mods[modstring]]
        if not flag then error("Unknown modifier " .. modstring) end
        mods = mods | flag
      else
        break
      end
      seq = seq:sub(3, -1)
    end

    local rem = string_to_codepoints(seq)
    if #rem > 1 then
      local known = vk[vv.api.string_upper(seq)]
      if not known then
        error("Unknown key: " .. seq)
      end
      key = known
    elseif #rem == 1 then
      key = vv.api.string_lower(seq)
    end

    if not key then error("Mapping is missing a key: " .. sequence) end
    return { mods = mods, key = key }
  end

  --- @type chord[]
  local sequence = {}
  while #lhs > 0 do
    local start, _end = lhs:find("^<[^>]+>")
    local chord = nil
    if start then
      local str = lhs:sub(start + 1, _end - 1)
      lhs = lhs:sub(_end + 1, -1)
      chord = parse_modifiers(str)
    else
      start, _end = lhs:find('^' .. utf8.charpattern)
      if not (start and _end) then
        error("no char pattern!!");
      end
      local str = lhs:sub(start, _end)
      lhs = lhs:sub(_end + 1, -1)
      local upper, lower = vv.api.string_upper(str), vv.api.string_lower(str)
      local is_shifted = str == upper and upper ~= lower
      chord = { key = lower, mods = is_shifted and mflags.shift or 0 }
    end
    table.insert(sequence, chord)
  end
  return sequence
end

--- @class velvet.keys.del.options

--- Delete the mapping associated with |lhs|
--- @param lhs string sequence of keys
function Keys:del(lhs)
  assert(type(lhs) == "string", "bad argument #1 (string expected)")

  local sequence = chords_from_string(lhs)
  --- @type velvet.keymap.keybind
  local map = self.root
  for _, chord in ipairs(sequence) do
    local lookup_key = chord_to_string(chord)
    map = map.children[lookup_key]
    if not map then error(("lhs %s does not match any current mapping."):format(lhs)) end
  end

  map.execute = nil
  map.options = {}

  -- recursively remove mappings in the tree if they do
  -- not have either a mapping (execute) or child mappings
  while map ~= self.root do
    assert(map, "keymap: invariant violated: removed keymap detached from tree")
    if next(map.children) or map.execute then break end
    map.parent.children[map.key] = nil
    map = map.parent
  end
  if self.on_keymap_changed then self:on_keymap_changed() end
end

--- @class velvet.keys.set.options
--- @field repeatable? boolean if true, the mapping may be repeated by pressing the last key again within |repeat_timeout| ms
--- @field description? string optional description of the mapping

--- Map |lhs| to function |rhs|
--- @param lhs string sequence of keys
--- @param rhs fun(nil): nil function triggered when |keys| are pressed
--- @param opts? velvet.keys.set.options options
function Keys:set(lhs, rhs, opts)
  assert(type(lhs) == "string", "bad argument #1 (string expected)")
  assert(type(rhs) == "function", "bad argument #2 (string expected)")
  assert(opts == nil or type(opts) == "table", "bad argument #3 (table or nil expected)")

  local sequence = chords_from_string(lhs)
  local map = self.root
  for _, chord in ipairs(sequence) do
    local lookup_key = chord_to_string(chord)
    if not map.children[lookup_key] then map.children[lookup_key] = { parent = map, children = {}, key = lookup_key, options = {} } end
    map = map.children[lookup_key]
  end
  map.execute = function() 
    local handler = function(e)
      return string.format("Unhandled error in keymap '%s': %s'", lhs, debug.traceback(e, 2))
    end
    local ok, err = xpcall(rhs, handler)
    if not ok then printerr(err) end
  end
  map.options = opts or {}
end

local function chain_str(map)
  local str = ''
  while map do
    str = (map.key or '') .. str
    map = map.parent
  end
  return str
end

--- @param map velvet.keymap
--- @param chain velvet.keymap.keybind
local function set_current_chain(map, chain)
  if chain ~= map.current_chain then
    map.current_chain = chain
    if map.on_chain_changed then map:on_chain_changed() end
  end
end

--- @param km velvet.keymap
local function on_unhandled_key(km, args)
  if km.on_unhandled_key then km:on_unhandled_key(args) end
end

--- @param km velvet.keymap
local function keymap_unwind(km)
  km.unwind_schedule = nil
  -- unwind is called when a key did not match any mappings.
  -- we walk the parent binding tree to find a mapping which is a prefix of the current chain.
  -- If we find a mapping, we trigger that mapping and replay the remaining unresolved keys
  -- from the root keymap. We are guaranteed to resolve at least 1 key by treating it as normal
  -- input in the root keymap.
  local map = km.current_chain
  while map and map.trigger do
    if map.execute then
      map.execute()
      break
    end

    if map.parent == km.root then
      map.trigger.key.event_type = 'press'
      on_unhandled_key(km, map.trigger)
      map.trigger.key.event_type = 'release'
      on_unhandled_key(km, map.trigger)
      break
    end
    map = map.parent
  end

  -- the keys from start->map have now been handled.
  -- Replay the remaining keys
  local last_unresolved = km.current_chain
  set_current_chain(km, km.root)
  --- @type velvet.api.on_key.event_args[]
  local pending = {}
  while last_unresolved ~= map do
    table.insert(pending, 1, last_unresolved.trigger)
    last_unresolved = last_unresolved.parent
  end
  for _, key in ipairs(pending) do
    km:on_key(key)
  end
end

--- @param args velvet.api.on_key.event_args
--- @return boolean args is a modifier
local function is_modifier(args)
  return modifier_keys[args.key.name] and true or false
end

--- @param km velvet.keymap
--- @param key velvet.api.window.key_event
--- @return velvet.api.window.key_event
local function maybe_remap(km, key)
  -- if a raw key (no modifiers, etc.) such as 'x' or 'F1' was specified,
  -- the mapping applies regardless of modifiers.
  local remap = km.remapped_keys[key.name]
  if remap then
    local new_chord = chords_from_string(remap)[1]
    local new_key = chord_to_key_event(new_chord)
    new_key.modifiers = key.modifiers
    new_key.event_type = key.event_type
    return new_key
  end

  -- Otherwise the mapping applies to the specific modifiers of the remapping
  local chord = clean_chord(chord_from_key_event(key))
  local chord_key, alt_key = chord_to_string(chord)
  remap = km.remapped_keys[alt_key] or km.remapped_keys[chord_key]
  if remap then
    local new_chord = chords_from_string(remap)[1]
    local new_key = chord_to_key_event(new_chord)
    new_key.event_type = key.event_type
    return new_key
  end
  return key
end

--- @param km velvet.keymap
--- @param next_chain velvet.keymap.keybind
--- @param args velvet.api.on_key.event_args
--- @param now integer
local function advance_chain(km, next_chain, args, now)
  next_chain.trigger = args
  if next(next_chain.children) then
    set_current_chain(km, next_chain)
    -- If the chain has continuations, schedule an unwind.
    -- Otherwise it will be impossible to type keys which are part of a keymap.
    km.unwind_schedule = vv.api.schedule_after(km.chain_unwind_timeout, function() keymap_unwind(km) end)
  else
    if not next_chain.execute then
      assert(next_chain.execute, "keymap: internal invariant violation: terminal chains must be executable!")
    end

    if next_chain.options.repeatable then
      km.last_repeat = now
    else
      -- if the key is not repeatable, we can trivially reset the chain and execute the mapping
      set_current_chain(km, km.root)
    end
    next_chain.execute()
  end
end

--- @param km velvet.keymap
--- @param args velvet.api.on_key.event_args
local function cancel_chain(km, args)
  if args.key.name == vk.ESCAPE and km.current_chain ~= km.root then
    -- escape should cancel any chain immediately without unwinding if the escape key was not a continuation.
    set_current_chain(km, km.root)
    km.last_repeat = 0
    return
  end

  if is_modifier(args) then
    -- if the key is a modifier, it may advance chains, but it should
    -- not cancel chains or trigger unwinding. Otherwise pressing LEFT_CONTROL
    -- in the middle of a binding using control as a modifier would cancel it.
    -- We still send it to windows though in case they are using kitty keyboard extensions
    -- and want to do something with modifiers.
    on_unhandled_key(km, args)
    return
  end

  if km.current_chain == km.root then
    -- if the key was not handled and we are in the root keymap,
    -- send the key event to the window.
    km.last_repeat = 0
    on_unhandled_key(km, args)
  elseif km.last_repeat > 0 then
    -- if the previous key was a repeat and the current key did not match anything,
    -- replay the current key in the root keymap
    km.last_repeat = 0
    set_current_chain(km, km.root)
    km:on_key(args)
  else
    -- otherwise, we should unwind the chain and replay the key.
    keymap_unwind(km)
    km:on_key(args)
  end
end

--- @param key velvet.api.window.key_event
--- @return string,string|nil chord_string string representation of |key|, and an optional alternative representation if the key is shifted.
local function key_event_to_string(key)
  local chord = clean_chord(chord_from_key_event(key))
  local chord_key, alt_key = chord_to_string(chord)
  return chord_key, alt_key
end

Keys.key_event_to_string = key_event_to_string

--- @param args velvet.api.on_key.event_args
function Keys:on_key(args)
  args.key = maybe_remap(self, args.key)
  if self.passthrough or args.key.event_type == 'release' then
    return false
  end

  -- cancel pending unwind on key
  if self.unwind_schedule then vv.api.schedule_cancel(self.unwind_schedule) end

  local chord_key, alt_key = Keys.key_event_to_string(args.key)

  local now = vv.api.get_current_tick()

  --- @type velvet.keymap.keybind|nil
  local next_chain = self.current_chain.children[alt_key] or self.current_chain.children[chord_key]
  if self.last_repeat > 0 and next_chain then
    if not next_chain.options.repeatable then
      next_chain = nil
    elseif now - self.last_repeat > self.repeat_timeout then
      next_chain = nil
    end
  end

  if next_chain then
    advance_chain(self, next_chain, args, now)
    return true
  else
    cancel_chain(self, args)
    return false
  end
end

--- @class which_key
--- @field description string user provided description
--- @field keys string keys matching this mapping

--- Introspect the keymap to see the available mappings in the provided
--- sequence. If sequence is nil, the current chain is used.
--- @param lhs string|nil the key sequence to introspect child mappings from
--- @param recurse? boolean recursively get mappings
--- @return which_key[] mappings
function Keys:which_key(lhs, recurse)
  local map = self.current_chain
  if lhs == '' then
    map = self.root
  elseif lhs then
    map = self.root
    local sequence = chords_from_string(lhs)
    for _, chord in ipairs(sequence) do
      local lookup_key = chord_to_string(chord)
      map = map.children[lookup_key]
      if not map then error(("lhs %s does not match any current mapping."):format(lhs)) end
    end
  end
  local which_keys = {}
  for k, m in pairs(map.children) do
    which_keys[#which_keys + 1] = {
      description = m.options.description or k, 
      keys = k, 
      terminal = not next(m.children), 
      repeatable = m.options.repeatable,
      children = recurse and self:which_key(lhs .. k, true) or nil
    }
  end
  table.sort(which_keys, function(a, b) return a.keys:upper() < b.keys:upper() end)
  return which_keys
end

--- Remap key |from| to |to|
--- @param from string key
--- @param to string key to map |from| to
function Keys:remap_key(from, to)
  assert(type(from) == "string", "bad argument #1 (string expected)")
  assert(type(to) == "string", "bad argument #1 (string expected)")

  local ch1 = chords_from_string(from)
  local ch2 = chords_from_string(to)

  assert(#ch1 == 1, "bad argument #1 (expected a single chord")
  assert(#ch2 == 1, "bad argument #2 (expected a single chord")

  self.remapped_keys[chord_to_string(ch1[1])] = chord_to_string(ch2[1])
end

--- Enable or disable passthrough mode. In passthrouh mode, the current keymap is ignored.
--- This is useful for sending keys to windows which would otherwise be intercepted.
function Keys:set_passthrough(b)
  local set = b and true or false
  if self.passthrough ~= set then
    self.passthrough = set
    -- reset keymap on passthrough
    self.last_repeat = 0
    set_current_chain(self, self.root)
    if self.on_passthrough_changed then self:on_passthrough_changed() end
  end
end
function Keys:get_passthrough() return self.passthrough end

local global_keymap = Keys.create()
global_keymap.on_chain_changed = function(self)
  local data = self.current_chain == self.root and nil or chain_str(self.current_chain)
  vv.events.emit(Keys.events.chain_changed, data)
end
global_keymap.on_keymap_changed = function() vv.events.emit(Keys.events.keymap_changed) end
global_keymap.on_passthrough_changed = function(self)
  vv.events.emit(Keys.events.passthrough_changed, self.passthrough)
end
global_keymap.on_unhandled_key = function(_, args)
  local win = vv.api.get_focused_window()
  if win ~= 0 then vv.api.window_send_raw_key(win, args.key) end
end

local evt = require('velvet.events')
local grp = evt.create_group('velvet.keys', true)
grp.on_key = function(k)
  global_keymap:on_key(k)
end

return global_keymap
