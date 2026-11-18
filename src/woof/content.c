/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "content.h"
#include "d_iwad.h"
#include "platform/SDL3/SDL.h"
#include "m_swap.h"
#include "sha1.h"
#include "w_wad.h"

int fd_iwad_info(const char *path, fd_iwad *output) {
    if (!path || !output) return 0;
    const char *name = path;
    for (const char *part = path; *part; part++) if (*part == '/' || *part == '\\') name = part + 1;
    GameMode_t mode;
    GameMission_t mission;
    D_GetModeAndMissionByIWADName(name, &mode, &mission);
    if (mission == none) return 0;
    FILE *file = fd_open_file(path, "rb");
    if (!file) return 0;
    wadinfo_t header;
    bool ok = fseek(file, 0, SEEK_END) == 0;
    long length = ok ? ftell(file) : -1;
    ok = length >= (long)sizeof(header) && fseek(file, 0, SEEK_SET) == 0
        && fread(&header, sizeof(header), 1, file) == 1 && !memcmp(header.identification, "IWAD", 4);
    int count = ok ? LONG(header.numlumps) : 0, offset = ok ? LONG(header.infotableofs) : 0;
    ok = ok && count > 0 && count <= 1000000 && offset >= (int)sizeof(header)
        && (uint64_t)offset + (uint64_t)count * sizeof(filelump_t) <= (uint64_t)length
        && fseek(file, offset, SEEK_SET) == 0;
    bool episode1 = false, episode3 = false, episode4 = false, patched = false;
    sha1_context_t hash;
    SHA1_Init(&hash);
    for (int i = 0; ok && i < count; i++) {
        filelump_t lump;
        if (fread(&lump, sizeof(lump), 1, file) != 1) { ok = false; break; }
        int position = LONG(lump.filepos), size = LONG(lump.size);
        if (position < 0 || size < 0 || (uint64_t)position + size > (uint64_t)length) { ok = false; break; }
        char label[9]; memcpy(label, lump.name, 8); label[8] = 0;
        episode1 |= !strcmp(label, "E1M1"); episode3 |= !strcmp(label, "E3M1"); episode4 |= !strcmp(label, "E4M1");
        patched |= !strcmp(label, "DEHACKED");
        SHA1_UpdateString(&hash, label);
        SHA1_UpdateInt32(&hash, 0);
        SHA1_UpdateInt32(&hash, position);
        SHA1_UpdateInt32(&hash, size);
    }
    fclose(file);
    if (!ok) return 0;
    if (mode == indetermined) mode = episode4 ? retail : episode3 ? registered : episode1 ? shareware : indetermined;
    if (mode == indetermined) return 0;
    memset(output, 0, sizeof(*output));
    output->mission = mission; output->gamemode = mode;
    output->network = (mission <= pack_plut && !patched) || mission == pack_freedoom;
    snprintf(output->title, sizeof(output->title), "%s", D_GetIWADDescription(name, mode, mission));
    SHA1_Final(output->checksum, &hash);
    return 1;
}
