#pragma once
#include <string>

#include "common/types.h"

namespace scmd {

// CHK 段：4 字节段名 + 4 字节长度 + 数据
// VERIFIED: 段名区分大小写且含尾部空格，例如 "DIM " / "ERA " / "MTXM"
struct ChkSection {
    std::string name;  // 4 字节
    Buffer data;
};

namespace section {
constexpr const char* kDim = "DIM ";   // u16 宽, u16 高
constexpr const char* kEra = "ERA ";   // u16 地形集 0..7
constexpr const char* kMtxm = "MTXM";  // u16 瓦片索引，宽*高
constexpr const char* kTile = "TILE";  // 旧版瓦片段，内容与 MTXM 相同
constexpr const char* kIsom = "ISOM";  // 编辑器等距地形，长度 = 8*(w/2+1)*(h+1)
constexpr const char* kMask = "MASK";  // 每格 1 字节可通行掩码
constexpr const char* kDd2 = "DD2 ";   // 装饰物，8 字节/条
constexpr const char* kThg2 = "THG2";  // SP 单位/精灵，10 字节/条
constexpr const char* kUnit = "UNIT";  // 36 字节/条
constexpr const char* kOwnr = "OWNR";  // 12 字节玩家控制者
constexpr const char* kSide = "SIDE";  // 12 字节玩家种族
constexpr const char* kForc = "FORC";  // 20 字节：8 玩家→队伍 + 4 名称 STR + 4 标志
constexpr const char* kColr = "COLR";  // 8 字节玩家颜色
constexpr const char* kUpgr = "UPGR";  // 1748 字节升级限制（暂原样保留）
constexpr const char* kPtec = "PTEC";  // 912 字节科技限制（暂原样保留）
constexpr const char* kTech = "TECH";  // 部分地图存在，暂原样保留
constexpr const char* kMrgn = "MRGN";  // 地点（区域）矩形
constexpr const char* kStr = "STR ";   // 字符串表
constexpr const char* kTrig = "TRIG";  // 触发器
constexpr const char* kMbrf = "MBRF";  // 任务简报（2400 B/条，结构同 TRIG）
constexpr const char* kWav = "WAV ";   // u32[512] 声音路径的 STR 下标
constexpr const char* kSwnm = "SWNM";  // u32[256] 开关名称的 STR 下标
}  // namespace section

}  // namespace scmd
