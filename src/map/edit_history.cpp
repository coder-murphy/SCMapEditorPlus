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

bool EditHistory::Undo(MapDocument& doc) {
    if (undo_.empty()) return false;
    EditCommand cmd = std::move(undo_.back());
    undo_.pop_back();

    doc.mutableTiles() = cmd.tilesBefore;
    doc.mutableUnits() = cmd.unitsBefore;
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
    doc.MarkDirty();

    undo_.push_back(std::move(cmd));
    return true;
}

const wchar_t* EditHistory::LastLabel() const {
    return undo_.empty() ? L"" : undo_.back().label.c_str();
}

}  // namespace scmd
