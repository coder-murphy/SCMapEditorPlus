#include "terrain/isom_terrain.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <random>

#include "terrain/tileset.h"

namespace scmd {

namespace {

// ── 常量（与 ScmDraft2 的等距地形实现一致）──
constexpr int DIR_NUM = 4;
constexpr int FROM_NUM = 4;
constexpr int SIDE_NUM = 2;

constexpr int LEFT = 0;
constexpr int TOP = 1;
constexpr int RIGHT = 2;
constexpr int BOTTOM = 3;

constexpr int LEFT_TOP = 0;
constexpr int TOP_RIGHT = 1;
constexpr int RIGHT_BOTTOM = 2;
constexpr int BOTTOM_LEFT = 3;

constexpr int LOW = 0;
constexpr int HIGH = 1;

constexpr int AS_EDGE = 0;
constexpr int AS_LOW = 1;
constexpr int AS_HIGH = 2;

constexpr u16 M_CENTER = 0x00;
constexpr u16 M_DIFFUSE(int n) { return static_cast<u16>(0x10 | n); }
constexpr u16 M_CLIFF(int n) { return static_cast<u16>(0x20 | n); }
constexpr u16 M_SIDE(int n) { return static_cast<u16>(0x30 | n); }

// 地形样式
constexpr u16 S_NONE = 0;
constexpr u16 S_MATTED = 1;
constexpr u16 S_DIFFUSE = 2;
constexpr u16 S_TILE = 3;
constexpr u16 S_STEP = 4;
constexpr u16 S_ROOF = 5;

// 垂直邻接 ID
constexpr u16 V_ABUT_NONE = 0;
constexpr u16 V_ABUT_CENTER = 1;
constexpr u16 V_ABUT_LEFT = 2;
constexpr u16 V_ABUT_RIGHT = 3;

constexpr int SHAPE_NUM = 14;
constexpr int SHAPE_ID_MAX = 16;

// ISOM 单元四个边的位置值（左/上/右/下），随单元奇偶变化
constexpr u16 kTilePosEven[DIR_NUM] = {0x8, 0xA, 0x0, 0x2};
constexpr u16 kTilePosOdd[DIR_NUM] = {0x4, 0xC, 0xE, 0x6};

inline int Opposite(int from) { return (from + 2) & 3; }
inline int Dir1OfFrom(int from) { return from; }
inline int Dir2OfFrom(int from) { return (from + 1) & 3; }
inline int SideOfFrom(int from) { return ((from + 1) & 3) / 2; }
inline int XDirOfFrom(int from) { return (((from + 1) & 3) / 2) * 2; }
inline int YDirOfFrom(int from) { return (from / 2) * 2 + 1; }
inline bool IsFromDir(int from, int dir) { return from == dir || ((from + 1) & 3) == dir; }

inline u16 ReadU16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }
inline void WriteU16(u8* p, u16 v) {
    p[0] = static_cast<u8>(v & 0xFF);
    p[1] = static_cast<u8>(v >> 8);
}

}  // namespace

// ── ERA 参数表（数据来自 ScmDraft2 逆向：PARAM_TABLE）──
const IsomTerrain::EraParam& IsomTerrain::ParamFor(int era) {
#define CP(isom, type, abut, style) {isom, type, abut, style}
#define EP(low, high, upend, type, c0, c1, c2, c3) {low, high, upend, type, {c0, c1, c2, c3}}
#define ZCP \
    { 0, 0, 0, 0 }
#define ZEP \
    { 0, 0, false, 0, {0, 0, 0, 0} }

    static const EraParam kTable[8] = {
        // 0: Badlands
        {0x0d,
         {CP(0x01, 0x02, 0x01, S_NONE), CP(0x09, 0x04, 0x0f, S_NONE),
          CP(0x02, 0x03, 0x04, S_NONE), CP(0x03, 0x05, 0x05, S_NONE),
          CP(0x04, 0x06, 0x02, S_NONE), CP(0x07, 0x07, 0x03, S_NONE),
          CP(0x08, 0x12, 0x0e, S_MATTED), CP(0x05, 0x0e, 0x0a, S_TILE),
          CP(0x06, 0x0f, 0x0b, S_DIFFUSE), ZCP, ZCP, ZCP, ZCP},
         {EP(0x01, 0x02, false, 0x22, 0x1e, 0x1f, 0x20, 0x21),
          EP(0x03, 0x01, false, 0x23, 0x22, 0x23, 0x24, 0x25),
          EP(0x01, 0x04, false, 0x14, 0, 0, 0, 0),
          EP(0x01, 0x06, false, 0x1c, 0, 0, 0, 0),
          EP(0x02, 0x07, false, 0x15, 0, 0, 0, 0),
          EP(0x01, 0x05, false, 0x1b, 0, 0, 0, 0),
          EP(0x05, 0x08, false, 0x1f, 0x1a, 0x1b, 0x1c, 0x1d),
          EP(0x01, 0x09, false, 0x16, 0, 0, 0, 0), ZEP, ZEP, ZEP, ZEP}},
        // 1: Space Platform
        {0x18,
         {CP(0x01, 0x02, 0x01, S_NONE), CP(0x09, 0x08, 0x0d, S_MATTED),
          CP(0x0a, 0x09, 0x12, S_MATTED), CP(0x02, 0x03, 0x02, S_NONE),
          CP(0x0e, 0x0b, 0x07, S_TILE), CP(0x0b, 0x04, 0x03, S_TILE),
          CP(0x08, 0x07, 0x06, S_TILE), CP(0x04, 0x05, 0x04, S_NONE),
          CP(0x0c, 0x06, 0x05, S_TILE), CP(0x0d, 0x0a, 0x08, S_MATTED),
          ZCP, ZCP, ZCP},
         {EP(0x01, 0x02, false, 0x14, 0x17, 0x18, 0x19, 0x1a),
          EP(0x02, 0x04, false, 0x15, 0x1b, 0x1c, 0x1d, 0x1e),
          EP(0x02, 0x08, false, 0x10, 0, 0, 0, 0),
          EP(0x02, 0x09, true, 0x11, 0x0e, 0x0f, 0x10, 0x11),
          EP(0x02, 0x0a, true, 0x12, 0x13, 0x14, 0x15, 0x16),
          EP(0x02, 0x0b, false, 0x0e, 0, 0, 0, 0),
          EP(0x04, 0x0c, false, 0x0f, 0, 0, 0, 0),
          EP(0x02, 0x0d, false, 0x13, 0x09, 0x0a, 0x0b, 0x0c),
          EP(0x02, 0x0e, false, 0x0d, 0, 0, 0, 0), ZEP, ZEP, ZEP}},
        // 2: Installation
        {0x16,
         {CP(0x01, 0x02, 0x01, S_STEP), CP(0x02, 0x03, 0x02, S_STEP),
          CP(0x03, 0x06, 0x03, S_ROOF), CP(0x04, 0x04, 0x04, S_TILE),
          CP(0x05, 0x05, 0x05, S_TILE), CP(0x06, 0x08, 0x07, S_TILE),
          CP(0x07, 0x07, 0x06, S_NONE), ZCP, ZCP, ZCP, ZCP, ZCP, ZCP},
         {EP(0x01, 0x02, false, 0x0c, 0x08, 0x09, 0x0a, 0x0b),
          EP(0x02, 0x03, false, 0x0d, 0x0c, 0x0d, 0x0e, 0x0f),
          EP(0x01, 0x04, false, 0x0a, 0, 0, 0, 0),
          EP(0x02, 0x05, false, 0x0b, 0, 0, 0, 0),
          EP(0x01, 0x06, false, 0x0e, 0, 0, 0, 0),
          EP(0x07, 0x01, false, 0x0f, 0x10, 0x11, 0x12, 0x13), ZEP, ZEP, ZEP, ZEP,
          ZEP, ZEP}},
        // 3: Ashworld
        {0x1b,
         {CP(0x01, 0x08, 0x07, S_NONE), CP(0x02, 0x02, 0x01, S_NONE),
          CP(0x03, 0x03, 0x02, S_TILE), CP(0x04, 0x06, 0x05, S_DIFFUSE),
          CP(0x08, 0x09, 0x08, S_TILE), CP(0x05, 0x04, 0x03, S_NONE),
          CP(0x06, 0x05, 0x04, S_TILE), CP(0x07, 0x07, 0x06, S_DIFFUSE),
          ZCP, ZCP, ZCP, ZCP, ZCP},
         {EP(0x01, 0x02, false, 0x11, 0x09, 0x0a, 0x0b, 0x0c),
          EP(0x02, 0x05, false, 0x10, 0x0d, 0x0e, 0x0f, 0x10),
          EP(0x02, 0x03, false, 0x0b, 0, 0, 0, 0),
          EP(0x05, 0x06, false, 0x0c, 0, 0, 0, 0),
          EP(0x02, 0x04, false, 0x0d, 0, 0, 0, 0),
          EP(0x05, 0x07, false, 0x0e, 0, 0, 0, 0),
          EP(0x02, 0x08, false, 0x0f, 0, 0, 0, 0), ZEP, ZEP, ZEP, ZEP, ZEP}},
        // 4..7: Jungle / Desert / Ice / Twilight（数值相同，仅名称不同）
        {0x11,
         {CP(0x03, 0x05, 0x05, S_NONE), CP(0x01, 0x02, 0x01, S_NONE),
          CP(0x0d, 0x04, 0x0f, S_NONE), CP(0x04, 0x08, 0x08, S_NONE),
          CP(0x06, 0x0f, 0x0b, S_DIFFUSE), CP(0x07, 0x0b, 0x06, S_DIFFUSE),
          CP(0x05, 0x09, 0x0c, S_DIFFUSE), CP(0x08, 0x10, 0x10, S_MATTED),
          CP(0x02, 0x03, 0x04, S_NONE), CP(0x09, 0x0a, 0x09, S_NONE),
          CP(0x0a, 0x0c, 0x07, S_DIFFUSE), CP(0x0b, 0x0d, 0x0d, S_DIFFUSE),
          CP(0x0c, 0x11, 0x11, S_MATTED)},
         {EP(0x01, 0x02, false, 0x22, 0x1e, 0x1f, 0x20, 0x21),
          EP(0x03, 0x01, false, 0x23, 0x22, 0x23, 0x24, 0x25),
          EP(0x01, 0x04, false, 0x17, 0, 0, 0, 0),
          EP(0x01, 0x06, false, 0x1c, 0, 0, 0, 0),
          EP(0x04, 0x05, false, 0x1d, 0, 0, 0, 0),
          EP(0x04, 0x07, false, 0x19, 0, 0, 0, 0),
          EP(0x04, 0x08, false, 0x20, 0x12, 0x13, 0x14, 0x15),
          EP(0x02, 0x09, false, 0x18, 0, 0, 0, 0),
          EP(0x09, 0x0a, false, 0x1a, 0, 0, 0, 0),
          EP(0x09, 0x0b, false, 0x1e, 0, 0, 0, 0),
          EP(0x09, 0x0c, false, 0x21, 0x16, 0x17, 0x18, 0x19),
          EP(0x01, 0x0d, false, 0x16, 0, 0, 0, 0)}},
        {0x11,
         {CP(0x03, 0x05, 0x05, S_NONE), CP(0x01, 0x02, 0x01, S_NONE),
          CP(0x0d, 0x04, 0x0f, S_NONE), CP(0x04, 0x08, 0x08, S_NONE),
          CP(0x06, 0x0f, 0x0b, S_DIFFUSE), CP(0x07, 0x0b, 0x06, S_DIFFUSE),
          CP(0x05, 0x09, 0x0c, S_DIFFUSE), CP(0x08, 0x10, 0x10, S_MATTED),
          CP(0x02, 0x03, 0x04, S_NONE), CP(0x09, 0x0a, 0x09, S_NONE),
          CP(0x0a, 0x0c, 0x07, S_DIFFUSE), CP(0x0b, 0x0d, 0x0d, S_DIFFUSE),
          CP(0x0c, 0x11, 0x11, S_MATTED)},
         {EP(0x01, 0x02, false, 0x22, 0x1e, 0x1f, 0x20, 0x21),
          EP(0x03, 0x01, false, 0x23, 0x22, 0x23, 0x24, 0x25),
          EP(0x01, 0x04, false, 0x17, 0, 0, 0, 0),
          EP(0x01, 0x06, false, 0x1c, 0, 0, 0, 0),
          EP(0x04, 0x05, false, 0x1d, 0, 0, 0, 0),
          EP(0x04, 0x07, false, 0x19, 0, 0, 0, 0),
          EP(0x04, 0x08, false, 0x20, 0x12, 0x13, 0x14, 0x15),
          EP(0x02, 0x09, false, 0x18, 0, 0, 0, 0),
          EP(0x09, 0x0a, false, 0x1a, 0, 0, 0, 0),
          EP(0x09, 0x0b, false, 0x1e, 0, 0, 0, 0),
          EP(0x09, 0x0c, false, 0x21, 0x16, 0x17, 0x18, 0x19),
          EP(0x01, 0x0d, false, 0x16, 0, 0, 0, 0)}},
        {0x11,
         {CP(0x03, 0x05, 0x05, S_NONE), CP(0x01, 0x02, 0x01, S_NONE),
          CP(0x0d, 0x04, 0x0f, S_NONE), CP(0x04, 0x08, 0x08, S_NONE),
          CP(0x06, 0x0f, 0x0b, S_DIFFUSE), CP(0x07, 0x0b, 0x06, S_DIFFUSE),
          CP(0x05, 0x09, 0x0c, S_DIFFUSE), CP(0x08, 0x10, 0x10, S_MATTED),
          CP(0x02, 0x03, 0x04, S_NONE), CP(0x09, 0x0a, 0x09, S_NONE),
          CP(0x0a, 0x0c, 0x07, S_DIFFUSE), CP(0x0b, 0x0d, 0x0d, S_DIFFUSE),
          CP(0x0c, 0x11, 0x11, S_MATTED)},
         {EP(0x01, 0x02, false, 0x22, 0x1e, 0x1f, 0x20, 0x21),
          EP(0x03, 0x01, false, 0x23, 0x22, 0x23, 0x24, 0x25),
          EP(0x01, 0x04, false, 0x17, 0, 0, 0, 0),
          EP(0x01, 0x06, false, 0x1c, 0, 0, 0, 0),
          EP(0x04, 0x05, false, 0x1d, 0, 0, 0, 0),
          EP(0x04, 0x07, false, 0x19, 0, 0, 0, 0),
          EP(0x04, 0x08, false, 0x20, 0x12, 0x13, 0x14, 0x15),
          EP(0x02, 0x09, false, 0x18, 0, 0, 0, 0),
          EP(0x09, 0x0a, false, 0x1a, 0, 0, 0, 0),
          EP(0x09, 0x0b, false, 0x1e, 0, 0, 0, 0),
          EP(0x09, 0x0c, false, 0x21, 0x16, 0x17, 0x18, 0x19),
          EP(0x01, 0x0d, false, 0x16, 0, 0, 0, 0)}},
        {0x11,
         {CP(0x03, 0x05, 0x05, S_NONE), CP(0x01, 0x02, 0x01, S_NONE),
          CP(0x0d, 0x04, 0x0f, S_NONE), CP(0x04, 0x08, 0x08, S_NONE),
          CP(0x06, 0x0f, 0x0b, S_DIFFUSE), CP(0x07, 0x0b, 0x06, S_DIFFUSE),
          CP(0x05, 0x09, 0x0c, S_DIFFUSE), CP(0x08, 0x10, 0x10, S_MATTED),
          CP(0x02, 0x03, 0x04, S_NONE), CP(0x09, 0x0a, 0x09, S_NONE),
          CP(0x0a, 0x0c, 0x07, S_DIFFUSE), CP(0x0b, 0x0d, 0x0d, S_DIFFUSE),
          CP(0x0c, 0x11, 0x11, S_MATTED)},
         {EP(0x01, 0x02, false, 0x22, 0x1e, 0x1f, 0x20, 0x21),
          EP(0x03, 0x01, false, 0x23, 0x22, 0x23, 0x24, 0x25),
          EP(0x01, 0x04, false, 0x17, 0, 0, 0, 0),
          EP(0x01, 0x06, false, 0x1c, 0, 0, 0, 0),
          EP(0x04, 0x05, false, 0x1d, 0, 0, 0, 0),
          EP(0x04, 0x07, false, 0x19, 0, 0, 0, 0),
          EP(0x04, 0x08, false, 0x20, 0x12, 0x13, 0x14, 0x15),
          EP(0x02, 0x09, false, 0x18, 0, 0, 0, 0),
          EP(0x09, 0x0a, false, 0x1a, 0, 0, 0, 0),
          EP(0x09, 0x0b, false, 0x1e, 0, 0, 0, 0),
          EP(0x09, 0x0c, false, 0x21, 0x16, 0x17, 0x18, 0x19),
          EP(0x01, 0x0d, false, 0x16, 0, 0, 0, 0)}},
    };
#undef CP
#undef EP
#undef ZCP
#undef ZEP
    return kTable[era < 0 || era >= 8 ? 0 : era];
}

const IsomTerrain::ShapeParam* IsomTerrain::Shapes() {
    static const ShapeParam kShapes[SHAPE_NUM] = {
        // LLHH
        {{LOW, LOW, HIGH, HIGH},
         {AS_LOW, AS_EDGE, AS_EDGE, AS_EDGE},
         {M_CENTER, M_CENTER, M_SIDE(3), M_SIDE(3)},
         {M_CENTER, M_SIDE(3)}},
        // HLLH
        {{HIGH, LOW, LOW, HIGH},
         {AS_EDGE, AS_LOW, AS_EDGE, AS_EDGE},
         {M_CENTER, M_CENTER, M_SIDE(1), M_SIDE(1)},
         {M_SIDE(1), M_CENTER}},
        // HHLL
        {{HIGH, HIGH, LOW, LOW},
         {AS_EDGE, AS_EDGE, M_CLIFF(AS_LOW), AS_EDGE},
         {M_SIDE(4), M_SIDE(4), M_CLIFF(0), M_CLIFF(0)},
         {M_SIDE(4), M_CLIFF(0)}},
        // LHHL
        {{LOW, HIGH, HIGH, LOW},
         {AS_EDGE, AS_EDGE, AS_EDGE, M_CLIFF(AS_LOW)},
         {M_SIDE(2), M_SIDE(2), M_CLIFF(1), M_CLIFF(1)},
         {M_CLIFF(1), M_SIDE(2)}},
        // LLLH
        {{LOW, LOW, LOW, HIGH},
         {AS_LOW, AS_LOW, AS_EDGE, AS_EDGE},
         {M_CENTER, M_CENTER, M_SIDE(1), M_SIDE(3)},
         {M_CENTER, M_CENTER}},
        // HLLL
        {{HIGH, LOW, LOW, LOW},
         {AS_EDGE, AS_LOW, AS_LOW, AS_EDGE},
         {M_CENTER, M_CENTER, M_CENTER, M_CLIFF(3)},
         {M_SIDE(6), M_CENTER}},
        // LHLL
        {{LOW, HIGH, LOW, LOW},
         {AS_EDGE, AS_EDGE, M_CLIFF(AS_LOW), M_CLIFF(AS_LOW)},
         {M_SIDE(2), M_SIDE(4), M_CLIFF(0), M_CLIFF(1)},
         {M_CLIFF(1), M_CLIFF(0)}},
        // LLHL
        {{LOW, LOW, HIGH, LOW},
         {AS_LOW, AS_EDGE, AS_EDGE, AS_LOW},
         {M_CENTER, M_CENTER, M_CLIFF(2), M_CENTER},
         {M_CENTER, M_SIDE(5)}},
        // LHHH
        {{LOW, HIGH, HIGH, HIGH},
         {AS_EDGE, M_DIFFUSE(AS_HIGH), M_DIFFUSE(AS_HIGH), AS_EDGE},
         {M_SIDE(2), M_DIFFUSE(8), M_DIFFUSE(8), M_SIDE(3)},
         {M_CLIFF(1), M_DIFFUSE(8)}},
        // HHLH
        {{HIGH, HIGH, LOW, HIGH},
         {M_DIFFUSE(AS_HIGH), AS_EDGE, AS_EDGE, M_DIFFUSE(AS_HIGH)},
         {M_DIFFUSE(7), M_SIDE(4), M_SIDE(1), M_DIFFUSE(7)},
         {M_DIFFUSE(7), M_CLIFF(0)}},
        // HLHH
        {{HIGH, LOW, HIGH, HIGH},
         {AS_EDGE, AS_EDGE, AS_EDGE, AS_EDGE},
         {M_CENTER, M_CENTER, M_SIDE(3), M_SIDE(1)},
         {M_SIDE(1), M_SIDE(3)}},
        // HHHL
        {{HIGH, HIGH, HIGH, LOW},
         {AS_EDGE, AS_EDGE, AS_EDGE, AS_EDGE},
         {M_SIDE(4), M_SIDE(2), M_CLIFF(1), M_CLIFF(0)},
         {M_SIDE(4), M_SIDE(2)}},
        // LHLH
        {{LOW, HIGH, LOW, HIGH},
         {AS_EDGE, AS_EDGE, AS_EDGE, AS_EDGE},
         {M_SIDE(2), M_SIDE(4), M_SIDE(1), M_SIDE(3)},
         {M_CLIFF(1), M_CLIFF(0)}},
        // HLHL
        {{HIGH, LOW, HIGH, LOW},
         {AS_EDGE, AS_EDGE, AS_EDGE, AS_EDGE},
         {M_CENTER, M_CENTER, M_CLIFF(2), M_CLIFF(3)},
         {M_SIDE(6), M_SIDE(5)}},
    };
    return kShapes;
}

// ── 过渡模板的 TILE 类型 / 邻接映射（对应 map_edge_* 函数）──
u16 IsomTerrain::MapEdgeTileType(const EraParam& p, const EraInfo& info, int low, int high,
                                 int edge, u16 temp) {
    switch (temp & 0xF0) {
        case 0x20:   // CLIFF
            temp = info.edge[edge].cliff ? static_cast<u16>(AS_EDGE)
                                         : static_cast<u16>(temp & ~0xF0u);
            break;
        case 0x10:   // DIFFUSE
            temp = (p.center[high].style == S_DIFFUSE) ? static_cast<u16>(temp & ~0xF0u)
                                                       : static_cast<u16>(AS_EDGE);
            break;
        default:
            break;
    }
    switch (temp) {
        case AS_LOW: return p.center[low].type;
        case AS_HIGH: return p.center[high].type;
        default: return p.edge[edge].type;
    }
}

u16 IsomTerrain::MapEdgeHorAbuttal(const EraParam& p, const EraInfo& info, int low, int high,
                                   int edge, u16 temp, u16* proj) {
    switch (temp & 0xF0) {
        case 0x00:
            return p.center[low].abut;
        case 0x10:
            if (p.center[high].style == S_DIFFUSE) return p.center[high].abut;
            return static_cast<u16>(M_SIDE(temp & 0x0Fu));
        case 0x20:
            if (info.edge[edge].cliff) {
                if (p.center[high].style != S_TILE && (temp & ~0x20u) == 2) {
                    proj[1] = V_ABUT_LEFT;
                } else if (p.center[high].style != S_TILE && (temp & ~0x20u) == 3) {
                    proj[0] = V_ABUT_RIGHT;
                }
                return p.edge[edge].cliff[temp & 0x0Fu];
            }
            return p.center[low].abut;
        case 0x30:
            return temp;
        default:
            return 0;
    }
}

u16 IsomTerrain::MapEdgeVerAbuttal(const EraParam& p, const EraInfo& info, int high, int edge,
                                   u16 temp) {
    switch (temp & 0xF0) {
        case 0x00:
            temp = 0;
            break;
        case 0x10:
            if (p.center[high].style == S_DIFFUSE) temp = 0;
            else temp = static_cast<u16>(temp & ~0x10u);
            break;
        case 0x20:
            if (info.edge[edge].cliff) return V_ABUT_CENTER;
            temp = 0;
            break;
        case 0x30:
            temp = static_cast<u16>(temp & ~0x30u);
            break;
        default:
            temp = 0;
            break;
    }
    if (!temp) return V_ABUT_NONE;
    const u16 style = p.center[high].style;
    if (temp <= 4) {
        if ((style == S_STEP || style == S_ROOF) && (temp % 2)) return V_ABUT_NONE;
        return V_ABUT_CENTER;
    }
    if (style == S_ROOF && temp == 7) return V_ABUT_LEFT;
    if (style == S_ROOF && temp == 8) return V_ABUT_RIGHT;
    const bool cliffLike = info.edge[edge].cliff || style == S_TILE;
    if (temp % 2) return cliffLike ? V_ABUT_NONE : V_ABUT_LEFT;
    return cliffLike ? V_ABUT_NONE : V_ABUT_RIGHT;
}

void IsomTerrain::Clear() {
    isomChanged_ = false;
    ready_ = false;
    width_ = height_ = era_ = rowW_ = line_ = 0;
    isom_.clear();
    dirty_.clear();
    tiles_.clear();
    dict_.clear();
    dictFlat_.clear();
    dictType_.clear();
    info_ = EraInfo();
    tileset_ = nullptr;
}

bool IsomTerrain::Learn(const Tileset& tileset, const Buffer& isom, const Buffer& tiles, int width,
                        int height, int era) {
    Clear();
    if (!tileset.IsLoaded() || width < 2 || height < 1 || width > 256 || height > 256) return false;
    if (era < 0 || era >= 8) return false;

    rowW_ = width / 2 + 1;
    line_ = height + 1;
    const std::size_t cells = static_cast<std::size_t>(rowW_) * line_;
    if (isom.size() < cells * 8) return false;
    if (tiles.size() < static_cast<std::size_t>(width) * height * 2) return false;

    width_ = width;
    height_ = height;
    era_ = era;
    tileset_ = &tileset;

    isom_.resize(cells * 4);
    for (std::size_t i = 0; i < isom_.size(); i++) isom_[i] = ReadU16(isom.data() + i * 2);
    dirty_.assign(cells, 0);

    tiles_.resize(static_cast<std::size_t>(width) * height);
    for (std::size_t i = 0; i < tiles_.size(); i++) tiles_[i] = ReadU16(tiles.data() + i * 2);

    BuildEraInfo();
    if (!info_.init) return false;

    // 从地图样本学习 ISOM_TILE -> CV5 组 的字典
    std::vector<TileIndex> computed(cells);
    for (int cy = 0; cy < line_; cy++) {
        for (int cx = 0; cx < rowW_; cx++) MakeTileIndex(cx, cy, computed.data());
    }

    struct Vote {
        u16 group = 0;
        u32 count = 0;
    };
    auto vote = [](auto& map, auto key, u16 group) {
        Vote& v = map[key];
        if (v.group == group) {
            v.count++;
        } else if (v.count == 0) {
            v.group = group;
            v.count = 1;
        } else if (--v.count == 0) {
            v.group = group;
            v.count = 1;
        }
    };

    std::unordered_map<u32, Vote> votes;
    std::unordered_map<u32, Vote> votesFlat;
    std::unordered_map<u16, Vote> votesType;
    votes.reserve(cells);
    votesFlat.reserve(cells);
    votesType.reserve(64);
    for (int cy = 0; cy < height_; cy++) {
        for (int cx = 0; cx < rowW_; cx++) {
            const TileIndex& t = computed[static_cast<std::size_t>(cy) * rowW_ + cx];
            if (t.type == 0) continue;
            if (cx * 2 >= width_) continue;   // 右侧多出的一列菱形只用于边界
            u32 key = 0;
            if (!KeyFor(t, key)) continue;
            const u16 value = tiles_[static_cast<std::size_t>(cy) * width_ + cx * 2];
            const u16 group = static_cast<u16>((value & 0x7FF0) >> 4);
            if (group == 0) continue;
            vote(votes, key, group);
            // 放宽键：忽略上下邻接 / 只看类型，用于地图样本不足时的兜底
            const u32 flat = key & ~(0x3u << 14) & ~(0x3u << 16);
            vote(votesFlat, flat, group);
            vote(votesType, static_cast<u16>(t.type & 0x3F), group);
        }
    }
    auto build = [&](const auto& src, auto& dst) {
        dst.reserve(src.size());
        for (const auto& kv : src) {
            if (kv.second.count == 0 || kv.second.group == 0) continue;
            DictEntry entry;
            entry.group = kv.second.group;
            entry.megaMask = MegaMaskOf(tileset, entry.group);
            if (!entry.megaMask) continue;
            dst[kv.first] = entry;
        }
    };
    build(votes, dict_);
    build(votesFlat, dictFlat_);
    build(votesType, dictType_);

    ready_ = !dict_.empty();
    isomChanged_ = false;
    return ready_;
}

u16 IsomTerrain::MegaMaskOf(const Tileset& tileset, u16 group) {
    u16 mask = 0;
    for (int i = 0; i < 16; i++) {
        if (tileset.HasSubtile(group, i)) mask |= static_cast<u16>(1u << i);
    }
    return mask;
}

bool IsomTerrain::SlotDefined(int slot) const {
    if (!ready_ || slot < 0 || slot >= 13) return false;
    return ParamFor(era_).center[slot].isom != 0;
}

void IsomTerrain::BuildEraInfo() {
    const EraParam& param = ParamFor(era_);
    info_ = EraInfo();
    for (int i = 0; i < 16; i++) info_.isom2center[i] = -1;

    for (int center = 0; center < 13; center++) {
        const u16 isom = param.center[center].isom;
        if (!isom) break;
        if (isom >= 16) continue;
        info_.isom2center[isom] = center;
        info_.centerNum++;
        if (param.center[center].type > info_.maxCenterType) {
            info_.maxCenterType = param.center[center].type;
        }
    }

    for (int edge = 0; edge < 12; edge++) {
        const EdgeParam& ep = param.edge[edge];
        if (!ep.low || !ep.high) break;
        int low = (ep.low < 16) ? info_.isom2center[ep.low] : -1;
        int high = (ep.high < 16) ? info_.isom2center[ep.high] : -1;
        if (low < 0 || high < 0) continue;
        if (ep.upend) std::swap(low, high);
        info_.edge[edge].low = low;
        info_.edge[edge].high = high;
        info_.edge[edge].cliff = ep.cliff[0] != 0;
        for (int shape = 0; shape < SHAPE_NUM; shape++) {
            for (int d = 0; d < 4; d++) {
                info_.edge[edge].shape[shape][d] =
                    Shapes()[shape].order[d] == HIGH ? high : low;
            }
        }
        info_.edgeNum++;
    }

    for (int center = 0; center < info_.centerNum; center++) {
        const u16 isom = param.center[center].isom;
        if (!isom) break;
        for (int edge = 0; edge < info_.edgeNum; edge++) {
            if (param.edge[edge].low == isom) {
                const int high = (param.edge[edge].high < 16)
                                     ? info_.isom2center[param.edge[edge].high]
                                     : -1;
                if (high >= 0 && info_.center[center].aboveNum < 6) {
                    info_.center[center].above[info_.center[center].aboveNum] = high;
                    info_.center[center].aboveEdge[info_.center[center].aboveNum] = edge;
                    info_.center[center].aboveNum++;
                }
            } else if (param.edge[edge].high == isom) {
                const int low = (param.edge[edge].low < 16)
                                    ? info_.isom2center[param.edge[edge].low]
                                    : -1;
                if (low >= 0) {
                    info_.center[center].below = low;
                    info_.center[center].belowEdge = edge;
                }
            }
        }
    }

    info_.init = info_.centerNum > 0 && info_.edgeNum > 0;
}

int IsomTerrain::IsomToCenter(u16 isom) const {
    if (isom >= ParamFor(era_).edgeStart || isom >= 16) return -1;
    return info_.isom2center[isom];
}

u16 IsomTerrain::CenterIsom(int center) const {
    if (center < 0 || center >= 13) return 0;
    return ParamFor(era_).center[center].isom;
}

bool IsomTerrain::GetIsomShape(u16 isom, int& low, int& high, int shape[4]) const {
    low = -1;
    high = -1;
    for (int i = 0; i < 4; i++) shape[i] = -1;

    const int center = IsomToCenter(isom);
    if (center >= 0) {
        low = high = center;
        for (int i = 0; i < 4; i++) shape[i] = center;
        return true;
    }
    const EraParam& param = ParamFor(era_);
    if (isom < param.edgeStart) return false;
    const int index = isom - param.edgeStart;
    const int edge = index / SHAPE_NUM;
    const int sh = index % SHAPE_NUM;
    if (edge >= info_.edgeNum) return false;
    low = info_.edge[edge].low;
    high = info_.edge[edge].high;
    for (int i = 0; i < 4; i++) shape[i] = info_.edge[edge].shape[sh][i];
    return true;
}

u16 IsomTerrain::MatchShape(const int shape[4]) const {
    int brush1 = shape[LEFT];
    int brush2 = brush1;
    if (shape[TOP] != brush1) brush2 = shape[TOP];
    if (shape[RIGHT] != brush1 && shape[RIGHT] != brush2) {
        if (brush1 != brush2) return 0;
        brush2 = shape[RIGHT];
    }
    if (shape[BOTTOM] != brush1 && shape[BOTTOM] != brush2) {
        if (brush1 != brush2) return 0;
        brush2 = shape[BOTTOM];
    }
    if (brush1 == brush2) return CenterIsom(brush1);

    const int edge = info_.center[brush1].below == brush2
                         ? info_.center[brush1].belowEdge
                         : (info_.center[brush2].below == brush1
                                ? info_.center[brush2].belowEdge
                                : -1);
    if (edge < 0) return 0;
    for (int sh = 0; sh < SHAPE_NUM; sh++) {
        if (std::memcmp(shape, info_.edge[edge].shape[sh], sizeof(int) * 4) == 0) {
            return static_cast<u16>(ParamFor(era_).edgeStart + edge * SHAPE_NUM + sh);
        }
    }
    return 0;
}

int IsomTerrain::SearchBrushLink(int from, int to) const {
    if (from == to) return from;
    if (from < 0 || from >= 13 || to < 0 || to >= 13) return from;
    int center = to;
    while (info_.center[center].below >= 0) {
        if (info_.center[center].below == from) return center;
        center = info_.center[center].below;
    }
    return info_.center[from].below >= 0 ? info_.center[from].below : from;
}

void IsomTerrain::CalcCornerPos(int from, int bx, int by, int& cx, int& cy) {
    cx = IsFromDir(from, LEFT) ? bx - 1 : bx;
    cy = IsFromDir(from, TOP) ? by - 1 : by;
}

void IsomTerrain::CalcLinkPos(int from, int bx, int by, int& lx, int& ly) {
    lx = IsFromDir(from, LEFT) ? bx - 1 : bx + 1;
    ly = IsFromDir(from, TOP) ? by - 1 : by + 1;
}

u16 IsomTerrain::TileAt(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return 0;
    return tiles_[static_cast<std::size_t>(y) * width_ + x];
}

void IsomTerrain::SetTile(int x, int y, u16 v) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    tiles_[static_cast<std::size_t>(y) * width_ + x] = v;
}

bool IsomTerrain::SetTilePos(int cx, int cy) {
    if (!InBounds(cx, cy)) return false;
    const u16* pos = ((cx + cy) & 1) ? kTilePosEven : kTilePosOdd;
    u16* cell = Cell(cx, cy);
    for (int i = 0; i < 4; i++) {
        cell[i] = static_cast<u16>((cell[i] & ~0x000Fu) | pos[i]);
    }
    return true;
}

bool IsomTerrain::UpdateIsom(u16 isom, int from, int bx, int by) {
    int cx = 0, cy = 0;
    CalcCornerPos(from, bx, by, cx, cy);
    if (!SetTilePos(cx, cy)) return false;
    dirty_[static_cast<std::size_t>(cy) * rowW_ + cx] = 1;

    const int opp = Opposite(from);
    u16* cell = Cell(cx, cy);
    const int idx1 = Dir1OfFrom(opp);
    const int idx2 = Dir2OfFrom(opp);
    cell[idx1] = static_cast<u16>((cell[idx1] & 0x000Fu) | (isom << 4));
    cell[idx2] = static_cast<u16>((cell[idx2] & 0x000Fu) | (isom << 4));
    return true;
}

u16 IsomTerrain::IsometricLink(u16 isom, int from, int bx, int by, bool& updated) {
    updated = false;
    int ccx = 0, ccy = 0;
    CalcCornerPos(from, bx, by, ccx, ccy);
    if (!InBounds(ccx, ccy)) return 0;

    const u16* cell = Cell(ccx, ccy);
    const u16 link = cell[from];
    if (!link) return 0;

    int lx = 0, ly = 0;
    CalcLinkPos(from, bx, by, lx, ly);
    if (!InBounds(lx, ly)) return 0;

    int low = 0, high = 0;
    int shape[4] = {-1, -1, -1, -1};
    if (!GetIsomShape(isom, low, high, shape)) return 0;
    int linkLow = 0, linkHigh = 0;
    int linkShape[4] = {-1, -1, -1, -1};
    if (!GetIsomShape(link, linkLow, linkHigh, linkShape)) return 0;

    const EraParam& param = ParamFor(era_);
    const int opp = Opposite(from);
    int brush1 = shape[Dir1OfFrom(from)];
    int brush2 = shape[Dir2OfFrom(from)];

    linkShape[Dir2OfFrom(opp)] = brush1;
    linkShape[Dir1OfFrom(opp)] = brush2;

    int brush3 = linkShape[Dir2OfFrom(from)];
    int brush4 = linkShape[Dir1OfFrom(from)];

    brush4 = SearchBrushLink(brush1, brush4);
    brush3 = SearchBrushLink(brush2, brush3);

    if (brush1 != brush2) {
        if (brush4 != brush2) brush4 = brush1;
        if (brush3 != brush1) brush3 = brush2;
    }
    linkShape[Dir2OfFrom(from)] = brush3;
    linkShape[Dir1OfFrom(from)] = brush4;

    if (low != high) {
        if (param.center[high].style == S_MATTED) {
            if (brush1 == low && linkShape[Dir1OfFrom(from)] != high) {
                linkShape[Dir1OfFrom(from)] = low;
            }
            if (brush2 == low && linkShape[Dir2OfFrom(from)] != high) {
                linkShape[Dir2OfFrom(from)] = low;
            }
        } else {
            brush4 = linkShape[Dir1OfFrom(from)];
            brush3 = linkShape[Dir2OfFrom(from)];
            if (param.center[brush1].style != S_MATTED &&
                param.center[brush4].style == S_MATTED &&
                shape[Dir2OfFrom(opp)] != brush4) {
                linkShape[Dir1OfFrom(from)] = brush1;
            }
            if (param.center[brush2].style != S_MATTED &&
                param.center[brush3].style == S_MATTED &&
                shape[Dir1OfFrom(opp)] != brush3) {
                linkShape[Dir2OfFrom(from)] = brush2;
            }
        }
    }

    if (linkShape[Dir1OfFrom(opp)] != linkShape[Dir2OfFrom(opp)]) {
        int cornerX = 0, cornerY = 0;
        CalcCornerPos(from, lx, ly, cornerX, cornerY);
        if (cornerX < 0 || cornerX >= rowW_) {
            linkShape[XDirOfFrom(from)] = linkShape[YDirOfFrom(opp)];
        }
        if (cornerY < 0 || cornerY >= line_) {
            linkShape[YDirOfFrom(from)] = linkShape[XDirOfFrom(opp)];
        }
    }

    const u16 newLink = MatchShape(linkShape);
    if (!newLink || newLink == link) return 0;

    for (int f = 0; f < 4; f++) UpdateIsom(newLink, f, lx, ly);
    updated = true;
    return newLink;
}

// 填充一组 ISOM 单元：先把单元自身写为纯中心值，再从这些单元向外解析连接。
// 这样内部不会残留过渡边（与 ScmDraft2 的等距画笔一致）。
bool IsomTerrain::Fill(int slot, const std::vector<std::pair<int, int>>& cells) {
    if (!ready_) return false;
    const EraParam& param = ParamFor(era_);
    if (slot < 0 || slot >= 13 || param.center[slot].isom == 0) return false;
    const u16 center = param.center[slot].isom;

    std::vector<std::pair<int, int>> filled;
    filled.reserve(cells.size());
    for (const auto& c : cells) {
        const int cx = c.first;
        const int cy = c.second;
        if (!InBounds(cx, cy)) continue;
        if (!SetTilePos(cx, cy)) continue;
        u16* cell = Cell(cx, cy);
        const u16* pos = ((cx + cy) & 1) ? kTilePosEven : kTilePosOdd;
        for (int i = 0; i < 4; i++) {
            cell[i] = static_cast<u16>(pos[i] | (center << 4));
        }
        dirty_[static_cast<std::size_t>(cy) * rowW_ + cx] = 1;
        filled.push_back({cx, cy});
    }
    if (filled.empty()) return false;

    std::deque<std::pair<int, int>> queue(filled.begin(), filled.end());
    ResolveLinks(queue);
    isomChanged_ = true;
    return true;
}

void IsomTerrain::ResolveLinks(std::deque<std::pair<int, int>>& queue) {
    int guard = 0;
    const int maxSteps = rowW_ * line_ * 16;
    while (!queue.empty() && guard++ < maxSteps) {
        const int x = queue.front().first;
        const int y = queue.front().second;
        queue.pop_front();
        if (!InBounds(x, y)) continue;
        const u16 current = Cell(x, y)[LEFT];
        if (!current) continue;
        for (int from = 0; from < 4; from++) {
            bool updated = false;
            IsometricLink(current, from, x, y, updated);
            if (!updated) continue;
            int lx = 0, ly = 0;
            CalcLinkPos(from, x, y, lx, ly);
            if (InBounds(lx, ly)) queue.push_back({lx, ly});
        }
    }
}

bool IsomTerrain::KeyFor(const TileIndex& t, u32& key) const {
    if (t.type > 0x3F) return false;
    key = static_cast<u32>(t.type & 0x3F) | (static_cast<u32>(t.leftAbut & 3) << 6) |
          (static_cast<u32>(t.topAbut & 3) << 8) | (static_cast<u32>(t.rightAbut & 3) << 10) |
          (static_cast<u32>(t.bottomAbut & 3) << 12) | (static_cast<u32>(t.upAbut & 3) << 14) |
          (static_cast<u32>(t.downAbut & 3) << 16);
    return true;
}

bool IsomTerrain::MakeTileIndex(int cx, int cy, TileIndex* computed) const {
    if (((cx ^ cy) & 1) != 0) return false;
    if (!InBounds(cx, cy)) return false;

    const u16* cell = Cell(cx, cy);
    const u16 isom = cell[LEFT];

    const EraParam& param = ParamFor(era_);
    TileMap tm{};

    const int center = IsomToCenter(isom);
    if (center >= 0) {
        const CenterParam& c = param.center[center];
        for (int f = 0; f < 4; f++) {
            tm.type[f] = c.type;
            tm.xAbut[f] = c.abut;
            tm.yAbut[f] = c.abut;
            tm.zAbut[f] = V_ABUT_NONE;
        }
    } else {
        if (isom < param.edgeStart) return false;
        const int index = isom - param.edgeStart;
        const int edge = index / SHAPE_NUM;
        const int sh = index % SHAPE_NUM;
        if (edge >= info_.edgeNum) return false;
        const ShapeParam& sp = Shapes()[sh];
        const int low = info_.edge[edge].low;
        const int high = info_.edge[edge].high;
        for (int f = 0; f < 4; f++) {
            tm.type[f] = MapEdgeTileType(param, info_, low, high, edge, sp.type[f]);
            tm.xAbut[f] = MapEdgeHorAbuttal(param, info_, low, high, edge, sp.xAbut[f], tm.proj);
            const u16 ytemp = sp.yAbut[SideOfFrom(f)];
            tm.yAbut[f] = MapEdgeHorAbuttal(param, info_, low, high, edge, ytemp, tm.proj);
            tm.zAbut[f] = MapEdgeVerAbuttal(param, info_, high, edge, ytemp);
        }
    }

    MapIsomTile(cx, cy, tm, computed);
    return true;
}

void IsomTerrain::MapIsomTile(int cx, int cy, const TileMap& tm, TileIndex* computed) const {
    for (int from = 0; from < 4; from++) {
        int cornerX = 0, cornerY = 0;
        CalcCornerPos(from, cx, cy, cornerX, cornerY);
        if (!InBounds(cornerX, cornerY)) continue;
        TileIndex& t = computed[static_cast<std::size_t>(cornerY) * rowW_ + cornerX];
        if (tm.type[from] > info_.maxCenterType || t.type <= info_.maxCenterType) {
            t.type = tm.type[from];
        }
        const int opp = Opposite(from);
        u16* h[4] = {&t.leftAbut, &t.topAbut, &t.rightAbut, &t.bottomAbut};
        *h[XDirOfFrom(opp)] = tm.xAbut[from];
        *h[YDirOfFrom(opp)] = tm.yAbut[from];

        u16* v[2] = {&t.upAbut, &t.downAbut};
        const int vi = IsFromDir(from, TOP) ? 1 : 0;
        if (!*v[vi]) *v[vi] = tm.zAbut[from];
    }

    // 投影下层地形到相邻单元的上下邻接
    {
        int cornerX = 0, cornerY = 0;
        CalcCornerPos(BOTTOM_LEFT, cx, cy, cornerX, cornerY);
        if (InBounds(cornerX, cornerY)) {
            computed[static_cast<std::size_t>(cornerY) * rowW_ + cornerX].downAbut = tm.proj[0];
            if (InBounds(cornerX, cornerY + 1)) {
                computed[static_cast<std::size_t>(cornerY + 1) * rowW_ + cornerX].upAbut =
                    tm.proj[0];
            }
        }
        CalcCornerPos(RIGHT_BOTTOM, cx, cy, cornerX, cornerY);
        if (InBounds(cornerX, cornerY)) {
            computed[static_cast<std::size_t>(cornerY) * rowW_ + cornerX].downAbut = tm.proj[1];
            if (InBounds(cornerX, cornerY + 1)) {
                computed[static_cast<std::size_t>(cornerY + 1) * rowW_ + cornerX].upAbut =
                    tm.proj[1];
            }
        }
    }
}

void IsomTerrain::AdjustDirty(int cx, int cy, const TileIndex* computed) {
    if (!dirty_[static_cast<std::size_t>(cy) * rowW_ + cx]) return;
    int y = cy;
    while (computed[static_cast<std::size_t>(y) * rowW_ + cx].upAbut) {
        if (--y < 0) break;
        dirty_[static_cast<std::size_t>(y) * rowW_ + cx] = 1;
    }
    y = cy;
    while (computed[static_cast<std::size_t>(y) * rowW_ + cx].downAbut) {
        if (++y >= line_) break;
        dirty_[static_cast<std::size_t>(y) * rowW_ + cx] = 1;
    }
}

u16 IsomTerrain::GenMegaIndex(int cx, int cy, const DictEntry& dict, u16 upAbut) const {
    if (!dict.megaMask) return 0;
    if (cy == 0 || !upAbut) {
        int often = 0;
        while (often < 16 && (dict.megaMask & (1u << often))) often++;
        int seldom = 0;
        int i = often + 1;
        while (i < 16 && (dict.megaMask & (1u << i))) {
            seldom++;
            i++;
        }
        if (!often) return 0;
        static std::mt19937 rng(std::random_device{}());
        if ((rng() % 100) >= 5 || !seldom) return static_cast<u16>(rng() % often);
        return static_cast<u16>(often + 1 + rng() % seldom);
    }
    return static_cast<u16>(TileAt(cx * 2, cy - 1) & 0x0F);
}

void IsomTerrain::UpdateTile(int cx, int cy, const TileIndex* computed) {
    if (!dirty_[static_cast<std::size_t>(cy) * rowW_ + cx]) return;

    const TileIndex& t = computed[static_cast<std::size_t>(cy) * rowW_ + cx];
    u32 key = 0;
    if (!KeyFor(t, key)) return;
    const DictEntry* entry = nullptr;
    auto it = dict_.find(key);
    if (it != dict_.end()) {
        entry = &it->second;
    } else {
        const u32 flat = key & ~(0x3u << 14) & ~(0x3u << 16);
        auto itFlat = dictFlat_.find(flat);
        if (itFlat != dictFlat_.end()) {
            entry = &itFlat->second;
        } else {
            auto itType = dictType_.find(static_cast<u16>(t.type & 0x3F));
            if (itType != dictType_.end()) entry = &itType->second;
        }
    }
    if (!entry || entry->group == 0) return;

    const u16 group = entry->group;
    u16 mega = GenMegaIndex(cx, cy, *entry, t.upAbut);
    if (tileset_) {
        if (!tileset_->HasSubtile(group, mega)) {
            mega = static_cast<u16>(tileset_->RandomValidSubtile(group));
        }
        if (!tileset_->HasSubtile(static_cast<u16>(group + 1), mega)) {
            for (int i = 0; i < 16; i++) {
                if (tileset_->HasSubtile(group, i) &&
                    tileset_->HasSubtile(static_cast<u16>(group + 1), i)) {
                    mega = static_cast<u16>(i);
                    break;
                }
            }
        }
    }

    SetTile(cx * 2, cy, static_cast<u16>((group << 4) | mega));
    SetTile(cx * 2 + 1, cy, static_cast<u16>(((group + 1) << 4) | mega));
}

void IsomTerrain::FlushTiles(Buffer& tilesOut, std::vector<std::pair<int, int>>* changed) {
    if (!ready_) return;
    const std::size_t cells = static_cast<std::size_t>(rowW_) * line_;
    std::vector<TileIndex> computed(cells);
    for (int cy = 0; cy < line_; cy++) {
        for (int cx = 0; cx < rowW_; cx++) MakeTileIndex(cx, cy, computed.data());
    }
    for (int cy = 0; cy < line_; cy++) {
        for (int cx = 0; cx < rowW_; cx++) AdjustDirty(cx, cy, computed.data());
    }

    std::vector<std::pair<int, int>> touched;
    for (int cy = 0; cy < line_; cy++) {
        for (int cx = 0; cx < rowW_; cx++) {
            if (!dirty_[static_cast<std::size_t>(cy) * rowW_ + cx]) continue;
            touched.push_back({cx * 2, cy});
            touched.push_back({cx * 2 + 1, cy});
        }
    }
    for (int cy = 0; cy < line_; cy++) {
        for (int cx = 0; cx < rowW_; cx++) UpdateTile(cx, cy, computed.data());
    }

    const std::size_t need = static_cast<std::size_t>(width_) * height_ * 2;
    if (tilesOut.size() < need) tilesOut.resize(need, 0);
    for (std::size_t i = 0; i < static_cast<std::size_t>(width_) * height_; i++) {
        WriteU16(tilesOut.data() + i * 2, tiles_[i]);
    }

    if (changed) {
        for (const auto& p : touched) {
            if (p.first >= 0 && p.first < width_ && p.second >= 0 && p.second < height_) {
                changed->push_back(p);
            }
        }
    }
    std::fill(dirty_.begin(), dirty_.end(), 0);
}

void IsomTerrain::WriteIsom(Buffer& out) const {
    out.resize(isom_.size() * 2);
    for (std::size_t i = 0; i < isom_.size(); i++) WriteU16(out.data() + i * 2, isom_[i]);
}

}  // namespace scmd
