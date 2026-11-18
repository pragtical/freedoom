/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "v_text.h"

class OdamexMenuFont
{
    const OGlobalFont previous = hu_font;

public:
    explicit OdamexMenuFont(const char *face = "DIGFONT")
    {
        if (face) V_SetFont(face);
    }

    ~OdamexMenuFont() { hu_font = previous; }
};
