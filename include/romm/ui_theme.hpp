#pragma once

#include <cstdint>

namespace romm {

struct UiColorRgba {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

struct UiSpacing {
    int xs;
    int sm;
    int md;
    int lg;
    int xl;
};

struct UiTypography {
    float titleScale;
    float bodyScale;
    float captionScale;
};

struct WiiShopTheme {
    UiColorRgba bg;
    UiColorRgba surface;
    UiColorRgba border;
    UiColorRgba primary;
    UiColorRgba primarySoft;
    UiColorRgba text;
    UiColorRgba subtext;
    UiSpacing spacing;
    UiTypography type;
};

struct WiiUHybridTheme {
    UiColorRgba bg;
    UiColorRgba shell;
    UiColorRgba border;
    UiColorRgba tile;
    UiColorRgba selectedTile;
    UiColorRgba accent;
    UiColorRgba muted;
    UiSpacing spacing;
};

inline constexpr WiiShopTheme kWiiShopTheme{
    /* bg */ {236, 236, 236, 255},
    /* surface */ {248, 250, 252, 255},
    /* border */ {132, 201, 236, 255},
    /* primary */ {66, 190, 240, 255},
    /* primarySoft */ {222, 245, 254, 255},
    /* text */ {20, 34, 48, 255},
    /* subtext */ {41, 120, 154, 255},
    /* spacing */ {4, 8, 12, 18, 24},
    /* type */ {3.0f, 2.3f, 1.7f},
};

inline constexpr WiiUHybridTheme kWiiUHybridTheme{
    /* bg */ {16, 20, 28, 255},
    /* shell */ {235, 236, 238, 255},
    /* border */ {213, 214, 217, 255},
    /* tile */ {245, 246, 248, 255},
    /* selectedTile */ {208, 243, 245, 255},
    /* accent */ {52, 169, 198, 255},
    /* muted */ {166, 170, 176, 255},
    /* spacing */ {6, 10, 16, 24, 32},
};

} // namespace romm
