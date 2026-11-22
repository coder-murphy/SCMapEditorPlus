#include "archive/mpq_archive.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

#include <windows.h>

#include "archive/mpq_codec.h"
#include "common/log.h"

namespace scmd {

namespace {

constexpr u32 kFlagImplode = 0x00000100;
constexpr u32 kFlagCompress = 0x00000200;
constexpr u32 kFlagEncrypted = 0x00010000;
constexpr u32 kFlagAdjusted = 0x00020000;
constexpr u32 kFlagSingleUnit = 0x01000000;
constexpr u32 kInvalidBlock = 0xFFFFFFFF;

u32 ReadU32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}
u16 ReadU16(const u8* p) {
    return static_cast<u16>(p[0] | (p[1] << 8));
}

// 把任意长度数据按 u32 解密，保持原长度
Buffer DecryptPadded(const u8* data, std::size_t len, u32 key) {
    Buffer tmp((len + 3) & ~static_cast<std::size_t>(3), 0);
    std::memcpy(tmp.data(), data, len);
    mpq::DecryptWords(reinterpret_cast<u32*>(tmp.data()), tmp.size() / 4, key);
    tmp.resize(len);
    return tmp;
}

std::string BaseName(const std::string& name) {
    const std::size_t at = name.find_last_of('\\');
    return at == std::string::npos ? name : name.substr(at + 1);
}

std::string NormalizeName(const std::string& name) {
    std::string out = name;
    for (char& c : out) {
        if (c == '/') c = '\\';
    }
    return out;
}

}  // namespace

struct MpqArchive::Impl {
    std::wstring path;
    Buffer raw;

    u32 sectorSize = 512;
    u32 hashPos = 0;
    u32 blockPos = 0;
    u32 hashCount = 0;
    u32 blockCount = 0;

    struct HashEntry {
        u32 name1 = 0;
        u32 name2 = 0;
        u16 locale = 0;
        u16 platform = 0;
        u32 blockIndex = kInvalidBlock;
    };
    struct BlockEntry {
        u32 pos = 0;
        u32 compressedSize = 0;
        u32 fileSize = 0;
        u32 flags = 0;
    };
    mutable std::wstring err;
    std::vector<MpqFileEntry> pending;
    std::vector<HashEntry> hash;
    std::vector<BlockEntry> blocks;

    bool LoadFile(const std::wstring& p) {
        std::ifstream in(p.c_str(), std::ios::binary | std::ios::ate);
        if (!in) return false;
        const std::streamoff size = in.tellg();
        if (size <= 0 || size > (64ll << 20)) return false;
        in.seekg(0, std::ios::beg);
        raw.resize(static_cast<std::size_t>(size));
        in.read(reinterpret_cast<char*>(raw.data()), size);
        return in.good() || in.eof();
    }

    bool Parse() {
        if (raw.size() < 32) return false;
        if (std::memcmp(raw.data(), "MPQ\x1A", 4) != 0) return false;

        const u32 sectorShift = ReadU16(raw.data() + 14);
        sectorSize = 512u << sectorShift;
        hashPos = ReadU32(raw.data() + 16);
        blockPos = ReadU32(raw.data() + 20);
        hashCount = ReadU32(raw.data() + 24);
        blockCount = ReadU32(raw.data() + 28);
        if (hashCount == 0 || hashCount > (1u << 20)) return false;
        if (blockCount > (1u << 20)) return false;
        if (static_cast<std::size_t>(hashPos) + static_cast<std::size_t>(hashCount) * 16 > raw.size()) return false;
        if (static_cast<std::size_t>(blockPos) + static_cast<std::size_t>(blockCount) * 16 > raw.size()) return false;

        // 哈希表
        Buffer ht(raw.begin() + hashPos, raw.begin() + hashPos + hashCount * 16);
        mpq::DecryptWords(reinterpret_cast<u32*>(ht.data()), ht.size() / 4,
                          mpq::HashString("(hash table)", 3));
        hash.resize(hashCount);
        for (u32 i = 0; i < hashCount; i++) {
            const u8* e = ht.data() + i * 16;
            hash[i].name1 = ReadU32(e);
            hash[i].name2 = ReadU32(e + 4);
            hash[i].locale = ReadU16(e + 8);
            hash[i].platform = ReadU16(e + 10);
            hash[i].blockIndex = ReadU32(e + 12);
        }

        // 块表
        Buffer bt(raw.begin() + blockPos, raw.begin() + blockPos + blockCount * 16);
        mpq::DecryptWords(reinterpret_cast<u32*>(bt.data()), bt.size() / 4,
                          mpq::HashString("(block table)", 3));
        blocks.resize(blockCount);
        for (u32 i = 0; i < blockCount; i++) {
            const u8* e = bt.data() + i * 16;
            blocks[i].pos = ReadU32(e);
            blocks[i].compressedSize = ReadU32(e + 4);
            blocks[i].fileSize = ReadU32(e + 8);
            blocks[i].flags = ReadU32(e + 12);
        }
        return true;
    }

    int FindBlock(const std::string& name) const {
        const std::string norm = NormalizeName(name);
        const u32 h1 = mpq::HashString(norm.c_str(), 1);
        const u32 h2 = mpq::HashString(norm.c_str(), 2);
        for (u32 i = 0; i < hashCount; i++) {
            const HashEntry& e = hash[i];
            if (e.blockIndex == kInvalidBlock) continue;
            if (e.name1 == h1 && e.name2 == h2) return static_cast<int>(e.blockIndex);
        }
        return -1;
    }

    bool ReadEntry(const std::string& name, Buffer& out) const {
        const int bi = FindBlock(name);
        if (bi < 0 || static_cast<std::size_t>(bi) >= blocks.size()) {
            err = L"entry-not-found";
            return false;
        }
        const BlockEntry& be = blocks[static_cast<std::size_t>(bi)];
        if (static_cast<std::size_t>(be.pos) + be.compressedSize > raw.size()) {
            err = L"block-out-of-range";
            return false;
        }

        u32 key = mpq::HashString(BaseName(NormalizeName(name)).c_str(), 3);
        // FIX_KEY：key = (hash + blockPos) ^ fileSize（StormLib 的 GetFileKey）
        if (be.flags & kFlagAdjusted) key = (key + be.pos) ^ be.fileSize;
        const bool compressed = (be.flags & (kFlagCompress | kFlagImplode)) != 0;

        out.clear();
        if (be.flags & kFlagSingleUnit) {
            Buffer blob(raw.begin() + be.pos, raw.begin() + be.pos + be.compressedSize);
            if (be.flags & kFlagEncrypted) {
                blob = DecryptPadded(blob.data(), blob.size(), key);
            }
            if (compressed) {
                Buffer plain;
                if (!mpq::DecompressSector(blob.data(), blob.size(), be.fileSize, plain)) {
                    err = L"decompress-single";
                    return false;
                }
                out.swap(plain);
            } else {
                out.swap(blob);
            }
            if (out.size() > be.fileSize) out.resize(be.fileSize);
            return true;
        }

        // 多扇区
        const std::size_t sectorCount =
            std::max<std::size_t>(1, (be.fileSize + sectorSize - 1) / sectorSize);
        std::vector<u32> table(sectorCount + 1, 0);

        if (compressed) {
            const std::size_t tableBytes = (sectorCount + 1) * 4;
            if (static_cast<std::size_t>(be.pos) + tableBytes > raw.size()) {
                err = L"sector-table-out-of-range";
                return false;
            }
            // 扇区偏移表只在该文件加密时才用 key-1 解密
            Buffer tr(raw.begin() + be.pos, raw.begin() + be.pos + tableBytes);
            if (be.flags & kFlagEncrypted) {
                tr = DecryptPadded(tr.data(), tr.size(), key - 1);
            }
            for (std::size_t i = 0; i <= sectorCount; i++) table[i] = ReadU32(tr.data() + i * 4) & 0x7FFFFFFF;
        } else {
            for (std::size_t i = 0; i <= sectorCount; i++) table[i] = static_cast<u32>(i * sectorSize);
        }

        Buffer whole;
        whole.reserve(be.fileSize);
        for (std::size_t i = 0; i < sectorCount; i++) {
            const std::size_t start = be.pos + table[i];
            const std::size_t end = be.pos + table[i + 1];
            if (end < start || end > raw.size()) {
                err = L"sector-bounds";
                return false;
            }
            Buffer sector(raw.begin() + start, raw.begin() + end);
            if (be.flags & kFlagEncrypted) {
                sector = DecryptPadded(sector.data(), sector.size(), key + static_cast<u32>(i));
            }
            const std::size_t want = std::min<std::size_t>(sectorSize, be.fileSize - i * sectorSize);
            if (compressed && sector.size() < want) {
                Buffer plain;
                if (!mpq::DecompressSector(sector.data(), sector.size(), want, plain)) {
                    wchar_t buf[64];
                    ::wsprintfW(buf, L"decompress-sector type=0x%02X", mpq::LastCompressType());
                    err = buf;
                    return false;
                }
                whole.insert(whole.end(), plain.begin(), plain.end());
            } else {
                whole.insert(whole.end(), sector.begin(), sector.end());
            }
        }
        if (whole.size() > be.fileSize) whole.resize(be.fileSize);
        out.swap(whole);
        return true;
    }
};

MpqArchive::MpqArchive() = default;
MpqArchive::~MpqArchive() { Close(); }

bool MpqArchive::Open(const std::wstring& path) {
    Close();
    auto* impl = new Impl();
    if (!impl->LoadFile(path) || !impl->Parse()) {
        delete impl;
        Log(L"[mpq] 打开失败: %s", path.c_str());
        return false;
    }
    impl->path = path;
    impl_ = impl;
    return true;
}

void MpqArchive::Close() {
    delete impl_;
    impl_ = nullptr;
}

bool MpqArchive::IsOpen() const { return impl_ != nullptr; }

bool MpqArchive::ReadFile(const std::string& entryName, Buffer& out) const {
    return impl_ && impl_->ReadEntry(entryName, out);
}

bool MpqArchive::FileExists(const std::string& entryName) const {
    return impl_ && impl_->FindBlock(entryName) >= 0;
}

bool MpqArchive::ReadScenarioChk(Buffer& out) const {
    static const char* kCandidates[] = {
        "staredit\\scenario.chk", "Scenario.chk", "scenario.chk", "Staredit\\Scenario.chk"};
    for (const char* name : kCandidates) {
        if (ReadFile(name, out)) return true;
    }
    return false;
}

const std::wstring& MpqArchive::path() const {
    static const std::wstring kEmpty;
    return impl_ ? impl_->path : kEmpty;
}

const wchar_t* MpqArchive::LastError() const {
    return impl_ ? impl_->err.c_str() : L"";
}

int MpqArchive::EntryCount() const {
    if (!impl_) return 0;
    int n = 0;
    for (const auto& e : impl_->hash) {
        if (e.blockIndex != kInvalidBlock) n++;
    }
    return n;
}

namespace {

void WriteU16(u8* p, u16 v) {
    p[0] = static_cast<u8>(v & 0xFF);
    p[1] = static_cast<u8>(v >> 8);
}

void WriteU32(u8* p, u32 v) {
    p[0] = static_cast<u8>(v & 0xFF);
    p[1] = static_cast<u8>((v >> 8) & 0xFF);
    p[2] = static_cast<u8>((v >> 16) & 0xFF);
    p[3] = static_cast<u8>((v >> 24) & 0xFF);
}

}  // namespace

bool MpqArchive::WriteArchive(const std::wstring& path, const std::vector<MpqFileEntry>& files) {
    if (files.empty()) return false;

    constexpr u32 kHashSize = 1024;
    constexpr u32 kHeaderSize = 32;

    // 能压小的条目写成「单块压缩」（zlib 类型 0x02 + raw deflate）：
    // flags = EXISTS | SINGLE_UNIT | COMPRESS，无需扇区偏移表。
    std::vector<Buffer> packed(files.size());
    std::vector<const Buffer*> payload(files.size(), nullptr);
    std::vector<u32> originalSize(files.size(), 0);
    std::vector<u32> blockFlags(files.size(), 0x80000000u);
    for (std::size_t i = 0; i < files.size(); i++) {
        const Buffer& data = files[i].data;
        originalSize[i] = static_cast<u32>(data.size());
        payload[i] = &data;
        if (data.size() >= 512 && data.size() <= (16u << 20)) {
            Buffer deflated;
            if (mpq::CompressRaw(data.data(), data.size(), deflated) &&
                deflated.size() + 1 + 16 < data.size()) {
                packed[i].reserve(deflated.size() + 1);
                packed[i].push_back(0x02);
                packed[i].insert(packed[i].end(), deflated.begin(), deflated.end());
                payload[i] = &packed[i];
                blockFlags[i] = 0x80000000u | 0x01000000u | 0x00000200u;
            }
        }
    }

    // 布局：header | 文件数据（16 字节对齐） | 哈希表 | 块表
    std::vector<u32> blockPos(files.size(), 0);
    u32 cursor = kHeaderSize;
    for (std::size_t i = 0; i < files.size(); i++) {
        cursor = (cursor + 15u) & ~15u;
        blockPos[i] = cursor;
        cursor += static_cast<u32>(payload[i]->size());
    }
    const u32 hashPos = (cursor + 15u) & ~15u;
    const u32 blockTablePos = hashPos + kHashSize * 16;
    const u32 totalSize = blockTablePos + static_cast<u32>(files.size()) * 16;

    Buffer out(totalSize, 0);
    std::memcpy(out.data(), "MPQ\x1A", 4);
    WriteU32(out.data() + 4, kHeaderSize);
    WriteU32(out.data() + 8, totalSize);
    WriteU16(out.data() + 12, 0);          // format version
    WriteU16(out.data() + 14, 3);          // sector shift -> 4096
    WriteU32(out.data() + 16, hashPos);
    WriteU32(out.data() + 20, blockTablePos);
    WriteU32(out.data() + 24, kHashSize);
    WriteU32(out.data() + 28, static_cast<u32>(files.size()));

    for (std::size_t i = 0; i < files.size(); i++) {
        if (!payload[i]->empty()) {
            std::memcpy(out.data() + blockPos[i], payload[i]->data(), payload[i]->size());
        }
    }

    // 哈希表：类型 0 决定槽位，类型 1/2 写入 name1/name2，locale 必须为 0
    std::vector<u8> hashTable(static_cast<std::size_t>(kHashSize) * 16, 0xFF);
    for (std::size_t i = 0; i < files.size(); i++) {
        const std::string name = NormalizeName(files[i].name);
        u32 slot = mpq::HashString(name.c_str(), 0) & (kHashSize - 1);
        for (u32 probe = 0; probe < kHashSize; probe++) {
            const u32 at = slot * 16;
            const u32 used = ReadU32(hashTable.data() + at + 12);
            if (used == kInvalidBlock) {
                WriteU32(hashTable.data() + at + 0, mpq::HashString(name.c_str(), 1));
                WriteU32(hashTable.data() + at + 4, mpq::HashString(name.c_str(), 2));
                WriteU16(hashTable.data() + at + 8, 0);     // locale = 0
                WriteU16(hashTable.data() + at + 10, 0);    // platform
                WriteU32(hashTable.data() + at + 12, static_cast<u32>(i));
                break;
            }
            slot = (slot + 1) & (kHashSize - 1);
        }
    }

    // 块表
    std::vector<u8> blockTable(files.size() * 16, 0);
    for (std::size_t i = 0; i < files.size(); i++) {
        u8* e = blockTable.data() + i * 16;
        WriteU32(e + 0, blockPos[i]);
        WriteU32(e + 4, static_cast<u32>(payload[i]->size()));  // compressed size
        WriteU32(e + 8, originalSize[i]);                       // file size
        WriteU32(e + 12, blockFlags[i]);
    }

    mpq::EncryptWords(reinterpret_cast<u32*>(hashTable.data()), kHashSize * 4,
                      mpq::HashString("(hash table)", 3));
    mpq::EncryptWords(reinterpret_cast<u32*>(blockTable.data()), files.size() * 4,
                      mpq::HashString("(block table)", 3));

    std::memcpy(out.data() + hashPos, hashTable.data(), hashTable.size());
    std::memcpy(out.data() + blockTablePos, blockTable.data(), blockTable.size());

    std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return f.good();
}

bool MpqArchive::AddFile(const std::string& entryName, const Buffer& data) {
    if (!impl_) return false;
    for (auto& e : impl_->pending) {
        if (NormalizeName(e.name) == NormalizeName(entryName)) {
            e.data = data;
            return true;
        }
    }
    impl_->pending.push_back({entryName, data});
    return true;
}

bool MpqArchive::SaveTo(const std::wstring& path) {
    return impl_ && !impl_->pending.empty() && WriteArchive(path, impl_->pending);
}

}  // namespace scmd
