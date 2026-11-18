-- SPDX-License-Identifier: GPL-2.0-or-later
-- Run with an isolated profile. This harness also accepts the helper checkpoint.
local core = require "core"
local common = require "core.common"
local command = require "core.command"
local config = require "core.config"
local system = require "system"
local json = require "core.json"
local renderer = require "renderer"
local tag = os.getenv("FREEDOOM_BENCH_TAG") or "native"
local duration = tonumber(os.getenv("FREEDOOM_BENCH_SECONDS")) or 12
config.fps, core.fps, config.auto_fps = 75, 75, true
config.plugins.freedoom = common.merge(config.plugins.freedoom or {}, {
  phase = 2, sound = false, filter = "linear", widescreen = true, resolution_scale = 1,
  state_dir = USERDIR .. "/benchmark-state"
})
local Freedoom = require "plugins.freedoom"
local Frame = require "plugins.freedoom.runtime"
local measured, frames, fresh, read_time = false, 0, 0, 0
local last_frame, intervals = nil, {}
local read = Frame.read
function Frame:read(...)
  local start = system.get_time()
  local pixels = read(self, ...)
  if measured and pixels then fresh, read_time = fresh + 1, read_time + system.get_time() - start end
  return pixels
end
local present = renderer.end_frame
renderer.end_frame = function(...)
  local result = present(...)
  if measured then
    local now = system.get_time()
    if last_frame then intervals[#intervals + 1] = now - last_frame end
    last_frame, frames = now, frames + 1
  end
  return result
end
local function text(path)
  local file = io.open(path, "r")
  if not file then return "" end
  local value = file:read("*a"); file:close(); return value
end
local function cpu(pid)
  local fields = {}
  for value in (text("/proc/" .. pid .. "/stat"):match("^.*%)%s+(.+)$") or ""):gmatch("%S+") do
    fields[#fields + 1] = value
  end
  return ((tonumber(fields[12]) or 0) + (tonumber(fields[13]) or 0)) / 100
end
local function pss(pid)
  return (tonumber(text("/proc/" .. pid .. "/smaps_rollup"):match("\nPss:%s+(%d+)")) or 0) / 1024
end
local function key(name)
  core.on_event("keypressed", name); coroutine.yield(.08)
  core.on_event("keyreleased", name); coroutine.yield(.2)
end
core.add_thread(function()
  local view
  local ok, err = xpcall(function()
    coroutine.yield(.3)
    system.window_has_focus = function() return true end
    system.set_window_mode(core.window, "maximized")
    assert(command.perform("freedoom:open")); view = core.active_view
    local timeout = system.get_time() + 15
    while not view.pixels and system.get_time() < timeout do coroutine.yield(.02) end
    assert(view.frame and view.pixels, tostring(view.status) .. view.log)
    coroutine.yield(2)
    key("escape"); key("return"); key("return"); coroutine.yield(2)
    core.on_event("keypressed", "w"); core.on_event("keypressed", "right")
    coroutine.yield(1)
    local pid = system.get_process_id()
    local child = view.process and view.process:pid()
    local initial_cpu = cpu(pid) + (child and cpu(child) or 0)
    local start = system.get_time(); measured = true
    coroutine.yield(duration)
    measured = false
    local elapsed = system.get_time() - start
    local used_cpu = cpu(pid) + (child and cpu(child) or 0) - initial_cpu
    table.sort(intervals)
    print("RESULT " .. json.encode({tag = tag, seconds = elapsed,
      backend = core.window:get_renderer_info(), width = view.width, height = view.height,
      fps = frames / elapsed, fresh_fps = fresh / elapsed,
      snapshot_ms = read_time / math.max(1, fresh) * 1000,
      combined_cpu_percent = used_cpu / elapsed * 100,
      pss_mb = pss(pid) + (child and pss(child) or 0),
      p99_ms = (intervals[math.max(1, math.ceil(#intervals * .99))] or 0) * 1000}))
    core.on_event("keyreleased", "w"); core.on_event("keyreleased", "right")
  end, debug.traceback)
  if view then view:stop() end
  if not ok then print("FAIL " .. err) end
  coroutine.yield(.3)
  core.quit(true, ok and 0 or 1)
end)
