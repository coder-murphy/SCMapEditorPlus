#include "map/edit_history.h"

#include "map/map_document.h"

namespace scmd {

void EditHistory::Clear() {
    undo_.clear();
    redo_.clear();
}

void EditHistory::Push(EditCommand command) {
    redo_.clear();
    undo_.push_back(std::move(command));
    while (undo_.size() > limit_) {
        undo_.erase(undo_.begin());
    }
}

void EditHistory::ApplyMeta(MapDocument& doc, const EditCommand& cmd, bool before) {
    if (cmd.hasMeta) {
        const Buffer& trig = before ? cmd.triggersBefore : cmd.triggersAfter;
        const Buffer& mrgn = before ? cmd.locationsBefore : cmd.locationsAfter;
        const Buffer& str = before ? cmd.stringsBefore : cmd.stringsAfter;
        doc.mutableTriggers().Parse(trig);
        doc.mutableLocations().Parse(mrgn);
        doc.mutableStrings().Parse(str);
    }
    if (cmd.hasPlayers) {
        doc.mutablePlayers().Restore(before ? cmd.playersBefore : cmd.playersAfter);
    }
    if (cmd.hasRestrictions) {
        doc.mutableRestrictions().Restore(before ? cmd.restrictionsBefore : cmd.restrictionsAfter);
    }
    if (cmd.hasDecor) {
        const Buffer& doodads = before ? cmd.doodadsBefore : cmd.doodadsAfter;
        const Buffer& fog = before ? cmd.fogBefore : cmd.fogAfter;
        const Buffer& sprites = before ? cmd.spritesBefore : cmd.spritesAfter;
        // Parse 会清除 modified_；快照为空（例如撤销“清空全部”）也要写回，
        // 否则保存时会继续沿用 CHK 里的旧段内容
        doc.mutableDoodads().Parse(doodads);
        doc.mutableDoodads().MarkModified();
        doc.mutableFog().Parse(fog, doc.width(), doc.height());
        doc.mutableFog().MarkModified();
        doc.mutableSprites().Parse(sprites);
        doc.mutableSprites().MarkModified();
    }
    if (cmd.hasScenarioExtra) {
        const Buffer& wav = before ? cmd.wavBefore : cmd.wavAfter;
        const Buffer& swnm = before ? cmd.swnmBefore : cmd.swnmAfter;
        const Buffer& mbrf = before ? cmd.mbrfBefore : cmd.mbrfAfter;
        doc.mutableSounds().Parse(wav);
        doc.mutableSounds().MarkModified();
        doc.mutableSwitchNames().Parse(swnm);
        doc.mutableSwitchNames().MarkModified();
        doc.mutableBriefing().Parse(mbrf);
    }
}

bool EditHistory::Undo(MapDocument& doc) {
    if (undo_.empty()) return false;
    EditCommand cmd = std::move(undo_.back());
    undo_.pop_back();

    doc.mutableTiles() = cmd.tilesBefore;
    doc.mutableUnits() = cmd.unitsBefore;
    ApplyMeta(doc, cmd, true);
    doc.MarkDirty();

    redo_.push_back(std::move(cmd));
    return true;
}

bool EditHistory::Redo(MapDocument& doc) {
    if (redo_.empty()) return false;
    EditCommand cmd = std::move(redo_.back());
    redo_.pop_back();

    doc.mutableTiles() = cmd.tilesAfter;
    doc.mutableUnits() = cmd.unitsAfter;
    ApplyMeta(doc, cmd, false);
    doc.MarkDirty();

    undo_.push_back(std::move(cmd));
    return true;
}

const wchar_t* EditHistory::LastLabel() const {
    return undo_.empty() ? L"" : undo_.back().label.c_str();
}

}  // namespace scmd
