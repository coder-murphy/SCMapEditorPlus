#pragma once
#include <string>
#include <vector>

#include "common/types.h"
#include "units/unit_model.h"

namespace scmd {

class MapDocument;

// 一次编辑的完整前后快照。
// 说明：地图上限 256x256（MTXM 128 KB），单位数百条，快照式实现换取正确性；
// 若日后需要更低内存，可替换为增量（tile index / unit index）记录。
struct EditCommand {
    std::wstring label;
    Buffer tilesBefore;
    Buffer tilesAfter;
    std::vector<UnitRecord> unitsBefore;
    std::vector<UnitRecord> unitsAfter;
    // TRIG / MRGN / STR 的序列化快照；hasMeta=false 表示该命令不涉及元数据
    bool hasMeta = false;
    Buffer triggersBefore;
    Buffer triggersAfter;
    Buffer locationsBefore;
    Buffer locationsAfter;
    Buffer stringsBefore;
    Buffer stringsAfter;
    // OWNR / SIDE / FORC / COLR 的 PlayerRoster::Snapshot()
    bool hasPlayers = false;
    Buffer playersBefore;
    Buffer playersAfter;
    // UPGR / PTEC / PUPx / PTEx 的 UpgradeTechRestrictions::Snapshot()
    bool hasRestrictions = false;
    Buffer restrictionsBefore;
    Buffer restrictionsAfter;
    // DD2 装饰物 / MASK 战争迷雾
    bool hasDecor = false;
    Buffer doodadsBefore;
    Buffer doodadsAfter;
    Buffer fogBefore;
    Buffer fogAfter;
    // SP 单位（THG2）；hasSprites=false 时 Undo/Redo 不触碰该层
    bool hasSprites = false;
    Buffer spritesBefore;      // THG2
    Buffer spritesAfter;
    // WAV / SWNM / MBRF
    bool hasScenarioExtra = false;
    Buffer wavBefore;
    Buffer wavAfter;
    Buffer swnmBefore;
    Buffer swnmAfter;
    Buffer mbrfBefore;
    Buffer mbrfAfter;
};

class EditHistory {
public:
    void SetLimit(std::size_t limit) { limit_ = limit; }
    void Clear();

    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    std::size_t UndoCount() const { return undo_.size(); }
    std::size_t RedoCount() const { return redo_.size(); }

    void Push(EditCommand command);
    bool Undo(MapDocument& doc);
    bool Redo(MapDocument& doc);

    // 最近一条命令的标签（用于 HUD）
    const wchar_t* LastLabel() const;

private:
    static void ApplyMeta(MapDocument& doc, const EditCommand& cmd, bool before);

    std::vector<EditCommand> undo_;
    std::vector<EditCommand> redo_;
    std::size_t limit_ = 50;
};

}  // namespace scmd
