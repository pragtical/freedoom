# Freedoom for Pragtical

Play Freedoom and FreeDM in a Pragtical tab using the Woof or Odamex engine.

> [!NOTE]
> This independent plugin is not affiliated with or endorsed by the Freedoom
> project.

## Install and Play

Requires Pragtical with LuaJIT. The release packages include both engines for
Linux and macOS (x86_64 and aarch64) and Windows (x86_64).

In Plugin Manager, search for **Freedoom for Pragtical** and install it. The
installation includes the Lua plugin, the native libraries and resources for
your platform, and the two Freedoom campaigns plus FreeDM.

For a manual install, copy the matching release checkout into
`USERDIR/plugins/freedoom/` with `init.lua` at its root. Extract your platform
archive and the shared `freedoom-iwads-0.13.0.zip` into that directory. Restart
Pragtical after installing or replacing the native libraries.

Run **Freedoom: Open** from the command palette to start the configured
campaign in Woof. Press **Esc** and choose **New Game**.

## Additional Games

Use **Freedoom: Select Game** to open FreeDM or another supported IWAD in Woof.
Put legally obtained IWADs directly in the plugin's [`iwads/`](iwads/README.md)
directory. See that guide for supported games and filenames. For a collection
outside the plugin that survives upgrades, set **Additional IWAD Directory**
in the plugin settings. The bundled games stay available.

## Controls

These are the default Woof controls; Odamex has its own in-game bindings.

| Input | Action |
| --- | --- |
| W / S or Up / Down | Move forward / backward |
| A / D | Strafe |
| Left / Right | Turn |
| Shift | Run |
| Ctrl | Fire |
| E / Space | Use doors and switches |
| 1-7 | Select weapon |
| Tab | Automap |
| Esc | Game menu |
| F2 / F3 | Save / load |
| F12 | Release input; press again to resume |
| Click the game | Capture the mouse without firing |
| Captured mouse movement | Turn |
| Captured left / right / middle button | Fire / use / move forward |

F12 pauses single-player but does not pause multiplayer. Switching tabs or
losing focus also releases input. When mouse capture is unavailable,
left-click fires and right-drag turns. Quitting from the game closes the tab.

Use **Freedoom: Restart** to restart the current game, or
**Freedoom: Toggle Filter** to switch between smooth and crisp scaling.

## Multiplayer

### Woof

Run **Freedoom: Multiplayer**. Choose an installed game, player name, and UDP
port (2342 by default).

- **Host Game** opens a private lobby. Choose the rules and map, wait for
  players to join, then click **Start Game**. The controller can change these
  options in **Room Settings** before launch and change the IWAD while alone.
- **Join Game** connects to an IPv4 address or `localhost`. Use the host's
  port. LAN discovery also requires UDP broadcast on the same network.
- **Find LAN Games** and **Find Internet Games** list available rooms. Internet
  discovery contacts the Chocolate Doom master server only when requested.
  Right-click a result to copy its address. Hosting does not publish your game.

Woof supports co-op and deathmatch for up to four players. Compatible Chocolate
Doom and Crispy Doom servers also work; Deathmatch 3.0 requires Woof.
Players need matching game data, and games cannot be joined after launch.
Woof will try a matching installed IWAD when the host identifies one, but
does not download missing files. Press **T** in-game to chat; the waiting room
has no chat. Closing the host's game tab ends the session.

### Odamex

Run **Freedoom: Odamex Multiplayer** to **Browse Servers**, join by address,
or **Start Offline**. Use Odamex's in-game controls for chat and spectating.
Server passwords are not saved. **Freedoom: Open** still uses Woof.

Install the server's IWAD locally before joining. **Odamex WAD Downloads**
is off by default; enable it to let Odamex fetch eligible add-on WADs and
reconnect. It does not download commercial IWADs. Use trusted servers and
mods: the engines parse WADs inside the editor process, so a native crash can
also crash Pragtical.

## Settings and Saves

Open **Settings > Plugins > Freedoom** to choose a default campaign or player
name, change display and mouse settings, or set a save directory. Changes to
the campaign, sound, and save directory require **Freedoom: Restart**.

New installs store saves and engine settings by engine and game under
`USERDIR/storage/freedoom/`. An existing local `state/` directory is still
used unless you set **Save Directory**.

The engines render to fit the tab. Lower **Resolution Scale** to reduce
rendering cost. In Woof, **Options > General > Video > True Color** toggles
RGB lighting and is on by default. For a smaller Odamex HUD, open **Options >
Display Options > Heads-up display** and disable **Scale status bar** or
**Scale HUD elements**. Use Pragtical's fullscreen controls for either engine.

Sound effects and MUS/MIDI music play through Pragtical. Digital music tracks
are not supported in the Odamex integration.

## Build and Test

See the [development guide](docs/development.md) for building, testing, and
packaging the plugin.

## Licenses

- The Lua integration is [MIT](LICENSE). The native bridges remain
  [GPL-2.0-or-later](src/COPYING).
- The [Woof](https://github.com/fabiangreffrath/woof) and
  [Odamex](https://github.com/odamex/odamex) engines, resources, and dependencies
  retain their upstream licenses. Native packages include their notices, and
  releases provide the corresponding source archive.
- [Freedoom and FreeDM 0.13.0](https://github.com/freedoom/freedoom/releases/tag/v0.13.0)
  are BSD-3-Clause game data. The shared IWAD archive includes the license and
  credits in `licenses/Freedoom/`.
