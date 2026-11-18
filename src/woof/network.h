/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FD_NETWORK_H
#define FD_NETWORK_H
#include "engine_api.h"
#include "platform/SDL3/SDL.h"
#define FD_NET_SERVERS 32
#define FD_NET_QUERY_TARGETS 128
enum { FD_NET_OFFLINE, FD_NET_HOST, FD_NET_JOIN, FD_NET_DISCOVER, FD_NET_INTERNET, FD_NET_INTERNET_JOIN };
enum { FD_NET_NONE, FD_NET_CONNECTING, FD_NET_LOBBY, FD_NET_PLAYING, FD_NET_SCANNING, FD_NET_RESULTS };
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
    fd_net_server servers[FD_NET_SERVERS];
    unsigned char wanted_iwad[20];
} fd_net_state;
typedef struct {
    SDL_Mutex *mutex;
    fd_net_options options;
    fd_net_state state;
    bool launch, refresh;
} fd_network;
FD_EXPORT fd_session *fd_start_network(const char *directory, const char *state,
    const char *iwad, int sound, const fd_net_options *options);
FD_EXPORT int fd_network_snapshot(fd_session *session, fd_net_state *state);
FD_EXPORT int fd_network_launch(fd_session *session);
FD_EXPORT int fd_network_refresh(fd_session *session);
FD_EXPORT int fd_network_configure(fd_session *session, const fd_net_options *options);
FD_EXPORT unsigned fd_network_options_size(void);
FD_EXPORT unsigned fd_network_state_size(void);
fd_network *fd_session_network(fd_session *session);
fd_network *fd_current_network(void);
const char *fd_current_iwad(void);
const char *fd_session_iwad(fd_session *session);
void fd_network_initialize(void);
void fd_network_setup(void);
void fd_network_update(void);
void fd_check_stop(void);
void fd_begin_exit(void);
void fd_network_connect_data(void *data);
int fd_network_gameversion(void);
bool fd_network_raw_view(void);
unsigned fd_network_pending_turn(void);
void fd_network_server_version(const char *version);
void fd_network_set_local_player(int player);
void fd_network_start_game(void *data);
bool fd_network_address(const char *text, unsigned default_port, char output[64]);
const char *fd_network_master(void);
void fd_network_master_response(void);
void fd_network_query_limited(void);
#endif
