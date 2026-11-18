-- mod-version:3.14
-- SPDX-License-Identifier: MIT
local core = require "core"
local common = require "core.common"
local command = require "core.command"
local config = require "core.config"
local style = require "core.style"
local View = require "core.view"
local renderer = require "renderer"
local system = require "system"
local directory = debug.getinfo(1, "S").source:sub(2):match("^(.*)[/\\]")
local setup_view
local odamex_setup
local games = require "plugins.freedoom.games"
local network_defaults = {name = "Player", address = "127.0.0.1", port = 2342,
  deathmatch = 0, episode = 1, map = 1, skill = 3, master = "master.chocolate-doom.org:2342"}
config.plugins.freedoom = common.merge({
  phase = 2, sound = true, filter = "linear", mouse_sensitivity = 1,
  widescreen = true, resolution_scale = 1,
  odamex_download = false,
  network = network_defaults,
  iwad_dir = games.default_directory,
  config_spec = {
    name = "Freedoom",
    {
      label = "Game", path = "phase", type = "selection", default = 2,
      values = {{"Phase 1", 1}, {"Phase 2", 2}},
      description = "Restart the game to apply."
    },
    {
      label = "Sound", path = "sound", type = "toggle", default = true,
      description = "Restart the game to apply."
    },
    {
      label = "Scaling Filter", path = "filter", type = "selection", default = "linear",
      values = {{"Linear", "linear"}, {"Nearest", "nearest"}},
      on_apply = function() core.redraw = true end
    },
    {
      label = "Widescreen", path = "widescreen", type = "toggle", default = true,
      on_apply = function() core.redraw = true end
    },
    {
      label = "Resolution Scale", path = "resolution_scale", type = "number",
      default = 1, min = .25, max = 1, step = .05,
      on_apply = function() core.redraw = true end
    },
    {
      label = "Mouse Sensitivity", path = "mouse_sensitivity", type = "number",
      default = 1, min = .1, max = 10, step = .1
    },
    {
      label = "Save Directory", path = "state_dir", type = "directory", default = "",
      description = "Empty uses the default directory. Restart the game to apply."
    },
    {
      label = "Additional IWAD Directory", path = "iwad_dir", type = "directory", default = games.default_directory,
      on_apply = function() if setup_view then setup_view:refresh_games() end end
    },
    {
      label = "Default Player Name", path = "network.name", type = "string", default = "Player",
      set_value = function(value) return require("plugins.freedoom.multiplayer").validate_name(value) end,
      on_apply = function(value)
        if setup_view then setup_view.fields.name:set_text(value) end
        if odamex_setup then odamex_setup.fields.name:set_text(value) end
        core.redraw = true
      end
    },
    {
      label = "Multiplayer", type = "button", icon = "G",
      on_click = "freedoom:multiplayer"
    },
    {
      label = "Odamex WAD Downloads", path = "odamex_download", type = "toggle", default = false,
      description = "Allow Odamex to fetch missing WADs from the server's download sites."
    }
  }
}, config.plugins.freedoom)
config.plugins.freedoom.network = common.merge(network_defaults, config.plugins.freedoom.network)
local function network_settings() return common.merge(network_defaults, config.plugins.freedoom.network) end

local keycodes = {
  space = 32,
  up = 0xad, down = 0xaf, left = 0xac, right = 0xae,
  ["left ctrl"] = 0xa3, ["right ctrl"] = 0xa3,
  ["left shift"] = 0xb6, ["right shift"] = 0xb6,
  ["return"] = 13, escape = 27, tab = 9, backspace = 127,
  f1 = 0xbb, f2 = 0xbc, f3 = 0xbd, f4 = 0xbe, f5 = 0xbf,
  f6 = 0xc0, f7 = 0xc1, f8 = 0xc2, f9 = 0xc3, f10 = 0xc4,
  f11 = 0xd7, pause = 255
}
local Freedoom = View:extend()
local current
local mouse_release_key = "f12"
local mouse_release_hint = mouse_release_key:upper() .. ": Release mouse"
local mouse_events = {
  mousemoved = "on_mouse_moved", mousepressed = "on_mouse_pressed",
  mousereleased = "on_mouse_released", mousewheel = "on_mouse_wheel"
}

function Freedoom:new(network, game)
  Freedoom.super.new(self)
  self.status, self.log = "Starting Freedoom...", ""
  self.keys, self.queue, self.enabled = {}, "", true
  self.width, self.height = 320, 240
  local ok, err = pcall(function()
    self.game = game or games.freedoom(config.plugins.freedoom.phase)
    self:set_network_options(network)
    self:start()
  end)
  if not ok then self:fail(err) end
end

function Freedoom:get_name()
  if self.odamex then return "Odamex: " .. (self.game and self.game.title or "Freedoom") end
  return self.game and not self.game.phase and self.game.title or "Freedoom"
end

function Freedoom:set_network_options(options)
  if self.network_panel then self.network_panel:hide() end
  self.network_info, self.network_panel, self.failed, self.network_pending = nil, nil, false, nil
  self.odamex = options and options.engine == "odamex" and options or nil
  if self.odamex then
    self.game, self.network_options = self.odamex.game, nil
    return
  end
  if not options then self.network_options = nil; return end
  self.network_options = network_settings()
  self.network_options.phase = config.plugins.freedoom.phase
  self.network_options = common.merge(self.network_options, options)
  self.game = self.network_options.game or self.game
  self.network_options.game = self.game
  self.network_options.available_games = games.scan()
  self.network_options.tried_iwads = self.network_options.tried_iwads or {}
  if self.game then self.network_options.tried_iwads[self.game.path] = true end
  local ui = require "plugins.freedoom.multiplayer"
  self.network_panel = ui.Panel {
    launch = function() if self.frame then self.frame:launch() end end,
    configure = function() self:configure_room() end,
    refresh = function()
      self.network_options.available_games = games.scan()
      if self.frame then self.frame:refresh() end
    end,
    cancel = function() ui.close(self) end,
    join = function(server)
      local reason = ui.server_status(server, self.network_options.available_games)
      if reason ~= "Waiting" and reason ~= "Unassigned" then core.log("Cannot join: %s", reason); return end
      local address, port = server.address:match("^(.-):(%d+)$")
      local options = common.merge({}, self.network_options)
      options.game = games.for_server(server, options.available_games, self.game)
      options.phase = options.game and options.game.phase or options.phase
      options.tried_iwads = {}
      options.mode = options.mode == 4 and 5 or 2
      options.address, options.port = address or server.address, tonumber(port) or 2342
      self:restart_game(options)
    end
  }
end

function Freedoom:configure_room()
  local info = self.frame and self.frame:network_info()
  if not info or info.phase ~= 2 or info.controller == 0 then return end
  local options = common.merge({}, self.network_options)
  options.deathmatch, options.episode, options.map, options.skill = info.deathmatch, info.episode, info.map, info.skill
  local editor = require("plugins.freedoom.multiplayer").Setup(function(selected)
    local current_info = self.frame and self.frame:network_info()
    if not current_info or current_info.phase ~= 2 or current_info.controller == 0 then
      return false, "Only the room controller can change settings before launch."
    end
    if selected.game.path ~= self.game.path then
      if current_info.num_players ~= 1 then return false, "Other players must leave before changing the game." end
      self:restart_game(common.merge(self.network_options, selected), selected.game)
    else
      if not self.frame:configure(selected) then return false, "Room settings could not be applied." end
      self.network_options = common.merge(self.network_options, selected)
    end
    return true
  end, self.game, options, {can_change_game = info.num_players == 1})
  core.root_view:get_active_node():add_view(editor)
end

function Freedoom:is_network_lobby()
  return self.network_panel and (not self.frame or not self.network_info or self.network_info.phase ~= 3)
end

function Freedoom:restart_game(options, game)
  self:stop()
  self.game = game or self.game
  self:set_network_options(options or self.network_options or self.odamex)
  self.keys, self.queue, self.enabled, self.playing = {}, "", true, nil
  self.status, self.log, self.pixels = "Starting Freedoom...", "", nil
  local ok, err = pcall(function() self:start() end)
  if not ok then self:fail(err) end
end

function Freedoom:send(op, a, b, c)
  if self.frame and #self.queue < 16384 then
    self.queue = self.queue .. string.char(op:byte(), a or 0, b or 0, c or 0)
    return true
  end
end

function Freedoom:flush_input()
  if #self.queue > 0 then
    local written = self.frame:write(self.queue)
    self.queue = self.queue:sub(written + 1)
  end
end

function Freedoom:set_key(code, down)
  if (self.keys[code] == true) ~= down then
    self.keys[code] = down or nil
    self:send("K", down and 1 or 0, code)
  end
end

function Freedoom:release_keys()
  for code in pairs(self.keys) do self:send("K", 0, code) end
  self.keys, self.mouse_look_drag = {}, false
  if self.mouse_window then
    local ok, err = self.mouse_window:set_relative_mouse_mode(false)
    if ok then
      self.mouse_window = nil
      self.mouse_hint_until = nil
      core.redraw = true
    else
      core.error("Freedoom mouse release: %s", err)
    end
  end
end

function Freedoom:stop()
  self:release_keys()
  self.playing = false
  self.generation = (self.generation or 0) + 1
  if self.frame then
    local frame = self.frame
    self.frame, self.stopping = nil, frame
    frame:stop()
    core.add_thread(function()
      while frame:status() < 2 do coroutine.yield(.01) end
      frame:close()
      if self.stopping == frame then self.stopping = nil end
    end)
  end
  self.queue = ""
end

function Freedoom:fail(message)
  self.failed = true
  self.status = tostring(message)
  self:stop()
  core.error("Freedoom: %s", self.status)
  core.redraw = true
end

function Freedoom:start()
  local Frame = require "plugins.freedoom.runtime"
  if not self.game and self.network_options and (self.network_options.mode == 3 or self.network_options.mode == 4) then
    self.game = self.network_options.available_games[1]
    self.network_options.game = self.game
  end
  assert(self.game, "Game data missing; install Freedoom with PPM or select an installed IWAD")
  local iwad = self.game.path
  local legacy_state = directory .. "/state"
  local configured_state = config.plugins.freedoom.state_dir
  local state_root = (configured_state ~= "" and configured_state or nil)
    or (system.get_file_info(legacy_state) and legacy_state or USERDIR .. "/storage/freedoom")
  local state = common.normalize_path(state_root .. (self.odamex and "/odamex/" or "/woof/") .. self.game.id)
  if not system.get_file_info(state) then assert(common.mkdirp(state)) end
  state = assert(system.absolute_path(state), "Cannot resolve Freedoom save directory")
  self.request_width, self.request_height = nil, nil
  self.requested_serial = nil
  self.generation = (self.generation or 0) + 1
  local generation = self.generation
  core.add_thread(function()
    while (self.stopping or Frame.is_closing()) and self.generation == generation do coroutine.yield(.01) end
    if self.generation ~= generation then return end
    if self.network_options and (self.network_options.mode == 4 or self.network_options.mode == 5) then
      self.network_pending = "Resolving master server..."; core.redraw = true
      local valid = function() return self.generation == generation end
      local ok, address, err = pcall(require("plugins.freedoom.multiplayer").resolve_master, self.network_options.master, valid)
      if not valid() then return end
      self.network_pending = nil
      if not ok then self:fail(address); return end
      if not address then self:fail(err); return end
      self.network_options.resolved_master = address
    end
    local ok, frame = pcall(Frame.new, state, iwad, config.plugins.freedoom.sound, self.network_options, self.odamex)
    if not ok then self:fail(frame); return end
    self.frame = frame
    if frame.warning then core.log("Freedoom audio disabled: %s", frame.warning) end
    self:send("F")
    while self.frame == frame do
      local ok, err = pcall(function() self:poll() end)
      if not ok then self:fail(err) end
      coroutine.yield(self.playing and not self:is_network_lobby() and .005 or .05)
    end
  end)
end

function Freedoom:poll()
  local frame = self.frame
  self.log = (self.log .. frame:log()):sub(-8192)
  if self.network_options then
    local info = frame:network_info()
    if info ~= self.network_info then self.network_info = info; core.redraw = true end
  end
  local status = frame:status()
  if status >= 2 then
    if status == 3 and self.network_info and self.network_options then
      local game = games.for_checksum(self.network_info.wanted_iwad, self.network_options.available_games)
      if game and not self.network_options.tried_iwads[game.path] then
        local options = common.merge({}, self.network_options)
        options.game, options.phase = game, game.phase or options.phase
        core.log_quiet("Freedoom: selecting matching local IWAD %s", game.title)
        self:restart_game(options, game)
        return
      end
    end
    self.failed = status == 3
    self.status = status == 2 and "Freedoom stopped" or "Freedoom stopped: " .. (self.log:match("([^\n]+)%s*$") or self.log)
    if status == 3 then core.error("Freedoom engine: %s", self.log) end
    local root = core.root_view.root_node
    local node = status == 2 and root:get_node_for_view(self)
    if node then node:close_view(root, self) else self:stop() end
    core.redraw = true
    return
  end
  local active = core.active_view == self and self.enabled and system.window_has_focus(core.window)
    and not core.root_view:get_modal_view()
  if self.playing ~= active then
    if not active then self:release_keys() end
    self.playing = active
    self:send("P", active and 0 or 1)
    frame:set_paused(not active)
    core.redraw = true
  end
  self:flush_input()
  if not self.frame then return end
  local sound_ok, sound_error = pcall(frame.audio_update, frame)
  if not sound_ok then
    frame:mute()
    core.error("Freedoom audio disabled: %s", sound_error)
  end
  local scale = common.clamp(tonumber(config.plugins.freedoom.resolution_scale) or 1, .25, 1)
  local height = math.max(240, math.floor(self.height * scale / 6) * 6)
  local width = math.max(320, math.floor(self.width * scale / 8) * 8)
  -- The compact resize packet has 12 bits per dimension.
  width = math.min(4088, width, math.floor(height * 3.6 / 8) * 8)
  height = math.min(height, math.floor(width * 3 / 4))
  if self.request_width ~= width or self.request_height ~= height then
    if self:send("R", width % 256, math.floor(width / 256) + (height % 16) * 16,
        math.floor(height / 16)) then
      self.request_width, self.request_height = width, height
    end
  end
end

function Freedoom:read_frame()
  if self.frame and core.active_view == self then
    local ok, pixels = pcall(self.frame.read, self.frame, self.width, self.height,
      config.plugins.freedoom.filter ~= "nearest")
    if not ok then self:fail(pixels); return end
    if pixels then self.pixels, self.status = pixels, nil; core.redraw = true end
    if self.playing and self.pixels then
      local serial = tonumber(self.frame.serial[0])
      if serial ~= self.requested_serial and self:send("F", 1) then
        self.requested_serial = serial
        -- Requests coalesce natively; prepare one image while this one draws.
        self:flush_input()
      end
    end
  end
end

function Freedoom:update()
  Freedoom.super.update(self)
  if self.mouse_hint_until and system.get_time() >= self.mouse_hint_until then
    self.mouse_hint_until = nil
    core.redraw = true
  end
  local ratio = config.plugins.freedoom.widescreen
    and common.clamp(self.size.x / math.max(1, self.size.y), 4 / 3, 3.6) or 4 / 3
  -- Multiples of six preserve Woof's 6:5 pixel-aspect correction exactly.
  local height = math.max(6, math.floor(math.min(3072, self.size.y, 4096 / ratio) / 6) * 6)
  local width = math.max(8, math.floor(math.min(4096, self.size.x, height * ratio) / 8) * 8)
  height = math.min(height, width * 3 / 4)
  if self.width ~= width or self.height ~= height then
    self.width, self.height, self.pixels = width, height, nil
  end
  if self:is_network_lobby() then
    if not self.network_panel.visible then self.network_panel:show() end
    self.network_panel:set_position(self.position.x, self.position.y)
    self.network_panel:set_size(self.size.x, self.size.y)
    self.network_panel:set_state(self.network_options, self.network_info, self.failed and self.status or nil, self.network_pending)
    self.network_panel:update()
  else
    if self.network_panel and self.network_panel.visible then self.network_panel:hide() end
    self:read_frame()
  end
  if self.playing then
    if core.request_update then core.request_update(self:is_network_lobby() and .05 or 1 / core.fps)
    else core.redraw = true end
  end
end

function Freedoom:draw()
  if self:is_network_lobby() then self.network_panel:draw(); return end
  self:draw_background({8, 10, 12, 255})
  if self.pixels then
    renderer.draw_pixels(self.pixels, self.position.x + math.floor((self.size.x - self.width) / 2),
      self.position.y + math.floor((self.size.y - self.height) / 2), self.width, self.height)
  end
  local hint = self.mouse_hint_until and mouse_release_hint
  local message = hint or self.status and self.status:match("[^\n]+")
  if not message and not self.enabled then
    message = (self.network_options or self.odamex and self.odamex.endpoint) and "Input released" or "Paused"
  end
  if message then
    local font = style.font
    local width = math.min(self.size.x, font:get_width(message) + 8)
    local x = self.position.x + style.padding.x
    if hint then x = self.position.x + math.floor((self.size.x - width) / 2) + 4 end
    local y = self.position.y + style.padding.y
    renderer.draw_rect(x - 4, y - 4, width, font:get_height() + 8, {8, 10, 12, 240})
    renderer.draw_text(font, message, x, y, style.text)
  end
end

function Freedoom:on_mouse_pressed(button, x, y, clicks)
  if self:is_network_lobby() then
    local root = core.root_view
    -- Keep widget capture from releasing the scrollbar press during handoff.
    if root.grab and root.grab.view == self then
      root:ungrab_mouse(root.grab.button)
      root:grab_mouse(button, self.network_panel)
    end
    return self.network_panel:on_mouse_pressed(button, x, y, clicks)
  end
  if not self.frame then return false end
  self.enabled = true
  if not self.mouse_window and not self.relative_mouse_unavailable
      and core.window.set_relative_mouse_mode then
    local ok, err = core.window:set_relative_mouse_mode(true)
    if ok then
      self.mouse_window = core.window
      self.mouse_hint_until = system.get_time() + 3
      core.redraw = true
      local grab = core.root_view.grab
      if grab and grab.view == self then core.root_view:ungrab_mouse(grab.button) end
      return true
    end
    self.relative_mouse_unavailable = true
    core.log_quiet("Freedoom: using drag controls; relative mouse unavailable: %s", err)
  end
  if button == "left" then self:set_key(0xa3, true) end
  if button == "right" then
    if self.mouse_window then self:set_key(0xa2, true) else self.mouse_look_drag = true end
  end
  if button == "middle" then self:set_key(0xad, true) end
  return true
end

function Freedoom:on_mouse_released(button, x, y)
  if self:is_network_lobby() then return self.network_panel:on_mouse_released(button, x, y) end
  if button == "left" then self:set_key(0xa3, false) end
  if button == "right" then self:set_key(0xa2, false); self.mouse_look_drag = false end
  if button == "middle" then self:set_key(0xad, false) end
end

function Freedoom:on_mouse_capture_lost()
  self:release_keys()
  if self.network_panel then self.network_panel:on_mouse_capture_lost() end
end

function Freedoom:on_mouse_moved(x, y, dx, dy)
  if self:is_network_lobby() then return self.network_panel:on_mouse_moved(x, y, dx, dy) end
  if not self.mouse_window and not self.mouse_look_drag then return false end
  local sensitivity = config.plugins.freedoom.mouse_sensitivity
  self:send("M", math.max(-127, math.min(127, math.floor(dx * sensitivity))) % 256,
    math.max(-127, math.min(127, math.floor(dy * sensitivity))) % 256)
  return true
end

function Freedoom:on_mouse_wheel(y, x)
  if self:is_network_lobby() then return self.network_panel:on_mouse_wheel(y, x) end
  return true
end
function Freedoom:on_text_input(text) end
function Freedoom:try_close(do_close)
  self:stop()
  if self.network_panel then self.network_panel:hide() end
  if current == self then current = nil end
  do_close()
end

local on_event = core.on_event
core.on_event = function(event, ...)
  local view = core.active_view
  if current and (event == "focuslost" or event == "quit") then current:release_keys() end
  if view and view:is(Freedoom) and view:is_network_lobby() and not core.root_view:get_modal_view() then
    if event == "keypressed" and view.network_panel:key(...) then return true end
    return on_event(event, ...)
  end
  if view and view:is(Freedoom) and view.frame and not core.root_view:get_modal_view() then
    if view.enabled and view.mouse_window then
      local handler = mouse_events[event]
      if handler then view[handler](view, ...); return true end
    end
    local key = ...
    if event == "keypressed" and key == mouse_release_key then
      view.enabled = not view.enabled
      view:release_keys()
      core.redraw = true
      return true
    end
    if view.enabled and (event == "keypressed" or event == "keyreleased") then
      local code = keycodes[key] or (#key == 1 and key:byte())
      if code then view:set_key(code, event == "keypressed") end
      if event == "keyreleased" then on_event(event, ...) end
      return true
    end
    if view.enabled and (event == "textinput" or event == "textediting") then return true end
  end
  return on_event(event, ...)
end

local function open_network(options)
  local function start()
    local node = current and core.root_view.root_node:get_node_for_view(current)
    if node then
      current:restart_game(options); node:set_active_view(current); core.set_active_view(current)
    else
      current = Freedoom(options); core.root_view:get_active_node():add_view(current)
    end
  end
  if current and current.frame and (not current.network_options
      or current.network_info and current.network_info.phase == 3) then
    core.nag_view:show("Start multiplayer", "Stop the current game?", {
      {text = "Start", default_yes = true}, {text = "Cancel", default_no = true}
    }, function(item) if item.text == "Start" then start() end end)
  else start() end
end
local function show_multiplayer()
  local node = setup_view and core.root_view.root_node:get_node_for_view(setup_view)
  if node then setup_view:refresh_games(current and current.game); node:set_active_view(setup_view); core.set_active_view(setup_view); return end
  setup_view = require("plugins.freedoom.multiplayer").Setup(open_network, current and current.game, network_settings())
  core.root_view:get_active_node():add_view(setup_view)
end
local function show_odamex()
  local node = odamex_setup and core.root_view.root_node:get_node_for_view(odamex_setup)
  if node then
    odamex_setup:refresh_games(current and current.game)
    node:set_active_view(odamex_setup)
    core.set_active_view(odamex_setup)
    return
  end
  local defaults = common.merge(network_settings(), {port = 10666})
  local ui = require "plugins.freedoom.multiplayer"
  odamex_setup = ui.Setup(open_network, current and current.game, defaults, nil, "odamex", ui.browse_odamex)
  core.root_view:get_active_node():add_view(odamex_setup)
end
local function open_game(game)
  local function start()
    local node = current and core.root_view.root_node:get_node_for_view(current)
    if node then
      current:set_network_options(nil)
      current:restart_game(nil, game)
      node:set_active_view(current); core.set_active_view(current)
    else
      current = Freedoom(nil, game); core.root_view:get_active_node():add_view(current)
    end
  end
  if current and current.frame then
    core.nag_view:show("Select game", "Stop the current game?", {
      {text = "Start", default_yes = true}, {text = "Cancel", default_no = true}
    }, function(item) if item.text == "Start" then start() end end)
  else start() end
end
command.add(nil, {
  ["freedoom:multiplayer"] = show_multiplayer,
  ["freedoom:host-game"] = show_multiplayer,
  ["freedoom:join-game"] = show_multiplayer,
  ["freedoom:find-lan-games"] = function()
    local options = network_settings()
    options.mode, options.phase = 3, config.plugins.freedoom.phase
    open_network(options)
  end,
  ["freedoom:find-internet-games"] = function()
    local options = network_settings()
    options.mode, options.phase = 4, config.plugins.freedoom.phase
    open_network(options)
  end,
  ["freedoom:open"] = function()
    local node = current and core.root_view.root_node:get_node_for_view(current)
    if node and current.game and current.game.phase then node:set_active_view(current); core.set_active_view(current); return end
    open_game(assert(games.freedoom(config.plugins.freedoom.phase), "Freedoom game data is missing"))
  end,
  ["freedoom:select-game"] = function()
    if config.plugins.freedoom.iwad_dir == games.default_directory and not system.get_file_info(games.default_directory) then
      assert(common.mkdirp(common.normalize_path(games.default_directory)))
    end
    local available = games.scan()
    local titles, choices = {}, {}
    for _, game in ipairs(available) do
      local title = game.title
      if choices[title] then title = title .. " (" .. game.id .. ")" end
      titles[#titles + 1] = title
      choices[title] = {text = title, info = game.path, game = game}
    end
    core.command_view:enter("Select game", {
      suggest = function(text)
        local result = {}
        for _, title in ipairs(common.fuzzy_match(titles, text)) do result[#result + 1] = choices[title] end
        return result
      end,
      submit = function(_, item) if item then open_game(item.game) end end
    })
  end,
})
command.add(function()
  local ok, runtime = pcall(require, "plugins.freedoom.runtime")
  return ok and runtime.odamex_installed()
end, {["freedoom:odamex-multiplayer"] = show_odamex})
command.add(Freedoom, {
  ["freedoom:toggle-filter"] = function(view)
    local settings = config.plugins.freedoom
    settings.filter = settings.filter == "nearest" and "linear" or "nearest"
    core.redraw = true
  end,
  ["freedoom:toggle-pause"] = function(view)
    view.enabled = not view.enabled; view:release_keys(); core.redraw = true
  end,
  ["freedoom:restart"] = function(view)
    view:restart_game()
  end
})

return Freedoom
