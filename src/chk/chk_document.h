#pragma once
#include <string>
#include <vector>

#include "chk/chk_section.h"

namespace scmd {

// CHK 段集合：保持原始顺序，便于无损往返
// VERIFIED: 只替换 MTXM 段内容、其余段原样透传，可被 ScmDraft2 正常打开
class ChkDocument {
public:
    bool Parse(const Buffer& chk);
    Buffer Serialize() const;

    bool Has(const std::string& name) const;
    const Buffer* Find(const std::string& name) const;
    void Set(const std::string& name, Buffer data);  // 不存在则追加到末尾

    const std::vector<ChkSection>& sections() const { return sections_; }

    // TODO: MRGN 置空可避免 ScmDraft2 显示游离矩形框（VERIFIED）
private:
    std::vector<ChkSection> sections_;
};

}  // namespace scmd
