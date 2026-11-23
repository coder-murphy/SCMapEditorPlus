#pragma once
#include <string>
#include <vector>

#include "common/types.h"

namespace scmd {

// units.dat 的单单位属性（列式存储，228 个单元）
struct UnitData {
    int id = 0;
    int graphic = 0;        // images.dat 下标
    int hp = 0;             // 已从 1/256 定点还原
    int shields = 0;
    bool shieldEnabled = false;
    int armor = 0;
    int minerals = 0;
    int gas = 0;
    int buildTime = 0;
    int sight = 0;
    int size = 0;
    int width = 0;          // 放置/碰撞矩形
    int height = 0;
    int circleWidth = 0;
    int circleHeight = 0;
    int portrait = 0;
    int movementType = 0;
    int weaponGround = 0;
    int weaponAir = 0;
    u32 specialFlags = 0;
    bool broodWar = false;
};

class UnitDataTable {
public:
    // 标准 units.dat 为 228 项列式数据（19876 B）
    bool Load(const std::wstring& path);
    bool Parse(const Buffer& data);
    bool loaded() const { return loaded_; }
    int count() const { return static_cast<int>(units_.size()); }
    const UnitData* Get(int id) const;
    const std::vector<UnitData>& items() const { return units_; }

    // 依次尝试：星际目录 / exe 目录 / 仓库 mpq（BrooDat 优先）中的 arr\units.dat
    static bool LoadAuto(UnitDataTable& out, std::wstring& source);

private:
    std::vector<UnitData> units_;
    bool loaded_ = false;
};

}  // namespace scmd
