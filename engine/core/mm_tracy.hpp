#pragma once

// Tracy integration — thin wrapper, no-ops when profiling is disabled
// @instrumentation ZoneScoped/FrameMark compile to nothing when USE_TRACY=OFF

#ifdef TRACY_ENABLE
#include <Tracy.hpp>
#else
#define ZoneScoped
#define FrameMark
namespace tracy {
inline void SetThreadName(const char*) noexcept {}
}
#endif
