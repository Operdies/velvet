local vv = require('velvet')

local keys = {}
local vk = require('velvet.keymap.named_keys')

-- create a custom event dispatched on the main event system
keys.passthrough_changed = "custom.keymap.passthrough_changed"
keys.chain_changed = "custom.keymap.chain_changed"
keys.keymap_changed = "custom.keymap.keymap_changed"

local keymap = {}
local remapped_keys = {}

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
  local primary_key = key.name and key.name or utf8.char(key.codepoint)
  -- Mappings such as <S-2> which (on a us layout) can also
  -- be specified as <S-@>. By checking the alternate codepoint, we can match both mappings.
  local alt_key = key.alternate_codepoint > 0 and utf8.char(key.alternate_codepoint)
  local mods = flags_from_modifiers(key.modifiers)

  -- Prevent modifiers from modifying themselves. This can only cause confusion.
  if modifier_keys[primary_key] then mods = mods & ~modifier_keys[primary_key] end
  return { key = primary_key, mods = mods, alt_key = alt_key }
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
  clean.key = chord.key:lower()
  clean.alt_key = chord.key:upper()
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

--- @class keymap
--- @field parent? keymap parent keymap
--- @field children table<string, keymap> child mappings
--- @field execute? fun(nil): nil keymap action
--- @field options velvet.keys.set.options
--- @field key? string the key of this keymap in its parent child table
--- @field trigger? velvet.api.session.key.event_args the exact event which caused this keymap to be entered

--- @type keymap
local root_keymap = { children = {}, options = {} }

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
function keys.del(lhs)
  assert(type(lhs) == "string", "bad argument #1 (string expected)")

  local sequence = chords_from_string(lhs)
  --- @type keymap
  local map = root_keymap
  for _, chord in ipairs(sequence) do
    local lookup_key = chord_to_string(chord)
    map = map.children[lookup_key]
    if not map then error(("lhs %s does not match any current mapping."):format(lhs)) end
  end

  map.execute = nil
  map.options = {}

  -- recursively remove mappings in the tree if they do
  -- not have either a mapping (execute) or child mappings
  while map ~= root_keymap do
    assert(map, "keymap: invariant violated: removed keymap detached from tree")
    if next(map.children) or map.execute then break end
    map.parent.children[map.key] = nil
    map = map.parent
  end
  vv.events.emit_event(keys.keymap_changed)
end

--- @class velvet.keys.set.options
--- @field repeatable? boolean if true, the mapping may be repeated by pressing the last key again within |key_repeat_timeout| ms
--- @field description? string optional description of the mapping

--- Map |lhs| to function |rhs|
--- @param lhs string sequence of keys
--- @param rhs fun(nil): nil function triggered when |keys| are pressed
--- @param opts? velvet.keys.set.options options
function keys.set(lhs, rhs, opts)
  assert(type(lhs) == "string", "bad argument #1 (string expected)")
  assert(type(rhs) == "function", "bad argument #2 (string expected)")
  assert(opts == nil or type(opts) == "table", "bad argument #3 (table or nil expected)")

  local sequence = chords_from_string(lhs)
  local map = root_keymap
  for _, chord in ipairs(sequence) do
    local lookup_key = chord_to_string(chord)
    if not map.children[lookup_key] then map.children[lookup_key] = { parent = map, children = {}, key = lookup_key, options = {} } end
    map = map.children[lookup_key]
  end
  map.execute = function() 
    local handler = function(e)
      local traceback = debug.traceback(e, 2)
      vv.log("Unhandled error in mapping " .. lhs, 'error')
      vv.log(traceback, 'debug')
    end
    xpcall(rhs, handler) 
  end
  map.options = opts or {}
  vv.events.emit_event(keys.keymap_changed)
end

local function chain_str(map)
  local str = ''
  while map do
    str = (map.key or '') .. str
    map = map.parent
  end
  return str
end


--- @type keymap
local current_chain = root_keymap

local function set_current_chain(chain)
  if chain ~= current_chain then
    current_chain = chain
    vv.events.emit_event(keys.chain_changed, current_chain == root_keymap and nil or chain_str(current_chain))
  end
end

--- @param args velvet.api.session.key.event_args
local function send_key_to_window(args)
  local win = vv.api.get_focused_window()
  if win ~= 0 then vv.api.window_send_raw_key(win, args.key) end
end

local sequence_id = 0
local chain_unwind_timeout = 2000

local dbg_oneline = function(x) vv.log(vv.inspect(x, { indent = ' ', newline = '', depth = 4 })) end

local function keymap_unwind()
  -- unwind is called when a key did not match any mappings.
  -- we walk the parent binding tree to find a mapping which is a prefix of the current chain.
  -- If we find a mapping, we trigger that mapping and replay the remaining unresolved keys
  -- from the root keymap. We are guaranteed to resolve at least 1 key by treating it as normal
  -- input in the root keymap.
  local map = current_chain
  while map and map.trigger do
    if map.execute then
      map.execute()
      break
    end

    if map.parent == root_keymap then
      map.trigger.key.event_type = 'press'
      send_key_to_window(map.trigger)
      map.trigger.key.event_type = 'release'
      send_key_to_window(map.trigger)
      break
    end
    map = map.parent
  end

  -- the keys from start->map have now been handled.
  -- Replay the remaining keys
  local last_unresolved = current_chain
  set_current_chain(root_keymap)
  --- @type velvet.api.session.key.event_args[]
  local pending = {}
  while last_unresolved ~= map do
    table.insert(pending, 1, last_unresolved.trigger)
    last_unresolved = last_unresolved.parent
  end
  for _, key in ipairs(pending) do
    keymap.on_key(key)
  end
end

local function schedule_unwind(seq)
  vv.api.schedule_after(chain_unwind_timeout, function()
    if sequence_id == seq then
      keymap_unwind()
    end
  end)
end

local function print_chain(map)
  dbg_oneline { current_chain = chain_str(map) }
end

--- @param args velvet.api.session.key.event_args
--- @return boolean args is a modifier
local function is_modifier(args)
  return modifier_keys[args.key.name] and true or false
end

--- @param key velvet.api.window.key_event
--- @return velvet.api.window.key_event
local function maybe_remap(key)
  local chord = clean_chord(chord_from_key_event(key))
  local chord_key, alt_key = chord_to_string(chord)
  local remap = remapped_keys[chord_key] or remapped_keys[alt_key]
  if remap then
    local new_chord = chords_from_string(remap)[1]
    local new_event = chord_to_key_event(new_chord)
    new_event.event_type = key.event_type
    return new_event
  end
  return key
end

local passthrough = false
local last_repeat = 0
--- @param args velvet.api.session.key.event_args
function keymap.on_key(args)
  args.key = maybe_remap(args.key)
  if passthrough or args.key.event_type == 'release' then
    -- TODO: Only send a release event if we previously sent pressed/repeat events to the recipient.
    -- This is a bit tricky because it involves tracking if the press/repeat event was blocked due
    -- to a keymap, and because the focus may have been changed by a key repeat.
    -- For now, we just always send the release event since a missed key release
    -- is likely to cause more trouble than an extra key release.
    send_key_to_window(args)
    return
  end

  -- Increment the sequence number to invalidate scheduled callbacks
  sequence_id = sequence_id + 1

  local chord = clean_chord(chord_from_key_event(args.key))
  local chord_key, alt_key = chord_to_string(chord)

  local now = vv.api.get_current_tick()

  --- @type keymap|nil
  local next_chain = current_chain.children[chord_key] or current_chain.children[alt_key]
  if last_repeat > 0 and next_chain then
    if not next_chain.options.repeatable then
      next_chain = nil
    elseif now - last_repeat > vv.options.key_repeat_timeout then
      next_chain = nil
    end
  end

  if next_chain then
    next_chain.trigger = args
    if next(next_chain.children) then
      -- If the chain has continuations, schedule an unwind.
      -- Otherwise it will be impossible to type keys which are part of a keymap.
      set_current_chain(next_chain)
      schedule_unwind(sequence_id)
    else
      if not next_chain.execute then
        print_chain(next_chain)
        assert(next_chain.execute, "keymap: internal invariant violation: terminal chains must be executable!")
      end

      if next_chain.options.repeatable then
        last_repeat = now
      else
        -- if the key is not repeatable, we can trivially reset the chain and execute the mapping
        set_current_chain(root_keymap)
      end
      next_chain.execute()
    end
  end

  if not next_chain then
    if args.key.name == vk.ESCAPE and current_chain ~= root_keymap then
      -- escape should cancel any chain immediately without unwinding if the escape key was not a continuation.
      set_current_chain(root_keymap)
      last_repeat = 0
      return
    end

    if is_modifier(args) then
      -- if the key is a modifier, it may advance chains, but it should
      -- not cancel chains or trigger unwinding. Otherwise pressing LEFT_CONTROL
      -- in the middle of a binding using control as a modifier would cancel it.
      -- We still send it to windows though in case they are using kitty keyboard extensions
      -- and want to do something with modifiers.
      send_key_to_window(args)
      return
    end

    if current_chain == root_keymap then
      -- if the key was not handled and we are in the root keymap,
      -- send the key event to the window.
      last_repeat = 0
      send_key_to_window(args)
    elseif last_repeat > 0 then
      -- if the previous key was a repeat and the current key did not match anything,
      -- replay the current key in the root keymap
      last_repeat = 0
      set_current_chain(root_keymap)
      keymap.on_key(args)
    else
      -- otherwise, we should unwind the chain and replay the key.
      keymap_unwind()
      keymap.on_key(args)
    end
  end
end

local evt = require('velvet.events')
local grp = evt.create_group('velvet.keys', true)
grp.session_on_key = keymap.on_key

--- @class which_key
--- @field description string user provided description
--- @field keys string keys matching this mapping

--- Introspect the keymap to see the available mappings in the provided
--- sequence. If sequence is nil, the current chain is used.
--- @param lhs string|nil the key sequence to introspect child mappings from
--- @param recurse? boolean recursively get mappings
--- @return which_key[] mappings
function keys.which_key(lhs, recurse)
  local map = current_chain
  if lhs == '' then
    map = root_keymap
  elseif lhs then
    map = root_keymap
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
      keys = k, terminal = not next(m.children), 
      children = recurse and keys.which_key(lhs .. k, true) or nil 
    }
  end
  table.sort(which_keys, function(a, b) return a.keys:upper() < b.keys:upper() end)
  return which_keys
end

--- Remap key |from| to |to|
--- @param from string key
--- @param to string key to map |from| to
function keys.remap_key(from, to)
  assert(type(from) == "string", "bad argument #1 (string expected)")
  assert(type(to) == "string", "bad argument #1 (string expected)")

  local ch1 = chords_from_string(from)
  local ch2 = chords_from_string(to)

  assert(#ch1 == 1, "bad argument #1 (expected a single chord")
  assert(#ch2 == 1, "bad argument #2 (expected a single chord")

  remapped_keys[chord_to_string(ch1[1])] = chord_to_string(ch2[1])
end

--- Enable or disable passthrough mode. In passthrouh mode, the current keymap is ignored.
--- This is useful for sending keys to windows which would otherwise be intercepted.
function keys.set_passthrough(b)
  local set = b and true or false
  if passthrough ~= set then
    passthrough = set
    -- reset keymap on passthrough
    last_repeat = 0
    set_current_chain(root_keymap)
    vv.events.emit_event(keys.passthrough_changed, passthrough)
  end
end
function keys.get_passthrough() return passthrough end

return keys
