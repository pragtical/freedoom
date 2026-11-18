/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "odamex.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif
#include <atomic>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <google/protobuf/stubs/common.h>
#include <thread>
#include <vector>

#include "c_console.h"
#include "cl_main.h"
#include "d_main.h"
#include "errors.h"
#include "gui_boot.h"
#include "i_net.h"
#include "i_system.h"
#include "i_video.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_misc.h"
#include "v_video.h"
#include "preview.h"
#include "hooks.h"

namespace {
struct Quit {};
using Term = void (*)();
std::vector<Term> cleanup;
std::atomic<bool> used{false};
const fd_odamex_host *host;
const fd_odamex_options *options;
const char *resource_dir, *state_dir;
}

bool gameisdead;
bool in_endoom;
uint32_t LanguageIDs[4];

void addterm(Term function, const char *) { cleanup.push_back(function); }
[[noreturn]] void fd_odamex_quit() { throw Quit{}; }
void fd_odamex_check_stop()
{
    if (host->stopping(host->context)) fd_odamex_quit();
}
void fd_odamex_frame(const unsigned char *rgba, unsigned width, unsigned height)
{
    host->frame(host->context, rgba, width, height, connected && gamestate == GS_LEVEL);
}
bool fd_odamex_input(unsigned char packet[4])
{
    return host->input && host->input(host->context, packet);
}
uint64_t fd_odamex_clock()
{
    if (host && host->clock) return host->clock(host->context);
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}
void fd_odamex_sleep(uint64_t ns)
{
    if (host && host->sleep) host->sleep(ns);
    else std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
}
void fd_odamex_log(const char *message)
{
    if (host && host->log) host->log(host->context, message);
}
EXTERN_CVAR(vid_defwidth)
EXTERN_CVAR(vid_defheight)
EXTERN_CVAR(cl_name)
EXTERN_CVAR(cl_serverdownload)
void fd_odamex_configure()
{
    if (options->name && *options->name) cl_name.Set(options->name);
    cl_serverdownload.Set(fd_odamex_download() ? "1" : "0");
}
const fd_odamex_host *fd_odamex_audio() { return options && options->sound ? host : nullptr; }
const char *fd_odamex_password() { return options->password; }
bool fd_odamex_download() { return options->download != 0; }
std::vector<std::string> fd_odamex_wad_dirs()
{
    namespace fs = std::filesystem;
    auto downloads = fs::path(state_dir) / "downloads";
    std::vector<std::string> dirs{resource_dir, fs::path(options->iwad).parent_path().string()};
    if (fs::is_directory(downloads)) dirs.push_back(downloads.string());
    return dirs;
}
void fd_odamex_resize()
{
    if (!host->size) return;
    static uint32_t previous;
    uint32_t wanted = host->size(host->context);
    if (!wanted || wanted == previous) return;
    previous = wanted;
    vid_defwidth.Set(static_cast<int>(wanted & 65535u));
    vid_defheight.Set(static_cast<int>(wanted >> 16));
}

std::string M_GetBinaryDir() { return resource_dir; }
std::string M_GetHomeDir(const std::string &) { return state_dir; }
std::string M_GetUserDir() { return state_dir; }
std::string M_GetWriteDir() { return state_dir; }
std::string M_ConsoleInput() { return {}; }
std::string M_GetStacktrace(std::string header) { return header; }
scannedWADs_t GUI_BootWindow() { I_FatalError("An explicit IWAD is required"); }
bool I_IsHeadless() { return false; }
void I_Init() { I_InitSound(); atterm(I_ShutdownSound); I_InitHardware(); }
void I_BeginRead() {}
void I_EndRead() {}
void I_Endoom() {}
void I_FinishClockCalibration() {}
void I_PaintConsole() {}
std::string I_GetClipboardText(bool) { return {}; }
ticcmd_t *I_BaseTiccmd() { static ticcmd_t empty; return &empty; }
void SetLanguageIDs()
{
    std::fill(std::begin(LanguageIDs), std::end(LanguageIDs), MAKE_ID('e', 'n', 'u', '\0'));
}
void I_BaseWarning(const std::string &message) { PrintFmt(PRINT_WARNING, "{}\n", message); }
[[noreturn]] void I_BaseError(const std::string &message) { throw CRecoverableError(message); }
[[noreturn]] void I_BaseFatalError(const std::string &message) { throw CFatalError(message); }
void I_ErrorMessageBox(const char *message) { host->error(host->context, message); }
void fd_odamex_disconnect()
{
    CL_QuitNetGame(NQ_SILENT);
    M_SaveDefaults();
}

static bool numeric_endpoint(const std::string &endpoint)
{
    auto colon = endpoint.find(':');
    if (colon == std::string::npos) return false;
    struct in_addr address;
    unsigned port = 0;
    const char *end = endpoint.data() + endpoint.size();
    auto parsed = std::from_chars(endpoint.data() + colon + 1, end, port);
    return parsed.ec == std::errc{} && parsed.ptr == end && port > 0 && port <= 65535
        && inet_pton(AF_INET, endpoint.substr(0, colon).c_str(), &address) == 1;
}

int fd_odamex_run(const fd_odamex_host *callbacks, const fd_odamex_options *selected)
{
    if (!callbacks || !callbacks->frame || !callbacks->error || !callbacks->stopping
        || !selected || !selected->resources || !selected->state || !selected->iwad) return -1;
    if (used.exchange(true)) {
        callbacks->error(callbacks->context, "Odamex client remained loaded after the previous session");
        return -1;
    }
    host = callbacks;
    options = selected;
    resource_dir = options->resources;
    state_dir = options->state;
    int result = 0;
    try {
        namespace fs = std::filesystem;
        const char *resources = resource_dir, *state = state_dir;
        const char *iwad = options->iwad, *endpoint = options->endpoint;
        if (!fs::path(resources).is_absolute() || !fs::is_directory(resources)
            || !fs::path(state).is_absolute() || !fs::is_directory(state)
            || !fs::path(iwad).is_absolute() || !fs::is_regular_file(iwad))
            I_FatalError("Odamex needs absolute resource, state and IWAD paths");
        std::vector<std::string> args = {"odamex", "-iwad", iwad, "-config",
            (fs::path(state) / "odamex.cfg").string(), "-width", "640", "-height", "480",
            "-bits", "32"};
        if (!options->sound) args.emplace_back("-nosound");
        if (endpoint) {
            if (!numeric_endpoint(endpoint)) I_FatalError("Expected an IPv4 address and port");
            args.emplace_back("-connect");
            args.emplace_back(endpoint);
        }
        std::vector<char *> argv;
        for (auto &arg : args) argv.push_back(arg.data());
        Args.SetArgs(static_cast<int>(argv.size()), argv.data());
        D_DoomMain();
    } catch (const Quit &) {
    } catch (const CDoomError &error) {
        I_ErrorMessageBox(error.GetMsg().c_str());
        result = -1;
    } catch (const std::exception &error) {
        I_ErrorMessageBox(error.what());
        result = -1;
    } catch (...) {
        I_ErrorMessageBox("Unexpected exception in Odamex");
        result = -1;
    }
    while (!cleanup.empty()) {
        Term function = cleanup.back();
        cleanup.pop_back();
        try {
            function();
        } catch (...) {
            I_ErrorMessageBox("Odamex cleanup failed");
            result = -1;
        }
    }
    // Each reload has a private statically linked Protobuf descriptor registry.
    google::protobuf::ShutdownProtobufLibrary();
    host = nullptr;
    options = nullptr;
    resource_dir = state_dir = nullptr;
    return result;
}
