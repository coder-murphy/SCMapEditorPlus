#include "ui/dialogs.h"

#include "ui/dialog_registry.h"

namespace scmd {

bool ShowRecoveredDialog(int dialogId, HWND owner) {
    return ShowDialog(dialogId, owner);
}

}  // namespace scmd
