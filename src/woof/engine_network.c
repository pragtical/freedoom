/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network.h"
#include "content.h"
#include "doomstat.h"
#include "i_exit.h"
#include "i_system.h"
#include "i_timer.h"
#include "m_io.h"
#include "net_client.h"
#include "net_io.h"
#include "net_netlib.h"
#include "net_query.h"
#include "net_server.h"
#include "sha1.h"
#include "w_wad.h"
#include "network-content.h"

static fd_net_state state;
static bool master_responded;
static unsigned content_index;
static bool vanilla_server;
void fd_network_master_response(void) { master_responded = true; }
void fd_network_query_limited(void) { state.limited = 1; }

static void publish(void) {
    fd_network *network = fd_current_network();
    fd_mutex_lock(network->mutex);
    state.revision = network->state.revision;
    if (memcmp(&state, &network->state, sizeof(state))) {
        ++state.revision;
        network->state = state;
    }
    fd_mutex_unlock(network->mutex);
}
int fd_network_snapshot(fd_session *session, fd_net_state *output) {
    fd_network *network = fd_session_network(session);
    if (!network || !output || !fd_mutex_trylock(network->mutex)) return 0;
    *output = network->state;
    fd_mutex_unlock(network->mutex);
    return 1;
}
int fd_network_launch(fd_session *session) {
    fd_network *network = fd_session_network(session);
    if (!network) return 0;
    fd_mutex_lock(network->mutex);
    bool allowed = network->state.phase == FD_NET_LOBBY && network->state.controller;
    if (allowed) network->launch = true;
    fd_mutex_unlock(network->mutex);
    return allowed;
}
int fd_network_refresh(fd_session *session) {
    fd_network *network = fd_session_network(session);
    if (!network || (network->options.mode != FD_NET_DISCOVER && network->options.mode != FD_NET_INTERNET)) return 0;
    fd_mutex_lock(network->mutex);
    network->refresh = true;
    fd_mutex_unlock(network->mutex);
    return 1;
}
int fd_network_configure(fd_session *session, const fd_net_options *options) {
    fd_network *network = fd_session_network(session);
    if (!network || !options || options->deathmatch < 0 || options->deathmatch > 3
        || options->episode < 1 || options->episode > 4 || options->map < 1 || options->map > 32
        || options->skill < 1 || options->skill > 5) return 0;
    fd_iwad game;
    if (!fd_iwad_info(fd_session_iwad(session), &game)
        || (game.gamemode != commercial && options->map > 9)
        || (game.gamemode == shareware && options->episode > 1)
        || (game.gamemode == registered && options->episode > 3)) return 0;
    fd_mutex_lock(network->mutex);
    bool allowed = network->state.phase == FD_NET_LOBBY && network->state.controller;
    if (allowed) {
        network->options.deathmatch = options->deathmatch;
        network->options.episode = options->episode;
        network->options.map = options->map;
        network->options.skill = options->skill;
    }
    fd_mutex_unlock(network->mutex);
    return allowed;
}
static bool take_request(bool *request) {
    fd_network *network = fd_current_network();
    fd_mutex_lock(network->mutex);
    bool value = *request; *request = false;
    fd_mutex_unlock(network->mutex);
    return value;
}
static void shutdown_network(void) { netlib_module.Shutdown(); }
void fd_network_initialize(void) {
    state.mode = fd_current_network()->options.mode;
    state.phase = state.mode ? FD_NET_CONNECTING : FD_NET_NONE;
    if (state.mode) snprintf(state.message, sizeof(state.message), "Connecting...");
    publish();
    I_AtExit(shutdown_network, true);
}
static void found_server(net_addr_t *address, net_querydata_t *data, unsigned ping, void *unused) {
    (void)unused;
    char name[64];
    snprintf(name, sizeof(name), "%s", NET_AddrToString(address));
    unsigned index = 0;
    while (index < state.server_count && strcmp(state.servers[index].address, name)) index++;
    if (index == FD_NET_SERVERS) { state.limited = 1; return; }
    if (index == state.server_count) state.server_count++;
    fd_net_server *server = &state.servers[index];
    snprintf(server->address, sizeof(server->address), "%s", name);
    snprintf(server->description, sizeof(server->description), "%s", data->description ? data->description : "Freedoom");
    snprintf(server->version, sizeof(server->version), "%s", data->version ? data->version : "");
    server->players = data->num_players; server->max_players = data->max_players;
    server->state = data->server_state; server->ping = ping; server->mission = data->gamemission;
    server->gamemode = data->gamemode;
    server->unassigned = data->num_players == 0 && data->gamemode == indetermined;
    server->protocol = data->protocol != NET_PROTOCOL_UNKNOWN;
    server->phase = !strncmp(server->version, "Woof ", 5) && data->gamemission == pack_freedoom
        ? (data->gamemode == retail ? 1 : data->gamemode == commercial ? 2 : 0) : 0;
    if (data->gamemission == doom && (data->gamemode == retail || data->gamemode == registered
        || data->gamemode == shareware)) server->phase = 1;
    else if (data->gamemission == doom2 && data->gamemode == commercial) server->phase = 2;
    server->compatible = (server->unassigned || server->phase != 0)
        && data->max_players > 0 && data->max_players <= NET_MAXPLAYERS;
    if ((data->gamemission == pack_tnt || data->gamemission == pack_plut) && data->gamemode == commercial)
        server->compatible = data->max_players > 0 && data->max_players <= NET_MAXPLAYERS;
    publish();
}
static bool start_query(bool internet) {
    memset(state.servers, 0, sizeof(state.servers)); state.server_count = state.limited = 0;
    master_responded = false; state.phase = FD_NET_SCANNING;
    snprintf(state.message, sizeof(state.message), "Searching %s...", internet ? "Internet" : "LAN");
    bool result = internet ? NET_StartMasterQuery() : NET_StartLANQuery();
    if (!result) {
        state.phase = FD_NET_RESULTS;
        snprintf(state.message, sizeof(state.message), "Cannot query the master server");
    }
    publish(); return result;
}
void fd_network_setup(void) {
    fd_network *network = fd_current_network();
    if (!state.mode) return;
    free((void *)net_player_name);
    net_player_name = strdup(network->options.name);
    if (!net_player_name) I_Error("Cannot allocate player name");
    if (state.mode != FD_NET_DISCOVER && state.mode != FD_NET_INTERNET) return;
    bool internet = state.mode == FD_NET_INTERNET;
    bool scanning = start_query(internet);
    unsigned started = I_GetTimeMS();
    for (;;) {
        fd_check_stop();
        if (take_request(&network->refresh)) {
            scanning = start_query(internet); started = I_GetTimeMS();
        }
        if (scanning) {
            bool pending = NET_Query_Poll(found_server, NULL);
            if (!pending || (unsigned)(I_GetTimeMS() - started) >= 12000) {
                scanning = false; state.phase = FD_NET_RESULTS;
                if (pending) state.limited = 1;
                if (internet && !master_responded)
                    snprintf(state.message, sizeof(state.message), "Master server did not respond");
                else snprintf(state.message, sizeof(state.message), "%u %s game%s found%s", state.server_count,
                    internet ? "Internet" : "LAN", state.server_count == 1 ? "" : "s", state.limited ? " (limited)" : "");
                publish();
            }
        }
        I_Sleep(10);
    }
}
static const sha1_digest_t *content_patches(void) {
    return gamemission == pack_freedoom ? fd_content[content_index].dehacked : fd_vanilla_dehacked;
}
void fd_network_connect_data(void *data) {
    net_connect_data_t *connect = data;
    connect->player_class = 0;
    connect->is_freedoom = W_CheckNumForName("FREEDOOM") >= 0;
    if (!state.mode) return;
    fd_iwad game;
    if (!fd_iwad_info(fd_current_iwad(), &game) || !game.network)
        I_Error("This IWAD's game data or Dehacked patches are not verified for multiplayer");
    FILE *file = M_fopen(fd_current_iwad(), "rb");
    if (!file) I_Error("Cannot read multiplayer IWAD");
    sha1_context_t hash; SHA1_Init(&hash);
    unsigned char buffer[16384]; size_t length;
    while ((length = fread(buffer, 1, sizeof(buffer), file))) {
        SHA1_Update(&hash, buffer, length); fd_check_stop();
    }
    bool failed = ferror(file); fclose(file);
    if (failed) I_Error("Cannot checksum multiplayer IWAD");
    sha1_digest_t digest;
    SHA1_Final(digest, &hash);
    content_index = 0;
    if (gamemission == pack_freedoom) {
        for (; content_index < sizeof(fd_content) / sizeof(*fd_content); content_index++)
            if (!memcmp(digest, fd_content[content_index].file, sizeof(digest))) break;
        if (content_index == sizeof(fd_content) / sizeof(*fd_content))
            I_Error("Multiplayer IWAD differs from the verified Freedoom 0.13.0 game data");
    }
    /* Standard directory/Dehacked fingerprints are derived from these exact
       fixed IWADs. Never hash Woof's extra resources or port-specific tables. */
    memcpy(connect->wad_sha1sum, game.checksum, sizeof(sha1_digest_t));
    memcpy(connect->deh_sha1sum, content_patches()[0], sizeof(sha1_digest_t));
    if (gamemission == pack_freedoom) connect->gamemission = gamemode == commercial ? doom2 : doom;
}
int fd_network_gameversion(void) {
    if (!state.mode) return gameversion;
    /* Chocolate/Crispy's wire IDs, independent of Woof's internal enum. */
    static const int versions[] = {5, 7, 8, 9, 10};
    return gameversion >= 0 && gameversion < 5 ? versions[gameversion] : 5;
}
bool fd_network_raw_view(void) { return net_client_connected && netgame && ticdup == 1; }
void fd_network_server_version(const char *version) {
    vanilla_server = !strncmp(version, "Chocolate ", 10) || !strncmp(version, "Crispy ", 7);
}
void fd_network_start_game(void *data) {
    net_gamesettings_t *settings = data;
    if (state.controller) {
        fd_network *network = fd_current_network();
        fd_mutex_lock(network->mutex);
        settings->deathmatch = network->options.deathmatch;
        settings->episode = network->options.episode;
        settings->map = network->options.map;
        settings->skill = network->options.skill - 1;
        fd_mutex_unlock(network->mutex);
    }
    if (vanilla_server && state.controller && settings->deathmatch > 2)
        I_Error("Deathmatch 3.0 requires a Woof-compatible server; select another game mode");
}
static void check_content(void) {
    if (!net_client_received_wait_data) return;
    if (memcmp(net_client_wait_data.wad_sha1sum, net_local_wad_sha1sum, sizeof(sha1_digest_t))) {
        memcpy(state.wanted_iwad, net_client_wait_data.wad_sha1sum, sizeof(sha1_digest_t));
        publish();
        I_Error("Multiplayer game data differs from the host (IWAD checksum)");
    }
    for (unsigned i = 0; i < 2; i++)
        if (!memcmp(net_client_wait_data.deh_sha1sum, content_patches()[i], sizeof(sha1_digest_t))) return;
    I_Error("Multiplayer game data differs from the host (unverified Dehacked checksum)");
}
void NET_WaitForLaunch(void) {
    fd_network *network = fd_current_network();
    state.phase = FD_NET_LOBBY;
    snprintf(state.message, sizeof(state.message), "Waiting for players");
    while (net_waiting_for_launch) {
        fd_check_stop(); NET_CL_Run(); NET_SV_Run();
        if (!net_client_connected) I_Error("Lost connection to server");
        check_content();
        if (net_client_received_wait_data) {
            if (net_client_wait_data.num_players > MAXPLAYERS)
                I_Error("Multiplayer supports at most four players");
            state.controller = net_client_wait_data.is_controller;
            state.num_players = net_client_wait_data.num_players;
            state.max_players = net_client_wait_data.max_players;
            state.consoleplayer = net_client_wait_data.consoleplayer;
            fd_mutex_lock(network->mutex);
            state.deathmatch = network->options.deathmatch;
            state.map = network->options.map;
            state.episode = network->options.episode;
            state.skill = network->options.skill;
            fd_mutex_unlock(network->mutex);
            for (unsigned i = 0; i < 4; i++) {
                state.names[i][0] = state.addresses[i][0] = 0;
                if (i < state.num_players) {
                    snprintf(state.names[i], sizeof(state.names[i]), "%.*s",
                        MAXPLAYERNAME - 1, net_client_wait_data.player_names[i]);
                    snprintf(state.addresses[i], sizeof(state.addresses[i]), "%.*s",
                        MAXPLAYERNAME - 1, net_client_wait_data.player_addrs[i]);
                }
            }
        }
        publish();
        if (take_request(&network->launch) && state.controller) NET_CL_LaunchGame();
        I_Sleep(10);
    }
    check_content();
    snprintf(state.message, sizeof(state.message), "Starting game..."); publish();
}
void fd_network_update(void) {
    if (!state.mode || state.mode == FD_NET_DISCOVER || state.mode == FD_NET_INTERNET
        || !netgame || gamestate != GS_LEVEL) return;
    if (state.phase == FD_NET_PLAYING && state.tic == (unsigned)gametic) return;
    if (!net_client_connected) I_Error("Disconnected from multiplayer game");
    state.phase = FD_NET_PLAYING; state.tic = gametic; state.consoleplayer = consoleplayer;
    state.deathmatch = deathmatch; state.map = gamemap; state.episode = gameepisode; state.skill = gameskill + 1;
    state.player_mask = state.num_players = 0;
    for (unsigned i = 0; i < 4; i++) if (playeringame[i]) {
        state.player_mask |= 1u << i; ++state.num_players;
    }
    state.message[0] = 0; publish();
}
