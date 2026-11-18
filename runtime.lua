-- SPDX-License-Identifier: MIT
local ffi = require "ffi"
local directory = debug.getinfo(1, "S").source:sub(2):match("^(.*)[/\\\\]")
directory = assert(require("system").absolute_path(directory), "Cannot resolve Freedoom plugin directory")
ffi.cdef [[
typedef struct fd_session fd_session;
typedef struct fd_reader fd_reader;
typedef struct {
  unsigned mission, gamemode, network;
  char title[96];
  unsigned char checksum[20];
} fd_iwad;
int fd_iwad_info(const char *path, fd_iwad *output);
typedef struct {
  int mode, port, deathmatch, episode, map, skill;
  char address[64], name[32], master[64];
} fd_net_options;
typedef struct {
  char address[64], description[128], version[32];
  unsigned players, max_players, state, ping, mission, protocol, compatible, unassigned, phase, gamemode;
} fd_net_server;
typedef struct {
  unsigned mode, phase, controller, num_players, max_players, server_count, revision;
  unsigned tic, player_mask, limited;
  int consoleplayer, deathmatch, map, episode, skill;
  char message[160], names[4][32], addresses[4][64];
  fd_net_server servers[32];
  unsigned char wanted_iwad[20];
} fd_net_state;
typedef struct {
  unsigned generation, sample, playing, paused, looping;
  float gain, pan, rate, offset;
} fd_voice_state;
typedef struct {
  fd_voice_state voices[32];
  unsigned music_generation, music_paused;
  float music_gain;
} fd_audio_state;
unsigned fd_abi_version(void);
fd_session *fd_start(const char *directory, const char *state, const char *iwad, int sound);
fd_session *fd_odamex_start(const char *directory, const char *state, const char *iwad,
  const char *endpoint, const char *name, const char *password, int sound, int download);
int fd_odamex_is_online(fd_session *session);
fd_session *fd_start_network(const char *directory, const char *state, const char *iwad,
  int sound, const fd_net_options *options);
int fd_network_snapshot(fd_session *session, fd_net_state *state);
int fd_network_launch(fd_session *session);
int fd_network_refresh(fd_session *session);
int fd_network_configure(fd_session *session, const fd_net_options *options);
unsigned fd_network_options_size(void);
unsigned fd_network_state_size(void);
int fd_status(fd_session *session);
int fd_set_truecolor(fd_session *session, int enabled);
int fd_input(fd_session *session, const unsigned char *packets, unsigned length);
void fd_stop(fd_session *session);
void fd_close(fd_session *session);
unsigned fd_read_log(fd_session *session, char *output, unsigned capacity);
fd_reader *fd_frame_reader(fd_session *session);
int fd_copy_frame(fd_reader *reader, uint32_t *pixels, unsigned width,
                  unsigned height, uint32_t *serial, int smooth);
int fd_audio_snapshot(fd_session *session, fd_audio_state *state);
unsigned fd_audio_sample(fd_session *session, unsigned id, void *output, unsigned capacity,
                         unsigned *rate, unsigned *encoded);
void fd_voice_finished(fd_session *session, unsigned channel, unsigned generation);
void fd_voice_progress(fd_session *session, unsigned channel, unsigned generation, float position);
unsigned fd_music_read(fd_session *session, int16_t *output, unsigned frames, unsigned generation);
void fd_audio_disable(fd_session *session);
]]
local cpu = ({x64 = "x86_64", arm64 = "aarch64", x86 = "i686"})[ffi.arch] or ffi.arch
local arch = ARCH or cpu .. "-" .. (ffi.os == "OSX" and "darwin" or ffi.os:lower())
local suffix = ffi.os == "Windows" and ".dll" or ".so"
local ok, library = pcall(ffi.load, directory .. "/freedoom-bridge." .. arch .. suffix)
assert(ok, "Freedoom runtime missing for " .. arch .. "; install the matching PPM package or build with Meson. " .. tostring(library))
assert(library.fd_abi_version() == 1, "Freedoom native ABI mismatch; restart Pragtical")
assert(pcall(function() return library.fd_start end), "Freedoom runtime is outdated; rebuild and restart Pragtical")
local Frame = {}
Frame.__index = Frame
local music_capacity = 3968
local closing
function Frame.is_closing() return closing ~= nil end
local odamex_path = directory .. "/freedoom-odamex-host." .. arch .. suffix
local assets = directory .. "/assets"
function Frame.odamex_installed() return require("system").get_file_info(odamex_path) ~= nil end
function Frame.iwad_info(path)
  assert(pcall(function() return library.fd_iwad_info end), "Freedoom runtime is outdated; rebuild and restart Pragtical")
  local info = ffi.new("fd_iwad[1]")
  if library.fd_iwad_info(path, info) == 0 then return end
  return {title = ffi.string(info[0].title), mission = tonumber(info[0].mission),
    gamemode = tonumber(info[0].gamemode), network = info[0].network ~= 0,
    checksum = ffi.string(info[0].checksum, 20)}
end

function Frame.new(state, iwad, sound, network, odamex)
  local library = odamex and ffi.load(odamex_path) or library
  assert(library.fd_abi_version() == 1, "Freedoom native ABI mismatch; restart Pragtical")
  local options
  if network then
    local checked, size = pcall(function() return library.fd_network_options_size() end)
    local state_checked, state_size = pcall(function() return library.fd_network_state_size() end)
    assert(checked and size == ffi.sizeof("fd_net_options") and state_checked and state_size == ffi.sizeof("fd_net_state"),
      "Freedoom networking runtime is outdated; rebuild and restart Pragtical")
    options = ffi.new("fd_net_options")
    for _, name in ipairs {"mode", "port", "deathmatch", "episode", "map", "skill"} do
      options[name] = network[name] or 0
    end
    local address, name, master = network.address or "", network.name or "Player", network.resolved_master or ""
    assert(#address < 64 and #name < 30 and #master < 64, "Server address or player name is too long")
    ffi.copy(options.address, address); ffi.copy(options.name, name)
    ffi.copy(options.master, master)
  end
  local mixer, audio, warning
  if sound then
    local loaded, module = pcall(require, "audio")
    if loaded then
      audio = module
      mixer, warning = audio.create_mixer({max_voices = 33})
    else
      warning = "Pragtical audio API unavailable"
    end
  end
  local handle
  if odamex then
    handle = library.fd_odamex_start(directory, state, iwad, odamex.endpoint, odamex.name,
      odamex.password, mixer and 1 or 0, odamex.download and 1 or 0)
  elseif options then
    handle = library.fd_start_network(assets, state, iwad, mixer and 1 or 0, options)
  else
    handle = library.fd_start(assets, state, iwad, mixer and 1 or 0)
  end
  if handle == nil then
    if mixer then mixer:close() end
    error("Cannot start Freedoom: invalid options or another engine session is still closing")
  end
  return setmetatable({handle = ffi.gc(handle, library.fd_close),
    reader = library.fd_frame_reader(handle), library = library,
    serial = ffi.new("uint32_t[1]"), log_buffer = ffi.new("char[8192]"),
    audio = audio, mixer = mixer, warning = warning, sounds = {}, voices = {},
    audio_state = ffi.new("fd_audio_state[1]"), pcm = ffi.new("int16_t[?]", music_capacity * 2),
    sample_rate = ffi.new("unsigned[1]"), encoded = ffi.new("unsigned[1]"),
    network_state = network and ffi.new("fd_net_state[1]")}, Frame)
end

function Frame:network_info()
  if not self.network_state or self.library.fd_network_snapshot(self.handle, self.network_state) == 0 then
    return self.network_info_cache
  end
  local state = self.network_state[0]
  if self.network_revision == state.revision then return self.network_info_cache end
  self.network_revision = tonumber(state.revision)
  local info = {names = {}, addresses = {}, servers = {}, message = ffi.string(state.message)}
  info.wanted_iwad = ffi.string(state.wanted_iwad, 20)
  for _, name in ipairs {"mode", "phase", "controller", "num_players", "max_players", "server_count",
      "tic", "player_mask", "limited", "consoleplayer", "deathmatch", "map", "episode", "skill"} do info[name] = tonumber(state[name]) end
  for i = 0, 3 do
    info.names[i + 1], info.addresses[i + 1] = ffi.string(state.names[i]), ffi.string(state.addresses[i])
  end
  for i = 0, math.min(32, info.server_count) - 1 do
    local server = state.servers[i]
    info.servers[i + 1] = {address = ffi.string(server.address), description = ffi.string(server.description),
      version = ffi.string(server.version), players = tonumber(server.players),
      max_players = tonumber(server.max_players), state = tonumber(server.state), ping = tonumber(server.ping),
      mission = tonumber(server.mission), gamemode = tonumber(server.gamemode),
      protocol = server.protocol ~= 0, compatible = server.compatible ~= 0,
      unassigned = server.unassigned ~= 0, phase = tonumber(server.phase)}
  end
  self.network_info_cache = info
  return info
end
function Frame:launch() return self.handle and self.library.fd_network_launch(self.handle) ~= 0 end
function Frame:refresh() return self.handle and self.library.fd_network_refresh(self.handle) ~= 0 end
function Frame:configure(settings)
  local options = ffi.new("fd_net_options")
  for _, name in ipairs {"deathmatch", "episode", "map", "skill"} do options[name] = settings[name] end
  return self.handle and self.library.fd_network_configure(self.handle, options) ~= 0
end

function Frame:status() return self.handle and self.library.fd_status(self.handle) or 2 end
function Frame:set_truecolor(enabled)
  if not self.handle then return false end
  local ok, setter = pcall(function() return self.library.fd_set_truecolor end)
  return ok and setter(self.handle, enabled and 1 or 0) ~= 0
end
function Frame:write(packets)
  return tonumber(self.library.fd_input(self.handle, packets, #packets))
end
function Frame:log()
  local length = self.library.fd_read_log(self.handle, self.log_buffer, 8192)
  return ffi.string(self.log_buffer, length)
end
function Frame:read(width, height, smooth)
  if self.width ~= width or self.height ~= height then
    self.buffer = ffi.new("uint32_t[?]", width * height)
    self.width, self.height, self.serial[0] = width, height, 0
  end
  if self.smooth ~= smooth then self.smooth, self.serial[0] = smooth, 0 end
  local result = self.library.fd_copy_frame(self.reader, self.buffer, width, height, self.serial,
    smooth and 1 or 0)
  assert(result >= 0, "Invalid Freedoom framebuffer")
  if result == 1 then return ffi.string(self.buffer, width * height * 4) end
end

function Frame:load_sound(id)
  if self.sounds[id] ~= nil then return self.sounds[id] end
  local size = tonumber(self.library.fd_audio_sample(self.handle, id, nil, 0, self.sample_rate, self.encoded))
  if size == 0 then return nil end
  local buffer = ffi.new("uint8_t[?]", size)
  if self.library.fd_audio_sample(self.handle, id, buffer, size, self.sample_rate, self.encoded) ~= size then return nil end
  local bytes = ffi.string(buffer, size)
  local sound, err
  if self.encoded[0] ~= 0 then
    sound, err = self.audio.load_memory(bytes, {predecode = true})
  else
    sound, err = self.audio.new_sound(bytes, {format = "u8", channels = 1,
      sample_rate = tonumber(self.sample_rate[0])})
  end
  self.sounds[id] = sound or false
  return sound, err
end

function Frame:set_paused(paused)
  if self.mixer then
    if paused then self.mixer:pause() else self.mixer:resume() end
  end
end

local function terminal(voice)
  local state, err = voice:get_state()
  assert(state, err)
  return state == "finished" or state == "stopped"
end

-- Playback can finish between any two Lua calls, including a state check and
-- its following setter. Only a still-active voice's failure is an audio error.
local function control(voice, method, ...)
  local ok, err = voice[method](voice, ...)
  if ok then return true end
  if terminal(voice) then return false end
  error(err or "Audio voice update failed", 0)
end

local function parameter(item, name, value, method)
  if item[name] == value then return true end
  if not control(item.voice, method, tonumber(value)) then return false end
  item[name] = value
  return true
end

local function update_voice(item, wanted)
  local voice = item.voice
  if terminal(voice)
      or not parameter(item, "gain", wanted.gain, "set_gain")
      or not parameter(item, "pan", wanted.pan, "set_pan")
      or not parameter(item, "rate", wanted.rate, "set_rate") then return false end
  if item.paused ~= wanted.paused then
    if not control(voice, wanted.paused ~= 0 and "pause" or "resume") then return false end
    item.paused = wanted.paused
  end
  return true
end

function Frame:audio_update()
  if not self.mixer or self.library.fd_audio_snapshot(self.handle, self.audio_state) == 0 then return end
  local state = self.audio_state[0]
  for channel = 0, 31 do
    local wanted = state.voices[channel]
    local generation = tonumber(wanted.generation)
    local item = self.voices[channel]
    if item and (wanted.playing == 0 or generation ~= item.generation) then
      item.voice:stop()
      self.voices[channel], item = nil, nil
    end
    if wanted.playing ~= 0 then
      if not item then
        local sound, err = self:load_sound(tonumber(wanted.sample))
        if sound then
          local voice
          voice, err = self.mixer:play(sound, {gain = tonumber(wanted.gain),
            pan = tonumber(wanted.pan), rate = tonumber(wanted.rate),
            start = tonumber(wanted.offset), loops = wanted.looping ~= 0 and -1 or 0,
            paused = wanted.paused ~= 0})
          if voice then
            item = {voice = voice, generation = generation, gain = wanted.gain,
              pan = wanted.pan, rate = wanted.rate, paused = wanted.paused}
            self.voices[channel] = item
          end
        end
        if not item and (sound == false or err) then
          self.library.fd_voice_finished(self.handle, channel, generation)
        end
      end
      if item then
        local voice = item.voice
        if not update_voice(item, wanted) then
          self.library.fd_voice_finished(self.handle, channel, generation)
          self.voices[channel] = nil
        else
          local position = voice:get_position()
          if position then self.library.fd_voice_progress(self.handle, channel, generation, position) end
        end
      end
    end
  end
  if self.music and terminal(self.music) then self.music = nil end
  if not self.music then
    self.music = assert(self.mixer:play_stream(
      {format = "s16", channels = 2, sample_rate = 49716}, {paused = true}))
    self.music_generation, self.music_gain = nil, nil
  end
  local music = self.music
  local generation = tonumber(state.music_generation)
  if self.music_generation ~= generation then
    if not control(music, "clear") then return end
    self.music_generation = generation
  end
  if self.music_gain ~= state.music_gain then
    if not control(music, "set_gain", tonumber(state.music_gain)) then return end
    self.music_gain = state.music_gain
  end
  if state.music_paused ~= 0 then
    control(music, "pause")
  else
    local queued, err = music:get_queued_bytes()
    if not queued then
      if terminal(music) then return end
      error(err, 0)
    end
    local capacity = math.max(0, music_capacity - math.floor(queued / 4))
    if capacity > 0 then
      local frames = self.library.fd_music_read(self.handle, self.pcm, capacity, generation)
      if frames > 0 then
        if not control(music, "write", ffi.string(self.pcm, frames * 4)) then return end
        queued = queued + frames * 4
      end
    end
    if queued > 0 then control(music, "resume") end
  end
end

function Frame:mute()
  if self.handle then self.library.fd_audio_disable(self.handle) end
  if self.mixer then self.mixer:close(); self.mixer = nil end
  for _, sound in pairs(self.sounds) do if sound then sound:close() end end
  self.sounds, self.voices = {}, {}
end

function Frame:stop()
  if self.handle then closing = self; self.library.fd_stop(self.handle) end
  self:mute()
end

function Frame:close()
  self:stop()
  if self.handle then
    self.library.fd_close(ffi.gc(self.handle, nil))
    self.handle, self.reader, self.buffer = nil, nil, nil
  end
  if closing == self then closing = nil end
end
return Frame
