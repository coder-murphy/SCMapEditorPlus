#pragma once
#include <array>
#include <vector>

#include "chk/chk_document.h"
#include "common/types.h"

namespace scmd {

// 单张限制表：UPGR / PTEC / PUPx / PTEx 共用布局
//   u8[12][N] primary   （升级最大等级 / 科技可用）
//   u8[12][N] secondary （升级初始等级 / 科技初始拥有）
//   u8[N]     globalPrimary
//   u8[N]     globalSecondary
//   u8[12][N] useDefaults
struct RestrictionTable {
    int itemCount = 0;
    bool present = false;
    bool modified = false;

    std::vector<u8> primary;
    std::vector<u8> secondary;
    std::vector<u8> globalPrimary;
    std::vector<u8> globalSecondary;
    std::vector<u8> useDefaults;

    static constexpr int kPlayers = 12;

    void Reset(int count);
    bool Parse(const Buffer& data, int count);
    Buffer Serialize() const;

    u8 Primary(int player, int item) const;
    u8 Secondary(int player, int item) const;
    u8 GlobalPrimary(int item) const;
    u8 GlobalSecondary(int item) const;
    u8 UseDefaults(int player, int item) const;

    void SetPrimary(int player, int item, u8 value);
    void SetSecondary(int player, int item, u8 value);
    void SetGlobalPrimary(int item, u8 value);
    void SetGlobalSecondary(int item, u8 value);
    void SetUseDefaults(int player, int item, u8 value);
};

// 四张限制表的集合；原版用 UPGR(46)/PTEC(24)，资料片用 PUPx(61)/PTEx(44)。
class UpgradeTechRestrictions {
public:
    static constexpr int kUpgradeCountVanilla = 46;
    static constexpr int kTechCountVanilla = 24;
    static constexpr int kUpgradeCountBW = 61;
    static constexpr int kTechCountBW = 44;

    bool Parse(const ChkDocument& chk);
    void SaveTo(ChkDocument& chk) const;
    void Reset();

    bool Modified() const;
    bool HasVanilla() const;
    bool HasBroodWar() const;

    RestrictionTable& Table(bool broodWar, bool tech);
    const RestrictionTable& Table(bool broodWar, bool tech) const;

    Buffer Snapshot() const;
    bool Restore(const Buffer& snapshot);

    RestrictionTable upgr;
    RestrictionTable ptec;
    RestrictionTable pupx;
    RestrictionTable ptex;
};

const wchar_t* RestrictionSectionName(bool broodWar, bool tech);

}  // namespace scmd
