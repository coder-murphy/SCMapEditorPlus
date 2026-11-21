#pragma once
#include <string>
#include <vector>

#include "common/types.h"

namespace scmd {

// ── MRGN（地点）──
// VERIFIED: 20 字节/条；1268 张地图按本结构重建后与原始字节完全一致。
// 字段：left / top / right / bottom / stringId / flags，其后 8 字节为保留区
//（实测存在非零内容，必须原样保留，否则重建不等）。
struct Location {
    u16 left = 0;
    u16 top = 0;
    u16 right = 0;
    u16 bottom = 0;
    u16 stringId = 0;
    u16 flags = 0;
    u8 reserved[8] = {0};
};

class LocationTable {
public:
    bool Parse(const Buffer& mrgn);
    Buffer Serialize() const;
    std::size_t Count() const { return items_.size(); }
    const std::vector<Location>& Items() const { return items_; }
    std::vector<Location>& MutableItems() { return items_; }

private:
    std::vector<Location> items_;
};

// ── STR（字符串表）──
// VERIFIED: u16 数量 + 以 0 结尾的字节串；1272 张地图重建后逐字节一致。
// 注意：实测存在"声明数量 1024、实际字节只够若干条"的写法，
// 因此必须保留**原始数量字段**与末尾残余字节，不能按解析到的条数重写。
class StringTable {
public:
    bool Parse(const Buffer& str);
    Buffer Serialize() const;
    std::size_t Count() const { return items_.size(); }
    std::size_t DeclaredCount() const { return declaredCount_; }
    const std::vector<std::string>& Items() const { return items_; }
    const std::string& At(std::size_t index) const;
    void Set(std::size_t index, std::string value);

private:
    std::vector<std::string> items_;
    Buffer trailing_;
    u16 declaredCount_ = 0;
    std::string empty_;
};

// ── TRIG（触发）──
// VERIFIED: 2400 字节/条，段内不再有额外头尾：
//   +0    16 × 条件（20 B）
//   +320  64 × 行动（32 B）
//   +2368 u32 执行标志 + 27 B 生效玩家 + 1 B 当前行动
// 条件/行动都以 type==0 表示空槽，运行时遇到第一个空槽即短路。
// 字段命名沿用原版 CHK 规范；dummy/reserved 原样保留，保证无损重建。
struct TriggerCondition {
    u32 locationId = 0;      // MRGN 下标
    u32 player = 0;          // 玩家/势力/组
    u32 amount = 0;          // 数量/数值
    u16 unitType = 0;        // 单位类型
    u8 comparison = 0;       // 0 至少 / 1 至多 / 2 等于
    u8 type = 0;             // 条件类型，0 = 空
    u8 resourceType = 0;     // 资源/分数/开关序号
    u8 property = 0;         // 附加属性（bit1 = 禁用）
    u8 reserved[2] = {0};
};

struct TriggerAction {
    u32 locationId = 0;
    u32 stringId = 0;        // STR 下标
    u32 wavId = 0;           // WAV 字符串下标
    u32 time = 0;
    u32 player = 0;
    u32 target = 0;          // 第二目标：地点/单位/开关/AI 脚本等
    u16 setting = 0;         // 单位类型/资源/分数等
    u8 type = 0;             // 行动类型，0 = 空
    u8 amount = 0;           // 数量/状态/序号
    u8 property = 0;         // 附加属性（bit1 = 禁用）
    u8 reserved[3] = {0};
};

struct Trigger {
    TriggerCondition conditions[16];
    TriggerAction actions[64];
    u32 executionFlags = 0;  // bit0 = 仅执行一次，bit2 = 保留触发，bit3 = 禁用
    u8 effectivePlayers[27] = {0};
    u8 currentAction = 0;

    // 条件/行动的“有效条数”：遇到第一个 type==0 即停止（运行时短路语义）
    int ConditionCount() const;
    int ActionCount() const;
    bool HasPlayer(int player) const;
    void ClearCondition(int index);
    void ClearAction(int index);
    void Clear();
};

class TriggerSection {
public:
    bool Parse(const Buffer& trig);
    Buffer Serialize() const;
    std::size_t BlockCount() const { return items_.size(); }
    const std::vector<Trigger>& Items() const { return items_; }
    std::vector<Trigger>& MutableItems() { return items_; }
    bool IsEmpty() const { return items_.empty() && trailing_.empty(); }
    // 尾部残余（长度不是 2400 整数倍时）原样保留
    const Buffer& Trailing() const { return trailing_; }

    static constexpr std::size_t kBlockSize = 2400;
    static constexpr std::size_t kConditionCount = 16;
    static constexpr std::size_t kActionCount = 64;

private:
    std::vector<Trigger> items_;
    Buffer trailing_;
};

}  // namespace scmd
