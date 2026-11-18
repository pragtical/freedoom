/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "odamex.h"
#include <memory>
#include "i_video.h"
#include "hooks.h"
#include "transport.h"

namespace {
class Capabilities final : public IVideoCapabilities
{
    IVideoModeList modes;
    size_t preset_count;

    void addSize(uint16_t width, uint16_t height)
    {
        modes.emplace_back(width, height, 32, WINDOW_Windowed);
        modes.emplace_back(width, height, 8, WINDOW_Windowed);
    }

public:
    Capabilities()
    {
        static constexpr uint16_t presets[][2] = {
            {640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1280, 960},
            {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160},
        };
        for (const auto &size : presets) addSize(size[0], size[1]);
        preset_count = modes.size();
    }

    void rememberSize(uint16_t width, uint16_t height)
    {
        for (const auto &mode : modes)
            if (mode.width == width && mode.height == height) return;
        // Keep the most recent non-preset tab size selectable.
        modes.erase(modes.begin() + preset_count, modes.end());
        addSize(width, height);
    }

    const IVideoModeList *getSupportedVideoModes() const override { return &modes; }
    EDisplayType getDisplayType() const override { return DISPLAY_WindowOnly; }
    const IVideoMode &getNativeMode() const override { return modes.front(); }
};

class Window final : public IWindow
{
    Capabilities &capabilities;
    IVideoMode mode{640, 480, 32, WINDOW_Windowed};
    PixelFormat format{32, 8, 8, 8, 8, 24, 16, 8, 0};
    std::unique_ptr<IWindowSurface> surface;
    std::vector<unsigned char> rgba;

public:
    explicit Window(Capabilities &capabilities) : capabilities(capabilities) {}

    const IWindowSurface *getPrimarySurface() const override { return surface.get(); }
    const IVideoMode &getVideoMode() const override { return mode; }
    const PixelFormat *getPixelFormat() const override { return &format; }
    EWindowMode getWindowMode() const override { return WINDOW_Windowed; }
    bool isFocused() const override { return true; }
    std::string getVideoDriverName() const override { return "Pragtical"; }

    bool setMode(const IVideoMode &requested) override
    {
        if (requested.width < 320 || requested.height < 200 ||
            requested.width > FD_MAX_WIDTH || requested.height > FD_MAX_HEIGHT ||
            (requested.bpp != 32 && requested.bpp != 8)) return false;
        capabilities.rememberSize(requested.width, requested.height);
        mode = requested;
        mode.window_mode = WINDOW_Windowed;
        format = mode.bpp == 32 ? PixelFormat(32, 8, 8, 8, 8, 24, 16, 8, 0) :
                                 PixelFormat(8, 0, 0, 0, 0, 0, 0, 0, 0);
        surface = std::make_unique<IWindowSurface>(mode.width, mode.height, &format);
        rgba.resize(static_cast<size_t>(mode.width) * mode.height * 4);
        return true;
    }

    void finishRefresh() override
    {
        if (!surface) return;
        auto *out = rgba.data();
        for (unsigned y = 0; y < mode.height; ++y) {
            const auto *row = surface->getBuffer(0, y);
            const auto *palette = surface->getPalette();
            for (unsigned x = 0; x < mode.width; ++x) {
                uint32_t color;
                if (mode.bpp == 32) memcpy(&color, row + x * 4, 4);
                else color = palette[row[x]];
                *out++ = static_cast<uint8_t>(color >> 16);
                *out++ = static_cast<uint8_t>(color >> 8);
                *out++ = static_cast<uint8_t>(color);
                *out++ = 255;
            }
        }
        fd_odamex_frame(rgba.data(), mode.width, mode.height);
    }
};

class Video final : public IVideoSubsystem
{
    Capabilities capabilities;
    Window window{capabilities};

public:
    const IVideoCapabilities *getVideoCapabilities() const override { return &capabilities; }
    const IWindow *getWindow() const override { return &window; }
};
}

IVideoSubsystem *fd_odamex_video()
{
    const PixelFormat format(32, 8, 8, 8, 8, 24, 16, 8, 0);
    argb_t::setChannels(format.getAPos(), format.getRPos(), format.getGPos(), format.getBPos());
    return new Video;
}
