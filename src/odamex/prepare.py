#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Adapt the pinned client without changing its downloaded source tree."""
from pathlib import Path
import sys

path = Path(sys.argv[1])
source = path.read_text(encoding='utf-8')


def replace(old, new):
    global source
    if source.count(old) != 1:
        raise ValueError(f'{path.name}: expected one occurrence of {old!r}')
    source = source.replace(old, new)


if path.name == 'i_input.cpp':
    # Keep upstream's persistent key binding names, not its device management.
    start = source.index('KeyNameTable key_names;')
    end = source.index('\n//\n// I_FlushInput', start)
    source = '#include "odamex.h"\n#include "i_input.h"\n' + source[start:end]
elif path.name == 'i_video.cpp':
    start = source.index('#if defined(SDL20)')
    end = source.index('#include "i_system.h"', start)
    source = source[:start] + source[end:]
    start = source.index('// [Russell] - Just for windows')
    end = source.index('// Global IVideoSubsystem', start)
    source = source[:start] + source[end:]
    start = source.index('\tif (I_IsHeadless())', source.index('void I_InitHardware()'))
    end = source.index('\n}', start)
    source = source[:start] + '\tvideo_subsystem = fd_odamex_video();' + source[end:]
    replace('\tI_FreeSurface(loading_icon_background_surface);\n\n\tdelete video_subsystem;',
            '\tI_FreeSurface(loading_icon_background_surface);\n'
            '\tI_FreeSurface(converted_surface);\n\tI_FreeSurface(matted_surface);\n'
            '\tI_FreeSurface(emulated_surface);\n\tI_FreeSurface(scaled_screenblocks_surface);\n'
            '\tprimary_surface = nullptr;\n\n\tdelete video_subsystem;')
elif path.name == 'v_video.cpp':
    # Pragtical owns fullscreen; do not snap tab sizes to fullscreen presets.
    replace('const EWindowMode window_mode = (EWindowMode)vid_fullscreen.asInt();',
            'const EWindowMode window_mode = WINDOW_Windowed;')
elif path.name in ('cl_main.cpp', 'm_menu.cpp'):
    indent = '\t' if path.name == 'cl_main.cpp' else '    '
    replace(indent + 'call_terms();\n' + ('\n' if path.name == 'm_menu.cpp' else '')
            + '\texit(EXIT_SUCCESS);', '\tfd_odamex_quit();')
    if path.name == 'cl_main.cpp':
        replace('const char *passhash = Args.GetArg(ParamIndex + 2);',
                'const char *passhash = fd_odamex_password();')
        replace("if (passhash && passhash[0] != '-' && passhash[0] != '+')", 'if (passhash && *passhash)')
    else:
        # Use the engine's big bitmap font rather than the IWAD's ornate
        # label art in the main menu. Keep the original actions and hotkeys.
        for lump, label, count in (
            ("M_NGAME", "New Game", 2), ("M_OPTION", "Options", 2),
            ("M_LOADG", "Load Game", 2), ("M_SAVEG", "Save Game", 2),
            ("M_RDTHIS", "Read This", 1), ("M_QUITG", "Quit Game", 2),
        ):
            old = f'{{1,"{lump}","",'
            if source.count(old) != count:
                raise ValueError(f'{path.name}: unexpected main menu entry {lump}')
            source = source.replace(old, f'{{1,"","{label}",')
        replace('void M_DrawMainMenu()\n{\n', 'void M_DrawMainMenu()\n{\n'
                '\tscreen->Dim(0, 0, I_GetSurfaceWidth(), I_GetSurfaceHeight(), "00 00 00", 0.8f);\n')
        replace('screen->DrawTextCleanMove(CR_RED, x, y, currentMenu->menuitems[i].textname);',
                'const int color = currentMenu == &MainDef ? (i == itemOn ? CR_GOLD : CR_WHITE) : CR_RED;\n'
                '\t\t\t\t\tscreen->DrawTextCleanMove(color, x, y, currentMenu->menuitems[i].textname);')
        replace('#include "v_text.h"', '#include "menu-font.h"')
        start = source.index('static void M_PlayerSetupDrawer()\n{')
        end = source.index('\nvoid M_ChangeTeam ', start)
        drawer = source[start:end]
        title = drawer.index('\t// Draw title')
        name = drawer.index('\t// Draw player name box', title)
        drawer = drawer[:title] + (
            '\t{\n\t\tconst OdamexMenuFont title_font("BIGFONT");\n'
            '\t\tconst char *title = "Player Setup";\n'
            '\t\tscreen->DrawTextCleanMove(CR_WHITE, 160 - V_StringWidth(title) / 2, 10, title);\n'
            '\t}\n\n\tconst OdamexMenuFont menu_font;\n\n'
        ) + drawer[name:]
        drawer = drawer.replace('DrawTextCleanMove (CR_RED,', 'DrawTextCleanMove (CR_WHITE,')
        drawer = drawer.replace('DrawTextCleanMove(CR_RED,', 'DrawTextCleanMove(CR_GOLD,')
        source = source[:start] + drawer + source[end:]
        replace('ch = ev->data3;\t// [RH] Use user keymap\n\t\t\tif',
                'ch = ev->data3;\t// [RH] Use user keymap\n'
                '\t\t\tconst OdamexMenuFont menu_font(genStringEnter == oldmenustring_t::PLAYERNAME ? "DIGFONT" : nullptr);\n'
                '\t\t\tif')
elif path.name == 'm_options.cpp':
    # Measure and draw with the same plain font without changing the HUD.
    replace('#include "v_text.h"', '#include "menu-font.h"')
    for signature in ('void M_SwitchMenu(menu_t* menu)', 'void M_OptDrawer (void)'):
        start = signature + '\n{\n'
        replace(start, start + '\tconst OdamexMenuFont menu_font;\n')
    start = source.index('void M_OptDrawer (void)')
    end = source.index('// M_OptItemSelectable', start)
    drawer = source[start:end]
    drawer = drawer.replace('color = CR_RED;', 'color = CR_WHITE;')
    drawer = drawer.replace('screen->DrawTextCleanMove (color, x, y, item->label);',
                            'screen->DrawTextCleanMove (i == CurrentItem ? CR_GOLD : color, x, y, item->label);')
    source = source[:start] + drawer + source[end:]
elif path.name == 'd_main.cpp' and path.parent.name == 'src':
    replace('D_RunTics(CL_RunTics, CL_DisplayTics);',
            'fd_odamex_check_stop();\n\t\t\tD_RunTics(CL_RunTics, CL_DisplayTics);')
    # Do not let a local game inherit launcher or console arguments.
    replace('M_FindResponseFile();', '(void)0;')
    replace('\tCL_DownloadInit();\n\tatterm(CL_DownloadShutdown);',
            '\tif (fd_odamex_download()) { CL_DownloadInit(); atterm(CL_DownloadShutdown); }')
    replace('\tD_CheckNetGame();', '\tD_CheckNetGame();\n\tatterm(fd_odamex_disconnect);')
    replace('\tM_LoadDefaults();', '\tM_LoadDefaults();\n\tfd_odamex_configure();')
    replace('\tV_AdjustVideoMode();', '\tfd_odamex_resize();\n\tV_AdjustVideoMode();')
elif path.name == 'd_main.cpp':
    replace('exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);',
            'if (!ok) I_FatalError("Information dump failed");\n\t\t\tfd_odamex_quit();')
elif path.name == 'i_net.cpp':
    replace('inet_socket = UDPsocket ();',
            'inet_socket = UDPsocket ();\n   atterm(CloseNetwork);')
elif path.name == 'i_time.cpp':
    for signature, body in [('dtime_t I_GetTime()', 'return fd_odamex_clock();'),
                            ('void I_Sleep(dtime_t sleep_time)', 'fd_odamex_sleep(sleep_time);')]:
        start = source.index('\n{', source.index(signature))
        end = source.index('\n}', start)
        source = source[:start] + '\n{\n    ' + body + source[end:]
elif path.name == 'c_console.cpp':
    replace('std::string newStr = str;', 'fd_odamex_log(str.c_str());\n\tstd::string newStr = str;')
elif path.name == 'm_resfile.cpp':
    start = source.index('\n{', source.index('std::vector<std::string> M_FileSearchDirs()'))
    end = source.index('\n}', start)
    source = source[:start] + '\n{\n\treturn fd_odamex_wad_dirs();' + source[end:]
elif path.name == 'cl_download.cpp':
    start = source.index('\n{', source.index('static StringTokens GetDownloadDirs()'))
    end = source.index('\n}', start)
    source = source[:start] + '\n{\n\treturn {M_GetDownloadDir()};' + source[end:]
    before = '\t// Remove all URL\'s that don\'t look like URL\'s.'
    replace(before, '''\tconst auto& name = filename.getBasename();
    if (!fd_odamex_download() || name.size() < 5 || name.size() > 128 ||
        name.front() == '.' || name.find_first_of("/\\\\:\\r\\n") != std::string::npos ||
        !iequals(name.substr(name.size() - 4), ".wad")) {
        PrintFmt(PRINT_WARNING, "Refusing an unsupported WAD download\\n");
        return false;
    }
''' + before)
    replace('curl_global_init(CURL_GLOBAL_ALL);',
            'if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) I_FatalError("Cannot initialize WAD downloads");')
    source = '#include <cerrno>\n' + source
elif path.name == 'otransfer.cpp':
    original = 'curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, 1L);'
    assert source.count(original) == 2
    source = source.replace(original, original + '''
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(m_curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(m_curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(m_curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(m_curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(m_curl, CURLOPT_MAXREDIRS, 4L);
    curl_easy_setopt(m_curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(m_curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(m_curl, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(128u << 20));''')
    replace('m_filePart = dest + ".part";\n\n\tm_file = fopen(m_filePart.c_str(), "wb+");\n'
            '\tif (m_file == NULL)\n\t\treturn errno;',
            '''m_filePart = dest + ".part.XXXXXX";
#ifdef _WIN32
    auto path = std::filesystem::u8path(m_filePart).wstring();
    if (_wmktemp_s(path.data(), path.size() + 1)) { m_filePart.clear(); return EIO; }
    m_filePart = std::filesystem::path(path).u8string();
    m_file = _wfopen(path.c_str(), L"wbx");
    if (!m_file) { m_filePart.clear(); return errno; }
#else
    int fd = mkstemp(m_filePart.data());
    if (fd < 0) { m_filePart.clear(); return errno; }
    m_file = fdopen(fd, "wb+");
    if (!m_file) { int error = errno; close(fd); return error; }
#endif''')
    replace('return 0;\n}\n\n// PUBLIC //', 'return dlnow > (128u << 20);\n}\n\n// PUBLIC //')
    replace('rename(m_filePart.c_str(), m_filename.c_str())', 'publish_download(m_filePart, m_filename)')
    replace('rename(m_filePart.c_str(), fallback.c_str())', 'publish_download(m_filePart, fallback)')
    replace('size_t OTransferCheck::curlWrite', '''static int publish_download(const std::string &from, const std::string &to)
{
    namespace fs = std::filesystem;
    std::error_code error;
    fs::create_hard_link(fs::u8path(from), fs::u8path(to), error);
    if (!error) return 0;
    errno = error == std::errc::file_exists ? EEXIST : EIO;
    return -1;
}

size_t OTransferCheck::curlWrite''')
    source = '#include <filesystem>\n' + source
    source = '#include <unistd.h>\n' + source

# Newer protobuf headers must be parsed before Odamex's implicit string
# conversions enter overload resolution.
if '"server.pb.h"' in source or '"svc_message.h"' in source:
    source = '#include "server.pb.h"\n' + source
elif path.name == 'i_net.cpp':
    source = '#include <google/protobuf/message.h>\n' + source
source = '#include "hooks.h"\n' + source
Path(sys.argv[2]).write_text(source, encoding='utf-8')
