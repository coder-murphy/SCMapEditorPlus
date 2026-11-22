#pragma once
#include <vector>

#include "common/types.h"

namespace scmd {

// 固定长度的 u32 STR 下标表（WAV 512 项 / SWNM 256 项）。
// 段长度不是 count*4 时原样保留，保存时按原始字节写回。
class StringIndexTable {
public:
    explicit StringIndexTable(std::size_t count) : expected_(count) {}

    bool Parse(const Buffer& data);
    Buffer Serialize() const;
    void Reset();
    bool present() const { return present_; }
    bool Modified() const { return modified_; }
    void MarkModified() { modified_ = true; }
    std::size_t Count() const { return expected_; }

    u32 Get(std::size_t index) const;
    bool Set(std::size_t index, u32 value);   // 返回是否发生变化

private:
    std::size_t expected_ = 0;
    std::vector<u32> ids_;
    Buffer raw_;
    bool present_ = false;
    bool modified_ = false;
};

}  // namespace scmd
