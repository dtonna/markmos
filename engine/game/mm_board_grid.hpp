// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <cstdint>
#include "core/mm_types.h"
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <bit>

// BoardGrid — SoA layout for match-3 / block puzzle
// Cache reason:
//   - cell_type[], cell_state[], dirty[], fall_dist[] are separate arrays
//   - match scan reads cell_type[] linearly = L1 cache streaming
//   - gravity pass reads cell_state[] + fall_dist[] = sequential
//   - No pointer chasing through OOP Cell objects
// Design:
//   - Flatten 2D to 1D: index = row * cols + col → O(1) neighbor lookup
//   - cell_type: 0 = empty, 1+ = tile type/color
//   - cell_state: bitmask for fast state queries
//   - Gravity queue: ring buffer of indices to fill — no per-frame alloc

static constexpr u16 BOARD_MAX_COLS = 16;
static constexpr u16 BOARD_MAX_ROWS = 16;
static constexpr u16 BOARD_MAX      = BOARD_MAX_COLS * BOARD_MAX_ROWS;

enum class CellType : u8 {
    Empty    = 0,
    Red      = 1,
    Blue     = 2,
    Green    = 3,
    Yellow   = 4,
    Purple   = 5,
    Orange   = 6,
    Special_ = 7,   // power-up tiles start here
};

enum class CellState : u8 {
    Empty   = 0,
    Idle    = 1,
    Falling = 2,
    Matched = 3,
    Swapping = 4,
    Cleared  = 5,
    Spawning = 6,
};

enum class MatchDirection : u8 {
    Horizontal, Vertical, Both
};

// Bitmask helpers for fast scan — operate on 64-bit chunks
struct BoardBitmask {
    u64 rows[BOARD_MAX_ROWS];   // 1 bit per column per row

    void clear() noexcept { memset(this, 0, sizeof(*this)); }
    void set(u8 row, u8 col) noexcept { rows[row] |= (1ull << col); }
    bool test(u8 row, u8 col) const noexcept { return (rows[row] >> col) & 1; }

    // Count consecutive bits in a row starting at (row, col)
    u8 count_run(u8 row, u8 col, u8 min_run) const noexcept {
        u64 mask = rows[row] >> col;
        if (!mask) return 0;
        u8 count = static_cast<u8>(std::countr_one(mask));
        return count >= min_run ? count : 0;
    }
};

struct alignas(64) BoardGrid {
    // Hot data — accessed every frame
    alignas(64) CellType  cell_type   [BOARD_MAX];  // tile color/type
    alignas(64) CellState cell_state  [BOARD_MAX];  // EMPTY/IDLE/FALLING/MATCHED
    alignas(64) u8   dirty       [BOARD_MAX];  // needs re-render
    alignas(64) i8    fall_dist   [BOARD_MAX];  // gravity distance in cells

    // Cold data — accessed infrequently
    u8 cols;
    u8 rows;
    u8 cell_size;   // pixels per cell (for render)
    u8 num_types;   // number of active tile types

    BoardGrid() { reset(); }

    void init(u8 c, u8 r, u8 types, u8 pixel_size) noexcept {
        cols      = c;
        rows      = r;
        num_types = types;
        cell_size = pixel_size;
        reset();
    }

    void reset() noexcept {
        memset(cell_type,  0, sizeof(cell_type));
        memset(cell_state, 0, sizeof(cell_state));
        memset(dirty,      0, sizeof(dirty));
        memset(fall_dist,  0, sizeof(fall_dist));
    }

    // 2D → 1D index
    u16 idx(u8 row, u8 col) const noexcept {
        return static_cast<u16>(row * cols + col);
    }

    // Neighbor access — bounds-checked, returns Empty for OOB
    CellType neighbor(u8 row, u8 col, i8 dr, i8 dc) const noexcept {
        u8 r = static_cast<u8>(static_cast<i16>(row) + dr);
        u8 c = static_cast<u8>(static_cast<i16>(col) + dc);
        if (r >= rows || c >= cols) return CellType::Empty;
        return cell_type[idx(r, c)];
    }

    // Check if cell is within board
    bool in_bounds(u8 row, u8 col) const noexcept {
        return row < rows && col < cols;
    }

    // Match find — linear scan, O(n), no alloc
    // Returns number of matches found; writes packed (start, len, dir) pairs into matches buffer
    // Packed format: [dir:1][start_row/col:7][start_col/row:8] = 16 bits, then run_len as separate entry
    // dir=0 = horizontal (row fixed, col varies), dir=1 = vertical (col fixed, row varies)
    // Cache reason: scans cell_type[] linearly, fits L1 for 16x16 = 256 entries
    static constexpr u16 MATCH_DIR_VERT = 0x8000;

    u8 find_matches(u8 min_run, u16 matches[BOARD_MAX]) const noexcept {
        u8 match_count = 0;

        auto emit_match = [&](u8 start, u8 fixed, u8 run_len, bool vertical) noexcept {
            if (match_count + 2 > BOARD_MAX) return;
            u16 packed = vertical
                ? static_cast<u16>(MATCH_DIR_VERT | (start << 8) | fixed)
                : static_cast<u16>((fixed << 8) | start);
            matches[match_count++] = packed;
            matches[match_count++] = run_len;
        };

        // Horizontal scan
        for (u8 r = 0; r < rows; ++r) {
            u8 run_start = 0;
            CellType run_type = cell_type[idx(r, 0)];
            for (u8 c = 1; c < cols; ++c) {
                CellType t = cell_type[idx(r, c)];
                if (t != run_type || t == CellType::Empty) {
                    u8 run_len = c - run_start;
                    if (run_len >= min_run && run_type != CellType::Empty) {
                        emit_match(run_start, r, run_len, false);
                    }
                    run_start = c;
                    run_type  = t;
                }
            }
            u8 run_len = cols - run_start;
            if (run_len >= min_run && run_type != CellType::Empty) {
                emit_match(run_start, r, run_len, false);
            }
        }

        // Vertical scan
        for (u8 c = 0; c < cols; ++c) {
            u8 run_start = 0;
            CellType run_type = cell_type[idx(0, c)];
            for (u8 r = 1; r < rows; ++r) {
                CellType t = cell_type[idx(r, c)];
                if (t != run_type || t == CellType::Empty) {
                    u8 run_len = r - run_start;
                    if (run_len >= min_run && run_type != CellType::Empty) {
                        emit_match(run_start, c, run_len, true);
                    }
                    run_start = r;
                    run_type  = t;
                }
            }
            u8 run_len = rows - run_start;
            if (run_len >= min_run && run_type != CellType::Empty) {
                emit_match(run_start, c, run_len, true);
            }
        }

        return match_count;
    }

    // Gravity — apply fall after match clear
    // Returns number of cells moved
    // Process: per column, bottom-up, compact non-empty cells down
    u8 apply_gravity() noexcept {
        u8 moved = 0;
        for (u8 c = 0; c < cols; ++c) {
            i8 write_row = static_cast<i8>(rows) - 1;
            for (i8 r = static_cast<i8>(rows) - 1; r >= 0; --r) {
                u16 i = idx(static_cast<u8>(r), c);
                if (cell_state[i] == CellState::Empty || cell_type[i] == CellType::Empty) {
                    continue;
                }
                if (r != write_row) {
                    // Move cell down
                    u16 dst = idx(static_cast<u8>(write_row), c);
                    cell_type[dst]   = cell_type[i];
                    cell_state[dst]  = CellState::Falling;
                    fall_dist[dst]   = static_cast<i8>(write_row - r);
                    dirty[dst]       = 1;
                    cell_type[i]     = CellType::Empty;
                    cell_state[i]    = CellState::Empty;
                    fall_dist[i]     = 0;
                    ++moved;
                }
                --write_row;
            }
        }
        return moved;
    }

    // Swap two cells — returns false if same position
    bool swap(u8 r1, u8 c1, u8 r2, u8 c2) noexcept {
        if (r1 == r2 && c1 == c2) return false;
        u16 i1 = idx(r1, c1);
        u16 i2 = idx(r2, c2);
        std::swap(cell_type[i1],  cell_type[i2]);
        std::swap(cell_state[i1], cell_state[i2]);
        cell_state[i1] = CellState::Swapping;
        cell_state[i2] = CellState::Swapping;
        dirty[i1] = dirty[i2] = 1;
        return true;
    }

    // Check if a swap would form a match (without committing)
    bool would_match(u8 r1, u8 c1, u8 r2, u8 c2) const noexcept {
        // Temporary swap on local copy — lightweight, no alloc
        BoardGrid temp = *this;
        temp.swap(r1, c1, r2, c2);
        u16 dummy[BOARD_MAX];
        return temp.find_matches(3, dummy) > 0;
    }

    // Spawn new tile at top of column
    void spawn(u8 col, CellType type) noexcept {
        for (u8 r = 0; r < rows; ++r) {
            u16 i = idx(r, col);
            if (cell_type[i] == CellType::Empty) {
                cell_type[i]   = type;
                cell_state[i]  = CellState::Spawning;
                fall_dist[i]   = static_cast<i8>(r + 1);  // distance to travel
                dirty[i]       = 1;
                return;
            }
        }
    }

    // Mark matched cells for clearing
    void mark_matched(const u16* matches, u8 count) noexcept {
        for (u8 m = 0; m + 1 < count; m += 2) {
            u16 packed = matches[m];
            u8 run_len = static_cast<u8>(matches[m + 1]);
            bool vertical = (packed & MATCH_DIR_VERT) != 0;
            if (vertical) {
                u8 start_r = static_cast<u8>((packed >> 8) & 0x7F);
                u8 c = static_cast<u8>(packed & 0xFF);
                for (u8 i = start_r; i < start_r + run_len; ++i) {
                    u16 index = idx(i, c);
                    cell_state[index] = CellState::Matched;
                    dirty[index] = 1;
                }
            } else {
                u8 r = static_cast<u8>(packed >> 8);
                u8 start_c = static_cast<u8>(packed & 0xFF);
                for (u8 i = start_c; i < start_c + run_len; ++i) {
                    u16 index = idx(r, i);
                    cell_state[index] = CellState::Matched;
                    dirty[index] = 1;
                }
            }
        }
    }
};

static_assert(offsetof(BoardGrid, fall_dist) + BOARD_MAX <= sizeof(BoardGrid),
              "BoardGrid SoA layout check");
