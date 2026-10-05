// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once

#include "core/mm_types.h"

// Shared engine-level UI palette: the single home for packed 0xAARRGGBB colors
// used by engine/ui defaults, the mm_07 gallery, markmos and the freecell board.
// Before this, the same grays were re-typed as literals in every file (mm_07
// alone had 0xFFAAAAAA 101 times), so a palette tweak meant hunting literals.
//
// Deliberately NOT covered, and why:
// - engine/tests/* fixtures stay literal: a test must read standalone, and its
//   colors are inputs, not design decisions.
// - apps/freecell/game/mm_theme.hpp stays the game theme: it already names its
//   own colors (card greens, felts). Where values coincide the names match on
//   purpose so the two systems read consistently, but neither includes the other.
// - One-off card/suit/atlas colors (used 1-4x) stay literal: naming each adds
//   indirection without benefit.
// - Shader sources (.frag/.msl) cannot include C++ headers.

namespace palette {

inline constexpr u32 TRANSPARENT = 0x00000000;
inline constexpr u32 BLACK       = 0xFF000000;
inline constexpr u32 WHITE       = 0xFFFFFFFF;

// Grays, dark to light. Names match mm_theme.hpp where values coincide.
inline constexpr u32 GRAY_1D1D1D = 0xFF1D1D1D;
inline constexpr u32 GRAY_222222 = 0xFF222222;
inline constexpr u32 PANEL       = 0xFF2D2D2D; // default panel background
inline constexpr u32 BUTTON_BG   = 0xFF3A3A3A; // default button fill
inline constexpr u32 GRAY_DARK   = 0xFF444444;
inline constexpr u32 GRAY        = 0xFF666666;
inline constexpr u32 GRAY_MID    = 0xFF888888;
inline constexpr u32 GRAY_LIGHT  = 0xFFAAAAAA; // default secondary label
inline constexpr u32 GRAY_PALE   = 0xFFCCCCCC;

// Accents.
inline constexpr u32 TOGGLE_GREEN = 0xFF5CB85C;
inline constexpr u32 FOCUS_BLUE   = 0xFF88AAFF;
inline constexpr u32 ACCENT_BLUE  = 0xFF4488FF;
inline constexpr u32 BLUE_DARK    = 0xFF3366CC;

// Game-state colors shared by markmos and the demos.
inline constexpr u32 RED         = 0xFFFF4444;
inline constexpr u32 RED_DARK     = 0xFFCC3333;
inline constexpr u32 GREEN_BRIGHT = 0xFF44FF44;
inline constexpr u32 GREEN_SOFT   = 0xFF88FF88;
inline constexpr u32 GREEN_MINT   = 0xFF44FF88;
inline constexpr u32 YELLOW       = 0xFFFFFF00;
inline constexpr u32 MAGENTA_SOFT = 0xFFFFAAFF;
inline constexpr u32 PURPLE_DIM   = 0xFF664466;
inline constexpr u32 PLUM         = 0xFF554466;

} // namespace palette
