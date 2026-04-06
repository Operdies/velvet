local prefix = '<C-x>'
-- Try changing the prefix and reloading
-- prefix = '<C-b>'
require("velvet.presets.dwm").setup({ prefix = prefix })

local function setup_logpanel()
  local logpanel = require('velvet.diagnostics.logpanel')
  logpanel.enable()
  vv.log({ x = 123 }, 'debug')
  vv.log("Info", 'info')
  vv.log({{1,2,3}}, 'warning')
  vv.log({{{4,5, x = 1}}}, 'error')
end

-- Try enabling the logpanel by incommenting this line and reloading:
-- setup_logpanel()
