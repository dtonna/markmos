#pragma once

// Frame-time accounting for the platform hosts.
//
// The hosts each keep a `_lastFrameTime` to derive dt for the simulation and
// throw it away, so "is it hitting 60?" had no place to be answered from. This
// is that place, and it is one header for all three hosts because all three
// compute dt identically (monotonic now minus last, clamped at 0.1 s) — three
// copies of a window and a sort would be three copies to drift.
//
// Header-only, no allocation, a fixed window: it lives inside the host object
// and costs PERF_WINDOW floats.

#include "core/mm_log.hpp"
#include "core/mm_types.h"

#include <cstdio>

#if defined(__APPLE__)
// On Apple platforms MM_LOG is printf, and stdout from a GUI app launched with
// `open` is block-buffered and invisible — there is no terminal to read. The
// unified log IS readable (`log show --last 1m --predicate 'eventMessage contains
// "PERF"'`), so the same line is mirrored there. Without this the probe compiles,
// runs, and cannot be observed on two of the three platforms it exists for.
//
// The line is formatted ONCE into a buffer and both sinks get "%s", because
// NSLog is a macro that wants a literal format and forwarding __VA_ARGS__
// through a second macro does not survive it.
#    include <Foundation/Foundation.h>
#    define MM_PERF_SINK(msg)     \
        do {                     \
            MM_LOG("%s", msg);   \
            NSLog(@"%s", msg);   \
        } while (0)
#else
#    define MM_PERF_SINK(msg) MM_LOG("%s", msg)
#endif

struct FramePerf {
    // 120 frames is two seconds at 60 Hz, and a whole number of vsync intervals
    // so a healthy app reports almost exactly one window. The u32 below is for
    // the REPORT index: PERF_WINDOW * 95 / 100 has to be written with the cast
    // or it is a float-to-uint conversion nobody meant.
    static constexpr u32 PERF_WINDOW = 120;

    // Call once per frame with the frame's duration in milliseconds. Call it
    // AFTER the host's dt clamp: a host that clamps at 100 ms cannot report a
    // longer frame, and saying otherwise would be the probe lying.
    void record(f32 ms) noexcept {
        samples[count] = ms;
        count           = (count + 1) % PERF_WINDOW;
        // The wrap to 0 IS the report point. An earlier version kept a separate
        // "how full are we" counter and compared the wrapped counter against it,
        // which after the first window is `0 < 119` forever — it reported once,
        // before it had any data, and then never again. A probe that quietly
        // stops reporting is worse than none: it reads as "nothing is slow".
        if (count != 0) {
            return;
        }

        f32 total = 0.0f;
        f32 worst = 0.0f;
        for (u32 i = 0; i < PERF_WINDOW; ++i) {
            total += samples[i];
            worst  = worst > samples[i] ? worst : samples[i];
        }
        // A copy, because sorting in place would destroy the window and the
        // next report would be sorted garbage.
        f32 sorted[PERF_WINDOW];
        for (u32 i = 0; i < PERF_WINDOW; ++i) {
            sorted[i] = samples[i];
        }
        // Insertion sort: 120 elements once every two seconds. std::sort would
        // pull in <algorithm> for a sort that runs twice a minute.
        for (u32 i = 1; i < PERF_WINDOW; ++i) {
            const f32 key = sorted[i];
            u32       j   = i;
            while (j > 0 && sorted[j - 1] > key) {
                sorted[j] = sorted[j - 1];
                --j;
            }
            sorted[j] = key;
        }
        const f32 mean = total / f32(PERF_WINDOW);
        char    line[96];
        snprintf(line, sizeof(line), "PERF: mean %.2fms (%.0f fps)  p95 %.2fms  worst %.2fms", mean, mean > 0.0f ? 1000.0f / mean : 0.0f,
                 sorted[static_cast<u32>(PERF_WINDOW) * 95u / 100u], worst);
        MM_PERF_SINK(line);
    }

    // Forgets the window — after a pause or a resize, where the first frames are
    // not representative and would otherwise be reported as a hitch.
    void reset() noexcept {
        count = 0;
        for (u32 i = 0; i < PERF_WINDOW; ++i) {
            samples[i] = 0.0f;
        }
    }

    f32 samples[PERF_WINDOW] = {};
    u32 count                 = 0;
};