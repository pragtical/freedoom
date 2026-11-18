-- SPDX-License-Identifier: GPL-2.0-or-later
local core = require "core"
local config = require "core.config"
local command = require "core.command"
local common = require "core.common"
local system = require "system"
local renderer = require "renderer"
local Freedoom = require "plugins.freedoom"
local settings = config.plugins.freedoom
settings.state_dir = USERDIR .. "/odamex-state"
settings.phase, settings.sound, settings.odamex_download = 2, true, true
settings.odamex_master = assert(os.getenv("FREEDOOM_ODAMEX_MASTER"))
local capture_directory = os.getenv("FREEDOOM_CAPTURE_DIR")
local view

local function until_(predicate, message)
  local deadline = system.get_time() + 25
  while not predicate() do
    if view and view.failed then error(tostring(view.status) .. "\n" .. view.log) end
    assert(system.get_time() < deadline, message)
    core.redraw = true
    coroutine.yield(.025)
  end
end

local function capture(name)
  if not capture_directory then return end
  coroutine.yield(.2)
  local w, h = core.window:get_size()
  assert(renderer.to_canvas(core.window, 0, 0, w, h):save_image(capture_directory .. "/" .. name .. ".png"))
end

local function close(view)
  local node = core.root_view.root_node:get_node_for_view(view)
  node:close_view(core.root_view.root_node, view)
end

local function press(key, held, after)
  core.on_event("keypressed", key)
  coroutine.yield(held or .08)
  core.on_event("keyreleased", key)
  coroutine.yield(after or .08)
end

core.add_thread(function()
  local ok, err = xpcall(function()
    coroutine.yield(.2)
    system.window_has_focus = function() return true end
    assert(command.perform("freedoom:odamex-multiplayer"), core.get_log())
    local setup = core.active_view
    assert(setup.engine == "odamex", core.get_log())
    setup.buttons[3]:on_click()
    local browser = core.active_view
    until_(function() return #browser.list.rows > 0 end, "No Odamex servers: " .. tostring(settings.odamex_master))
    local server = browser.list:get_row_data(1)
    assert(server.iwad and server.iwad:lower():find("freedoom2", 1, true), server.iwad)
    assert(server.password and server.addons:lower():find("fdqa", 1, true), server.addons)
    browser.list:set_selected(1)
    browser.list:on_row_click(1, server)
    coroutine.yield(.1)
    assert(browser.join.clickable)
    capture("odamex-servers")
    browser.join:on_click()
    assert(core.active_view == setup and not setup.fields.password:get_text():find("test"))
    setup.fields.password:set_text("test-password")
    assert(setup:launch(2))
    view = core.active_view
    assert(view:is(Freedoom) and view.odamex)
    until_(function() return view.frame and view.frame.library.fd_odamex_is_online(view.frame.handle) == 1
      and view.pixels end, "The client did not render an online level")
    coroutine.yield(1.2)
    capture("odamex-online")
    core.on_event("keypressed", "space")
    coroutine.yield(.1)
    core.on_event("keyreleased", "space")
    until_(function() return view.log:find("joined the game", 1, true) end,
      "The server did not admit the player")
    capture("odamex-playing")
    local frame = view.frame
    close(view)
    view = nil
    until_(function() return not frame.handle end, "Odamex worker did not close")

    assert(command.perform("freedoom:odamex-multiplayer"))
    assert(core.active_view == setup)
    setup.fields.name:set_text(("W"):rep(14))
    assert(setup:launch(6))
    view = core.active_view
    until_(function() return view.frame and view.pixels and view.frame.music
      and view.frame.music:get_queued_bytes() > 0 end, "Offline title or its music did not start")
    press("escape")
    capture("odamex-menu")
    press("down")
    press("return")
    capture("odamex-options")
    press("return")
    press("return")
    press("w")
    capture("odamex-player-setup")
    press("return")
    for _ = 1, 5 do press("down") end
    press("right", .8, .65)
    press("down")
    press("left", .8, .65)
    close(view)
    view = nil
    until_(function() return not require("plugins.freedoom.runtime").is_closing() end, "Offline session did not close")
    assert(command.perform("freedoom:open"))
    view = core.active_view
    until_(function() return view.pixels end, "Woof could not start after Odamex")
    close(view)
    view = nil
    until_(function() return not require("plugins.freedoom.runtime").is_closing() end, "Woof did not close")
    print("PASS Odamex browser, password, WAD download/reconnect, joining play, offline music and switching to Woof")
  end, debug.traceback)
  if view then print(view.log) end
  if not ok then print(err) end
  core.quit(true, ok and 0 or 1)
end)
