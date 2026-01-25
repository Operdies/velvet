local keys = {}

local grp = nil

--- @param args velvet.api.session.key.event_args
local function on_key(args)
  dbg(args)
end

local evt = require('velvet.events')
function keys.setup()
  grp = evt.create_group('velvet.keys', true)
  grp.session_on_key = on_key
end

function keys.destroy()
  if grp then 
    evt.delete_group(grp)
    grp = nil
  end
end

return keys
