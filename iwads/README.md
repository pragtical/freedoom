# Additional IWADs

Place legally obtained IWAD files directly in this folder. Keep their recognized
filenames; case does not matter. Subdirectories are not scanned.

| Game | Filename |
| --- | --- |
| Doom / The Ultimate Doom | `doom.wad`, `doomu.wad` |
| Doom Shareware | `doom1.wad` |
| Doom II | `doom2.wad`, `doom2f.wad` (French) |
| Final Doom: TNT - Evilution | `tnt.wad` |
| Final Doom: The Plutonia Experiment | `plutonia.wad` |
| Freedoom and FreeDM | `freedoom1.wad`, `freedoom2.wad`, `freedm.wad` |
| Chex Quest and Chex Quest 3 | `chex.wad`, `chex3v.wad`, `chex3d2.wad` |
| HACX | `hacx.wad` |
| REKKR and REKKR: Sunken Land | `rekkrsa.wad`, `rekkrsl.wad` |

These names are recognized by the Woof game picker. Odamex and multiplayer
servers may support a smaller set or require an exact IWAD revision.

Run **Freedoom: Select Game** to choose an IWAD. **Freedoom: Open** uses
`freedoom1.wad` or `freedoom2.wad` from this same folder. Plugin Manager and
`scripts/fetch-assets.py` install both campaigns and `freedm.wad` automatically.
Master Levels and other PWADs are add-ons, not selectable games. Heretic,
Hexen, and Strife are not supported by these engines.

Keep manually added game files out of Git and release packages. This guide is
kept in the repository; the separate asset package supplies the bundled IWADs.
Keep your original WADs or a backup: Plugin Manager can replace the plugin folder
when upgrading. For a permanent collection outside the plugin, set
**Additional IWAD Directory** in Pragtical's Freedoom settings.
