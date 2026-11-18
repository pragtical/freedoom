-- SPDX-License-Identifier: GPL-2.0-or-later
local ffi = require "ffi"
local library, root = assert(arg[1]), assert(arg[2])
local load_library = ffi.load
ffi.load = function() return load_library(library) end
package.loaded.system = {absolute_path = function(path) return path end}
local Frame = assert(loadfile(root .. "/runtime.lua"))()
ffi.load = load_library

local function stream(state)
  local voice = {state = state or "paused", resumes = 0}
  function voice:get_state() return self.state end
  function voice:clear() return true end
  function voice:set_gain() return true end
  function voice:pause() self.state = "paused"; return true end
  function voice:resume() self.resumes = self.resumes + 1; return true end
  function voice:get_queued_bytes() return 0 end
  return voice
end
local function fixture()
  local state = ffi.new("fd_audio_state[1]")
  state[0].music_generation, state[0].music_gain, state[0].music_paused = 1, 1, 1
  state[0].voices[0].generation, state[0].voices[0].playing = 3, 1
  state[0].voices[0].gain, state[0].voices[0].rate = .5, 1
  local ended = 0
  local native = {
    fd_audio_snapshot = function(_, output) output[0] = state[0]; return 1 end,
    fd_voice_finished = function(_, channel, generation)
      assert(channel == 0 and generation == 3); ended = ended + 1
    end,
    fd_voice_progress = function() end,
    fd_music_read = function() return 0 end
  }
  local mixer = {created = 0}
  function mixer:play_stream() self.created = self.created + 1; return stream() end
  local frame = setmetatable({handle = {}, library = native, mixer = mixer,
    voices = {}, music = stream(), music_generation = 1, music_gain = 1,
    audio_state = ffi.new("fd_audio_state[1]"), pcm = ffi.new("int16_t[8192]")}, Frame)
  return frame, state, function() return ended end
end

for _, method in ipairs {"set_gain", "set_pan", "set_rate", "pause", "resume", "already_finished"} do
  local frame, state, ended = fixture()
  local voice = {state = method == "already_finished" and "finished" or "playing"}
  function voice:get_state() return self.state end
  function voice:get_position() return 0 end
  for _, name in ipairs {"set_gain", "set_pan", "set_rate", "pause", "resume"} do
    voice[name] = function(self)
      if name == method then self.state = "finished"; return nil, "voice has finished or stopped" end
      return true
    end
  end
  local item = {voice = voice, generation = 3, gain = .5, pan = 0, rate = 1, paused = 0}
  if method == "set_gain" then item.gain = 0
  elseif method == "set_pan" then item.pan = -1
  elseif method == "set_rate" then item.rate = 2
  elseif method == "pause" then state[0].voices[0].paused = 1
  elseif method == "resume" then item.paused = 1 end
  frame.voices[0] = item
  frame:audio_update()
  assert(frame.mixer and not frame.voices[0] and ended() == 1, method)
end

do
  local frame = fixture()
  frame.voices[0] = {generation = 3, gain = 0, pan = 0, rate = 1, paused = 0,
    voice = {get_state = function() return "playing" end,
      set_gain = function() return nil, "device failure" end}}
  local ok, err = pcall(frame.audio_update, frame)
  assert(not ok and tostring(err):find("device failure", 1, true), "Real errors were hidden")
end
do
  local frame, state = fixture()
  state[0].voices[0].playing, state[0].music_paused = 0, 0
  frame.music = stream("finished")
  frame:audio_update()
  assert(frame.mixer.created == 1 and frame.music.resumes == 0,
    "Finished stream did not recover, or an empty stream was resumed")
end
do
  local frame, state = fixture()
  state[0].voices[0].playing, state[0].music_gain = 0, .7
  function frame.music:set_gain()
    self.state = "finished"; return nil, "voice has finished or stopped"
  end
  frame:audio_update()
  frame:audio_update()
  assert(frame.mixer.created == 1 and frame.music:get_state() == "paused",
    "Music completion during a control disabled audio")
end
do
  local frame = setmetatable({handle = {}, network_state = ffi.new("fd_net_state[1]"), library = {
    fd_network_snapshot = function(_, output)
      output[0].server_count = 1
      local server = output[0].servers[0]
      ffi.copy(server.version, "Chocolate Hexen 3.1.1")
      server.mission, server.gamemode = 7, 2
      return 1
    end
  }}, Frame)
  local server = frame:network_info().servers[1]
  assert(server.mission == 7 and server.gamemode == 2 and server.version == "Chocolate Hexen 3.1.1")
end
print("PASS voice completion races, real errors, stream recovery, empty startup and server metadata")
