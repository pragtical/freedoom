-- SPDX-License-Identifier: GPL-2.0-or-later
local core = require "core"
local config = require "core.config"
local command = require "core.command"
local common = require "core.common"
local renderer = require "renderer"
local system = require "system"
config.plugins.freedoom = common.merge(config.plugins.freedoom or {}, {
  sound = false, phase = 2, state_dir = USERDIR .. "/games-state",
  iwad_dir = os.getenv("FREEDOOM_TEST_IWAD_DIR")
})
local Freedoom = require "plugins.freedoom"
-- Runtime replacement must retain defaults even after plugin initialization.
config.plugins.freedoom.network = {name = "Editor",
  port = tonumber(os.getenv("FREEDOOM_TEST_PORT")) or 2342}
local games = require "plugins.freedoom.games"
local role = os.getenv("FREEDOOM_TEST_ROLE") or "picker"
local view
local function wait(seconds) coroutine.yield(seconds or .02) end
local function until_(predicate)
  local deadline = system.get_time() + 25
  while not predicate() do
    assert(not view or not view.failed, view and view.status)
    assert(system.get_time() < deadline, "Game test timed out")
    core.redraw = true; wait()
  end
end
local function key(name)
  core.on_event("keypressed", name); wait(.05)
  core.on_event("keyreleased", name); wait(.15)
end
local function capture(name)
  local directory = os.getenv("FREEDOOM_CAPTURE_DIR")
  if directory then
    wait(.2)
    local w, h = core.window:get_size()
    assert(renderer.to_canvas(core.window, 0, 0, w, h):save_image(directory .. "/" .. name .. ".png"))
  end
end
core.add_thread(function()
  local ok, err = xpcall(function()
    wait(.3)
    system.window_has_focus = function() return true end
    system.set_window_mode(core.window, "maximized")
    local available = games.scan()
    assert(#available >= 6, "Owned Doom IWADs were not discovered")
    if role == "picker" then
      assert(command.perform("freedoom:select-game"))
      core.on_event("textinput", "plutonia")
      until_(function() return #core.command_view.suggestions == 1 end)
      assert(core.command_view.suggestions[1].game.mission == 3)
      capture("game-picker")
      key("return"); view = core.active_view
      until_(function() return view:is(Freedoom) and view.frame and view.pixels end)
      assert(view.game.mission == 3 and not view.game.phase)
      wait(2); capture("plutonia-title")
      key("escape"); key("return"); key("return"); wait(2)
      capture("plutonia-gameplay")
      assert(config.plugins.freedoom.phase == 2, "Selecting a game changed Freedoom's default")
      assert(command.perform("freedoom:open"))
      wait(.2); key("return")
      view = core.active_view
      until_(function() return view:is(Freedoom) and view.game.phase == 2 and view.frame and view.pixels end)
      assert(config.plugins.freedoom.phase == 2)
      capture("freedoom-open-default")
    else
      assert(command.perform("freedoom:find-lan-games")); view = core.active_view
      assert(view.network_options.deathmatch == 0 and view.network_options.skill == 3,
        "Partial network settings lost the multiplayer defaults")
      until_(function() return view.network_info and #view.network_info.servers == 1 end)
      local panel = view.network_panel
      until_(function() return #panel.list.rows == 1 end)
      local row = panel.list.rows[1]
      core.on_event("mousemoved", panel.list.position.x + 20, panel.list.position.y + row.y + row.h / 2, 0, 0)
      core.on_event("mousepressed", "left", panel.list.position.x + 20, panel.list.position.y + row.y + row.h / 2, 1)
      core.on_event("mousereleased", "left", panel.list.position.x + 20, panel.list.position.y + row.y + row.h / 2)
      wait(.1); assert(panel.join.clickable)
      panel.join:on_click()
      local expected = system.absolute_path(assert(os.getenv("FREEDOOM_TEST_IWAD")))
      until_(function() return view.game.path == expected and view.network_info
        and view.network_info.phase == 2 and view.network_info.num_players == 2 end)
      assert(config.plugins.freedoom.phase == 2, "Online IWAD selection changed Freedoom's default")
      local file = assert(io.open(USERDIR .. "/game-ready", "w")); file:write(view.game.id); file:close()
      until_(function() return view.network_info and view.network_info.phase == 3 and view.network_info.tic > 80 and view.pixels end)
      capture("online-" .. view.game.mission)
    end
    print("PASS game selection", role)
  end, debug.traceback)
  if view and view:is(Freedoom) then view:stop() end
  if not ok then print("FAIL", err) end
  core.quit(true, ok and 0 or 1)
end)
