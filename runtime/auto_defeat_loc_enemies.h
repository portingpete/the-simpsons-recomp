#pragma once

#include <cstdint>

struct PPCContext;

namespace Simpsons {

// Called at the original main-frame scheduler boundary. This opt-in path uses
// the game's damage message so normal death handling and mission events run.
void autoDefeatLocEnemiesFrame(PPCContext& ctx, uint8_t* base);

}
