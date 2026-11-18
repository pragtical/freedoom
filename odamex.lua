-- SPDX-License-Identifier: MIT
local system = require "system"
local odamex = {master = "master1.odamex.net:15000"}
local challenge = 777123
local version = 12 * 256 + 30
local modes = {[0] = "Co-op", "Deathmatch", "Team DM", "CTF", "Horde"}

local function word(value)
  return string.char(value % 256, math.floor(value / 256) % 256,
    math.floor(value / 65536) % 256, math.floor(value / 16777216) % 256)
end

local function reader(data)
  local pos = 1
  local function bytes(n)
    local last = pos + n - 1
    assert(last <= #data)
    local value = data:sub(pos, last)
    pos = last + 1
    return value
  end
  local function number(n)
    local value = 0
    local raw = bytes(n)
    for i = n, 1, -1 do value = 256 * value + raw:byte(i) end
    return value
  end
  local function string_()
    local end_ = data:find("\0", pos, true)
    assert(end_ and end_ - pos < 1024)
    return bytes(end_ - pos + 1):sub(1, -2)
  end
  return bytes, number, string_
end

function odamex.parse_master(data)
  local _, number = reader(data)
  if #data < 6 or number(4) ~= challenge then return end
  local count = number(2)
  if #data < 6 + count * 6 then return end
  local entries, seen = {}, {}
  for _ = 1, math.min(count, 256) do
    local host = ("%d.%d.%d.%d"):format(number(1), number(1), number(1), number(1))
    local port = number(2)
    local endpoint = host .. ":" .. port
    if port > 0 and not seen[endpoint] then
      entries[#entries + 1] = {host = host, port = port, endpoint = endpoint}
      seen[endpoint] = true
    end
  end
  return entries
end

function odamex.parse_server(data, cookie)
  local bytes, number, string_ = reader(data)
  local function parse()
    local tag = number(4)
    if tag ~= 0xad032003 then return end
    local packed, protocol = number(4), number(4)
    if protocol < 6 or protocol > 8 or number(4) ~= cookie then return end
    number(4)
    if protocol >= 7 then string_() else number(4) end
    local function display(text) return text:gsub("%c", ""):sub(1, 160) end
    local cvars = {}
    for _ = 1, number(1) do
      local key, type_ = string_(), number(1)
      if type_ >= 2 and type_ <= 4 then cvars[key] = number(2 ^ (type_ - 2))
      elseif type_ == 5 or type_ == 6 then cvars[key] = string_()
      else assert(type_ == 0 or type_ == 1 or type_ == 255) end
    end
    local password = #bytes(number(1)) > 0
    local map = display(string_())
    if tonumber(cvars.sv_timelimit or 0) ~= 0 then number(2) end
    local mode = cvars.sv_gametype or 0
    if mode == 2 or mode == 3 then
      for _ = 1, number(1) do string_(); bytes(6) end
    end
    for _ = 1, number(1) do string_() end
    local wads = {}
    for _ = 1, number(1) do
      wads[#wads + 1] = display(string_())
      bytes(number(1))
    end
    local players = number(1)
    for _ = 1, players do string_(); bytes(15 + ((mode == 2 or mode == 3) and 1 or 0)) end
    local major, minor, patch = math.floor(packed / 256), math.floor(packed % 256 / 10), packed % 256 % 10
    return {engine = "odamex", version = ("%d.%d.%d"):format(major, minor, patch),
      protocol = major == 12 and minor <= 3, description = display(cvars.sv_hostname or "Odamex"),
      players = players, max_players = cvars.sv_maxclients or cvars.sv_maxplayers or 255,
      game_type = modes[mode] or "Other", map = map, password = password, iwad = wads[2],
      addons = table.concat(wads, ", ", 3)}
  end
  local ok, server = pcall(parse)
  if ok then return server end
end

local function wait_addresses(items, live)
  local deadline = system.get_time() + 2
  while live() and system.get_time() < deadline do
    local waiting
    for _, item in ipairs(items) do
      if item.address and item.address:get_status() == "waiting" then waiting = true; break end
    end
    if not waiting then return true end
    coroutine.yield(.02)
  end
  return live()
end

function odamex.discover(endpoint, live)
  local net = require "net"
  local master, err = require("plugins.freedoom.multiplayer").resolve_master(endpoint, live)
  if not master then return nil, err end
  local host, port = master:match("^(.-):(%d+)$")
  local local_ = {address = net.resolve_address("0.0.0.0")}
  local remote = {address = net.resolve_address(host)}
  if not wait_addresses({local_, remote}, live) then return end
  if not local_.address or local_.address:get_status() ~= "success"
      or not remote.address or remote.address:get_status() ~= "success" then
    return nil, "Cannot open the Odamex directory connection"
  end
  local socket, failure = net.open_udp(local_.address, 0)
  if not socket then return nil, failure end
  local function scan()
    local sent, send_error = socket:send(remote.address, tonumber(port), word(challenge))
    if not sent then return nil, send_error end
    local entries, deadline = nil, system.get_time() + 3
    while live() and not entries and system.get_time() < deadline do
      for _ = 1, 32 do
        local packet = socket:receive()
        if not packet then break end
        if packet:get_port() == tonumber(port) and packet:get_address():get_ip() == host then
          entries = odamex.parse_master(packet:get_data())
          if entries then break end
        end
      end
      if not entries then coroutine.yield(.025) end
    end
    if not entries then return nil, "Odamex directory did not respond" end
    local cookie = math.floor(system.get_time() * 1000000) % 4294967296
    local query = word(0xad011002) .. word(version) .. word(8) .. word(cookie)
    local requests, remaining = {}, 0
    for _, item in ipairs(entries) do item.address = net.resolve_address(item.host) end
    if not wait_addresses(entries, live) then return end
    for _, item in ipairs(entries) do
      if item.address and item.address:get_status() == "success" and socket:send(item.address, item.port, query) then
        requests[item.endpoint] = system.get_time()
        remaining = remaining + 1
      end
    end
    local results = {}
    deadline = system.get_time() + 2
    while live() and remaining > 0 and system.get_time() < deadline do
      for _ = 1, 64 do
        local packet = socket:receive()
        if not packet then break end
        local address = packet:get_address():get_ip() .. ":" .. packet:get_port()
        if requests[address] then
          local server = odamex.parse_server(packet:get_data(), cookie)
          if server then
            server.address = address
            server.ping = math.floor((system.get_time() - requests[address]) * 1000)
            results[#results + 1] = server
            requests[address], remaining = nil, remaining - 1
          end
        end
      end
      if remaining > 0 then coroutine.yield(.015) end
    end
    table.sort(results, function(a, b)
      if a.players ~= b.players then return a.players > b.players end
      if a.ping ~= b.ping then return a.ping < b.ping end
      return a.address < b.address
    end)
    return results
  end
  local ok, result, message = pcall(scan)
  socket:close()
  if not ok then return nil, tostring(result) end
  return result, message
end

return odamex
