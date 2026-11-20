#pragma once
#include "common/types.h"

namespace scmd {

// UNIT 段记录：36 字节/条（VERIFIED，与 SC 1.16 地图一致）
//   +0  u32 序列号
//   +4  u16 x（像素，= 瓦片*32）
//   +6  u16 y
//   +8  u16 单位类型（176 矿 / 177 / 178 / 188 气 / 214 出生点）
//   +10 u16 未使用
//   +12 u16 有效状态
//   +14 u16 有效属性（0x0012 资源、0x0002 出生点）
//   +16 u8  所有者
//   +17 u8  HP%
//   +18 u8  护盾%
//   +19 u8  能量%
//   +20 u32 资源量（语义待核实）
//   +24 u16 堆叠数
//   +26 u16 队列数
//   +28 u32 未使用
struct UnitRecord {
    u32 serial = 0;
    u16 x = 0;
    u16 y = 0;
    u16 type = 0;
    u16 unused0 = 0;
    u16 validState = 0;
    u16 validProps = 0;
    u8 owner = 0;
    u8 hpPercent = 100;
    u8 shieldPercent = 100;
    u8 energyPercent = 100;
    u32 resource = 0;
    u16 stackCount = 0;
    u16 queueCount = 0;
    u32 unused1 = 0;

    static constexpr size_t kSize = 36;
};

enum class UnitType : u16 {
    kMineral1 = 176,
    kMineral2 = 177,
    kMineral3 = 178,
    kGeyser = 188,
    kStartLocation = 214,
};

}  // namespace scmd
