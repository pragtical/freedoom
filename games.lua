-- SPDX-License-Identifier: MIT
local config = require "core.config"
local system = require "system"
local directory = debug.getinfo(1, "S").source:sub(2):match("^(.*)[/\\]")
local games = {default_directory = directory .. "/iwads"}

function games.scan()
  local runtime = require "plugins.freedoom.runtime"
  local result, seen = {}, {}
  local function add(path, phase)
    path = system.absolute_path(path)
    if not path then return end
    local info = runtime.iwad_info(path)
    if not info or seen[info.checksum] then return end
    seen[info.checksum] = true
    info.path, info.phase = path, phase
    local name = path:match("([^/\\]+)$"):lower():gsub("%.wad$", "")
    local identity = info.checksum:sub(1, 4):gsub(".", function(byte) return string.format("%02x", byte:byte()) end)
    info.id = phase and "phase" .. phase or name .. "-" .. identity
    info.episode_style = info.gamemode ~= 2
    info.wire_mission = info.mission == 8 and (info.gamemode == 2 and 1 or 0) or info.mission
    result[#result + 1] = info
  end
  local defaults = system.list_dir(games.default_directory) or {}
  table.sort(defaults)
  local bundled = {}
  for _, game in ipairs({{"freedoom1.wad", 1}, {"freedoom2.wad", 2}, {"freedm.wad"}}) do
    local filename, phase = game[1], game[2]
    bundled[filename] = true
    for _, name in ipairs(defaults) do
      if name:lower() == filename then
        add(games.default_directory .. PATHSEP .. name, phase)
        break
      end
    end
  end
  local root = config.plugins.freedoom.iwad_dir or games.default_directory
  local entries = root == games.default_directory and defaults or system.list_dir(root) or {}
  if entries ~= defaults then table.sort(entries) end
  for _, name in ipairs(entries) do
    local lower = name:lower()
    if lower:match("%.wad$") and (entries ~= defaults or not bundled[lower]) then
      add(root .. PATHSEP .. name)
    end
  end
  return result
end

function games.freedoom(phase, available)
  for _, game in ipairs(available or games.scan()) do if game.phase == phase then return game end end
end

function games.for_server(server, available, preferred)
  if server.unassigned then return preferred or available[1] end
  if server.mission == 8 and server.version:match("^Woof ") then
    return games.freedoom(server.phase, available)
  end
  local fallback
  for _, game in ipairs(available) do
    if game.network and game.wire_mission == server.mission and game.gamemode == server.gamemode then
      if preferred and game.path == preferred.path then return game end
      fallback = fallback or game
    end
  end
  return fallback
end

function games.for_checksum(checksum, available)
  for _, game in ipairs(available) do
    if game.network and game.checksum == checksum then return game end
  end
end

function games.for_odamex(iwad, available)
  if not iwad then return end
  local name = iwad:lower():gsub("%.wad$", "")
  for _, game in ipairs(available or {}) do
    if game.path:match("([^/\\]+)$"):lower():gsub("%.wad$", "") == name then return game end
  end
end

return games
