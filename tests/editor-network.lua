-- SPDX-License-Identifier: GPL-2.0-or-later
local core = require "core"
local command = require "core.command"
local config = require "core.config"
local common = require "core.common"
local system = require "system"
local renderer = require "renderer"
local port = assert(tonumber(os.getenv("FREEDOOM_TEST_PORT")))
local role = os.getenv("FREEDOOM_TEST_ROLE") or "host"
local captures = os.getenv("FREEDOOM_CAPTURE_DIR")
local saved_clipboard = system.get_clipboard()
config.plugins.freedoom = common.merge(config.plugins.freedoom or {}, {
  state_dir = USERDIR .. "/network-state", sound = false, phase = 2,
  network = {name = "Editor", address = "127.0.0.1", port = port, deathmatch = 1}
})
local Freedoom = require "plugins.freedoom"
config.plugins.freedoom.network = common.merge({name = "Player", address = "127.0.0.1",
  port = 2342, deathmatch = 0, episode = 1, map = 1, skill = 3,
  master = os.getenv("FREEDOOM_TEST_MASTER") or "master.chocolate-doom.org:2342"}, config.plugins.freedoom.network)
local view
io.stdout:setvbuf("line")
local function wait(seconds) coroutine.yield(seconds or .03) end
local function until_(predicate, timeout)
  local deadline = system.get_time() + (timeout or 20)
  while not predicate() do
    assert(not view or not view.failed, view and view.status)
    assert(system.get_time() < deadline, "Network editor test timed out")
    core.redraw = true
    if core.request_update then core.request_update(.01) end
    wait()
  end
end
local function capture(name)
  if not captures then return end
  wait(.2)
  local w, h = core.window:get_size()
  assert(renderer.to_canvas(core.window, 0, 0, w, h):save_image(captures .. "/" .. role .. "-" .. name .. ".png"))
end
local function click_at(x, y)
  core.on_event("mousemoved", x, y, 0, 0)
  core.on_event("mousepressed", "left", x, y, 1)
  core.on_event("mousereleased", "left", x, y)
  wait(.1)
end
local function click(widget)
  click_at(widget.position.x + widget.size.x / 2, widget.position.y + widget.size.y / 2)
end
local function right_click_at(x, y)
  core.on_event("mousemoved", x, y, 0, 0)
  core.on_event("mousepressed", "right", x, y, 1)
  core.on_event("mousereleased", "right", x, y)
  wait(.1)
end
local function drag_scrollbar(list, axis)
  local bar = axis == "x" and list.h_scrollbar or list.v_scrollbar
  assert(bar.normal_rect.scrollable > bar.normal_rect.along_size, "Scrollbar fixture has no overflow")
  local x, y, w, h = bar:get_thumb_rect()
  core.on_event("mousemoved", x + w / 2, y + h / 2, 0, 0); wait(.1)
  x, y, w, h = bar:get_thumb_rect()
  x, y = x + w / 2, y + h / 2
  core.on_event("mousepressed", "left", x, y, 1)
  assert(bar.dragging, "Server-list " .. axis .. " scrollbar capture was cancelled on press")
  local dx, dy = axis == "x" and 60 or 0, axis == "y" and 40 or 0
  core.on_event("mousemoved", x + dx, y + dy, dx, dy); wait(.1)
  assert(list.scroll.to[axis] > 0, "Server-list " .. axis .. " scrollbar did not scroll")
  core.on_event("mousereleased", "left", list.position.x - 10, list.position.y - 10)
  assert(not bar.dragging and not core.root_view.grab, "Scrollbar capture survived release outside the list")
end
core.add_thread(function()
  local ok, err = xpcall(function()
    wait(.3)
    local focused = true
    system.window_has_focus = function() return focused end
    system.set_window_mode(core.window, "maximized")
    assert(command.perform("freedoom:multiplayer"))
    local setup = core.active_view
    assert(setup.fields and setup.buttons, core.get_log())
    assert(setup.fields.game:get_selected_data(), "Setup did not select the default game")
    until_(function() return setup.size.x > 0 and setup.fields.port.position.y > setup.fields.name.position.y end)
    local settings = require "plugins.settings"
    local pane = assert(settings.ui.plugin_sections:get_pane("Freedoom"))
    local name_control
    for _, group in ipairs(settings.ui.plugin_sections.entries[pane.container].options) do
      if group.text:find("network.name", 1, true) then
        for _, control in ipairs(group.controls) do
          if control.type_name == "widget.textbox" then name_control = control end
        end
      end
    end
    assert(name_control, "Default Player Name setting is missing")
    name_control:on_change("Saved Player")
    assert(config.plugins.freedoom.network.name == "Saved Player")
    assert(setup.fields.name:get_text() == "Saved Player", "Setup did not refresh the saved default name")
    local saved = assert(loadfile(USERDIR .. "/user_settings.lua"))()
    assert(saved.config.plugins.freedoom.network.name == "Saved Player", "Default name was not persisted")
    setup.fields.port:set_value(65536)
    assert(not setup:launch(1), "Setup accepted an invalid UDP port")
    setup.fields.port:set_value(port)
    setup.error:set_label("")
    click(setup.fields.name)
    core.on_event("textinput", "X")
    assert(setup.fields.name:get_text():find("X"), "Player-name field did not accept input")
    setup.fields.name:set_text("Editor")
    capture("setup")
    if captures and role == "host" then
      system.set_window_mode(core.window, "normal")
      system.set_window_size(core.window, 650, 900, 100, 100)
      wait(.5); capture("setup-narrow")
      system.set_window_mode(core.window, "maximized"); wait(.5)
    end
    local layout = setup.fields.port
    assert(layout.position.y > setup.fields.name.position.y, string.format(
      "Setup fields overlap: port=%s name=%s size=%s,%s visible=%s", layout.position.y,
      setup.fields.name.position.y, setup.size.x, setup.size.y, tostring(setup.visible)))
    if role == "host" then click(setup.buttons[1])
    else click(setup.buttons[role == "internet" and 4 or 3]) end
    view = core.active_view
    assert(view:is(Freedoom), "Setup did not open a game view")
    assert(config.plugins.freedoom.network.name == "Saved Player", "Session name overwrote the saved default")
    if role == "host" then
      until_(function() return view.network_info and view.network_info.phase == 2
        and view.network_info.controller ~= 0 and view.network_info.num_players == 1 end)
      assert(view.network_panel.detail.original_label:find("Freedoom: Phase 2", 1, true),
        "Lobby did not show the initial game")
      click(view.network_panel.configure)
      local room = core.active_view
      assert(room.room.can_change_game, "Solo controller cannot change game")
      local selected
      for index, game in ipairs(room.games) do
        if game.phase == 1 then selected = index; break end
      end
      assert(selected, "Missing alternate Freedoom game")
      room.fields.game:set_selected(selected)
      click(room.buttons[1])
      until_(function() return core.active_view == view and view.frame and view.game.phase == 1
        and view.network_info and view.network_info.phase == 2 and view.network_info.num_players == 1 end)
      local ready = assert(io.open(assert(os.getenv("FREEDOOM_TEST_READY")), "w"))
      ready:write("ready\n"); ready:close()
    else
      until_(function() return view.network_info and #view.network_info.servers == (role == "internet" and 11 or 1) end)
      wait(.3); capture("discovery")
      local panel = view.network_panel
      local target, copy_row, copy_server
      for index, row in ipairs(panel.list.rows) do
        local server = panel.list:get_row_data(index)
        if server.phase == 1 and server.compatible then
          target = row
          assert(row[#row] == "The Ultimate Doom")
        else
          assert(row[#row] == (server.mission == 2 and "TNT: Evilution" or "Hexen"), "Game column lost the advertised game name")
          click_at(panel.list.position.x + 20, panel.list.position.y + row.y + row.h / 2)
          assert(not panel.join.clickable, "Naming an unsupported game enabled joining it")
          copy_row, copy_server = row, server
        end
      end
      assert(target, "Browser lost the playable server")
      click_at(panel.list.position.x + 20, panel.list.position.y + target.y + target.h / 2)
      assert(panel.selected, "Server row did not respond to a click")
      assert(panel.server_address.visible and panel.server_address.original_label:find(panel.selected.address, 1, true),
        "Selected server address is not displayed")
      if not copy_row then copy_row, copy_server = target, panel.selected end
      right_click_at(panel.list.position.x + 20, panel.list.position.y + copy_row.y + copy_row.h / 2)
      local menu = panel.context_menu
      assert(menu.show_context_menu and panel.selected.address == copy_server.address,
        "Right-click did not target the server under the pointer")
      assert(panel.server_address.original_label:find(copy_server.address, 1, true))
      capture("server-context")
      local copied = false
      for _, item, x, y, width, height in menu:each_item() do
        if item.text == "Copy Server Address" then
          click_at(x + width / 2, y + height / 2); copied = true; break
        end
      end
      assert(copied and system.get_clipboard() == copy_server.address and not menu.show_context_menu,
        "Context menu did not copy the targeted server address")
      right_click_at(panel.server_address.position.x + 10, panel.server_address.position.y + 5)
      assert(menu.show_context_menu, "Address label did not open its copy menu")
      core.on_event("keypressed", "escape"); core.on_event("keyreleased", "escape"); wait(.1)
      assert(not menu.show_context_menu and view.frame, "Escape closed the browser instead of the copy menu")
      if role == "internet" then
        system.set_window_mode(core.window, "normal")
        system.set_window_size(core.window, 650, 500, 100, 100)
        wait(.5); capture("address-narrow")
        assert(panel.server_address.position.x >= panel.position.x
          and panel.server_address.position.x + panel.server_address.size.x <= panel.position.x + panel.size.x,
          "Server address extends beyond the narrow browser")
        local list = panel.list
        list.scroll.x, list.scroll.to.x, list.scroll.y, list.scroll.to.y = 0, 0, 0, 0
        wait(.1); drag_scrollbar(list, "x"); drag_scrollbar(list, "y")
        capture("scrollbars-dragged")
        list.scroll.x, list.scroll.to.x, list.scroll.y, list.scroll.to.y = 0, 0, 0, 0
        system.set_window_mode(core.window, "maximized"); wait(.5)
      end
      click_at(panel.list.position.x + 20, panel.list.position.y + target.y + target.h / 2)
      click(panel.join)
    end
    until_(function() return view.network_info and view.network_info.phase == 2
      and view.network_info.num_players == 2 end)
    assert(view.network_options.phase == 1, "Game selection did not choose the server's Freedoom phase")
    assert(config.plugins.freedoom.phase == 2, "Joining changed the local default game")
    assert(view.network_panel.detail.original_label:find("Freedoom: Phase 1", 1, true),
      "Lobby did not show the game after switching or joining")
    wait(.3); capture("lobby")
    if role == "host" then
      click(view.network_panel.configure)
      local room = core.active_view
      assert(room.room and room.fields.map and not room.fields.address, "Room settings did not open")
      assert(not room.room.can_change_game and #room.fields.game.list.rows == 2,
        "Game change is available while other players are connected")
      room.fields.map:set_value(2)
      room.fields.skill:set_selected(2)
      capture("room-settings")
      click(room.buttons[1])
      until_(function() return core.active_view == view and view.network_info.map == 2 end)
      click(view.network_panel.start)
    else
      assert(not view.network_panel.configure.visible, "Non-controller has a room-settings button")
      view:configure_room()
      assert(core.active_view == view, "Non-controller opened room settings")
    end
    until_(function() return view.network_info and view.network_info.phase == 3 and view.pixels end)
    assert(view.network_info.deathmatch == 1)
    assert(view.network_info.player_mask == 3)
    until_(function()
      if view.network_info.tic < 100 or not view.pixels then return false end
      local colors = {}
      for i = 1, #view.pixels - 3, 148 do colors[view.pixels:sub(i, i + 2)] = true end
      local count = 0
      for _ in pairs(colors) do count = count + 1 end
      return count > 16
    end)
    capture("playing")
    local tick = view.network_info.tic
    core.on_event("keypressed", "f12")
    assert(not view.enabled, "F12 did not release input; active view=" .. core.active_view:get_name())
    core.on_event("keyreleased", "f12")
    until_(function() return view.network_info.tic > tick + 10 end, 4)
    core.on_event("keypressed", "f12"); core.on_event("keyreleased", "f12")
    focused = false; tick = view.network_info.tic
    until_(function() return not view.playing and view.network_info.tic > tick + 10 end, 4)
    focused = true
    local other = require("core.view")()
    core.root_view:get_active_node():add_view(other)
    tick = view.network_info.tic
    until_(function() return view.network_info.tic > tick + 10 end, 4)
    local node = core.root_view.root_node:get_node_for_view(view)
    node:set_active_view(view); core.set_active_view(view)
    local frame = view.frame
    node:close_view(core.root_view.root_node, view)
    until_(function() return not frame.handle end)
    assert(not core.root_view.root_node:get_node_for_view(view))
    print("PASS network editor " .. role)
  end, debug.traceback)
  system.set_clipboard(saved_clipboard)
  if view then view:stop() end
  if not ok then print("FAIL " .. err) end
  core.quit(true, ok and 0 or 1)
end)
