-- SPDX-License-Identifier: GPL-2.0-or-later
local core = require "core"
local command = require "core.command"
local config = require "core.config"
local common = require "core.common"
local renderer = require "renderer"
local system = require "system"
local variant = os.getenv("FREEDOOM_TEST_MOUSE") or "native"
local captures = os.getenv("FREEDOOM_CAPTURE_DIR")
config.fps, config.auto_fps = 75, true
config.plugins.freedoom = common.merge(config.plugins.freedoom or {},
  {state_dir = USERDIR .. "/test-state", sound = true, filter = "linear"})
local Freedoom = require "plugins.freedoom"
local spec = config.plugins.freedoom.config_spec
assert(spec.name == "Freedoom")
local paths = {}
for _, option in ipairs(spec) do if option.path then paths[option.path] = option end end
assert(paths.phase.type == "selection" and paths.phase.default == 2)
assert(paths.state_dir.type == "directory" and paths.state_dir.default == "")
assert(paths.resolution_scale.min == .25 and paths.resolution_scale.max == 1)
assert(paths["network.name"].type == "string" and paths["network.name"].default == "Player")
assert(paths.iwad_dir.type == "directory"
  and paths.iwad_dir.default == require("plugins.freedoom.games").default_directory)
assert(paths["network.name"].set_value("Tester") == "Tester")
assert(not paths["network.name"].set_value(""))
local view, methods, setter
local draw_text = renderer.draw_text
io.stdout:setvbuf("line")
local function wait(seconds) coroutine.yield(seconds) end
local function key(name)
  core.on_event("keypressed", name); wait(.06)
  core.on_event("keyreleased", name); wait(.2)
end
local function check_capture_hint(expected)
  local shown = false
  renderer.draw_text = function(font, text, ...)
    if text == "F12: Release mouse" then shown = true end
    return draw_text(font, text, ...)
  end
  core.redraw = true
  wait(.15)
  renderer.draw_text = draw_text
  assert(shown == expected, "Release hint did not match mouse capture")
end
local function capture(name)
  if not captures then return end
  wait(.1)
  local w, h = core.window:get_size()
  assert(renderer.to_canvas(core.window, 0, 0, w, h):save_image(captures .. "/" .. variant .. "-" .. name .. ".png"))
end
local function ready()
  local deadline = system.get_time() + 15
  while not view.pixels and system.get_time() < deadline do wait(.03) end
  assert(view.frame and view.pixels, tostring(view.status) .. view.log)
end
core.add_thread(function()
  local ok, err = xpcall(function()
    wait(.3)
    local settings = require "plugins.settings"
    assert(settings.ui.plugin_sections:get_pane("Freedoom"), "Freedoom settings pane missing")
    system.set_window_mode(core.window, "maximized")
    local focused = true
    system.window_has_focus = function() return focused end
    assert(command.perform("freedoom:open"))
    view = core.active_view
    ready(); wait(2.5)
    assert(view.width > 320 and view.height > 240)
    assert(view.frame.mixer, "Host audio failed: " .. tostring(view.frame.warning))
    assert(view.frame.music and view.frame.music_generation, "No OPL stream")
    capture("title")
    key("escape"); key("return"); key("return"); wait(2)
    core.on_event("keypressed", "w"); core.on_event("keypressed", "right"); wait(.6)
    core.on_event("keyreleased", "w"); core.on_event("keyreleased", "right")
    capture("gameplay")
    core.on_event("keypressed", "left ctrl")
    core.on_event("keypressed", "w"); core.on_event("keypressed", "right")
    wait(4)
    core.on_event("keyreleased", "left ctrl")
    core.on_event("keyreleased", "w"); core.on_event("keyreleased", "right")
    assert(view.frame.mixer, "Effect completion disabled audio")
    methods = getmetatable(core.window); setter = methods.set_relative_mouse_mode
    if variant == "missing" then methods.set_relative_mouse_mode = nil
    elseif variant == "rejected" then methods.set_relative_mouse_mode = function() return nil, "test driver rejection" end end
    local x, y = view.position.x + view.size.x / 2, view.position.y + view.size.y / 2
    core.on_event("mousepressed", "left", x, y, 1); core.on_event("mousereleased", "left", x, y)
    assert((view.mouse_window ~= nil) == (variant == "native"))
    check_capture_hint(variant == "native")
    if variant == "native" then capture("mouse-captured") end
    core.on_event("mousepressed", "right", x, y, 1)
    core.on_event("mousemoved", x + 25, y, 25, 0)
    core.on_event("mousereleased", "right", x + 25, y)
    key("f12"); assert(not view.enabled and not view.mouse_window); wait(.2)
    check_capture_hint(false)
    local serial = tonumber(view.frame.serial[0]); wait(.15); view:read_frame()
    assert(tonumber(view.frame.serial[0]) == serial, "Paused game kept advancing")
    assert(command.perform("freedoom:toggle-filter")); wait(.1)
    assert(view.frame.smooth == false and view.pixels)
    key("f12"); assert(view.enabled)
    if variant == "native" then
      core.on_event("mousepressed", "left", x, y, 1); core.on_event("mousereleased", "left", x, y)
      assert(view.mouse_window)
      check_capture_hint(true)
      wait(3.1)
      check_capture_hint(false)
      assert(view.mouse_window, "The hint timed out and released the mouse")
    end
    focused = false; wait(.15); assert(not view.playing and not view.mouse_window)
    check_capture_hint(false)
    focused = true; wait(.15); assert(view.playing)
    methods.set_relative_mouse_mode = setter
    local old = view.frame
    assert(command.perform("freedoom:restart")); ready()
    assert(not old.handle, "Old engine session survived restart")
    wait(2)
    old = view.frame
    key("escape"); key("q"); key("return"); key("y")
    local deadline = system.get_time() + 5
    while (old.handle or core.root_view.root_node:get_node_for_view(view))
        and system.get_time() < deadline do wait(.03) end
    assert(not old.handle and not view.frame, "Menu Quit left an engine session")
    assert(not core.root_view.root_node:get_node_for_view(view), "Menu Quit left the game tab open")
    assert(not view.mouse_window, "Menu Quit kept mouse capture")
    assert(system.get_file_info(USERDIR .. "/test-state/woof/phase2/woof.cfg"))
    local closed = view
    assert(command.perform("freedoom:open")); view = core.active_view; ready()
    assert(view ~= closed, "Open reused the closed game view")
    old = view.frame; view:stop(); wait(.3)
    assert(not old.handle, "Close left an engine session")
    assert(core.root_view.root_node:get_node_for_view(view), "Explicit stop closed the tab")
    assert(command.perform("freedoom:restart")); ready()
    old = view.frame
    local other = require("core.view")()
    core.root_view:get_active_node():add_view(other)
    assert(old:write("Q\0\0\0") == 4)
    deadline = system.get_time() + 5
    while (old.handle or core.root_view.root_node:get_node_for_view(view))
        and system.get_time() < deadline do wait(.03) end
    assert(not old.handle and not core.root_view.root_node:get_node_for_view(view),
      "Inactive engine exit left the game tab open")
    assert(core.active_view == other, "Engine exit closed or stole focus from another tab")
    assert(command.perform("freedoom:open")); view = core.active_view; ready()
    view:fail("Expected lifecycle-test error"); wait(.3)
    assert(not view.frame and core.root_view.root_node:get_node_for_view(view),
      "Error closed the diagnostic tab")
  end, debug.traceback)
  renderer.draw_text = draw_text
  if methods then methods.set_relative_mouse_mode = setter end
  if view then view:stop() end
  print(ok and ("PASS in-process Woof " .. variant) or ("FAIL " .. err))
  core.quit(true, ok and 0 or 1)
end)
