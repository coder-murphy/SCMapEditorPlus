#pragma once
#include <string>
#include <vector>

#include "common/types.h"

namespace scmd {

// 地图对象图形解析：units.dat → flingy.dat → sprites.dat → images.dat → images.tbl。
// 单位类型走完整链；THG2 的 sprite id 与 DD2 的 type 直接作为 sprites.dat 下标。
class ObjectGraphics {
public:
    // 依次尝试星际目录 / exe 目录 / 工程根：先找解包后的 arr\*.dat，再读 mpq 副本
    bool LoadAuto();
    bool loaded() const { return loaded_; }

    // 返回 unit\ 下的相对路径（如 terran\marine.grp）；无图形时返回空串
    std::wstring UnitGrpPath(int unitType) const;
    std::wstring SpriteGrpPath(int spriteId) const;

private:
    std::vector<u8> unitFlingy_;      // units.dat 0x0000，228 B
    std::vector<u16> flingySprite_;   // flingy.dat 0x0000，209×2 B
    std::vector<u16> spriteImage_;    // sprites.dat 0x0000，517×2 B
    std::vector<u32> imageGrpFile_;   // images.dat 0x0000，999×4 B（images.tbl 下标，1 起）
    std::vector<std::wstring> grpNames_;   // images.tbl
    bool loaded_ = false;
};

}  // namespace scmd
