#pragma once
#include "common/types.h"

namespace scmd {

// UNIT 段记录：36 字节/条（VERIFIED，与 SC 1.16 地图一致）
//   +0  u32 序列号
//   +4  u16 x（像素，= 瓦片*32）
//   +6  u16 y
//   +8  u16 单位类型（176 矿 / 177 / 178 / 188 气 / 214 出生点）
//   +10 u16 关系（副建筑 / 虫洞链接位）
//   +12 u16 特殊属性有效掩码（bit0 隐形 / bit1 钻地 / bit2 升起 / bit3 幻影 / bit4 无敌）
//   +14 u16 可改属性掩码（owner / HP / 护盾 / 能量 / 资源 / 机库）
//   +16 u8  所有者
//   +17 u8  HP%
//   +18 u8  护盾%
//   +19 u8  能量%
//   +20 u32 资源量
//   +24 u16 机库 / 载弹数量
//   +26 u16 激活状态位（位序同 +12）
//   +28 8 B 未用 u32 + 关联单位序列号（实测非零，保存时必须原样写回）
struct UnitRecord {
    u32 serial = 0;
    u16 x = 0;
    u16 y = 0;
    u16 type = 0;
    u16 unused0 = 0;      // +10 关系位
    u16 validState = 0;   // +12 特殊属性有效掩码（非激活状态）
    u16 validProps = 0;   // +14 可改属性掩码
    u8 owner = 0;
    u8 hpPercent = 100;
    u8 shieldPercent = 100;
    u8 energyPercent = 100;
    u32 resource = 0;
    u16 stackCount = 0;   // +24 机库 / 载弹数量
    u16 queueCount = 0;   // +26 激活状态位（bit0 隐形 / bit1 钻地 / bit2 升起 / bit3 幻影 / bit4 无敌）
    u8 reserved[8] = {0};

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
