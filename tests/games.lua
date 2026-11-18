-- SPDX-License-Identifier: GPL-2.0-or-later
local root = assert(arg[1])
USERDIR, PATHSEP = "/profile", "/"
local settings = {}
package.loaded["core.config"] = {plugins = {freedoom = settings}}
local files = {
  [root .. "/iwads"] = {"FREEDOOM1.WAD", "freedoom2.wad", "freedm.wad", "DOOM.WAD", "DOOM2.WAD",
    "TNT.WAD", "PLUTONIA.WAD", "ignored.wad", "nested"},
  [root .. "/iwads/nested"] = {"CHEX.WAD"},
  ["/extra"] = {"TNT.WAD"}
}
package.loaded.system = {
  absolute_path = function(path) return path end,
  list_dir = function(path) return files[path] end,
  get_file_info = function(path) return files[path] and {type = "dir"} end
}
package.loaded["plugins.freedoom.runtime"] = {iwad_info = function(path)
  local name = path:match("([^/]+)$"):lower()
  local records = {
    ["freedoom1.wad"] = {8, 3, "free1"}, ["freedoom2.wad"] = {8, 2, "free2"}, ["freedm.wad"] = {8, 2, "freedm"},
    ["doom.wad"] = {0, 3, "doom"}, ["doom2.wad"] = {1, 2, "doom2"},
    ["tnt.wad"] = {2, 2, "tnt"}, ["plutonia.wad"] = {3, 2, "plutonia"},
    ["chex.wad"] = {4, 3, "chex"}
  }
  local record = records[name]
  if record then return {mission = record[1], gamemode = record[2], checksum = record[3], title = name, network = true} end
end}
local games = assert(loadfile(root .. "/games.lua"))()
local available = games.scan()
assert(#available == 7, "IWAD discovery did not stay at the root or deduplicate")
assert(games.freedoom(1, available).id == "phase1")
assert(games.freedoom(2, available).id == "phase2")
local freedm = assert(games.for_checksum("freedm", available))
assert(not freedm.phase and games.for_odamex("FREEDM.WAD", available) == freedm)
local doom2 = assert(games.for_checksum("doom2", available))
assert(not doom2.phase and doom2.id:match("^doom2%-"))
local server = {mission = 1, gamemode = 2, version = "Chocolate Doom 3.1.1"}
assert(games.for_server(server, available, doom2) == doom2)
assert(games.for_server(server, available, games.freedoom(2, available)).phase == 2)
assert(games.for_server({mission = 2, gamemode = 2, version = "Woof 11.3.0"}, available).checksum == "tnt")
assert(games.for_server({mission = 8, gamemode = 3, phase = 1, version = "Woof 16.0.0"}, available).phase == 1)
assert(not games.for_checksum("missing", available))
assert(not games.for_server({mission = 6, gamemode = 1, version = "Chocolate Heretic"}, available))
assert(games.default_directory == root .. "/iwads")
settings.iwad_dir = "/extra"
local custom = games.scan()
assert(#custom == 4 and games.for_checksum("tnt", custom).path == "/extra/TNT.WAD")
assert(games.for_checksum("freedm", custom).path == freedm.path)
print("PASS IWAD discovery, deduplication, separate state and server/checksum selection")
