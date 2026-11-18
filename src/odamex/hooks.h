/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include "preview.h"

class IVideoSubsystem;
IVideoSubsystem *fd_odamex_video();
void fd_odamex_check_stop();
void fd_odamex_disconnect();
[[noreturn]] void fd_odamex_quit();
void fd_odamex_frame(const unsigned char *rgba, unsigned width, unsigned height);
bool fd_odamex_input(unsigned char packet[4]);
uint64_t fd_odamex_clock();
void fd_odamex_sleep(uint64_t ns);
void fd_odamex_configure();
const char *fd_odamex_password();
bool fd_odamex_download();
std::vector<std::string> fd_odamex_wad_dirs();
void fd_odamex_resize();
void fd_odamex_log(const char *message);
const fd_odamex_host *fd_odamex_audio();
