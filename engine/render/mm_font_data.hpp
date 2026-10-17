// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <cstdint>

// Karla (ASCII) is always linked — every app draws text.
//
// The other three are whole TTFs baked for glyphs most apps never draw, so
// each is opt-in behind an ENGINE_FONT_* CMake option and the declaration is
// absent when the set is off. A caller must therefore #if on the same macro
// rather than checking the length: the symbol does not exist at all.
//
// g_noto_symbols_ttf lives in mm_font_sym_data.hpp, not here, because it is a
// separate translation unit that is only compiled when ENGINE_FONT_SYM is ON.

extern const unsigned char* g_karla_ttf;
extern const unsigned int  g_karla_ttf_size;

#if MM_ENGINE_FONT_TH
// Sarabun — Thai. +94 KB.
extern const unsigned char* g_sarabun_ttf;
extern const unsigned int  g_sarabun_ttf_size;
#endif

// MM_ENGINE_FONT_SYM1 needs no data symbol at all: U+238C arrives pre-baked in
// mm_glyph_undo_data.hpp (280 bytes) rather than as a 185 KB TTF. The macro
// gates the include in mm_renderer.hpp and nothing else.