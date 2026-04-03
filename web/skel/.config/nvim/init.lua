vim.g.loaded_tutor_mode_plugin = 1
vim.g.loaded_spellfile_plugin = 1
vim.g.loaded_zipPlugin = 1
vim.g.loaded_gzip = 1
vim.g.loaded_tarPlugin = 1
vim.o.swapfile = false
vim.o.number = true

vim.opt.completeopt = { "menu", "menuone", "popup", "noinsert", "noselect", "fuzzy" }

vim.o.wildmenu = true
vim.o.wildmode = "longest:full,full"
vim.o.wildoptions = "fuzzy,pum,tagfile"
vim.opt.wildignore = { "*.o", "*.a" }
vim.lsp.config('lua_ls', {

  cmd = { 'lua-language-server', '--force-accept-workspace' },
  filetypes = { 'lua' },
  -- Sets the "workspace" to the directory where any of these files is found.
  root_markers = {
    ".luarc.json",
  },
  settings = {
    Lua = {
      runtime = {
        version = 'Lua 5.5',
      }
    }
  }
})

vim.lsp.enable({ "lua_ls" })

vim.api.nvim_create_autocmd('LspAttach', {
  group = augroup,
  callback = function(ev)
    local function bufmap(mode, l, r, opts)
      opts = opts or {}
      opts.buffer = bufnr
      vim.keymap.set(mode, l, r, opts)
    end

    local client = vim.lsp.get_client_by_id(ev.data.client_id)
    if client and client:supports_method('textDocument/completion') then
      local triggers = '_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.>'

      local chars = {}; triggers:gsub(".", function(c) table.insert(chars, c) end)
      client.server_capabilities.completionProvider.triggerCharacters = chars
      vim.lsp.completion.enable(true, client.id, ev.buf, { autotrigger = true })
    end
    bufmap('n', 'gd', vim.lsp.buf.definition)
  end,
})

