#pragma once
#include <array>

#include "chk/chk_document.h"
#include "common/types.h"

namespace scmd {

// OWNR 控制者枚举（VERIFIED，见 Staredit CHK 规范）
enum class PlayerController : u8 {
    kInactive = 0,
    kComputerGame = 1,
    kHuman = 2,
    kRescuePassive = 3,
    kUnused = 4,
    kComputer = 5,
    kHumanOpen = 6,
    kNeutral = 7,
    kClosed = 8,
};

// SIDE 种族枚举
enum class PlayerRace : u8 {
    kZerg = 0,
    kTerran = 1,
    kProtoss = 2,
    kUnknownIndependent = 3,
    kUnknownNeutral = 4,
    kUserSelect = 5,
    kRandom = 6,
    kInactive = 7,
};

struct PlayerSlot {
    u8 controller = 0;   // OWNR
    u8 race = 0;         // SIDE
    u8 color = 0;        // COLR（仅前 8 槽）
    u8 force = 0;        // FORC 玩家→队伍（0..3）
};

struct ForceSettings {
    u16 nameStringId = 0;   // FORC 的 STR 下标
    u8 flags = 0;           // FORC 的标志字节
};

// OWNR/SIDE/FORC/COLR 四段的完整模型。
// - 12 个 OWNR/SIDE 槽位（8 玩家 + 4 中立/关闭槽）
// - 4 个 Force，各自名称 STR 下标与标志
// - 未修改时 MapDocument 不重写这些段，保证无损往返
class PlayerRoster {
public:
    static constexpr int kPlayerSlots = 12;
    static constexpr int kPlayers = 8;
    static constexpr int kForces = 4;

    bool Parse(const ChkDocument& chk);
    void SaveTo(ChkDocument& chk) const;
    void Reset();

    bool Modified() const { return modified_; }
    void MarkModified() { modified_ = true; }

    const PlayerSlot& player(int index) const;
    PlayerSlot& mutablePlayer(int index);
    const ForceSettings& force(int index) const;
    ForceSettings& mutableForce(int index);

    void SetController(int index, u8 value);
    void SetRace(int index, u8 value);
    void SetColor(int index, u8 value);
    void SetForce(int index, u8 value);
    void SetForceNameId(int index, u16 value);
    void SetForceFlags(int index, u8 value);

    // 撤销快照：固定 58 字节，含 presence 与四段数据
    Buffer Snapshot() const;
    bool Restore(const Buffer& snapshot);

    bool hasOwnr() const { return hasOwnr_; }
    bool hasSide() const { return hasSide_; }
    bool hasForc() const { return hasForc_; }
    bool hasColr() const { return hasColr_; }

private:
    std::array<PlayerSlot, kPlayerSlots> players_{};
    std::array<ForceSettings, kForces> forces_{};
    bool hasOwnr_ = false;
    bool hasSide_ = false;
    bool hasForc_ = false;
    bool hasColr_ = false;
    bool modified_ = false;
};

const wchar_t* PlayerControllerName(u8 value);
const wchar_t* PlayerRaceName(u8 value);
const wchar_t* PlayerColorName(u8 value);

}  // namespace scmd
