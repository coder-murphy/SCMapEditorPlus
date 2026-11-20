#pragma once
#include "common/types.h"

namespace scmd {

// OWNR / SIDE / FORC / COLR 段的模型
// TODO: 8 玩家 + 12 势力 + 4 队伍
struct Player {
    u32 flags = 0;
    u8 race = 0;
    u8 color = 0;
    u8 force = 0;
};

}  // namespace scmd
