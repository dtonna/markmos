// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// mm_event_bus.hpp — typed game-layer event bus (header-only).
//
// Two buses with a clear boundary:
//   ui::UiEvent  (poll/drain) ......... widget layer (buttons, lists, ...)
//   EventBus     (pub/sub + queue) .... game layer (win, moves, app lifecycle)
//
// Adapted from the proposed sketch for engine conventions:
//   - fixed-pool subscribers (no std::vector in the hot path)
//   - newest-wins queue overflow (never assert-drops)
//   - noexcept everywhere, MM_LOG (no printf)
//   - off() during dispatch is deferred (swap-pop under iteration is UB)
//   - HandleInfo drops the unused gen (SlotHandle itself is generational)
#pragma once

#include "../core/mm_log.hpp"
#include "core/mm_types.h"
#include "../core/mm_slotmap.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>

template <typename T>
concept GameEvent = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T> && sizeof(T) <= 32;

// ============================================================================
// Single source of truth - add events here only
// ============================================================================
#define MM_EVENT_LIST(X) \
    X(AppPause)          \
    X(AppResume)         \
    X(LowMemory)         \
    X(SurfaceChanged)    \
    X(GameWon)           \
    X(InvalidMove)       \
    X(CardMoved)         \
    X(CoinCollected)     \
    X(PlayerDied)

enum class EventID : u8 {
#define X(name) name,
    MM_EVENT_LIST(X)
#undef X
        Count
};

struct AppPause {};
struct AppResume {};
struct LowMemory {};
struct SurfaceChanged {
    u32 w, h;
    void    *native_handle;
};
struct GameWon {
    u32 moves;
    f32    time;
};
struct InvalidMove {
    u32 from, to;
};
struct CardMoved {
    u32 cardId;
    u32 fromPile;
    u32 toPile;
};
struct CoinCollected {
    u32 amount;
};
struct PlayerDied {
    u32 playerId;
};

template <GameEvent E> struct EventTraits;
#define X(name)                                      \
    template <> struct EventTraits<name> {           \
        static constexpr EventID id = EventID::name; \
    };
MM_EVENT_LIST(X)
#undef X

// ============================================================================
// EventBus
// ============================================================================
class EventBus {
    struct Handler {
        void *ctx = nullptr;
        // Opaque bits of the subscriber's void(*)(void*, const E&).
        // Round-trips through memcpy only; emit<E>() restores the exact
        // same type before calling, so there is never an
        // incompatible-function-type cast (-Wcast-function-type-strict).
        alignas(void *) u8 fn_bits[sizeof(void *)] = {};
        EventID eventId                                 = EventID::Count;
    };

    static constexpr size_t   K_EVENT_COUNT = static_cast<size_t>(EventID::Count);
    static constexpr u8  K_MAX_SUBS    = 8;
    static constexpr u32 QCAP          = 256;
    static constexpr u8  K_DEFERRED    = 16;

    Handler                   sub_pool[K_EVENT_COUNT][K_MAX_SUBS];
    u8                   sub_count[K_EVENT_COUNT] = {};

    struct HandleInfo {
        EventID  eid;
        u32 index;
    };
    Slotmap<HandleInfo, 64> handleMap;

    // Deferred off() requests collected while dispatching (see off()).
    u8                 emit_depth = 0;
    SlotHandle              deferred[K_DEFERRED];
    u8                 deferred_count = 0;

    // Double-buffered queue: emit/enqueue write qRead, flush() dispatches the
    // other side, so handlers may enqueue without disturbing iteration.
    // Full buffer overwrites the oldest (newest state wins).
    struct Tagged {
        EventID id;
        alignas(16) u8 data[32];
    };
    Tagged   q[2][QCAP];
    u32 qHead[2]  = {0, 0};
    u32 qTail[2]  = {0, 0};
    u32 qCount[2] = {0, 0};
    int      qRead     = 0;

    void     process_deferred() noexcept {
        if (deferred_count == 0) {
            return;
        }
        // Snapshot first: off() below runs direct removals, which must not
        // disturb the list being drained.
        SlotHandle pending[K_DEFERRED];
        u8    n = deferred_count;
        for (u8 i = 0; i < n; ++i) {
            pending[i] = deferred[i];
        }
        deferred_count = 0;
        for (u8 i = 0; i < n; ++i) {
            off(pending[i]);
        }
    }

    void remove_at(EventID eid, u32 idx) noexcept {
        auto    &vec   = sub_pool[static_cast<size_t>(eid)];
        auto    &count = sub_count[static_cast<size_t>(eid)];
        u32 last  = static_cast<u32>(count) - 1;
        if (idx != last) {
            vec[idx] = vec[last];
            // Fix the handle of the swapped entry: find it and update index.
            for (auto it = handleMap.iter(); auto *hi = it.next();) {
                if (hi->eid == eid && hi->index == last) {
                    hi->index = idx;
                    break;
                }
            }
        }
        --count;
    }

  public:
    template <GameEvent E> [[nodiscard]] SlotHandle on(void *ctx, void (*fn)(void *, const E &)) noexcept {
        constexpr auto eid   = EventTraits<E>::id;
        auto          &count = sub_count[static_cast<size_t>(eid)];
        if (count >= K_MAX_SUBS || fn == nullptr) {
            return SlotHandle::invalid();
        }
        static_assert(sizeof(fn) <= sizeof(Handler::fn_bits), "subscriber fn pointer does not fit Handler storage");
        auto   &vec = sub_pool[static_cast<size_t>(eid)];
        Handler entry{};
        entry.ctx     = ctx;
        entry.eventId = eid;
        std::memcpy(entry.fn_bits, &fn, sizeof(fn));
        vec[count] = entry;
        HandleInfo info{eid, count};
        SlotHandle h = handleMap.emplace(info);
        if (h == SlotHandle::invalid()) {
            return h;
        }
        ++count;
        return h;
    }

    // Unsubscribe. Safe to call inside a handler: the removal is deferred
    // until the current dispatch unwinds (swap-pop under iteration is UB).
    void off(SlotHandle h) noexcept {
        if (emit_depth > 0) {
            if (deferred_count < K_DEFERRED) {
                deferred[deferred_count++] = h;
            } else {
                MM_LOG("EventBus: deferred off() overflow, handle kept");
            }
            return;
        }
        auto *info = handleMap.get(h);
        if (info == nullptr) {
            return; // stale/unknown handle: no-op
        }
        auto &count = sub_count[static_cast<size_t>(info->eid)];
        if (info->index >= count) {
            handleMap.free(h);
            return;
        }
        // Slot identity holds: every mutation goes through remove_at/on,
        // which keeps HandleInfo.index in sync with the dense array.
        remove_at(info->eid, info->index);
        handleMap.free(h);
    }

    template <GameEvent E> void emit(const E &e) noexcept {
        constexpr auto e_id = EventTraits<E>::id;
        const auto    &vec  = sub_pool[static_cast<size_t>(e_id)];
        const u8  n    = sub_count[static_cast<size_t>(e_id)];
        ++emit_depth;
        for (u8 i = 0; i < n; ++i) {
            // NOTE: count is snapshotted; handlers must not call off()
            // directly (it is deferred) nor flush() (order inversion).
            // fn_bits round-trips to the exact subscribed type — no
            // incompatible-function-type cast, no UB on the call.
            void (*typed_fn)(void *, const E &) = nullptr;
            static_assert(sizeof(typed_fn) == sizeof(vec[i].fn_bits), "subscriber fn pointer size mismatch");
            std::memcpy(&typed_fn, vec[i].fn_bits, sizeof(typed_fn));
            typed_fn(vec[i].ctx, e);
        }
        --emit_depth;
        if (emit_depth == 0) {
            process_deferred();
        }
    }

    template <GameEvent E> inline bool enqueue(const E &e) noexcept {
        static_assert(sizeof(E) <= 32, "Event >32 bytes - increase Tagged::data");
        if (qCount[qRead] >= QCAP) {
            qTail[qRead] = (qTail[qRead] + 1) % QCAP; // drop oldest
        } else {
            ++qCount[qRead];
        }
        auto &slot = q[qRead][(qTail[qRead] + qCount[qRead] - 1) % QCAP];
        slot.id    = EventTraits<E>::id;
        std::memcpy(slot.data, &e, sizeof(E));
        return true;
    }

    template <GameEvent E> [[nodiscard]] bool can_enqueue() const noexcept { return qCount[qRead] < QCAP; }

    inline void                               flush() noexcept {
        int r           = qRead;
        qRead          ^= 1;
        u32 count  = qCount[r];
        for (u32 i = 0; i < count; ++i) {
            auto &te = q[r][(qTail[r] + i) % QCAP];
            switch (te.id) {
#define X(name)                                           \
    case EventID::name: {                                 \
        name ev_tmp{};                                    \
        std::memcpy(&ev_tmp, te.data, sizeof(name));      \
        emit(ev_tmp);                                     \
        break;                                            \
    }
                MM_EVENT_LIST(X)
#undef X
            default:
                break;
            }
        }
        qCount[r] = 0;
        qTail[r]  = 0;
    }

    template <GameEvent E> [[nodiscard]] u32 count() const noexcept {
        constexpr auto e_id = EventTraits<E>::id;
        return sub_count[static_cast<size_t>(e_id)];
    }

    u32 pending() const noexcept { return qCount[qRead]; }

    void     stats() const noexcept {
        MM_LOG("EventBus: %u queued, total types %zu", qCount[qRead], K_EVENT_COUNT);
        for (size_t i = 0; i < K_EVENT_COUNT; ++i) {
            if (sub_count[i] > 0) {
                MM_LOG("  [%zu] %u subs", i, static_cast<u32>(sub_count[i]));
            }
        }
    }

    void clear() noexcept {
        for (size_t i = 0; i < K_EVENT_COUNT; ++i) {
            sub_count[i] = 0;
        }
        handleMap = Slotmap<HandleInfo, 64>{};
        qHead[0] = qHead[1] = 0;
        qTail[0] = qTail[1] = 0;
        qCount[0] = qCount[1] = 0;
        deferred_count        = 0;
        emit_depth            = 0;
    }
};

extern EventBus g_event_bus; // global singleton, defined in engine/game/mm_event_bus.cpp
inline bool     EventBusIsInit() { return true; }
