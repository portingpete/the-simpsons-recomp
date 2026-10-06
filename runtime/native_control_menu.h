#pragma once
#include "ppc_context.h"

// Native callbacks for the existing Options Controls screen. These do not draw
// host UI: labels and all interaction remain in its original Apt movie.
void SimpsonsNativeControlsPopulate(PPCContext&,uint8_t*);
void SimpsonsNativeControlsSave(PPCContext&,uint8_t*);
void SimpsonsNativeControlsLeave(PPCContext&,uint8_t*);
void SimpsonsNativeControlMenuTick(PPCContext&,uint8_t*);
