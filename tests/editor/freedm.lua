-- SPDX-License-Identifier: GPL-2.0-or-later
local core = require "core"
local config = require "core.config"
local command = require "core.command"
local test = require "core.test"
local system = require "system"
local Freedoom = require "plugins.freedoom"
local games = require "plugins.freedoom.games"
local settings = config.plugins.freedoom
local freedm_path = "[/\\]iwads[/\\]freedm%.wad$"

local function wait_for_frame(view)
  local deadline = system.get_time() + 8
  while not view.pixels and not view.failed and system.get_time() < deadline do
    coroutine.yield(.01)
  end
  test.ok(view.pixels, view.status)
end

test.describe("bundled FreeDM", function()
  test.before_each(function(context)
    context.iwad_dir, context.phase, context.sound, context.state_dir =
      settings.iwad_dir, settings.phase, settings.sound, settings.state_dir
    settings.iwad_dir = games.default_directory
    settings.phase = 2
    settings.sound = false
    settings.state_dir = USERDIR .. PATHSEP .. "freedm-test-state"
  end)

  test.after_each(function(context)
    settings.iwad_dir, settings.phase, settings.sound, settings.state_dir =
      context.iwad_dir, context.phase, context.sound, context.state_dir
    if core.command_view:is_open() then core.command_view:exit(false) end
    local view = context.view
    if view and view:is(Freedoom) then
      local root = core.root_view.root_node
      local node = root:get_node_for_view(view)
      if node then node:close_view(root, view) end
      local deadline = system.get_time() + 5
      while view.stopping and system.get_time() < deadline do coroutine.yield(.01) end
      test.is_nil(view.stopping, "Game worker did not stop")
    end
  end)

  test.test("recognizes FreeDM as a separate verified IWAD", function()
    local available = games.scan()
    local freedm = test.not_nil(games.for_odamex("FREEDM.WAD", available))
    test.is_nil(freedm.phase)
    test.ok(freedm.network)
    test.equal(freedm.mission, 8)
    test.equal(freedm.gamemode, 2)
    test.match(freedm.path, freedm_path)
    local phase2 = test.not_nil(games.freedoom(2, available))
    test.not_equal(freedm.checksum, phase2.checksum)
    test.equal(games.for_checksum(freedm.checksum, available), freedm)
  end)

  test.test("opens FreeDM in the game picker with a custom IWAD directory", function(context)
    settings.iwad_dir = USERDIR .. PATHSEP .. "no-additional-iwads"
    test.ok(command.perform("freedoom:select-game"))
    local palette = core.command_view
    palette:set_text("freedm")
    palette:update_suggestions()
    local selection
    for index, suggestion in ipairs(palette.suggestions) do
      if suggestion.game.path:match(freedm_path) then selection = index; break end
    end
    palette.suggestion_idx = test.not_nil(selection, "FreeDM is missing from the game picker")
    palette:submit()
    context.view = core.active_view
    test.ok(context.view:is(Freedoom))
    wait_for_frame(context.view)
    test.is_nil(context.view.game.phase)
    test.equal(settings.phase, 2)
  end)

  test.test("keeps the Open command on the configured campaign", function(context)
    test.ok(command.perform("freedoom:open"))
    context.view = core.active_view
    test.ok(context.view:is(Freedoom))
    wait_for_frame(context.view)
    test.equal(context.view.game.phase, 2)
    test.match(context.view.game.path, "[/\\]iwads[/\\]freedoom2%.wad$")
  end)
end)
