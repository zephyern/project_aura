// SPDX-FileCopyrightText: 2025-2026 Volodymyr Papush (21CNCStudio)
// SPDX-License-Identifier: GPL-3.0-or-later
// GPL-3.0-or-later: https://www.gnu.org/licenses/gpl-3.0.html
// Want to use this code in a commercial product while keeping modifications proprietary?
// Purchase a Commercial License: see COMMERCIAL_LICENSE_SUMMARY.md

#pragma once

#include "config/AppConfig.h"
#include <lvgl.h>

class UiController;

class UiLocalization {
public:
    static Config::Language nextLanguage(Config::Language current);
    static void applyCurrentLanguage(UiController &owner);
    static void cycleLanguage(UiController &owner);
    static void refreshTextsForScreen(UiController &owner, int screen_id);
    static void refreshAllTexts(UiController &owner);
    // The 14 / 18 px body fonts of a language: JetBrains Mono (Latin) for
    // Western languages, Noto Sans SC for Chinese, Noto Sans JP for Japanese.
    static const lv_font_t *bodyFont14(Config::Language language);
    static const lv_font_t *bodyFont18(Config::Language language);

private:
    static void updateLanguageLabel(UiController &owner);
    static void updateLanguageFonts(UiController &owner);
};
