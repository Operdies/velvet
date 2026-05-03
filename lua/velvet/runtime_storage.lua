-- NOTE: known stores only knows about stores which have been referenced
-- since the last reload. We could fix this by tracking all keys in another
-- runtime storage, but it is not clear what the use case would be.
-- For now, we will treat unreferenced keys as though they don't exist.
local known_stores = {}

-- write all stores on reload.
local e = vv.events.create_group("velvet.runtime_storage", true)
e['pre_reload.late'] = function()
  for k, v in pairs(known_stores) do
    -- currently, this can't fail. The C implementation silently skips unsupported
    -- keys and value types (function and tables for keys, functions for values)
    -- But let's not risk values being lost.
    pcall(vv.api.runtime_store_value, k, v)
  end
end

return {
  --- Create a new runtime storage. Values written to this object
  --- are automatically persisted so they survive vv.api.reload().
  --- Values are lost when velvet is shut down.
  --- @param key string name of this store. Should be unique.
  --- @return table storage A storage table which can be used to access runtime-persisted values.
  create = function(key)
    if not known_stores[key] then
      known_stores[key] = vv.api.runtime_load_value(key) or {}
    end
    return known_stores[key]
  end,
  --- inspect known stores. This is meant for debugging, and not programmatic use.
  --- @return table<string, table> known_stores a table of all known stores.
  inspect = function() 
    local copy = {}
    for k, v in pairs(known_stores) do
      copy[k] = v
    end
    return copy
  end,
}
