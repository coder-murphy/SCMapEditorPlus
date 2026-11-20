#include "map/map_document.h"

#include "archive/mpq_archive.h"
#include <algorithm>

#include "common/log.h"

namespace scmd {

namespace {

const wchar_t* kEraNames[8] = {
    L"Badlands", L"Space", L"Installation", L"Ashworld",
    L"Jungle", L"Desert", L"Ice", L"Twilight",
};

u32 ReadU32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}
u16 ReadU16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }

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

constexpr std::size_t kUnitRecordSize = 36;

}  // namespace

const wchar_t* MapDocument::eraName() const {
    return era_ < 8 ? kEraNames[era_] : L"Unknown";
}

namespace {

// ERA -> 地形集文件名前缀
const wchar_t* kEraPrefix[8] = {
    L"StarDat.mpq_badlands", L"StarDat.mpq_platform", L"StarDat.mpq_install", L"StarDat.mpq_ashworld",
    L"StarDat.mpq_jungle",   L"BrooDat.mpq_desert",   L"BrooDat.mpq_ice",     L"BrooDat.mpq_twilight",
};

}  // namespace

bool MapDocument::Open(const std::wstring& path, const std::wstring& tilesetDir) {
    path_.clear();
    units_.clear();
    tiles_.clear();
    width_ = height_ = 0;
    era_ = 0;

    MpqArchive mpq;
    if (!mpq.Open(path)) return false;

    Buffer chkBytes;
    if (!mpq.ReadScenarioChk(chkBytes)) {
        Log(L"[map] scenario.chk 未找到");
        return false;
    }
    if (!chk_.Parse(chkBytes)) {
        Log(L"[map] CHK 段解析失败");
        return false;
    }

    const Buffer* dim = chk_.Find(section::kDim);
    if (!dim || dim->size() < 4) {
        Log(L"[map] 缺少 DIM 段");
        return false;
    }
    width_ = ReadU16(dim->data());
    height_ = ReadU16(dim->data() + 2);
    if (width_ <= 0 || height_ <= 0 || width_ > kMaxMapSize || height_ > kMaxMapSize) {
        Log(L"[map] 尺寸非法: %dx%d", width_, height_);
        return false;
    }

    if (const Buffer* era = chk_.Find(section::kEra); era && era->size() >= 2) {
        const u16 v = ReadU16(era->data());
        era_ = v < 8 ? v : 0;
    }

    // MTXM 优先，旧图缺 MTXM 时退回 TILE
    if (const Buffer* mtxm = chk_.Find(section::kMtxm); mtxm && mtxm->size() >= 2) {
        tiles_ = *mtxm;
    } else if (const Buffer* tile = chk_.Find(section::kTile); tile && tile->size() >= 2) {
        tiles_ = *tile;
    }

    // UNIT：36 字节/条
    if (const Buffer* unit = chk_.Find(section::kUnit)) {
        const std::size_t count = unit->size() / UnitRecord::kSize;
        units_.reserve(count);
        for (std::size_t i = 0; i < count; i++) {
            const u8* p = unit->data() + i * UnitRecord::kSize;
            UnitRecord r;
            r.serial = ReadU32(p);
            r.x = ReadU16(p + 4);
            r.y = ReadU16(p + 6);
            r.type = ReadU16(p + 8);
            r.validState = ReadU16(p + 12);
            r.validProps = ReadU16(p + 14);
            r.owner = p[16];
            r.hpPercent = p[17];
            r.shieldPercent = p[18];
            r.energyPercent = p[19];
            r.resource = ReadU32(p + 20);
            r.stackCount = ReadU16(p + 24);
            r.queueCount = ReadU16(p + 26);
            units_.push_back(r);
        }
    }

    // 地形集（可选）：渲染需要，缺失时文档仍可打开
    tileset_.Unload();
    if (!tilesetDir.empty() && era_ < 8) {
        if (!tileset_.Load(tilesetDir, kEraPrefix[era_])) {
            Log(L"[map] 地形集加载失败（ERA %u），地形将不显示", era_);
        }
    }

    path_ = path;
    dirty_ = false;
    Log(L"[map] opened %dx%d era=%u units=%zu", width_, height_, era_, units_.size());
    return true;
}

namespace {

constexpr u16 kMineralLow = 176;
constexpr u16 kMineralHigh = 178;
constexpr u16 kGeyser = 188;

// 单位像素坐标 -> 所在格（锚点规则的反解）
int UnitTileX(const UnitRecord& r) { return static_cast<int>(r.x) / 32; }
int UnitTileY(const UnitRecord& r) { return static_cast<int>(r.y) / 32; }

}  // namespace

u16 MapDocument::TileAt(int tx, int ty) const {
    if (tx < 0 || ty < 0 || tx >= width_ || ty >= height_) return 0;
    const std::size_t at = (static_cast<std::size_t>(ty) * width_ + tx) * 2;
    if (at + 1 >= tiles_.size()) return 0;
    return static_cast<u16>(tiles_[at] | (tiles_[at + 1] << 8));
}

bool MapDocument::IsWalkableTile(int tx, int ty) const {
    if (tx < 0 || ty < 0 || tx >= width_ || ty >= height_) return false;
    if (!tileset_.IsLoaded()) return true;      // 无法校验时放行
    const u16 group = static_cast<u16>((TileAt(tx, ty) & 0x7FF0) >> 4);
    return tileset_.IsWalkable(group);
}

bool MapDocument::PaintTile(int tx, int ty, u16 value) {
    if (tx < 0 || ty < 0 || tx >= width_ || ty >= height_) return false;
    const std::size_t at = (static_cast<std::size_t>(ty) * width_ + tx) * 2;
    if (at + 1 >= tiles_.size()) return false;
    tiles_[at] = static_cast<u8>(value & 0xFF);
    tiles_[at + 1] = static_cast<u8>(value >> 8);
    dirty_ = true;
    return true;
}

bool MapDocument::PaintRect(int x0, int y0, int x1, int y1, u16 value) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    bool any = false;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (PaintTile(x, y, value)) any = true;
        }
    }
    return any;
}

bool MapDocument::PlaceUnit(int tx, int ty, u16 type, u8 owner, u32 resource) {
    if (!IsWalkableTile(tx, ty)) return false;

    u32 serial = 1;
    for (const auto& u : units_) {
        if (u.serial >= serial) serial = u.serial + 1;
    }

    UnitRecord r;
    r.serial = serial;
    r.x = static_cast<u16>(tx * 32);
    r.type = type;
    r.owner = owner;
    r.resource = resource;
    r.hpPercent = 100;
    r.shieldPercent = 100;
    r.energyPercent = 100;
    r.validState = 0x0018;

    if (type >= kMineralLow && type <= kMineralHigh) {
        r.y = static_cast<u16>(ty * 32 + 16);
        r.validProps = 0x0012;
    } else if (type == kGeyser) {
        r.y = static_cast<u16>(ty * 32);
        r.validProps = 0x0012;
    } else {
        r.y = static_cast<u16>(ty * 32 + 16);
        r.validProps = 0x0002;      // 与生成器的出生点一致
    }

    units_.push_back(r);
    dirty_ = true;
    return true;
}

int MapDocument::UnitCountAtTile(int tx, int ty) const {
    int n = 0;
    for (const auto& u : units_) {
        if (UnitTileX(u) == tx && UnitTileY(u) == ty) n++;
    }
    return n;
}

int MapDocument::RemoveUnitsAtTile(int tx, int ty) {
    const std::size_t before = units_.size();
    units_.erase(std::remove_if(units_.begin(), units_.end(),
                                [tx, ty](const UnitRecord& u) {
                                    return UnitTileX(u) == tx && UnitTileY(u) == ty;
                                }),
                 units_.end());
    const int removed = static_cast<int>(before - units_.size());
    if (removed > 0) dirty_ = true;
    return removed;
}

bool MapDocument::Save(const std::wstring& path) {
    if (!IsOpen()) return false;

    // 地形：原图有 MTXM 就写 MTXM，否则写 TILE
    if (!tiles_.empty()) {
        if (chk_.Has(section::kMtxm)) {
            chk_.Set(section::kMtxm, tiles_);
        } else {
            chk_.Set(section::kTile, tiles_);
        }
    }

    // 单位：按 36 字节记录回写
    Buffer unitBytes(units_.size() * kUnitRecordSize, 0);
    for (std::size_t i = 0; i < units_.size(); i++) {
        const UnitRecord& r = units_[i];
        u8* p = unitBytes.data() + i * kUnitRecordSize;
        WriteU32(p + 0, r.serial);
        WriteU16(p + 4, r.x);
        WriteU16(p + 6, r.y);
        WriteU16(p + 8, r.type);
        WriteU16(p + 10, 0);
        WriteU16(p + 12, r.validState);
        WriteU16(p + 14, r.validProps);
        p[16] = r.owner;
        p[17] = r.hpPercent;
        p[18] = r.shieldPercent;
        p[19] = r.energyPercent;
        WriteU32(p + 20, r.resource);
        WriteU16(p + 24, r.stackCount);
        WriteU16(p + 26, r.queueCount);
        // 28..35 保留为 0
    }
    if (chk_.Has(section::kUnit) || !units_.empty()) {
        chk_.Set(section::kUnit, unitBytes);
    }

    const Buffer chkBytes = chk_.Serialize();
    std::vector<MpqFileEntry> files;
    const std::string list = "staredit\\scenario.chk";
    files.push_back({"(listfile)", Buffer(list.begin(), list.end())});
    files.push_back({"staredit\\scenario.chk", chkBytes});

    if (!MpqArchive::WriteArchive(path, files)) {
        Log(L"[map] 写出失败: %s", path.c_str());
        return false;
    }

    dirty_ = false;
    Log(L"[map] saved %s  chk=%u bytes", path.c_str(), static_cast<unsigned>(chkBytes.size()));
    return true;
}

}  // namespace scmd
