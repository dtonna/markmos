// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <cstdint>
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

static constexpr uint16_t BOARD_MAX_COLS = 16;
static constexpr uint16_t BOARD_MAX_ROWS = 16;
static constexpr uint16_t BOARD_MAX      = BOARD_MAX_COLS * BOARD_MAX_ROWS;

enum class CellType : uint8_t {
    Empty    = 0,
    Red      = 1,
    Blue     = 2,
    Green    = 3,
    Yellow   = 4,
    Purple   = 5,
    Orange   = 6,
    Special_ = 7,   // power-up tiles start here
};

enum class CellState : uint8_t {
    Empty   = 0,
    Idle    = 1,
    Falling = 2,
    Matched = 3,
    Swapping = 4,
    Cleared  = 5,
    Spawning = 6,
};

enum class MatchDirection : uint8_t {
    Horizontal, Vertical, Both
};

// Bitmask helpers for fast scan — operate on 64-bit chunks
struct BoardBitmask {
    uint64_t rows[BOARD_MAX_ROWS];   // 1 bit per column per row

    void clear() noexcept { memset(this, 0, sizeof(*this)); }
    void set(uint8_t row, uint8_t col) noexcept { rows[row] |= (1ull << col); }
    bool test(uint8_t row, uint8_t col) const noexcept { return (rows[row] >> col) & 1; }

    // Count consecutive bits in a row starting at (row, col)
    uint8_t count_run(uint8_t row, uint8_t col, uint8_t min_run) const noexcept {
        uint64_t mask = rows[row] >> col;
        if (!mask) return 0;
        uint8_t count = static_cast<uint8_t>(std::countr_one(mask));
        return count >= min_run ? count : 0;
    }
};

struct alignas(64) BoardGrid {
    // Hot data — accessed every frame
    alignas(64) CellType  cell_type   [BOARD_MAX];  // tile color/type
    alignas(64) CellState cell_state  [BOARD_MAX];  // EMPTY/IDLE/FALLING/MATCHED
    alignas(64) uint8_t   dirty       [BOARD_MAX];  // needs re-render
    alignas(64) int8_t    fall_dist   [BOARD_MAX];  // gravity distance in cells

    // Cold data — accessed infrequently
    uint8_t cols;
    uint8_t rows;
    uint8_t cell_size;   // pixels per cell (for render)
    uint8_t num_types;   // number of active tile types

    BoardGrid() { reset(); }

    void init(uint8_t c, uint8_t r, uint8_t types, uint8_t pixel_size) noexcept {
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
    uint16_t idx(uint8_t row, uint8_t col) const noexcept {
        return static_cast<uint16_t>(row * cols + col);
    }

    // Neighbor access — bounds-checked, returns Empty for OOB
    CellType neighbor(uint8_t row, uint8_t col, int8_t dr, int8_t dc) const noexcept {
        uint8_t r = static_cast<uint8_t>(static_cast<int16_t>(row) + dr);
        uint8_t c = static_cast<uint8_t>(static_cast<int16_t>(col) + dc);
        if (r >= rows || c >= cols) return CellType::Empty;
        return cell_type[idx(r, c)];
    }

    // Check if cell is within board
    bool in_bounds(uint8_t row, uint8_t col) const noexcept {
        return row < rows && col < cols;
    }

    // Match find — linear scan, O(n), no alloc
    // Returns number of matches found; writes packed (start, len, dir) pairs into matches buffer
    // Packed format: [dir:1][start_row/col:7][start_col/row:8] = 16 bits, then run_len as separate entry
    // dir=0 = horizontal (row fixed, col varies), dir=1 = vertical (col fixed, row varies)
    // Cache reason: scans cell_type[] linearly, fits L1 for 16x16 = 256 entries
    static constexpr uint16_t MATCH_DIR_VERT = 0x8000;

    uint8_t find_matches(uint8_t min_run, uint16_t matches[BOARD_MAX]) const noexcept {
        uint8_t match_count = 0;

        auto emit_match = [&](uint8_t start, uint8_t fixed, uint8_t run_len, bool vertical) noexcept {
            if (match_count + 2 > BOARD_MAX) return;
            uint16_t packed = vertical
                ? static_cast<uint16_t>(MATCH_DIR_VERT | (start << 8) | fixed)
                : static_cast<uint16_t>((fixed << 8) | start);
            matches[match_count++] = packed;
            matches[match_count++] = run_len;
        };

        // Horizontal scan
        for (uint8_t r = 0; r < rows; ++r) {
            uint8_t run_start = 0;
            CellType run_type = cell_type[idx(r, 0)];
            for (uint8_t c = 1; c < cols; ++c) {
                CellType t = cell_type[idx(r, c)];
                if (t != run_type || t == CellType::Empty) {
                    uint8_t run_len = c - run_start;
                    if (run_len >= min_run && run_type != CellType::Empty) {
                        emit_match(run_start, r, run_len, false);
                    }
                    run_start = c;
                    run_type  = t;
                }
            }
            uint8_t run_len = cols - run_start;
            if (run_len >= min_run && run_type != CellType::Empty) {
                emit_match(run_start, r, run_len, false);
            }
        }

        // Vertical scan
        for (uint8_t c = 0; c < cols; ++c) {
            uint8_t run_start = 0;
            CellType run_type = cell_type[idx(0, c)];
            for (uint8_t r = 1; r < rows; ++r) {
                CellType t = cell_type[idx(r, c)];
                if (t != run_type || t == CellType::Empty) {
                    uint8_t run_len = r - run_start;
                    if (run_len >= min_run && run_type != CellType::Empty) {
                        emit_match(run_start, c, run_len, true);
                    }
                    run_start = r;
                    run_type  = t;
                }
            }
            uint8_t run_len = rows - run_start;
            if (run_len >= min_run && run_type != CellType::Empty) {
                emit_match(run_start, c, run_len, true);
            }
        }

        return match_count;
    }

    // Gravity — apply fall after match clear
    // Returns number of cells moved
    // Process: per column, bottom-up, compact non-empty cells down
    uint8_t apply_gravity() noexcept {
        uint8_t moved = 0;
        for (uint8_t c = 0; c < cols; ++c) {
            int8_t write_row = static_cast<int8_t>(rows) - 1;
            for (int8_t r = static_cast<int8_t>(rows) - 1; r >= 0; --r) {
                uint16_t i = idx(static_cast<uint8_t>(r), c);
                if (cell_state[i] == CellState::Empty || cell_type[i] == CellType::Empty) {
                    continue;
                }
                if (r != write_row) {
                    // Move cell down
                    uint16_t dst = idx(static_cast<uint8_t>(write_row), c);
                    cell_type[dst]   = cell_type[i];
                    cell_state[dst]  = CellState::Falling;
                    fall_dist[dst]   = static_cast<int8_t>(write_row - r);
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
    bool swap(uint8_t r1, uint8_t c1, uint8_t r2, uint8_t c2) noexcept {
        if (r1 == r2 && c1 == c2) return false;
        uint16_t i1 = idx(r1, c1);
        uint16_t i2 = idx(r2, c2);
        std::swap(cell_type[i1],  cell_type[i2]);
        std::swap(cell_state[i1], cell_state[i2]);
        cell_state[i1] = CellState::Swapping;
        cell_state[i2] = CellState::Swapping;
        dirty[i1] = dirty[i2] = 1;
        return true;
    }

    // Check if a swap would form a match (without committing)
    bool would_match(uint8_t r1, uint8_t c1, uint8_t r2, uint8_t c2) const noexcept {
        // Temporary swap on local copy — lightweight, no alloc
        BoardGrid temp = *this;
        temp.swap(r1, c1, r2, c2);
        uint16_t dummy[BOARD_MAX];
        return temp.find_matches(3, dummy) > 0;
    }

    // Spawn new tile at top of column
    void spawn(uint8_t col, CellType type) noexcept {
        for (uint8_t r = 0; r < rows; ++r) {
            uint16_t i = idx(r, col);
            if (cell_type[i] == CellType::Empty) {
                cell_type[i]   = type;
                cell_state[i]  = CellState::Spawning;
                fall_dist[i]   = static_cast<int8_t>(r + 1);  // distance to travel
                dirty[i]       = 1;
                return;
            }
        }
    }

    // Mark matched cells for clearing
    void mark_matched(const uint16_t* matches, uint8_t count) noexcept {
        for (uint8_t m = 0; m + 1 < count; m += 2) {
            uint16_t packed = matches[m];
            uint8_t run_len = static_cast<uint8_t>(matches[m + 1]);
            bool vertical = (packed & MATCH_DIR_VERT) != 0;
            if (vertical) {
                uint8_t start_r = static_cast<uint8_t>((packed >> 8) & 0x7F);
                uint8_t c = static_cast<uint8_t>(packed & 0xFF);
                for (uint8_t i = start_r; i < start_r + run_len; ++i) {
                    uint16_t index = idx(i, c);
                    cell_state[index] = CellState::Matched;
                    dirty[index] = 1;
                }
            } else {
                uint8_t r = static_cast<uint8_t>(packed >> 8);
                uint8_t start_c = static_cast<uint8_t>(packed & 0xFF);
                for (uint8_t i = start_c; i < start_c + run_len; ++i) {
                    uint16_t index = idx(r, i);
                    cell_state[index] = CellState::Matched;
                    dirty[index] = 1;
                }
            }
        }
    }
};

static_assert(offsetof(BoardGrid, fall_dist) + BOARD_MAX <= sizeof(BoardGrid),
              "BoardGrid SoA layout check");
