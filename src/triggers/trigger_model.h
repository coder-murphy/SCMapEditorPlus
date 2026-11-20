#pragma once
#include "common/types.h"

namespace scmd {

// TRIG / MRGN / STR 段的模型
// VERIFIED: MRGN（地点矩形）若从模板继承多余记录，ScmDraft2 会显示大量游离矩形框，
//           生成地图时应置空
struct Location {
    u32 id = 0;
    u16 left = 0;
    u16 top = 0;
    u16 right = 0;
    u16 bottom = 0;
    u16 stringId = 0;
    u16 elevation = 0;
};

}  // namespace scmd
