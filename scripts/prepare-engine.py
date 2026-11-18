#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Apply checked integration/reset records to the pinned, unmodified sources."""
import argparse
import hashlib
import json
from pathlib import Path
from woof_truecolor import adapt

parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("manifest", type=Path)
parser.add_argument("--platform", required=True)
args = parser.parse_args()
source = args.source.read_bytes().replace(b"\r\n", b"\n")
records = json.loads(args.manifest.read_text(encoding="utf-8"))
record = {**records["common"], **records["windows" if args.platform == "windows" else "posix"]}[args.source.name]
assert hashlib.sha256(source).hexdigest() == record["sha256"], args.source
edits = []
for offset, names in record["locals"]:
    marker = f"fd_recorded_{offset}"
    registration = " ".join(f"fd_state_register(&{name}, sizeof({name}));" for name in names)
    edits.append((offset, f" static int {marker}; if (!{marker}) {{ {marker} = 1; {registration} }}"))
for offset, text in sorted(edits, reverse=True):
    source = source[:offset] + text.encode("ascii") + source[offset:]
source = source.decode("utf-8")
if args.source.name == "d_main.c":
    assert source.count("setbuf(stdout,NULL);") == 1
    source = source.replace("setbuf(stdout,NULL);", "/* Stdio belongs to the editor. */")
    source = source.replace("  NET_Init();", "  NET_Init(); fd_network_setup();")
    prefix = '#include "network.h"\n'
elif args.source.name == "i_timer.c":
    source = source.replace("SDL_GetPerformanceCounter()", "fd_performance_counter()")
    source = source.replace("    SDL_Delay(ms);", "    fd_check_stop(); SDL_Delay(ms); fd_check_stop();")
    prefix = '#include "woof.h"\n#include "network.h"\n'
else:
    prefix = ""
if args.source.name == "netlib.c":
    if args.platform == 'windows':
        source = source.replace('#include <iphlpapi.h>', '#define boolean win32_rpc_boolean\n'
            '  #include <iphlpapi.h>\n  #undef boolean')
    prefix = '#ifndef _WIN32\n#include <ifaddrs.h>\n#include <poll.h>\n#endif\n'
    old = '''            struct hostent *hp;
            hp = gethostbyname(host);
            if (hp)
            {
                memcpy(&address->host, hp->h_addr, hp->h_length);
            }
            else
            {
                retval = -1;
                netlib_set_error("failed to get host from '%s'", host);
            }'''
    assert source.count(old) == 1
    source = source.replace(old, '''            retval = -1;
            netlib_set_error("Numeric IPv4 address required: '%s'", host);''')
    source = source.replace('''    // Bind locally, if appropriate''', '''    // The engine is embedded in a process that may own many descriptors.
#ifdef __USE_W32_SOCKETS
    u_long nonblocking = 1;
    if (ioctlsocket(sock->channel, FIONBIO, &nonblocking) != 0
        || !SetHandleInformation((HANDLE)sock->channel, HANDLE_FLAG_INHERIT, 0))
        goto error_return;
#else
    int flags = fcntl(sock->channel, F_GETFL);
    if (flags < 0 || fcntl(sock->channel, F_SETFL, flags | O_NONBLOCK) < 0
        || fcntl(sock->channel, F_SETFD, FD_CLOEXEC) < 0) goto error_return;
#endif
    // Bind locally, if appropriate''')
    source = source.replace('''static int socket_ready(SOCKET sock)
{''', '''static int socket_ready(SOCKET sock)
{
#ifndef __USE_W32_SOCKETS
    struct pollfd descriptor = {.fd = sock, .events = POLLIN};
    return poll(&descriptor, 1, 0) > 0 && (descriptor.revents & POLLIN);
#else''')
    source = source.replace('''    return retval == 1;
}''', '''    return retval == 1;
#endif
}''')
    old = '''        status = sendto(sock->channel, (const char *)packet->data, packet->len,
                        0, (struct sockaddr *)&sock_addr, sock_len);
        if (status >= 0)'''
    assert source.count(old) == 1
    source = source.replace('    struct sockaddr_in sock_addr;\n    int status;',
                            '    struct sockaddr_in sock_addr = {0};\n    int status;')
    source = source.replace(old, '''        status = sendto(sock->channel, (const char *)packet->data, packet->len,
                        0, (struct sockaddr *)&sock_addr, sock_len);
        if (status < 0 && packet->address.host == INADDR_BROADCAST)
        {
            // Some hosts cannot route the limited broadcast address.
#ifndef __USE_W32_SOCKETS
            struct ifaddrs *interfaces;
            if (getifaddrs(&interfaces) == 0)
            {
                for (const struct ifaddrs *ifa = interfaces; ifa; ifa = ifa->ifa_next)
                {
                    if ((ifa->ifa_flags & (IFF_UP | IFF_BROADCAST)) != (IFF_UP | IFF_BROADCAST)
                        || !ifa->ifa_broadaddr || ifa->ifa_broadaddr->sa_family != AF_INET)
                        continue;
                    sock_addr.sin_addr = ((const struct sockaddr_in *)ifa->ifa_broadaddr)->sin_addr;
                    int sent = sendto(sock->channel, (const char *)packet->data, packet->len,
                                      0, (struct sockaddr *)&sock_addr, sock_len);
                    if (sent >= 0) status = sent;
                }
                freeifaddrs(interfaces);
            }
#endif
            // A loopback-only runner should still discover a local host.
            if (status < 0)
            {
                sock_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                status = sendto(sock->channel, (const char *)packet->data, packet->len,
                                0, (struct sockaddr *)&sock_addr, sock_len);
            }
        }
        if (status < 0)
            netlib_set_error("sendto failed (socket error %d)", netlib_get_last_error());
        if (status >= 0)''')
    for old in ('''        void (*handler)(int);
        handler = signal(SIGPIPE, SIG_IGN);
        if (handler != SIG_DFL)
        {
            signal(SIGPIPE, handler);
        }''', '''        void (*handler)(int);
        handler = signal(SIGPIPE, SIG_DFL);
        if (handler != SIG_IGN)
        {
            signal(SIGPIPE, handler);
        }'''):
        assert source.count(old) == 1
        source = source.replace(old, "        /* Signal handlers belong to Pragtical. */")
if args.source.name == "net_netlib.c":
    source = source.replace("if (addr == &addr_table[i]->net_addr)", "if (addr_table[i] && addr == &addr_table[i]->net_addr)")
    source = source.replace("    netlib_quit();", '''    netlib_free_packet(recvpacket); recvpacket = NULL;
    netlib_udp_close(udpsocket); udpsocket = NULL;
    netlib_quit(); initted = false;''')
    source = source.replace('''    if (!initted)
    {
        return;
    }

    netlib_free_packet''', '''    netlib_free_packet''')
    source = source.replace("    recvpacket = netlib_alloc_packet(1500);", '''    recvpacket = netlib_alloc_packet(1500);
    if (!recvpacket) I_Error("Cannot allocate network receive buffer");''')
if args.source.name == "net_client.c":
    prefix = '#include "network.h"\n'
    source = source.replace("    // We are now successfully connected.",
        "    fd_network_server_version(server_version);\n\n    // We are now successfully connected.")
    marker = "    client_state = CLIENT_STATE_IN_GAME;"
    assert source.count(marker) == 1
    source = source.replace(marker, '''    // NET_CL_Run can deliver game tics in the same poll as GAMESTART.
    fd_network_set_local_player(settings.consoleplayer);
''' + marker)
    source = source.replace("    NET_WriteSettings(packet, settings);",
        "    fd_network_start_game(settings);\n    NET_WriteSettings(packet, settings);")
    source = source.replace("            NET_RequestHolePunch(client_context, addr);",
        "            if (fd_network_master()[0]) NET_RequestHolePunch(client_context, addr);")
    source = source.replace('''    NET_Log("client: beginning disconnect");''', '''    if (client_connection.state == NET_CONN_STATE_CONNECTING) {
        NET_CL_Shutdown(); return;
    }
    NET_Log("client: beginning disconnect");''')
    source = source.replace("if (I_GetTimeMS() - start_time > 5000)", "if (I_GetTimeMS() - start_time > 500)")
if args.source.name == "net_query.c":
    prefix = '#include "network.h"\n'
    old = "addr = NET_ResolveAddress(context, MASTER_SERVER_ADDRESS);"
    assert source.count(old) == 1
    source = source.replace(old, "addr = fd_network_master()[0] ? NET_ResolveAddress(context, fd_network_master()) : NULL;")
    old = "    targets = I_Realloc(targets, sizeof(query_target_t) * (num_targets + 1));"
    assert source.count(old) == 1
    source = source.replace(old, '''    if (num_targets >= FD_NET_QUERY_TARGETS) {
        fd_network_query_limited(); return NULL;
    }
''' + old)
    old = "        target = GetTargetForAddr(addr, true);\n        broadcast_target"
    assert source.count(old) == 1
    source = source.replace(old, "        target = GetTargetForAddr(addr, true);\n        if (!target) return;\n        broadcast_target")
    old = "        addr = NET_ResolveAddress(query_context, addr_str);"
    assert source.count(old) == 1
    source = source.replace(old, '''        char endpoint[64];
        if (!fd_network_address(addr_str, 2342, endpoint)) continue;
        addr = NET_ResolveAddress(query_context, endpoint);''')
    old = "    // Read a list of strings containing the addresses of servers"
    assert source.count(old) == 1
    source = source.replace(old, "    fd_network_master_response();\n\n" + old)
    old = "    free(targets);\n    targets = NULL;\n    num_targets = 0;"
    assert source.count(old) == 2
    # Refresh must release address references, not only the target array.
    position = source.index("void NET_Query_Init(void)")
    source = source[:position] + source[position:].replace(old, "    FreeTargets();", 1)
if args.source.name == "net_server.c":
    source = source.replace("if (I_GetTimeMS() - start_time > 5000)", "if (I_GetTimeMS() - start_time > 500)")
if args.source.name == "d_net.c":
    source = source.replace("    net_connect_data_t connect_data;", "    net_connect_data_t connect_data = {0};")
    source = source.replace("    //W_Checksum(connect_data->wad_sha1sum);", "    fd_network_connect_data(connect_data);")
    source = source.replace("    settings->gameversion = gameversion;", "    settings->gameversion = fd_network_gameversion();")
    prefix = '#include "network.h"\n'
if args.source.name == "net_structrw.c":
    marker = "boolean NET_ReadSettings(net_packet_t *packet, net_gamesettings_t *settings)\n{"
    assert source.count(marker) == 1
    source = source.replace(marker, marker + "\n    memset(settings, 0, sizeof(*settings));")
if args.source.name == "d_loop.c":
    prefix = '#include "network.h"\n'
    source += '''
void fd_network_set_local_player(int player) { localplayer = player; }

unsigned fd_network_pending_turn(void) {
    if (!fd_network_raw_view()) return 0;
    unsigned angle = 0;
    /* Predict only the local camera's mouse turn, never simulation or peers.
       Keyboard turns remain interpolated through player->ticangle. */
    for (int tic = gametic; tic < maketic && tic - gametic < BACKUPTICS; tic++) {
        const ticcmd_t *cmd = &ticdata[tic % BACKUPTICS].cmds[localplayer];
        angle += (unsigned)(short)(cmd->angleturn - cmd->ticangleturn) << FRACBITS;
    }
    return angle;
}
'''
if args.source.name == "g_input.c":
    prefix = '#include "network.h"\n'
    marker = "    if (!netgame || solonet)"
    assert source.count(marker) == 1
    source = source.replace(marker, "    if (!netgame || solonet || fd_network_raw_view())")
if args.source.name == "p_mobj.c":
    # Deathmatch spawn angles intentionally wrap at one full turn.
    old = "mobj->angle      = ANG45 * (mthing->angle/45);"
    assert source.count(old) == 1
    source = source.replace(old, "mobj->angle = (angle_t)ANG45 * (angle_t)(mthing->angle/45);")
if args.source.name == "i_sound.c":
    assert source.count("    &sound_3d_module,") == 1
    source = source.replace("    &sound_3d_module,", "")
    source = source.replace("NUM_SND_MODULES - 1,", "arrlen(sound_modules) - 1,")
    source = source.replace("Sound module (0 = Standard; 1 = OpenAL 3D; 2 = PC Speaker Sound)", "Sound module (Pragtical stereo)")
if args.source.name == "md5.c":
    # Empty WAD lumps have a NULL cache pointer. A zero-length update must not
    # pass that pointer to memcpy, even though it hashes no bytes.
    marker = "MD5Update(struct MD5Context *ctx, md5byte const *buf, unsigned len)\n{"
    assert source.count(marker) == 1
    source = source.replace(marker, marker + "\n        if (!len) return;")
if args.source.name == "r_main.c":
    prefix = '#include "network.h"\n'
    source = source.replace("// Don't use localview during a netgame (single-player or solo-net only).",
        "// The bridge can predict local mouse turns at normal network tic rate.")
    source = source.replace("    (!netgame || solonet)", "    (!netgame || solonet || fd_network_raw_view())")
    source = source.replace("return (player->mo->angle + localview.angle - player->ticangle +",
        "return (player->mo->angle + fd_network_pending_turn() + localview.angle - player->ticangle +")
    # Defaults only: M_LoadDefaults still applies the user's saved choices.
    settings = {
        "fuzzmode, FUZZ_BLOCKY, FUZZ_BLOCKY": "fuzzmode, FUZZ_REFRACTION, FUZZ_BLOCKY",
        "stretchsky, false": "stretchsky, true",
        "linearsky, false": "linearsky, true",
        "r_swirl, false": "r_swirl, true",
        "brightmaps, false": "brightmaps, true",
    }
    for old, new in settings.items():
        assert source.count(old) == 1
        source = source.replace(old, new)
if args.source.name == "mn_setup.c":
    marker = "void MN_SetupResetMenu(void)"
    assert source.count(marker) == 1
    source = source.replace(marker, "static void fd_original_setup_reset_menu(void)")
    source += '''
void MN_SetupResetMenu(void) {
    fd_original_setup_reset_menu();
    DisableItems(true, gen_settings1, "resolution_scale", "dynamic_resolution",
        "widescreen", "fullscreen", "uncapped", "fpslimit", "use_vsync");
    DisableItems(true, gen_settings2, "snd_module", "snd_hrtf", "Equalizer Options");
    DisableItems(true, sfx_settings1, "snd_resampler", "snd_doppler");
}
'''
source = adapt(args.source.name, source)
name = args.source.stem
capture = "\n".join(f"    fd_state_register(&{name}, sizeof({name}));" for name in record["globals"])
source = '#include "engine_memory.h"\n' + prefix + source
source += f"\nvoid fd_capture_{name}(void) {{\n{capture}\n}}\n"
if not args.output.exists() or args.output.read_text(encoding="utf-8") != source:
    args.output.write_text(source, encoding="utf-8")
