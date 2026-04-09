local M = {}

--- @class velvet.cli.command
--- @field name string
--- @field description? string
--- @field action fun(name: string, args: string[]): any

--- @type table<string, velvet.cli.command>
local commands = {}

local function describe()
  local cmdlist = {}
  for _, cmd in pairs(commands) do
    cmdlist[#cmdlist+1] = cmd
  end
  table.sort(cmdlist, function (x, y) return x.name:lower() < y.name:lower() end)

  local col1 = 0
  for _, cmd in ipairs(cmdlist) do
    col1 = math.max(col1, vv.api.string_display_width(cmd.name))
  end

  local disp = {}
  for _, cmd in ipairs(cmdlist) do
    local padding = 8 + (col1 - vv.api.string_display_width(cmd.name))
    disp[#disp+1] = "  " .. cmd.name .. string.rep(' ', padding) .. (cmd.description or "")
  end

  return table.concat(disp, "\n") .. "\n"
end

--- @param command velvet.cli.command
function M.add_command(command)
  if type(command) ~= 'table' then
    error(("bad argument #1 (table expected, got %s)"):format(type(command)))
  end
  if type(command.name) ~= 'string' then
    error(("bad field 'name' (string expected, got %s)"):format(type(command.name)))
  end
  if type(command.action) ~= 'function' then
    error(("bad field 'action' (function expected, got %s)"):format(type(
      command.action)))
  end
  if command.description and type(command.description) ~= 'string' then
    error(
      ("bad type in field 'description' (string expected, got %s)"):format(type(command.description)))
  end
  commands[command.name] = command
end

--- @param name string The command to execute
--- @param args? string[] Optional parameters
function M.execute(name, args)
  local cmd = commands[name]
  if not cmd then return ("Unknown command '%s'\n\n%s"):format(name, describe()) end
  return cmd.action(name, args or {})
end

return M
