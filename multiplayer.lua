-- SPDX-License-Identifier: MIT
local core = require "core"
local config = require "core.config"
local style = require "core.style"
local Widget = require "widget"
local Label = require "widget.label"
local Button = require "widget.button"
local TextBox = require "widget.textbox"
local NumberBox = require "widget.numberbox"
local SelectBox = require "widget.selectbox"
local ListBox = require "widget.listbox"
local system = require "system"
local games = require "plugins.freedoom.games"
local ContextMenu = require "core.contextmenu"

local function resolve_master(endpoint, current)
  local host, port = (endpoint or ""):match("^([^:]+):(%d+)$")
  if not host then host, port = endpoint or "", "2342" end
  port = tonumber(port)
  if #host > 253 or not host:match("^[%w%.%-]+$") or not port or port < 1 or port > 65535 then
    return nil, "Invalid master server address"
  end
  local a, b, c, d = host:match("^(%d+)%.(%d+)%.(%d+)%.(%d+)$")
  if a and tonumber(a) <= 255 and tonumber(b) <= 255 and tonumber(c) <= 255 and tonumber(d) <= 255 then
    return string.format("%d.%d.%d.%d:%d", tonumber(a), tonumber(b), tonumber(c), tonumber(d), port)
  end
  if host:match("^[%d%.]+$") then return nil, "Invalid master IPv4 address" end
  local ok, net = pcall(require, "net")
  if not ok then return nil, "Pragtical DNS API unavailable" end
  local address, err = net.resolve_address(host)
  if not address then return nil, "Cannot resolve master server: " .. tostring(err) end
  local deadline = system.get_time() + 5
  while current() do
    local status, failure = address:get_status()
    if status == "success" then
      local ip = address:get_ip()
      if not ip or not ip:match("^%d+%.%d+%.%d+%.%d+$") then
        return nil, "Master server requires a usable IPv4 address"
      end
      return ip .. ":" .. port
    end
    if status == "failure" then return nil, "Cannot resolve master server: " .. tostring(failure) end
    if system.get_time() >= deadline then return nil, "Master server lookup timed out" end
    coroutine.yield(.05)
  end
  return nil, "Master lookup cancelled"
end
local function validate_name(name)
  if type(name) ~= "string" or name == "" or #name >= 30 or name:find("[%z\r\n]") then
    return nil, "Player name must contain 1-29 bytes without line breaks."
  end
  return name
end
local function server_status(server, available_games)
  if not server.protocol then return "Protocol mismatch" end
  if server.engine == "odamex" then
    if server.max_players > 0 and server.players >= server.max_players then return "Full" end
    if not games.for_odamex(server.iwad, available_games) then return "Missing IWAD" end
    return server.password and "Password" or "Joinable"
  end
  if server.state ~= 0 then return "In progress" end
  if not server.compatible then return "Different game" end
  if available_games and not games.for_server(server, available_games) then
    return "Missing game data"
  end
  if server.players >= math.min(server.max_players, 4) then return "Full" end
  return server.unassigned and "Unassigned" or "Waiting"
end
local game_names = {[1] = "Doom II", [2] = "TNT: Evilution", [3] = "The Plutonia Experiment",
  [4] = "Chex Quest", [5] = "Hacx"}
local chocolate_games = {[6] = "Heretic", [7] = "Hexen", [8] = "Strife", [9] = "Doom II (French)"}
local function server_game(server)
  if server.engine == "odamex" then return server.iwad or "Unknown" end
  if server.unassigned then return "Unassigned" end
  local mission, mode = server.mission, server.gamemode
  if mission == 0 then
    return mode == 0 and "Doom (Shareware)" or mode == 3 and "The Ultimate Doom" or "Doom"
  end
  if game_names[mission] then return game_names[mission] end
  -- Woof uses different mission IDs from Chocolate/Crispy after Hacx.
  if server.version:match("^Woof ") then
    if mission == 6 then return "REKKR" end
    if mission == 7 then return "Chex Quest 3" end
    if mission == 8 then
      return mode == 3 and "Freedoom: Phase 1" or mode == 2 and "Freedoom: Phase 2 / FreeDM" or "Freedoom"
    end
  elseif server.version:match("^Chocolate ") or server.version:match("^Crispy ") then
    if chocolate_games[mission] then return chocolate_games[mission] end
    if server.version:match("^Crispy ") then
      if mission == 10 then return "No Rest for the Living" end
      if mission == 11 then return "Master Levels for Doom II" end
    end
  end
  return string.format("Unknown (mission %d, mode %d)", mission, mode)
end

local modes = {"Co-operative", "Deathmatch", "Deathmatch 2.0", "Deathmatch 3.0"}
local function selection(parent, values, selected)
  local control = SelectBox(parent)
  for index, value in ipairs(values) do control:add_option(value, index) end
  control:set_selected(selected)
  return control
end
local function close(view)
  local root = core.root_view.root_node
  local node = root:get_node_for_view(view)
  if node then node:close_view(root, view) end
end
local function button_layout(buttons, left, top, width)
  local x, height = left, 0
  for _, button in ipairs(buttons) do
    if button.visible then
      if x > left and x + button:get_width() > left + width then
        x, top = left, top + height + style.padding.y
      end
      button:set_position(x, top)
      x = x + button:get_width() + style.padding.x
      height = math.max(height, button:get_height())
    end
  end
  return top + height
end

local Setup = Widget:extend()
function Setup:new(open, preferred, defaults, room, engine, browse)
  Setup.super.new(self, nil, false)
  self.name = engine == "odamex" and "Odamex Multiplayer" or "Freedoom Multiplayer"
  self.border.width, self.scrollable = 0, true
  self.open = open
  defaults = defaults or config.plugins.freedoom.network
  self.defaults, self.room, self.engine = defaults, room, engine
  self.title = Label(self, room and "Room Settings" or self.name)
  self.fields, self.rows = {}, {}
  local function field(name, label, widget)
    self.fields[name] = widget
    self.rows[#self.rows + 1] = {Label(self, label), widget}
  end
  if not room then field("name", "Player name", TextBox(self, defaults.name)) end
  field("game", "Game", SelectBox(self))
  if not room then
    field("address", "Server IPv4 address", TextBox(self, defaults.address, "127.0.0.1"))
    field("port", "UDP port", NumberBox(self, defaults.port, 1, 65535, 1))
  end
  if engine == "odamex" then
    local password = TextBox(self, "", "Optional")
    password:set_password_mode(true)
    field("password", "Password", password)
  else
    field("deathmatch", "Game mode", selection(self, modes, defaults.deathmatch + 1))
    field("episode", "Episode", NumberBox(self, defaults.episode, 1, 4, 1))
    field("map", "Map", NumberBox(self, defaults.map, 1, 32, 1))
    field("skill", "Difficulty", selection(self, {
      "I'm too young to die", "Hey, not too rough", "Hurt me plenty", "Ultra-Violence", "Nightmare"
    }, defaults.skill))
  end
  self.error = Label(self, "", true)
  self:refresh_games(preferred)
  self.error.foreground_color = style.syntax.keyword
  self.buttons = {}
  local actions = room and {{"Apply Settings", defaults.mode}, {"Cancel", 0}}
    or engine == "odamex" and {{"Join Game", 2}, {"Start Offline", 6}, {"Browse Servers", 7}}
    or {{"Host Game", 1}, {"Join Game", 2}, {"Find LAN Games", 3}, {"Find Internet Games", 4}}
  for _, item in ipairs(actions) do
    local button = Button(self, item[1])
    button:set_icon(item[2] >= 3 and "L" or "G")
    button.on_click = function()
      if item[2] == 0 then close(self)
      elseif item[2] == 7 then browse(self)
      else self:launch(item[2]) end
    end
    self.buttons[#self.buttons + 1] = button
  end
  self:show()
end
function Setup:refresh_games(preferred)
  local control = self.fields.game
  preferred = preferred or control:get_selected_data()
  self.games = games.scan()
  control.list:clear(); control:set_label("select")
  local selected = 0
  for index, game in ipairs(self.games) do
    if not self.room or self.room.can_change_game or preferred and game.path == preferred.path then
      control:add_option(game.title, game)
      if preferred and game.path == preferred.path
          or not preferred and game.phase == config.plugins.freedoom.phase then selected = #control.list.rows - 1 end
    end
  end
  control:set_selected(selected)
end
function Setup:launch(mode)
  local fields = self.fields
  local options = {mode = mode, name = fields.name and fields.name:get_text() or self.defaults.name,
    address = fields.address and fields.address:get_text() or self.defaults.address,
    port = fields.port and fields.port:get_value() or self.defaults.port,
    episode = fields.episode and fields.episode:get_value(), map = fields.map and fields.map:get_value(),
    skill = fields.skill and fields.skill:get_selected(), deathmatch = fields.deathmatch and fields.deathmatch:get_selected() - 1,
    game = fields.game:get_selected_data()}
  local name, err = validate_name(options.name)
  if not name then self.error:set_label(err); return false end
  if options.port % 1 ~= 0 or options.port < 1 or options.port > 65535 then
    self.error:set_label("UDP port must be a whole number from 1 to 65535."); return false
  end
  if not options.game or options.skill and (options.skill < 1 or options.deathmatch < 0) then
    self.error:set_label("Select a game, difficulty and game mode."); return false
  end
  options.phase = options.game.phase or config.plugins.freedoom.phase
  local a, b, c, d = options.address:match("^(%d+)%.(%d+)%.(%d+)%.(%d+)$")
  if mode == 2 and options.address ~= "localhost"
      and not (a and tonumber(a) <= 255 and tonumber(b) <= 255 and tonumber(c) <= 255 and tonumber(d) <= 255) then
    self.error:set_label("Enter an IPv4 address or localhost."); return false
  end
  if self.engine == "odamex" then
    options.engine = self.engine
    options.download = config.plugins.freedoom.odamex_download
    local password = fields.password:get_text()
    if #password > 128 or password:find("[%z\r\n]") then
      self.error:set_label("Password must be at most 128 bytes without line breaks."); return false
    end
    if mode == 2 then
      options.endpoint = (a and string.format("%d.%d.%d.%d", a, b, c, d) or "127.0.0.1") .. ":" .. options.port
      options.password = password ~= "" and password or nil
    end
    fields.password:set_text("")
    self.error:set_label("")
    self.open(options)
    return true
  end
  if mode == 1 or mode == 2 then
    if not options.game.network then self.error:set_label("This game's Dehacked patches are not verified for multiplayer."); return false end
    local max_map = options.game.episode_style and 9 or 32
    local max_episode = options.game.gamemode == 0 and 1 or options.game.gamemode == 1 and 3 or 4
    if options.map % 1 ~= 0 or options.map < 1 or options.map > max_map
        or options.episode % 1 ~= 0 or options.episode < 1 or options.episode > max_episode then
      self.error:set_label("Choose a valid episode (1-" .. max_episode .. ") and map (1-" .. max_map .. ")."); return false
    end
  end
  if self.room then
    local ok, err = self.open(options)
    if not ok then self.error:set_label(err or "Cannot update room settings"); return false end
    close(self)
    return true
  end
  for key, value in pairs(options) do
    if key ~= "mode" and key ~= "phase" and key ~= "name" and key ~= "game" then config.plugins.freedoom.network[key] = value end
  end
  if options.game.phase then config.plugins.freedoom.phase = options.game.phase end
  self.error:set_label("")
  self.open(options)
  return true
end
function Setup:update()
  local padding = style.padding
  local width = math.max(80, math.min(700 * SCALE, self.size.x - padding.x * 4))
  local left, top = padding.x * 2, padding.y * 2
  self.title:set_position(left, top); top = top + self.title:get_height() + padding.y * 2
  for _, row in ipairs(self.rows) do
    local narrow = width < 480 * SCALE
    row[1]:set_position(left, top)
    if not row[2]:is(NumberBox) then row[2]:set_size(narrow and width or width - 180 * SCALE) end
    row[2]:set_position(narrow and left or left + 180 * SCALE,
      narrow and top + row[1]:get_height() + padding.y / 2 or top)
    top = top + (narrow and row[1]:get_height() + padding.y / 2 or 0)
      + math.max(row[1]:get_height(), row[2]:get_height()) + padding.y
  end
  self.error:set_position(left, top); self.error:set_size(width)
  top = top + self.error:get_height() + padding.y
  button_layout(self.buttons, left, top, width)
  Setup.super.update(self)
end
function Setup:try_close(done) self:hide(); done() end

local Panel = Widget:extend()
function Panel:new(actions)
  Panel.super.new(self, nil, false)
  self.actions, self.border.width, self.scrollable = actions, 0, false
  self.title = Label(self, "Freedoom Multiplayer")
  self.detail = Label(self, "", true)
  self.server_address = Label(self, "", true)
  self.server_address:hide()
  self.message = Label(self, "", true)
  self.list = ListBox(self)
  self.context_menu = ContextMenu()
  self.context_menu:register(nil, {
    {text = "Copy Server Address", command = function() system.set_clipboard(self.context_address) end}
  })
  self.start = Button(self, "Start Game")
  self.configure = Button(self, "Room Settings")
  self.join = Button(self, "Join Game")
  self.refresh = Button(self, "Refresh")
  self.cancel = Button(self, "Cancel")
  self.start.on_click = actions.launch
  self.configure.on_click = actions.configure
  self.refresh.on_click = actions.refresh
  self.cancel.on_click = actions.cancel
  self.join.on_click = function() if self.selected and self.join.clickable then actions.join(self.selected) end end
  self.list.on_row_click = function(_, _, server) self.selected = server end
  self:show()
end
function Panel:set_state(options, info, error, pending)
  self.info, self.options = info, options
  local discovery = options.mode == 3 or options.mode == 4
  local game_title = options.game and options.game.title or "Freedoom"
  local odamex = options.engine == "odamex"
  self.title:set_label(odamex and "Odamex Servers"
    or discovery and (options.mode == 4 and "Freedoom Internet Games" or "Freedoom LAN Games") or "Freedoom Multiplayer")
  local mode = modes[options.deathmatch + 1]
  self.detail:set_label(odamex and options.master or discovery and (game_title .. "  |  "
      .. (options.mode == 4 and options.master or "UDP " .. options.port))
    or (options.mode == 1 and "Host" or options.address) .. "  |  UDP " .. options.port
      .. "  |  " .. game_title .. "  |  "
      .. (info and info.controller ~= 0 and (mode .. "  |  Map " .. options.map .. "  |  Skill " .. options.skill)
        or "Game settings chosen by host"))
  self.message:set_label(error or pending or (info and info.message) or "Connecting...")
  self.start.visible = not error and info and info.phase == 2 and info.controller ~= 0 or false
  self.configure.visible = self.start.visible
  self.join.visible = discovery and not error
  self.refresh.visible = discovery and not error
  self.cancel:set_label(error and "Close" or "Cancel")
  local rows = info and (discovery and info.servers or info.names) or {}
  local signature = {discovery and "servers" or "players"}
  for index, row in ipairs(rows) do
    if discovery then
      signature[#signature + 1] = row.address .. ":" .. row.players .. ":" .. (row.state or row.map or "")
        .. ":" .. row.description .. ":" .. row.ping .. ":" .. row.version
        .. ":" .. server_game(row) .. ":" .. server_status(row, options.available_games)
    elseif index <= (info.num_players or 0) then signature[#signature + 1] = row .. info.addresses[index] end
  end
  signature = table.concat(signature, "|")
  if signature ~= self.signature then
    self.signature = signature
    local selected = self.selected and self.selected.address
    self.selected = nil; self.list:clear(); self.list.columns = {}
    self.list:add_column(discovery and "Server" or "Player", 260 * SCALE)
    self.list:add_column("Address", 200 * SCALE)
    if discovery then
      self.list:add_column("Players", 80 * SCALE)
      self.list:add_column("Ping", 70 * SCALE)
      self.list:add_column("Version", 150 * SCALE)
      self.list:add_column("State", 150 * SCALE)
      self.list:add_column("Game", 240 * SCALE)
      if odamex then
        self.list:add_column("Map", 100 * SCALE)
        self.list:add_column("Mode", 110 * SCALE)
        self.list:add_column("Addons", 230 * SCALE)
      end
    end
    for index, row in ipairs(rows) do
      if discovery then
        local cells = {row.description, ListBox.COLEND, row.address, ListBox.COLEND,
          row.players .. "/" .. (odamex and row.max_players or math.min(row.max_players, 4)), ListBox.COLEND, row.ping .. " ms",
          ListBox.COLEND, row.version, ListBox.COLEND, server_status(row, options.available_games),
          ListBox.COLEND, server_game(row)}
        if odamex then
          for _, value in ipairs {row.map, row.game_type, row.addons} do
            cells[#cells + 1] = ListBox.COLEND; cells[#cells + 1] = value
          end
        end
        self.list:add_row(cells, row)
        if row.address == selected then self.selected = row; self.list:set_selected(index) end
      elseif index <= (info.num_players or 0) then
        self.list:add_row({row, ListBox.COLEND, info.addresses[index]})
      end
    end
    self.list:recalc_all_rows()
  end
  local status = self.selected and server_status(self.selected, options.available_games)
  self.server_address.visible = discovery and self.selected ~= nil
  self.server_address:set_label(self.selected and "Server address: " .. self.selected.address or "")
  self.join.clickable = status == "Waiting" or status == "Unassigned" or status == "Joinable" or status == "Password"
  self.join.foreground_color = self.join.clickable and style.text or style.dim
end
function Panel:update()
  local padding = style.padding
  local left, top = padding.x * 2, padding.y * 2
  local width = math.max(40, self.size.x - left * 2)
  self.title:set_position(left, top); top = top + self.title:get_height() + padding.y
  self.detail:set_position(left, top); self.detail:set_size(width); top = top + self.detail:get_height() + padding.y
  if self.server_address.visible then
    self.server_address:set_position(left, top); self.server_address:set_size(width)
    top = top + self.server_address:get_height() + padding.y
  end
  self.message:set_position(left, top); self.message:set_size(width); top = top + self.message:get_height() + padding.y
  local buttons = {self.start, self.configure, self.join, self.refresh, self.cancel}
  local height = button_layout(buttons, left, 0, width)
  local bottom = math.max(top + 40, self.size.y - height - padding.y * 2)
  self.list:set_position(left, top); self.list:set_size(width, math.max(40, bottom - top - padding.y))
  button_layout(buttons, left, bottom, width)
  Panel.super.update(self)
end
function Panel:hide()
  if self.context_menu then self.context_menu:hide() end
  Panel.super.hide(self)
end
function Panel:on_mouse_pressed(button, x, y, clicks)
  if self.context_menu.show_context_menu then
    return self.context_menu:on_mouse_pressed(button, x, y, clicks)
  end
  if button == "right" and self.options and (self.options.mode == 3 or self.options.mode == 4) then
    local index = self.list:mouse_on_top(x, y) and not self.list:scrollbar_overlaps_point(x, y)
      and self.list:get_row_at_position(x, y)
    local server = index and self.list:get_row_data(index)
      or self.server_address:is_visible() and self.server_address:mouse_on_top(x, y) and self.selected
    if server then
      if index then self.list:set_selected(index) end
      self.selected, self.context_address = server, server.address
      return self.context_menu:show(x, y)
    end
  end
  return Panel.super.on_mouse_pressed(self, button, x, y, clicks)
end
function Panel:key(key)
  if self.context_menu.show_context_menu then return false end
  if key == "escape" then self.actions.cancel(); return true end
  if key == "return" then
    if self.start.visible then self.actions.launch()
    elseif self.join.visible then self.join:on_click() end
    return true
  end
  if (self.options.mode == 3 or self.options.mode == 4) and (key == "up" or key == "down") and #self.list.rows > 0 then
    local index = math.max(1, math.min(#self.list.rows, self.list:get_selected() + (key == "up" and -1 or 1)))
    self.list:set_selected(index); self.selected = self.list:get_row_data(index)
    return true
  end
end

local function browse_odamex(setup)
  local odamex = require "plugins.freedoom.odamex"
  local panel, generation, info
  local options = {mode = 4, engine = "odamex", deathmatch = 0,
    master = config.plugins.freedoom.odamex_master or odamex.master}
  local function refresh()
    generation = (generation or 0) + 1
    local id = generation
    options.available_games = games.scan()
    info = {message = "Querying Odamex servers...", servers = {}}
    core.add_thread(function()
      local function live() return generation == id and panel.visible end
      local ok, servers, err = pcall(odamex.discover, options.master, live)
      if not live() then return end
      if not ok then err, servers = servers, nil end
      info = {message = servers and (#servers == 1 and "1 server" or #servers .. " servers") or tostring(err),
        servers = servers or {}}
      core.redraw = true
    end)
  end
  panel = Panel {
    cancel = function() close(panel) end,
    refresh = refresh,
    join = function(server)
      local host, port = server.address:match("^(.-):(%d+)$")
      setup:refresh_games(games.for_odamex(server.iwad, options.available_games))
      setup.fields.address:set_text(host)
      setup.fields.port:set_value(tonumber(port))
      setup.fields.password:set_text("")
      close(panel)
      local node = core.root_view.root_node:get_node_for_view(setup)
      if node then node:set_active_view(setup); core.set_active_view(setup) end
      if not server.password then setup:launch(2) end
    end
  }
  panel.name = "Odamex Servers"
  function panel:update()
    self:set_state(options, info)
    Panel.update(self)
  end
  function panel:try_close(done)
    generation = generation + 1
    self:hide()
    done()
  end
  core.root_view:get_active_node():add_view(panel)
  refresh()
end

return {Setup = Setup, Panel = Panel, close = close, resolve_master = resolve_master,
  server_status = server_status, server_game = server_game, validate_name = validate_name,
  browse_odamex = browse_odamex}
