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
constexpr const char* kUnit = "UNIT";  // 36 字节/条
constexpr const char* kMrgn = "MRGN";  // 地点（区域）矩形
constexpr const char* kStr = "STR ";   // 字符串表
constexpr const char* kTrig = "TRIG";  // 触发器
}  // namespace section

}  // namespace scmd
