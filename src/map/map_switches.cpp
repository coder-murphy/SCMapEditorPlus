#include "map/map_switches.h"

#include <algorithm>
#include <random>
#include <vector>

#include "map/map_document.h"

namespace scmd {

namespace {

bool PlayerInRange(int player) { return player >= 0 && player <= 7; }

bool HasRevealerAt(const MapDocument& doc, int player, u16 x, u16 y) {
    for (const auto& u : doc.units()) {
        if (u.type == kMapRevealerUnitType && u.owner == player && u.x == x && u.y == y) {
            return true;
        }
    }
    return false;
}

}  // namespace

int PlaceMapRevealers(MapDocument& doc, int player) {
    if (!doc.IsOpen() || (player != -1 && !PlayerInRange(player))) return 0;

    int added = 0;
    const int first = player < 0 ? 0 : player;
    const int last = player < 0 ? 7 : player;
    for (int p = first; p <= last; p++) {
        for (int ty = kMapRevealerGridStep; ty < doc.height(); ty += kMapRevealerGridStep) {
            for (int tx = kMapRevealerGridStep; tx < doc.width(); tx += kMapRevealerGridStep) {
                if (static_cast<int>(doc.units().size()) >= kMapUnitSoftLimit) {
                    if (added > 0) doc.MarkDirty();
                    return added;
                }
                const u16 x = static_cast<u16>(tx * kTilePixels);
                const u16 y = static_cast<u16>(ty * kTilePixels + kTilePixels / 2);
                if (HasRevealerAt(doc, p, x, y)) continue;
                if (doc.PlaceUnit(tx, ty, kMapRevealerUnitType, static_cast<u8>(p), 0, true)) {
                    added++;
                }
            }
        }
    }
    if (added > 0) doc.MarkDirty();
    return added;
}

int RemoveMapRevealers(MapDocument& doc, int player) {
    if (!doc.IsOpen() || (player != -1 && !PlayerInRange(player))) return 0;

    auto& units = doc.mutableUnits();
    const std::size_t before = units.size();
    units.erase(std::remove_if(units.begin(), units.end(),
                               [player](const UnitRecord& u) {
                                   if (u.type != kMapRevealerUnitType) return false;
                                   if (player < 0) return u.owner < 8;   // 所有玩家
                                   return u.owner == player;
                               }),
                units.end());
    const int removed = static_cast<int>(before - units.size());
    if (removed > 0) doc.MarkDirty();
    return removed;
}

bool SetFogForPlayers(MapDocument& doc, int player, bool fogged) {
    if (!doc.IsOpen()) return false;
    if (player == -1) {
        doc.mutableFog().SetAll(fogged ? 0xFF : 0x00, doc.width(), doc.height());
        doc.MarkDirty();
        return true;
    }
    if (!PlayerInRange(player)) return false;
    const bool changed = doc.mutableFog().SetPlayerFog(player, fogged, doc.width(), doc.height());
    if (changed) doc.MarkDirty();
    return changed;
}

int RandomizeMineralTypes(MapDocument& doc) {
    if (!doc.IsOpen()) return 0;
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> pick(176, 178);
    int changed = 0;
    for (auto& u : doc.mutableUnits()) {
        if (u.type < 176 || u.type > 178) continue;
        const u16 next = static_cast<u16>(pick(rng));
        if (next != u.type) {
            u.type = next;
            changed++;
        }
    }
    if (changed > 0) doc.MarkDirty();
    return changed;
}

}  // namespace scmd
