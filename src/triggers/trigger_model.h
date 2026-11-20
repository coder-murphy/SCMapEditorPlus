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
// VERIFIED（结构层）: 1297 张地图的 TRIG 长度全部是 2400 的整数倍，
//   按"2400 字节一组"保存为不透明块。
// TODO: 组内字段语义（条件/行动/玩家开关）尚未解析，保持原样透传。
class TriggerSection {
public:
    bool Parse(const Buffer& trig);
    Buffer Serialize() const { return raw_; }
    std::size_t BlockCount() const { return raw_.size() / kBlockSize; }
    const Buffer& Raw() const { return raw_; }
    bool IsEmpty() const { return raw_.empty(); }

    static constexpr std::size_t kBlockSize = 2400;

private:
    Buffer raw_;
};

}  // namespace scmd
