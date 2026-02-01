local vv = require('velvet')

local keys = {}
local vk = require('velvet.keymap.named_keys')

local keymap = {}

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

--- @param mods velvet.api.key_modifiers
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

--- @param chord chord
--- @return chord clean chord with unsupported modifiers stripped
local function clean_chord(chord)
  local clean = vv.deepcopy(chord)
  local lower = chord.key:lower()
  local upper = chord.key:upper()
  if lower ~= chord.key then
    clean.alt_key = lower
  elseif upper ~= chord.key then
    clean.alt_key = upper
  end
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
    local named = vk[chord.key]
    return named and '<' .. named .. '>' or chord.alt_key or chord.key
  end
  local str = '<'
  for _, pair in ipairs(mappings) do
    local flag = pair[3]
    if chord.mods & flag == flag then
      str = str .. pair[2] .. '-'
    end
  end
  local alt_key = chord.alt_key and str .. chord.alt_key .. '>'
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

    if #seq > 1 then
      local known = vk[seq:upper()]
      if not known then
        error("Unknown key: " .. seq)
      end
      key = known
    else
      key = seq:lower()
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
      local str = lhs:sub(1, 1)
      lhs = lhs:sub(2, -1)
      chord = { key = str:lower(), mods = str:upper() == str and mflags.shift or 0 }
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
  map.execute = rhs
  map.options = opts or {}
end

--- @type keymap
local current_chain = root_keymap

--- @param args velvet.api.session.key.event_args
local function send_key_to_window(args)
  local win = vv.api.get_focused_window()
  vv.api.window_send_raw_key(win, args.key)
end

local sequence_id = 0
local chain_unwind_timeout = 2000

local dbg_oneline = function(x) dbg(x, { indent = ' ', newline = '', depth = 4 }) end

local function keymap_unwind()
  -- unwind is called when a key did not match any mappings.
  -- we walk the parent binding tree to find a mapping which is a prefix of the current chain.
  -- If we find a mapping, we trigger that mapping and replay the remaining unresolved keys
  -- from the root keymap. We are guaranteed to resolve at least 1 key by treating it as normal
  -- input in the root keymap.
  local map = current_chain
  while map and map.trigger do
    if map.execute then
      pcall(map.execute)
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
  current_chain = root_keymap
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
  local str = ''
  while map do
    str = (map.key or '') .. str
    map = map.parent
  end
  dbg_oneline { current_chain = str }
end

--- @param args velvet.api.session.key.event_args
--- @return boolean args is a modifier
local function is_modifier(args)
  return modifier_keys[args.key.name] and true or false
end

local last_repeat = 0
--- @param args velvet.api.session.key.event_args
function keymap.on_key(args)
  if args.key.event_type == 'release' then
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
  dbg({args = args, chord = chord})
  local chord_key, alt_key = chord_to_string(chord)

  --- @type keymap
  local next_chain = current_chain.children[chord_key] or current_chain.children[alt_key]

  if next_chain then
    next_chain.trigger = args
    if next(next_chain.children) then
      -- If the chain has continuations, schedule an unwind.
      -- Otherwise it will be impossible to type keys which are part of a keymap.
      current_chain = next_chain
      schedule_unwind(sequence_id)
    else
      if not next_chain.execute then
        print_chain(next_chain)
        assert(next_chain.execute, "keymap: internal invariant violation: terminal chains must be executable!")
      end

      if not next_chain.options.repeatable and last_repeat == 0 then
        -- if the key is not repeatable, we can trivially reset the chain and execute the mapping
        current_chain = root_keymap
        pcall(next_chain.execute)
      else
        -- if it is repeatable, we need to check if it has timed out and act accordingly.
        local now = vv.api.get_current_tick()
        if last_repeat == 0 then
          -- this is the first repeat, and therefore cannot have timed out
          last_repeat = now
          pcall(next_chain.execute)
        else
          local did_time_out = now - last_repeat > vv.options.key_repeat_timeout
          if did_time_out then
            -- if we timed out, we need to replay this key in the root keymap.
            last_repeat = 0
            current_chain = root_keymap
            keymap.on_key(args)
            return
          else
            -- if we did not time out, update last repeat and execute the mapping.
            last_repeat = now
            pcall(next_chain.execute)
          end
        end
      end
    end
  end

  if not next_chain then
    if args.key.name == vk.ESCAPE and current_chain ~= root_keymap then
      -- escape should cancel any chain immediately without unwinding if the escape key was not a continuation.
      current_chain = root_keymap
      return
    end

    if is_modifier(args) then
      dbg_oneline{modifiers=args}
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
      send_key_to_window(args)
    elseif last_repeat > 0 then
      -- if the previous key was a repeat and the current key did not match anything,
      -- replay the current key in the root keymap
      last_repeat = 0
      current_chain = root_keymap
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
--- @return which_key[] mappings
function keys.which_key(lhs)
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
    which_keys[#which_keys+1] = { description = m.options.description or k, keys = k }
  end
  table.sort(which_keys, function(a, b) return a.keys:upper() < b.keys:upper() end)
  return which_keys
end

return keys
