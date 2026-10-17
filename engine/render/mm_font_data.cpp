// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#include "mm_font_data.hpp"
#include "mm_font_ttf_data.hpp"

extern const unsigned char* g_karla_ttf = thirdparty_imgui_misc_fonts_Karla_Regular_ttf;
extern const unsigned int  g_karla_ttf_size = thirdparty_imgui_misc_fonts_Karla_Regular_ttf_len;
[[maybe_unused]] static const auto _karla_ref = &g_karla_ttf_size;

#if MM_ENGINE_FONT_TH
#include "mm_font_th_data.hpp"
extern const unsigned char* g_sarabun_ttf = _sarabun_ttf_src;
extern const unsigned int  g_sarabun_ttf_size = _sarabun_ttf_src_size;
#endif

// g_noto_symbols_ttf / g_noto_symbols_ttf_len are defined in mm_font_sym_data.cpp,
// which is only compiled when ENGINE_FONT_SYM is ON. Declared in mm_font_sym_data.hpp.
