-- SPDX-License-Identifier: GPL-2.0-or-later
local root = assert(arg[1])
USERDIR, PATHSEP = "/tmp/freedoom-ui-test", "/"
local Object = {}
function Object:extend() return setmetatable({super = self}, {__index = self}) end
package.loaded.core = {}
package.loaded["core.config"] = {}
package.loaded["core.style"] = {}
package.loaded["core.contextmenu"] = Object
package.loaded.widget = Object
for _, name in ipairs {"label", "button", "textbox", "numberbox", "selectbox", "listbox"} do
  package.loaded["widget." .. name] = Object
end
local now, lookups, response, polls = 0, 0, {}, 0
package.loaded.system = {get_time = function() return now end}
package.loaded.net = {resolve_address = function()
  lookups = lookups + 1
  return {
    get_status = function()
      polls = polls + 1
      if response.failure then return "failure", response.failure end
      return response.wait and "waiting" or (polls < 3 and "waiting" or "success")
    end,
    get_ip = function() return response.ip or "127.0.0.1" end
  }
end}
package.loaded["plugins.freedoom.games"] = assert(loadfile(root .. "/games.lua"))()
local ui = assert(loadfile(root .. "/multiplayer.lua"))()
local function resolve(endpoint, fixture, cancel)
  now, polls, response = 0, 0, fixture or {}
  local thread = coroutine.create(function()
    return ui.resolve_master(endpoint, function() return not cancel or polls < 2 end)
  end)
  while true do
    local ok, value, err = coroutine.resume(thread)
    assert(ok, value)
    if coroutine.status(thread) == "dead" then return value, err end
    now = now + value
    assert(now < 6, "Resolver exceeded its deadline")
  end
end
assert(resolve("127.000.0.001:12345") == "127.0.0.1:12345" and lookups == 0)
assert(resolve("master.example:2342") == "127.0.0.1:2342" and polls == 3)
for _, endpoint in ipairs {"bad:port", "255.999.0.1", "master.example:0", "master.example:999999"} do
  assert(not resolve(endpoint), endpoint)
end
local value, err = resolve("master.example", {failure = "no such host"})
assert(not value and err:find("no such host"))
value, err = resolve("master.example", {ip = "::1"})
assert(not value and err:find("IPv4"))
value, err = resolve("master.example", {wait = true})
assert(not value and err:find("timed out"))
value, err = resolve("master.example", {wait = true}, true)
assert(not value and err:find("cancelled"))
local function status(changes, available)
  local server = {protocol = true, compatible = true, players = 1, max_players = 8,
    state = 0, version = "Woof 16.0.0", phase = 2, mission = 8, gamemode = 2}
  for name, setting in pairs(changes or {}) do server[name] = setting end
  return ui.server_status(server, available)
end
assert(status() == "Waiting")
assert(status {unassigned = true, players = 0} == "Unassigned")
assert(status {players = 4} == "Full")
assert(status {state = 2} == "In progress")
assert(status {state = 2, max_players = 9, compatible = false} == "In progress")
assert(status({state = 2}, {{phase = 2}}) == "In progress")
assert(status {protocol = false} == "Protocol mismatch")
assert(status {version = "Chocolate Doom 3.1.1"} == "Waiting")
assert(status {version = "Woof 11.3.0"} == "Waiting")
assert(status {compatible = false} == "Different game")
assert(status({phase = 1}, {{phase = 1}, {phase = 2}}) == "Waiting")
assert(status({phase = 1}, {{phase = 2}}) == "Missing game data")
assert(status({unassigned = true, phase = 0, players = 0}, {{phase = 2}}) == "Unassigned")
local function game(mission, mode, version)
  return ui.server_game {mission = mission, gamemode = mode, version = version or "Woof 16.0.0"}
end
assert(ui.server_game {unassigned = true} == "Unassigned")
assert(game(0, 0) == "Doom (Shareware)")
assert(game(0, 1) == "Doom")
assert(game(0, 3) == "The Ultimate Doom")
for mission, name in ipairs {"Doom II", "TNT: Evilution", "The Plutonia Experiment", "Chex Quest", "Hacx"} do
  assert(game(mission, 2) == name)
end
assert(game(6, 3) == "REKKR")
assert(game(7, 3) == "Chex Quest 3")
assert(game(8, 3) == "Freedoom: Phase 1")
assert(game(8, 2) == "Freedoom: Phase 2 / FreeDM")
assert(game(6, 1, "Chocolate Heretic 3.1.1") == "Heretic")
assert(game(7, 2, "Chocolate Hexen 3.1.1") == "Hexen")
assert(game(8, 2, "Chocolate Strife 3.1.1") == "Strife")
assert(game(9, 2, "Crispy Doom 7.1") == "Doom II (French)")
assert(game(10, 2, "Crispy Doom 7.1") == "No Rest for the Living")
assert(game(11, 2, "Crispy Doom 7.1") == "Master Levels for Doom II")
assert(game(8, 2, "Unknown engine") == "Unknown (mission 8, mode 2)")
assert(game(255, 255) == "Unknown (mission 255, mode 255)")
assert(ui.validate_name("Player") == "Player")
assert(ui.validate_name(string.rep("a", 29)))
for _, name in ipairs {"", string.rep("a", 30), "line\nbreak", "bad\0name"} do
  local value, err = ui.validate_name(name)
  assert(not value and err)
end
print("PASS asynchronous DNS, timeout/cancel, IPv4 checks, server joinability and game names")
