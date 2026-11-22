#include "players/player_model.h"

#include <cstring>
#include <utility>

namespace scmd {

namespace {
constexpr u8 kSnapshotMaskOwnr = 1u << 0;
constexpr u8 kSnapshotMaskSide = 1u << 1;
constexpr u8 kSnapshotMaskForc = 1u << 2;
constexpr u8 kSnapshotMaskColr = 1u << 3;
constexpr std::size_t kSnapshotSize = 4 + 1 + 1 + 12 + 12 + 8 + 8 + 4 * 3;

u16 ReadU16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }
void WriteU16(u8* p, u16 v) {
    p[0] = static_cast<u8>(v & 0xFF);
    p[1] = static_cast<u8>(v >> 8);
}
}  // namespace

void PlayerRoster::Reset() {
    players_.fill(PlayerSlot{});
    forces_.fill(ForceSettings{});
    for (int i = 0; i < kPlayerSlots; i++) {
        players_[i].controller = static_cast<u8>(PlayerController::kInactive);
        players_[i].race = static_cast<u8>(PlayerRace::kInactive);
        players_[i].force = 0;
        players_[i].color = static_cast<u8>(i < kPlayers ? i : 0);
    }
    // 规范：9-11 默认不活动，12 默认中立
    players_[11].race = static_cast<u8>(PlayerRace::kUnknownNeutral);
    hasOwnr_ = hasSide_ = hasForc_ = hasColr_ = false;
    modified_ = false;
}

bool PlayerRoster::Parse(const ChkDocument& chk) {
    Reset();
    bool any = false;

    if (const Buffer* ownr = chk.Find("OWNR"); ownr && ownr->size() == 12) {
        for (int i = 0; i < kPlayerSlots; i++) players_[i].controller = (*ownr)[i];
        hasOwnr_ = true;
        any = true;
    }
    if (const Buffer* side = chk.Find("SIDE"); side && side->size() == 12) {
        for (int i = 0; i < kPlayerSlots; i++) players_[i].race = (*side)[i];
        hasSide_ = true;
        any = true;
    }
    if (const Buffer* colr = chk.Find("COLR"); colr && colr->size() == 8) {
        for (int i = 0; i < kPlayers; i++) players_[i].color = (*colr)[i];
        hasColr_ = true;
        any = true;
    }
    if (const Buffer* forc = chk.Find("FORC"); forc && forc->size() == 20) {
        for (int i = 0; i < kPlayers; i++) players_[i].force = (*forc)[i];
        for (int i = 0; i < kForces; i++) {
            forces_[i].nameStringId = ReadU16(forc->data() + 8 + i * 2);
            forces_[i].flags = (*forc)[16 + i];
        }
        hasForc_ = true;
        any = true;
    }

    modified_ = false;
    return any;
}

void PlayerRoster::SaveTo(ChkDocument& chk) const {
    if (!modified_) return;

    Buffer ownr(12, 0);
    Buffer side(12, 0);
    Buffer colr(8, 0);
    Buffer forc(20, 0);
    for (int i = 0; i < kPlayerSlots; i++) {
        ownr[i] = players_[i].controller;
        side[i] = players_[i].race;
    }
    for (int i = 0; i < kPlayers; i++) {
        colr[i] = players_[i].color;
        forc[i] = players_[i].force;
    }
    for (int i = 0; i < kForces; i++) {
        WriteU16(forc.data() + 8 + i * 2, forces_[i].nameStringId);
        forc[16 + i] = forces_[i].flags;
    }

    chk.Set("OWNR", std::move(ownr));
    chk.Set("SIDE", std::move(side));
    chk.Set("COLR", std::move(colr));
    chk.Set("FORC", std::move(forc));
}

const PlayerSlot& PlayerRoster::player(int index) const {
    static const PlayerSlot kEmpty;
    return index >= 0 && index < kPlayerSlots ? players_[index] : kEmpty;
}

PlayerSlot& PlayerRoster::mutablePlayer(int index) {
    static PlayerSlot kScratch;
    if (index < 0 || index >= kPlayerSlots) return kScratch;
    modified_ = true;
    return players_[index];
}

const ForceSettings& PlayerRoster::force(int index) const {
    static const ForceSettings kEmpty;
    return index >= 0 && index < kForces ? forces_[index] : kEmpty;
}

ForceSettings& PlayerRoster::mutableForce(int index) {
    static ForceSettings kScratch;
    if (index < 0 || index >= kForces) return kScratch;
    modified_ = true;
    return forces_[index];
}

void PlayerRoster::SetController(int index, u8 value) {
    if (index < 0 || index >= kPlayerSlots) return;
    players_[index].controller = value;
    modified_ = true;
}
void PlayerRoster::SetRace(int index, u8 value) {
    if (index < 0 || index >= kPlayerSlots) return;
    players_[index].race = value;
    modified_ = true;
}
void PlayerRoster::SetColor(int index, u8 value) {
    if (index < 0 || index >= kPlayers) return;
    players_[index].color = value;
    modified_ = true;
}
void PlayerRoster::SetForce(int index, u8 value) {
    if (index < 0 || index >= kPlayers) return;
    players_[index].force = value < kForces ? value : 0;
    modified_ = true;
}
void PlayerRoster::SetForceNameId(int index, u16 value) {
    if (index < 0 || index >= kForces) return;
    forces_[index].nameStringId = value;
    modified_ = true;
}
void PlayerRoster::SetForceFlags(int index, u8 value) {
    if (index < 0 || index >= kForces) return;
    forces_[index].flags = value;
    modified_ = true;
}

Buffer PlayerRoster::Snapshot() const {
    Buffer out(kSnapshotSize, 0);
    std::memcpy(out.data(), "PLR1", 4);
    out[4] = 1;
    u8 mask = 0;
    if (hasOwnr_) mask |= kSnapshotMaskOwnr;
    if (hasSide_) mask |= kSnapshotMaskSide;
    if (hasForc_) mask |= kSnapshotMaskForc;
    if (hasColr_) mask |= kSnapshotMaskColr;
    out[5] = mask;

    std::size_t at = 6;
    for (int i = 0; i < kPlayerSlots; i++) out[at++] = players_[i].controller;
    for (int i = 0; i < kPlayerSlots; i++) out[at++] = players_[i].race;
    for (int i = 0; i < kPlayers; i++) out[at++] = players_[i].color;
    for (int i = 0; i < kPlayers; i++) out[at++] = players_[i].force;
    for (int i = 0; i < kForces; i++) {
        WriteU16(out.data() + at, forces_[i].nameStringId);
        at += 2;
        out[at++] = forces_[i].flags;
    }
    return out;
}

bool PlayerRoster::Restore(const Buffer& snapshot) {
    if (snapshot.size() != kSnapshotSize) return false;
    if (std::memcmp(snapshot.data(), "PLR1", 4) != 0 || snapshot[4] != 1) return false;

    const u8 mask = snapshot[5];
    hasOwnr_ = (mask & kSnapshotMaskOwnr) != 0;
    hasSide_ = (mask & kSnapshotMaskSide) != 0;
    hasForc_ = (mask & kSnapshotMaskForc) != 0;
    hasColr_ = (mask & kSnapshotMaskColr) != 0;

    std::size_t at = 6;
    for (int i = 0; i < kPlayerSlots; i++) players_[i].controller = snapshot[at++];
    for (int i = 0; i < kPlayerSlots; i++) players_[i].race = snapshot[at++];
    for (int i = 0; i < kPlayers; i++) players_[i].color = snapshot[at++];
    for (int i = 0; i < kPlayers; i++) players_[i].force = snapshot[at++];
    for (int i = 0; i < kForces; i++) {
        forces_[i].nameStringId = ReadU16(snapshot.data() + at);
        at += 2;
        forces_[i].flags = snapshot[at++];
    }
    modified_ = true;
    return true;
}

const wchar_t* PlayerControllerName(u8 value) {
    static const wchar_t* const kNames[] = {
        L"未启用", L"电脑(游戏)", L"人类玩家", L"Rescue Passive", L"未使用",
        L"电脑", L"人类(开放槽)", L"中立", L"关闭槽",
    };
    return value < sizeof(kNames) / sizeof(kNames[0]) ? kNames[value] : L"未知";
}

const wchar_t* PlayerRaceName(u8 value) {
    static const wchar_t* const kNames[] = {
        L"虫族", L"人族", L"神族", L"未知(独立)", L"未知(中立)",
        L"用户选择", L"随机", L"不活动",
    };
    return value < sizeof(kNames) / sizeof(kNames[0]) ? kNames[value] : L"未知";
}

const wchar_t* PlayerColorName(u8 value) {
    static const wchar_t* const kNames[] = {
        L"红", L"蓝", L"青", L"紫", L"橙", L"棕", L"白", L"黄",
        L"绿", L"淡黄", L"棕褐", L"水蓝", L"淡绿", L"蓝紫", L"粉", L"深青",
    };
    return value < sizeof(kNames) / sizeof(kNames[0]) ? kNames[value] : L"颜色?";
}

}  // namespace scmd
