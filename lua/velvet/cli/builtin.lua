  vv.cli.add_command({ name = "quit", action = vv.api.quit, description = "Quit the velvet server, killing all windows." });
  vv.cli.add_command({ name = "reload", action = vv.api.reload, description = "Restart the lua VM and source configs." });
  vv.cli.add_command({
    name = "detach",
    action = function() vv.api.client_detach(vv.api.get_active_client()) end,
    description =
      "Detach the current terminal from the server."
  });
  vv.cli.add_command({
    name = "spawn",
    action = function(_, ...) vv.api.window_create_process({...}, { working_directory = vv.cwd() }) end,
    description = "Spawn a new window running the provided command."
  })
  require('velvet.cli.log')

