#pragma once
#include <string>
#include <vector>

#include "common/types.h"

namespace scmd {

// MPQ 容器（.scm / .scx 本体）
// VERIFIED:
//   哈希槽位用 hash 类型 0 计算，但查找文件时用 hash 类型 1/2 匹配 name1/name2；
//   locale 必须为 0x0000；必须包含 (listfile)；
//   哈希表用 hash("(hash table)",3) 解密、块表用 hash("(block table)",3)；
//   扇区表用 key-1、第 i 扇区用 key+i；key = hash(basename,3)（ADJUSTED 时 + filePos）。
// 写出用的条目
struct MpqFileEntry {
    std::string name;
    Buffer data;
};

class MpqArchive {
public:
    MpqArchive();
    ~MpqArchive();

    MpqArchive(const MpqArchive&) = delete;
    MpqArchive& operator=(const MpqArchive&) = delete;

    bool Open(const std::wstring& path);
    void Close();
    bool IsOpen() const;

    // 读取条目（自动处理扇区、解密、zlib/implode 解压）
    bool ReadFile(const std::string& entryName, Buffer& out) const;
    bool FileExists(const std::string& entryName) const;

    // 依次尝试 scenario.chk / Scenario.chk / staredit\\scenario.chk / Staredit\\Scenario.chk
    bool ReadScenarioChk(Buffer& out) const;

    const std::wstring& path() const;

    // 已分配的条目数（哈希表中 blockIndex != 0xFFFFFFFF）
    int EntryCount() const;

    // 最近一次读取失败的阶段说明（空字符串表示无错误）
    const wchar_t* LastError() const;

    // 写出：未压缩单块（flags = 0x80000000），哈希/块表按格式加密
    // VERIFIED: 该布局可被 Storm(SFmpq.dll) 与 ScmDraft2 读取
    static bool WriteArchive(const std::wstring& path, const std::vector<MpqFileEntry>& files);

    bool AddFile(const std::string& entryName, const Buffer& data);
    bool SaveTo(const std::wstring& path);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace scmd
