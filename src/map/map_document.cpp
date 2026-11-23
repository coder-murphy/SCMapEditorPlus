#include "map/map_document.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <utility>

#include "archive/mpq_archive.h"
#include "common/log.h"
#include "res/resource.h"

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

// 读取本工程内嵌的原版默认地图数据（RCDATA）；SCMDCore/Cli 无资源时返回 false
bool LoadEmbeddedData(int id, Buffer& out) {
    HMODULE module = ::GetModuleHandleW(nullptr);
    const HRSRC info = ::FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!info) return false;
    const DWORD size = ::SizeofResource(module, info);
    const HGLOBAL handle = ::LoadResource(module, info);
    if (!handle || size == 0) return false;
    const void* data = ::LockResource(handle);
    if (!data) return false;
    out.assign(static_cast<const u8*>(data), static_cast<const u8*>(data) + size);
    return true;
}

}  // namespace

const wchar_t* MapDocument::eraName() const {
    return era_ < 8 ? kEraNames[era_] : L"Unknown";
}

namespace {

// MPQ 内部文件：由写出器自行维护，不能原样回填
bool IsInternalMpqFile(const std::string& name) {
    static const char* const kInternal[] = {
        "(listfile)", "(attributes)", "(signature)", "(user data)", "(patch_metadata)",
    };
    for (const char* s : kInternal) {
        if (_stricmp(name.c_str(), s) == 0) return true;
    }
    return false;
}

bool IsScenarioChk(const std::string& name) {
    return _stricmp(name.c_str(), "staredit\\scenario.chk") == 0 ||
           _stricmp(name.c_str(), "staredit/scenario.chk") == 0 ||
           _stricmp(name.c_str(), "scenario.chk") == 0;
}

// 取 MPQ 中除 CHK/内部文件外的全部条目；名称来自 (listfile)
void LoadExtraFiles(const MpqArchive& mpq, std::vector<MpqFileEntry>& out) {
    out.clear();
    Buffer list;
    if (!mpq.ReadFile("(listfile)", list)) return;
    std::string names(list.begin(), list.end());
    std::size_t at = 0;
    while (at < names.size()) {
        std::size_t end = at;
        while (end < names.size() && names[end] != '\r' && names[end] != '\n' && names[end] != 0) {
            end++;
        }
        std::string name = names.substr(at, end - at);
        at = end + 1;
        // 去掉末尾空白
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
        if (name.empty() || IsInternalMpqFile(name) || IsScenarioChk(name)) continue;
        Buffer data;
        if (mpq.ReadFile(name, data)) {
            out.push_back({name, std::move(data)});
        } else {
            Log(L"[map] 额外文件读取失败: %hs", name.c_str());
        }
    }
}

// ERA -> 地形集文件名前缀
const wchar_t* kEraPrefix[8] = {
    L"StarDat.mpq_badlands", L"StarDat.mpq_platform", L"StarDat.mpq_install", L"StarDat.mpq_ashworld",
    L"StarDat.mpq_jungle",   L"BrooDat.mpq_desert",   L"BrooDat.mpq_ice",     L"BrooDat.mpq_twilight",
};

}  // namespace

bool MapDocument::Open(const std::wstring& path, const std::wstring& tilesetDir) {
    opened_ = false;
    path_.clear();
    units_.clear();
    tiles_.clear();
    extraFiles_.clear();
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
    players_.Parse(chk_);
    restrictions_.Parse(chk_);
    LoadExtraFiles(mpq, extraFiles_);

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
            r.unused0 = ReadU16(p + 10);
            r.validState = ReadU16(p + 12);
            r.validProps = ReadU16(p + 14);
            r.owner = p[16];
            r.hpPercent = p[17];
            r.shieldPercent = p[18];
            r.energyPercent = p[19];
            r.resource = ReadU32(p + 20);
            r.stackCount = ReadU16(p + 24);
            r.queueCount = ReadU16(p + 26);
            std::memcpy(r.reserved, p + 28, 8);
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

    // 触发 / 地点 / 字符串
    triggers_.Parse(Buffer());
    locations_.Parse(Buffer());
    strings_.Parse(Buffer());
    if (const Buffer* trig = chk_.Find("TRIG")) triggers_.Parse(*trig);
    if (const Buffer* mrgn = chk_.Find("MRGN")) locations_.Parse(*mrgn);
    if (const Buffer* str = chk_.Find("STR ")) strings_.Parse(*str);

    // 任务简报 / 声音 / 开关名
    briefing_.Parse(Buffer());
    sounds_.Parse(Buffer());
    switchNames_.Parse(Buffer());
    if (const Buffer* mbrf = chk_.Find(section::kMbrf)) briefing_.Parse(*mbrf);
    if (const Buffer* wav = chk_.Find(section::kWav)) sounds_.Parse(*wav);
    if (const Buffer* swnm = chk_.Find(section::kSwnm)) switchNames_.Parse(*swnm);

    // 装饰物 / 战争迷雾 / SP 单位
    doodads_.Parse(Buffer());
    fog_.Parse(Buffer(), width_, height_);
    sprites_.Parse(Buffer());
    if (const Buffer* dd2 = chk_.Find(section::kDd2)) doodads_.Parse(*dd2);
    if (const Buffer* mask = chk_.Find(section::kMask)) fog_.Parse(*mask, width_, height_);
    if (const Buffer* thg2 = chk_.Find(section::kThg2)) sprites_.Parse(*thg2);

    path_ = path;
    opened_ = true;
    dirty_ = false;
    Log(L"[map] opened %dx%d era=%u units=%zu trig=%zu loc=%zu str=%zu extra=%zu",
        width_, height_, era_, units_.size(), triggers_.BlockCount(),
        locations_.Count(), strings_.Count(), extraFiles_.size());
    return true;
}

bool MapDocument::CreateNew(int width, int height, u16 era, const std::wstring& tilesetDir,
                            int triggerPreset) {
    Close();
    if (width <= 0 || height <= 0 || width > kMaxMapSize || height > kMaxMapSize) return false;

    width_ = width;
    height_ = height;
    era_ = era < 8 ? era : 0;

    Buffer dim(4, 0);
    WriteU16(dim.data(), static_cast<u16>(width_));
    WriteU16(dim.data() + 2, static_cast<u16>(height_));
    Buffer eraBytes(2, 0);
    WriteU16(eraBytes.data(), era_);
    // 全 0 的 MTXM：瓦片组 0，正常地形集下可行走
    tiles_.assign(static_cast<std::size_t>(width_) * height_ * 2, 0);

    chk_.Parse(Buffer());
    chk_.Set(section::kDim, std::move(dim));
    chk_.Set(section::kEra, std::move(eraBytes));
    chk_.Set(section::kMtxm, tiles_);

    // 原版新建地图的默认数据：VCOD + 可选 TRIG 模板
    Buffer vcod;
    if (LoadEmbeddedData(IDR_ASSET_VCOD, vcod) && vcod.size() == 1040) {
        chk_.Set("VCOD", std::move(vcod));
    }
    if (triggerPreset == 1 || triggerPreset == 2) {
        Buffer trig;
        const int id = triggerPreset == 1 ? IDR_ASSET_TRIG3 : IDR_ASSET_TRIG4;
        if (LoadEmbeddedData(id, trig) && !trig.empty()) {
            chk_.Set("TRIG", trig);
            triggers_.Parse(trig);
        }
    }

    if (!tilesetDir.empty() && !tileset_.Load(tilesetDir, kEraPrefix[era_])) {
        Log(L"[map] 新地图地形集加载失败（ERA %u）", era_);
    }

    opened_ = true;
    dirty_ = true;      // 新建地图尚未落盘
    Log(L"[map] new %dx%d era=%u", width_, height_, era_);
    return true;
}

bool MapDocument::Resize(int width, int height, int offsetX, int offsetY, bool smoothEdges) {
    if (!opened_ || width <= 0 || height <= 0 || width > kMaxMapSize || height > kMaxMapSize) {
        return false;
    }

    Buffer next(static_cast<std::size_t>(width) * height * 2, 0);
    // 平滑模式：新增边缘按最近的原始瓦片延伸；否则填充 0
    const bool smooth = smoothEdges && width_ > 0 && height_ > 0;
    for (int ny = 0; ny < height; ny++) {
        for (int nx = 0; nx < width; nx++) {
            int sx = nx - offsetX;
            int sy = ny - offsetY;
            if (sx < 0 || sy < 0 || sx >= width_ || sy >= height_) {
                if (!smooth) continue;
                sx = std::clamp(sx, 0, width_ - 1);
                sy = std::clamp(sy, 0, height_ - 1);
            }
            const std::size_t s = (static_cast<std::size_t>(sy) * width_ + sx) * 2;
            if (s + 1 >= tiles_.size()) continue;
            const std::size_t d = (static_cast<std::size_t>(ny) * width + nx) * 2;
            next[d] = tiles_[s];
            next[d + 1] = tiles_[s + 1];
        }
    }

    tiles_ = std::move(next);
    width_ = width;
    height_ = height;
    fog_.Resize(width_, height_, offsetX, offsetY);

    // 单位：按格平移，越界丢弃
    std::vector<UnitRecord> kept;
    kept.reserve(units_.size());
    for (const auto& u : units_) {
        const int nx = static_cast<int>(u.x) + offsetX * 32;
        const int ny = static_cast<int>(u.y) + offsetY * 32;
        if (nx < 0 || ny < 0 || nx >= width_ * 32 || ny >= height_ * 32) continue;
        UnitRecord moved = u;
        moved.x = static_cast<u16>(nx);
        moved.y = static_cast<u16>(ny);
        kept.push_back(moved);
    }
    units_ = std::move(kept);

    Buffer dim(4, 0);
    WriteU16(dim.data(), static_cast<u16>(width_));
    WriteU16(dim.data() + 2, static_cast<u16>(height_));
    chk_.Set(section::kDim, std::move(dim));
    chk_.Set(chk_.Has(section::kMtxm) ? section::kMtxm : section::kTile, tiles_);

    dirty_ = true;
    Log(L"[map] resized %dx%d off=%d,%d units=%zu", width_, height_, offsetX, offsetY, units_.size());
    return true;
}

void MapDocument::Close() {
    opened_ = false;
    dirty_ = false;
    path_.clear();
    width_ = height_ = 0;
    era_ = 0;
    tiles_.clear();
    units_.clear();
    triggers_.Parse(Buffer());
    locations_.Parse(Buffer());
    strings_.Parse(Buffer());
    players_.Reset();
    restrictions_.Reset();
    doodads_.Reset();
    fog_.Reset();
    sprites_.Reset();
    briefing_.Parse(Buffer());
    sounds_.Reset();
    switchNames_.Reset();
    extraFiles_.clear();
    chk_.Parse(Buffer());
    tileset_.Unload();
}

bool MapDocument::ReadExtraFile(const std::string& name, Buffer& out) const {
    for (const auto& f : extraFiles_) {
        if (_stricmp(f.name.c_str(), name.c_str()) == 0) {
            out = f.data;
            return true;
        }
    }
    return false;
}

bool MapDocument::SetExtraFile(const std::string& name, Buffer data) {
    if (name.empty() || IsInternalMpqFile(name) || IsScenarioChk(name)) return false;
    for (auto& f : extraFiles_) {
        if (_stricmp(f.name.c_str(), name.c_str()) == 0) {
            f.name = name;
            f.data = std::move(data);
            dirty_ = true;
            return true;
        }
    }
    extraFiles_.push_back({name, std::move(data)});
    dirty_ = true;
    return true;
}

bool MapDocument::RemoveExtraFile(const std::string& name) {
    for (auto it = extraFiles_.begin(); it != extraFiles_.end(); ++it) {
        if (_stricmp(it->name.c_str(), name.c_str()) == 0) {
            extraFiles_.erase(it);
            dirty_ = true;
            return true;
        }
    }
    return false;
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

bool MapDocument::PlaceUnit(int tx, int ty, u16 type, u8 owner, u32 resource, bool force) {
    if (tx < 0 || ty < 0 || tx >= width_ || ty >= height_) return false;
    if (!force && !IsWalkableTile(tx, ty)) return false;

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

int MapDocument::RemoveInvalidUnits() {
    if (!opened_) return 0;

    std::vector<UnitRecord> kept;
    kept.reserve(units_.size());
    int removed = 0;
    for (const UnitRecord& u : units_) {
        const bool outOfBounds = u.x >= width_ * 32 || u.y >= height_ * 32;
        bool duplicate = false;
        for (const UnitRecord& k : kept) {
            if (k.serial == u.serial) {
                duplicate = true;
                break;
            }
        }
        if (outOfBounds || duplicate) {
            removed++;
            continue;
        }
        kept.push_back(u);
    }

    if (removed > 0) {
        units_ = std::move(kept);
        dirty_ = true;
        Log(L"[unit] removed %d invalid units", removed);
    }
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
        WriteU16(p + 10, r.unused0);
        WriteU16(p + 12, r.validState);
        WriteU16(p + 14, r.validProps);
        p[16] = r.owner;
        p[17] = r.hpPercent;
        p[18] = r.shieldPercent;
        p[19] = r.energyPercent;
        WriteU32(p + 20, r.resource);
        WriteU16(p + 24, r.stackCount);
        WriteU16(p + 26, r.queueCount);
        std::memcpy(p + 28, r.reserved, 8);
    }
    if (chk_.Has(section::kUnit) || !units_.empty()) {
        chk_.Set(section::kUnit, unitBytes);
    }

    if (chk_.Has("TRIG")) chk_.Set("TRIG", triggers_.Serialize());
    if (chk_.Has("MRGN") || locations_.Count() > 0) chk_.Set("MRGN", locations_.Serialize());
    if (chk_.Has("STR ") || strings_.Count() > 0) chk_.Set("STR ", strings_.Serialize());
    if (players_.Modified()) players_.SaveTo(chk_);
    if (restrictions_.Modified()) restrictions_.SaveTo(chk_);
    // 可选段为空表示“应不存在”，直接删除而不是写出零长段
    auto applyOptional = [this](const char* name, bool modified, Buffer bytes) {
        if (!modified) return;
        if (bytes.empty()) chk_.Remove(name);
        else chk_.Set(name, std::move(bytes));
    };
    applyOptional(section::kDd2, doodads_.Modified(), doodads_.Serialize());
    applyOptional(section::kMask, fog_.Modified(), fog_.Serialize());
    applyOptional(section::kThg2, sprites_.Modified(), sprites_.Serialize());
    applyOptional(section::kWav, sounds_.Modified(), sounds_.Serialize());
    applyOptional(section::kSwnm, switchNames_.Modified(), switchNames_.Serialize());
    if (chk_.Has(section::kMbrf) || briefing_.BlockCount() > 0) {
        chk_.Set(section::kMbrf, briefing_.Serialize());
    }

    const Buffer chkBytes = chk_.Serialize();
    std::vector<MpqFileEntry> files;

    // listfile 覆盖 CHK 与全部额外文件，避免保存后自定义 BIN 变成孤儿条目
    std::string list = "staredit\\scenario.chk\r\n";
    for (const auto& extra : extraFiles_) {
        list += extra.name;
        list += "\r\n";
    }
    files.push_back({"(listfile)", Buffer(list.begin(), list.end())});
    files.push_back({"staredit\\scenario.chk", chkBytes});
    for (const auto& extra : extraFiles_) files.push_back(extra);

    if (!MpqArchive::WriteArchive(path, files)) {
        Log(L"[map] 写出失败: %s", path.c_str());
        return false;
    }

    dirty_ = false;
    path_ = path;
    opened_ = true;
    Log(L"[map] saved %s  chk=%u bytes", path.c_str(), static_cast<unsigned>(chkBytes.size()));
    return true;
}

}  // namespace scmd
