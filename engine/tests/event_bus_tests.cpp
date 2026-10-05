// EventBus tests — subscribe/emit/unsubscribe, deferred off, queue order,
// overflow (newest wins), clear, stale handles, max subs.
// Plain main() + assert(), no framework. Header-only, no backend needed.
#include "../game/mm_event_bus.hpp"
#include "core/mm_types.h"

#include <cassert>
#include <cstdio>

struct Rec {
    u32 ids[300];
    u32 vals[300];
    u32 n = 0;
    void push(u32 id, u32 v) {
        if (n < 300) {
            ids[n]  = id;
            vals[n] = v;
            ++n;
        }
    }
    void reset() { n = 0; }
};

static Rec g_rec;

static void OnCard(void* ctx, const CardMoved& e) {
    (void)ctx;
    g_rec.push(1, e.cardId);
}
static void OnCard2(void* ctx, const CardMoved& e) {
    (void)ctx;
    g_rec.push(2, e.cardId);
}
static void OnWon(void* ctx, const GameWon& e) {
    (void)ctx;
    g_rec.push(7, e.moves);
}

struct OffCtx {
    EventBus*  bus;
    SlotHandle victim;
};
static void OnOffOther(void* ctx, const CardMoved& e) {
    auto* o = static_cast<OffCtx*>(ctx);
    g_rec.push(9, e.cardId);
    o->bus->off(o->victim); // deferred: victim still gets this emit
}
static void OnVictim(void* ctx, const CardMoved& e) {
    (void)ctx;
    g_rec.push(8, e.cardId);
}
static void OnSelfOff(void* ctx, const CardMoved& e) {
    auto* o = static_cast<OffCtx*>(ctx);
    g_rec.push(6, e.cardId);
    o->bus->off(o->victim); // victim == self here
}

int main() {
    // ─── Sub + direct emit (order + payload + ctx) ───
    {
        EventBus bus;
        g_rec.reset();
        int tag = 5;
        SlotHandle h = bus.on<CardMoved>(&tag, OnCard);
        assert(!(h == SlotHandle::invalid()));
        assert(bus.count<CardMoved>() == 1);
        bus.emit(CardMoved{42, 3, 7});
        assert(g_rec.n == 1 && g_rec.ids[0] == 1 && g_rec.vals[0] == 42);
        bus.off(h);
        assert(bus.count<CardMoved>() == 0);
        bus.emit(CardMoved{43, 3, 7});
        assert(g_rec.n == 1); // unsubscribed: silent
    }

    // ─── Two handlers, both fire in subscribe order ───
    {
        EventBus bus;
        g_rec.reset();
        bus.on<CardMoved>(nullptr, OnCard);
        bus.on<CardMoved>(nullptr, OnCard2);
        bus.emit(CardMoved{9, 0, 1});
        assert(g_rec.n == 2 && g_rec.ids[0] == 1 && g_rec.ids[1] == 2);
        assert(g_rec.vals[0] == 9 && g_rec.vals[1] == 9);
    }

    // ─── off() unknown / twice: safe no-ops ───
    {
        EventBus bus;
        g_rec.reset();
        bus.off(SlotHandle::invalid());
        SlotHandle h = bus.on<CardMoved>(nullptr, OnCard);
        bus.off(h);
        bus.off(h); // second free: no-op
        bus.emit(CardMoved{1, 0, 0});
        assert(g_rec.n == 0);
    }

    // ─── Stale handle after slot reuse ───
    {
        EventBus bus;
        g_rec.reset();
        SlotHandle a = bus.on<CardMoved>(nullptr, OnCard);
        bus.off(a);
        SlotHandle b = bus.on<CardMoved>(nullptr, OnCard2);
        (void)b;
        bus.off(a); // stale: must not kill b
        bus.emit(CardMoved{3, 0, 0});
        assert(g_rec.n == 1 && g_rec.ids[0] == 2);
    }

    // ─── off() inside handler is deferred ───
    {
        EventBus bus;
        g_rec.reset();
        OffCtx    ctx{&bus, SlotHandle::invalid()};
        SlotHandle v = bus.on<CardMoved>(nullptr, OnVictim);
        ctx.victim   = v;
        bus.on<CardMoved>(&ctx, OnOffOther); // runs after victim
        bus.emit(CardMoved{11, 0, 0});
        // Victim still got this emit (removal deferred past dispatch).
        assert(g_rec.n == 2 && g_rec.ids[0] == 8 && g_rec.ids[1] == 9);
        g_rec.reset();
        bus.emit(CardMoved{12, 0, 0});
        assert(g_rec.n == 1 && g_rec.ids[0] == 9); // victim gone now
    }

    // ─── Self off inside handler ───
    {
        EventBus bus;
        g_rec.reset();
        OffCtx     ctx{&bus, SlotHandle::invalid()};
        SlotHandle s = bus.on<CardMoved>(&ctx, OnSelfOff);
        ctx.victim   = s;
        bus.emit(CardMoved{21, 0, 0});
        assert(g_rec.n == 1 && g_rec.ids[0] == 6);
        bus.emit(CardMoved{22, 0, 0});
        assert(g_rec.n == 1); // self removed
    }

    // ─── enqueue -> flush keeps FIFO order across types ───
    {
        EventBus bus;
        g_rec.reset();
        bus.on<CardMoved>(nullptr, OnCard);
        bus.on<GameWon>(nullptr, OnWon);
        bus.enqueue(CardMoved{1, 0, 0});
        bus.enqueue(GameWon{30, 95.5f});
        bus.enqueue(CardMoved{2, 0, 0});
        assert(bus.pending() == 3);
        bus.flush();
        assert(g_rec.n == 3);
        assert(g_rec.ids[0] == 1 && g_rec.vals[0] == 1);
        assert(g_rec.ids[1] == 7 && g_rec.vals[1] == 30);
        assert(g_rec.ids[2] == 1 && g_rec.vals[2] == 2);
        assert(bus.pending() == 0);
    }

    // ─── Overflow keeps the newest (drops oldest) ───
    {
        EventBus bus;
        g_rec.reset();
        bus.on<CardMoved>(nullptr, OnCard);
        for (u32 k = 0; k < 260; ++k) {
            assert(bus.enqueue(CardMoved{k, 0, 0}));
        }
        assert(!bus.can_enqueue<GameWon>());
        assert(bus.pending() == 256);
        bus.flush();
        assert(g_rec.n == 256);
        assert(g_rec.vals[0] == 4);   // 260 - 256 dropped from the front
        assert(g_rec.vals[255] == 259);
    }

    // ─── Enqueue during flush lands in the other buffer (next flush) ───
    {
        EventBus bus;
        g_rec.reset();
        bus.on<CardMoved>(nullptr, OnCard);
        bus.enqueue(CardMoved{100, 0, 0});
        bus.flush();
        assert(g_rec.n == 1 && g_rec.vals[0] == 100);
        bus.enqueue(CardMoved{101, 0, 0});
        assert(bus.pending() == 1);
    }

    // ─── A handler may enqueue: delivered on the next flush, in order ───
    {
        struct ChainCtx {
            EventBus* bus;
            int       left;
        };
        struct Chain {
            static void fn(void* ctx, const CardMoved& e) {
                auto* c = static_cast<ChainCtx*>(ctx);
                g_rec.push(4, e.cardId);
                if (c->left > 0) {
                    --c->left;
                    c->bus->enqueue(CardMoved{e.cardId + 1, 0, 0});
                }
            }
        };
        EventBus bus;
        g_rec.reset();
        ChainCtx cc{&bus, 2};
        bus.on<CardMoved>(&cc, Chain::fn);
        bus.enqueue(CardMoved{50, 0, 0});
        bus.flush(); // delivers 50, chains 51 into the other buffer
        assert(g_rec.n == 1 && g_rec.vals[0] == 50);
        bus.flush(); // delivers 51, chains 52
        assert(g_rec.n == 2 && g_rec.vals[1] == 51);
        bus.flush(); // delivers 52, chain spent
        assert(g_rec.n == 3 && g_rec.vals[2] == 52);
        assert(bus.pending() == 0);
    }

    // ─── Max subs: 9th rejected ───
    {
        EventBus bus;
        for (int k = 0; k < 8; ++k) {
            SlotHandle h = bus.on<CardMoved>(nullptr, OnCard);
            assert(!(h == SlotHandle::invalid()));
        }
        assert(bus.count<CardMoved>() == 8);
        SlotHandle over = bus.on<CardMoved>(nullptr, OnCard);
        assert(over == SlotHandle::invalid());
        assert(bus.count<CardMoved>() == 8);
    }

    // ─── clear() resets subs + queue ───
    {
        EventBus bus;
        g_rec.reset();
        bus.on<CardMoved>(nullptr, OnCard);
        bus.enqueue(CardMoved{1, 0, 0});
        bus.clear();
        assert(bus.count<CardMoved>() == 0);
        assert(bus.pending() == 0);
        bus.flush();
        assert(g_rec.n == 0);
        // Bus is reusable after clear.
        bus.on<CardMoved>(nullptr, OnCard2);
        bus.emit(CardMoved{5, 0, 0});
        assert(g_rec.n == 1 && g_rec.ids[0] == 2);
    }

    // ─── Emit with no subscribers: safe no-op ───
    {
        EventBus bus;
        bus.emit(PlayerDied{3});
        bus.flush();
    }

    printf("[eventbus] all tests passed\n");
    return 0;
}
