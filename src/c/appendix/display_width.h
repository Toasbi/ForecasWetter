#pragma once
#include <pebble.h>

// Bucket Pebble platforms by display width. Layer-local constants
// (bar widths, paddings, etc.) ifdef on these, not on the platform name,
// so multiple platforms with the same screen size share one set of
// numbers and a new 144-px platform doesn't require touching every layer.
#if defined(PBL_PLATFORM_EMERY)
    #define DISPLAY_WIDTH_200
#elif defined(PBL_PLATFORM_APLITE) || defined(PBL_PLATFORM_BASALT) || \
      defined(PBL_PLATFORM_DIORITE) || defined(PBL_PLATFORM_FLINT)
    #define DISPLAY_WIDTH_144
#else
    #error "display_width.h: unrecognised platform — add a bucket"
#endif
