#include "chk/chk_document.h"

#include <algorithm>
namespace scmd {

bool ChkDocument::Parse(const Buffer& chk) {
    sections_.clear();
    size_t off = 0;
    while (off + 8 <= chk.size()) {
        ChkSection s;
        s.name.assign(reinterpret_cast<const char*>(&chk[off]), 4);
        const u32 len = static_cast<u32>(chk[off + 4]) | (static_cast<u32>(chk[off + 5]) << 8) |
                        (static_cast<u32>(chk[off + 6]) << 16) | (static_cast<u32>(chk[off + 7]) << 24);
        if (len > 0x800000u || off + 8 + len > chk.size()) break;
        s.data.assign(chk.begin() + off + 8, chk.begin() + off + 8 + len);
        sections_.push_back(std::move(s));
        off += 8 + len;
    }
    return !sections_.empty();
}

Buffer ChkDocument::Serialize() const {
    Buffer out;
    for (const auto& s : sections_) {
        out.insert(out.end(), s.name.begin(), s.name.begin() + 4);
        const u32 len = static_cast<u32>(s.data.size());
        out.push_back(static_cast<u8>(len & 0xff));
        out.push_back(static_cast<u8>((len >> 8) & 0xff));
        out.push_back(static_cast<u8>((len >> 16) & 0xff));
        out.push_back(static_cast<u8>((len >> 24) & 0xff));
        out.insert(out.end(), s.data.begin(), s.data.end());
    }
    return out;
}

bool ChkDocument::Has(const std::string& name) const { return Find(name) != nullptr; }

const Buffer* ChkDocument::Find(const std::string& name) const {
    for (const auto& s : sections_)
        if (s.name == name) return &s.data;
    return nullptr;
}

void ChkDocument::Set(const std::string& name, Buffer data) {
    for (auto& s : sections_) {
        if (s.name == name) { s.data = std::move(data); return; }
    }
    sections_.push_back(ChkSection{name, std::move(data)});
}

void ChkDocument::Remove(const std::string& name) {
    sections_.erase(std::remove_if(sections_.begin(), sections_.end(),
                                   [&](const ChkSection& s) { return s.name == name; }),
                    sections_.end());
}

}  // namespace scmd
