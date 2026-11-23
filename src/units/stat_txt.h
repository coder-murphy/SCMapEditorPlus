#pragma once
#include <string>
#include <vector>

#include "common/types.h"

namespace scmd {

// stat_txt.tbl：单位/界面文本表（u16 数量 + u16 偏移表 + 0 结尾字符串）。
// 前 228 项与 units.dat 的单位 ID 一一对应，用于 172「单位名字」显示。
class StatTextTable {
public:
    bool Parse(const Buffer& data);
    bool loaded() const { return loaded_; }
    std::size_t Count() const { return names_.size(); }
    // 下标越界或空条目返回空串
    const std::wstring& Name(int index) const;

    // 依次尝试：星际目录 / exe 目录 / 仓库 mpq（BrooDat 优先）中的 arr\stat_txt.tbl
    static bool LoadAuto(StatTextTable& out, std::wstring& source);

private:
    std::vector<std::wstring> names_;
    std::wstring empty_;
    bool loaded_ = false;
};

}  // namespace scmd
