#pragma once

// Profiling
// ---------
// Enable Tracy at CMake configure: -DMMO_ENABLE_TRACY=ON (default OFF, zero cost).
// Run the game, launch tracy-profiler GUI, click Connect (loopback by default).
//
// Macros:
//   ENGINE_PROFILE_FRAME()        - mark a frame boundary (call once per frame)
//   ENGINE_PROFILE_FRAME_NAMED(n) - named frame boundary (string literal)
//   ENGINE_PROFILE_ZONE(name)     - scoped zone with literal name
//   ENGINE_PROFILE_ZONE_TAG(t)    - scoped zone, dynamic tag string attached
//   ENGINE_PROFILE_PLOT(n, v)     - emit a numeric sample on plot n
//   ENGINE_PROFILE_MESSAGE(m)     - one-shot string message on the timeline

#if defined(MMO_ENABLE_TRACY) && MMO_ENABLE_TRACY

#include <tracy/Tracy.hpp>
#include <cstring>

#define ENGINE_PROFILE_FRAME()        FrameMark
#define ENGINE_PROFILE_FRAME_NAMED(n) FrameMarkNamed(n)
#define ENGINE_PROFILE_ZONE(name)     ZoneScopedN(name)
#define ENGINE_PROFILE_ZONE_TAG(t)    ZoneScoped; ZoneText((t), std::strlen(t))
#define ENGINE_PROFILE_PLOT(n, v)     TracyPlot(n, v)
#define ENGINE_PROFILE_MESSAGE(m)     TracyMessage(m, std::strlen(m))

#else

#define ENGINE_PROFILE_FRAME()        ((void)0)
#define ENGINE_PROFILE_FRAME_NAMED(n) ((void)0)
#define ENGINE_PROFILE_ZONE(name)     ((void)0)
#define ENGINE_PROFILE_ZONE_TAG(t)    ((void)0)
#define ENGINE_PROFILE_PLOT(n, v)     ((void)0)
#define ENGINE_PROFILE_MESSAGE(m)     ((void)0)

#endif
