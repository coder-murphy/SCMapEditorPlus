#include "players/restriction_model.h"

#include <cstring>
#include <utility>

namespace scmd {

namespace {
std::size_t TableSize(int count) {
    return static_cast<std::size_t>(RestrictionTable::kPlayers) * count * 3 +
           static_cast<std::size_t>(count) * 2;
}

u8 GetAt(const std::vector<u8>& data, int at) {
    return at >= 0 && static_cast<std::size_t>(at) < data.size() ? data[static_cast<std::size_t>(at)] : 0;
}

void SetAt(std::vector<u8>& data, int at, u8 value) {
    if (at >= 0 && static_cast<std::size_t>(at) < data.size()) data[static_cast<std::size_t>(at)] = value;
}
}  // namespace

void RestrictionTable::Reset(int count) {
    itemCount = count;
    present = false;
    modified = false;
    const std::size_t perPlayer = static_cast<std::size_t>(kPlayers) * count;
    primary.assign(perPlayer, 0);
    secondary.assign(perPlayer, 0);
    globalPrimary.assign(count, 0);
    globalSecondary.assign(count, 0);
    useDefaults.assign(perPlayer, 1);
}

bool RestrictionTable::Parse(const Buffer& data, int count) {
    Reset(count);
    if (count <= 0 || data.size() != TableSize(count)) return false;

    const std::size_t perPlayer = static_cast<std::size_t>(kPlayers) * count;
    std::size_t at = 0;
    std::memcpy(primary.data(), data.data() + at, perPlayer);
    at += perPlayer;
    std::memcpy(secondary.data(), data.data() + at, perPlayer);
    at += perPlayer;
    std::memcpy(globalPrimary.data(), data.data() + at, count);
    at += count;
    std::memcpy(globalSecondary.data(), data.data() + at, count);
    at += count;
    std::memcpy(useDefaults.data(), data.data() + at, perPlayer);

    present = true;
    modified = false;
    return true;
}

Buffer RestrictionTable::Serialize() const {
    if (itemCount <= 0) return Buffer();
    const std::size_t perPlayer = static_cast<std::size_t>(kPlayers) * itemCount;
    Buffer out(TableSize(itemCount), 0);
    std::size_t at = 0;
    std::memcpy(out.data() + at, primary.data(), perPlayer);
    at += perPlayer;
    std::memcpy(out.data() + at, secondary.data(), perPlayer);
    at += perPlayer;
    std::memcpy(out.data() + at, globalPrimary.data(), itemCount);
    at += itemCount;
    std::memcpy(out.data() + at, globalSecondary.data(), itemCount);
    at += itemCount;
    std::memcpy(out.data() + at, useDefaults.data(), perPlayer);
    return out;
}

u8 RestrictionTable::Primary(int player, int item) const {
    return GetAt(primary, player * itemCount + item);
}
u8 RestrictionTable::Secondary(int player, int item) const {
    return GetAt(secondary, player * itemCount + item);
}
u8 RestrictionTable::GlobalPrimary(int item) const { return GetAt(globalPrimary, item); }
u8 RestrictionTable::GlobalSecondary(int item) const { return GetAt(globalSecondary, item); }
u8 RestrictionTable::UseDefaults(int player, int item) const {
    return GetAt(useDefaults, player * itemCount + item);
}

void RestrictionTable::SetPrimary(int player, int item, u8 value) {
    SetAt(primary, player * itemCount + item, value);
    modified = true;
}
void RestrictionTable::SetSecondary(int player, int item, u8 value) {
    SetAt(secondary, player * itemCount + item, value);
    modified = true;
}
void RestrictionTable::SetGlobalPrimary(int item, u8 value) {
    SetAt(globalPrimary, item, value);
    modified = true;
}
void RestrictionTable::SetGlobalSecondary(int item, u8 value) {
    SetAt(globalSecondary, item, value);
    modified = true;
}
void RestrictionTable::SetUseDefaults(int player, int item, u8 value) {
    SetAt(useDefaults, player * itemCount + item, value);
    modified = true;
}

void UpgradeTechRestrictions::Reset() {
    upgr.Reset(kUpgradeCountVanilla);
    ptec.Reset(kTechCountVanilla);
    pupx.Reset(kUpgradeCountBW);
    ptex.Reset(kTechCountBW);
}

bool UpgradeTechRestrictions::Parse(const ChkDocument& chk) {
    Reset();
    bool any = false;
    if (const Buffer* data = chk.Find("UPGR"); data && upgr.Parse(*data, kUpgradeCountVanilla)) any = true;
    if (const Buffer* data = chk.Find("PTEC"); data && ptec.Parse(*data, kTechCountVanilla)) any = true;
    if (const Buffer* data = chk.Find("PUPx"); data && pupx.Parse(*data, kUpgradeCountBW)) any = true;
    if (const Buffer* data = chk.Find("PTEx"); data && ptex.Parse(*data, kTechCountBW)) any = true;
    return any;
}

void UpgradeTechRestrictions::SaveTo(ChkDocument& chk) const {
    if (upgr.modified) chk.Set("UPGR", upgr.Serialize());
    if (ptec.modified) chk.Set("PTEC", ptec.Serialize());
    if (pupx.modified) chk.Set("PUPx", pupx.Serialize());
    if (ptex.modified) chk.Set("PTEx", ptex.Serialize());
}

bool UpgradeTechRestrictions::Modified() const {
    return upgr.modified || ptec.modified || pupx.modified || ptex.modified;
}

bool UpgradeTechRestrictions::HasVanilla() const { return upgr.present || ptec.present; }
bool UpgradeTechRestrictions::HasBroodWar() const { return pupx.present || ptex.present; }

RestrictionTable& UpgradeTechRestrictions::Table(bool broodWar, bool tech) {
    return broodWar ? (tech ? ptex : pupx) : (tech ? ptec : upgr);
}

const RestrictionTable& UpgradeTechRestrictions::Table(bool broodWar, bool tech) const {
    return broodWar ? (tech ? ptex : pupx) : (tech ? ptec : upgr);
}

Buffer UpgradeTechRestrictions::Snapshot() const {
    Buffer out;
    out.push_back('R');
    out.push_back('S');
    out.push_back('T');
    out.push_back('1');
    out.push_back(1);
    u8 mask = 0;
    if (upgr.present || upgr.modified) mask |= 1u << 0;
    if (ptec.present || ptec.modified) mask |= 1u << 1;
    if (pupx.present || pupx.modified) mask |= 1u << 2;
    if (ptex.present || ptex.modified) mask |= 1u << 3;
    out.push_back(mask);
    auto append = [&out](const RestrictionTable& table) {
        const Buffer data = table.Serialize();
        out.insert(out.end(), data.begin(), data.end());
    };
    if (upgr.present || upgr.modified) append(upgr);
    if (ptec.present || ptec.modified) append(ptec);
    if (pupx.present || pupx.modified) append(pupx);
    if (ptex.present || ptex.modified) append(ptex);
    return out;
}

bool UpgradeTechRestrictions::Restore(const Buffer& snapshot) {
    if (snapshot.size() < 6 || snapshot[0] != 'R' || snapshot[1] != 'S' ||
        snapshot[2] != 'T' || snapshot[3] != '1' || snapshot[4] != 1) {
        return false;
    }
    const u8 mask = snapshot[5];
    std::size_t at = 6;
    auto readTable = [&snapshot, &at, mask](RestrictionTable& table, int count, u8 bit) -> bool {
        table.Reset(count);
        if ((mask & bit) == 0) return true;
        const std::size_t need = TableSize(count);
        if (at + need > snapshot.size()) return false;
        Buffer data(snapshot.begin() + static_cast<std::ptrdiff_t>(at),
                    snapshot.begin() + static_cast<std::ptrdiff_t>(at + need));
        at += need;
        if (!table.Parse(data, count)) return false;
        table.modified = true;
        return true;
    };
    if (!readTable(upgr, kUpgradeCountVanilla, 1u << 0)) return false;
    if (!readTable(ptec, kTechCountVanilla, 1u << 1)) return false;
    if (!readTable(pupx, kUpgradeCountBW, 1u << 2)) return false;
    if (!readTable(ptex, kTechCountBW, 1u << 3)) return false;
    return true;
}

const wchar_t* RestrictionSectionName(bool broodWar, bool tech) {
    return broodWar ? (tech ? L"PTEx" : L"PUPx") : (tech ? L"PTEC" : L"UPGR");
}

}  // namespace scmd
