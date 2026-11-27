#include "ui/doodad_palette.h"

#include <cwchar>
#include <map>

#include "common/log.h"
#include "map/decor_model.h"
#include "ui/dlg_common.h"

namespace scmd {

namespace {
constexpr const wchar_t* kClassName = L"SCMDPlusDoodadPalette";
constexpr const wchar_t* kWindowTitle = L"装饰物";
constexpr int kClientWidth = 320;
constexpr int kClientHeight = 306;

HWND CreateChild(HWND parent, HINSTANCE instance, const wchar_t* cls, const wchar_t* text,
                 DWORD style, int x, int y, int w, int h, int id, DWORD exStyle = 0) {
    HWND child = ::CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                                   parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   instance, nullptr);
    if (child) {
        ::SendMessageW(child, WM_SETFONT,
                       reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }
    return child;
}

bool Checked(HWND hwnd) {
    return ::SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void SetCheck(HWND hwnd, bool on) {
    ::SendMessageW(hwnd, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}
}  // namespace

bool DoodadPalette::Create(HINSTANCE instance, HWND ownerWnd) {
    if (hwnd_) return true;
    instance_ = instance;
    ownerWnd_ = ownerWnd;
    lastError_ = 0;

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = &DoodadPalette::WndProcThunk;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        lastError_ = ::GetLastError();
        Log(L"[doodad] RegisterClass 失败 %u", lastError_);
        return false;
    }

    RECT rc = {0, 0, kClientWidth, kClientHeight};
    ::AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_TOOLWINDOW);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;

    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    RECT ownerRect;
    if (ownerWnd && ::GetWindowRect(ownerWnd, &ownerRect)) {
        x = ownerRect.right + 12;
        y = ownerRect.top + 40;
        ClampWindowToWorkArea(x, y, width, height);
    }

    hwnd_ = ::CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, kWindowTitle, WS_OVERLAPPEDWINDOW,
                              x, y, width, height, ownerWnd, nullptr, instance, this);
    if (!hwnd_) {
        lastError_ = ::GetLastError();
        Log(L"[doodad] CreateWindow 失败 %u", lastError_);
        return false;
    }
    CreateControls();
    return true;
}

bool DoodadPalette::Show(HINSTANCE instance, HWND ownerWnd, const DoodadTable* table, u16 type,
                         u8 ownerId, bool disabled, bool allowAnywhere) {
    if (!Create(instance, ownerWnd)) return false;
    type_ = type;
    owner_ = ownerId;
    disabled_ = disabled;
    allowAnywhere_ = allowAnywhere;
    Refresh(table, type, ownerId, disabled);
    ::ShowWindow(hwnd_, SW_SHOWNORMAL);
    ::SetForegroundWindow(hwnd_);
    return true;
}

void DoodadPalette::Refresh(const DoodadTable* table, u16 type, u8 ownerId, bool disabled) {
    table_ = table;
    type_ = type;
    owner_ = ownerId;
    disabled_ = disabled;
    if (!hwnd_) return;
    RebuildTypeList();
    SyncControls();
}

void DoodadPalette::SetAllowAnywhere(bool allow) {
    if (allowAnywhere_ == allow) return;
    allowAnywhere_ = allow;
    if (!hwnd_) return;
    updating_ = true;
    SetCheck(anywhereCheck_, allow);
    updating_ = false;
}

void DoodadPalette::Close() {
    if (hwnd_) ::DestroyWindow(hwnd_);
}

void DoodadPalette::CreateControls() {
    CreateChild(hwnd_, instance_, L"BUTTON", L"放置参数", BS_GROUPBOX, 8, 8, 304, 100, 0);

    CreateChild(hwnd_, instance_, L"STATIC", L"类型", SS_LEFT, 20, 31, 32, 18, 0);
    typeEdit_ = CreateChild(hwnd_, instance_, L"EDIT", L"0",
                            WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, 56, 28, 72, 22, kTypeEdit);
    CreateChild(hwnd_, instance_, L"STATIC", L"0-65535", SS_LEFT, 134, 31, 60, 18, 0);

    CreateChild(hwnd_, instance_, L"STATIC", L"玩家", SS_LEFT, 20, 62, 32, 18, 0);
    ownerCombo_ = CreateChild(hwnd_, instance_, L"COMBOBOX", L"",
                              WS_BORDER | WS_VSCROLL | CBS_DROPDOWNLIST, 56, 59, 90, 200,
                              kOwnerCombo);
    if (ownerCombo_) {
        for (int i = 0; i < 8; i++) {
            wchar_t label[32];
            ::wsprintfW(label, L"Player %d", i + 1);
            const LRESULT at = ::SendMessageW(ownerCombo_, CB_ADDSTRING, 0,
                                              reinterpret_cast<LPARAM>(label));
            if (at != CB_ERR) {
                ::SendMessageW(ownerCombo_, CB_SETITEMDATA, static_cast<WPARAM>(at),
                               static_cast<LPARAM>(i));
            }
        }
    }

    disabledCheck_ = CreateChild(hwnd_, instance_, L"BUTTON", L"禁用（不显示）",
                                 BS_AUTOCHECKBOX | WS_TABSTOP, 156, 61, 140, 20, kDisabledCheck);
    anywhereCheck_ = CreateChild(hwnd_, instance_, L"BUTTON", L"允许任意放置（0x8010）",
                                 BS_AUTOCHECKBOX | WS_TABSTOP, 16, 116, 200, 20, kAnywhereCheck);

    CreateChild(hwnd_, instance_, L"STATIC", L"地图中的类型（单击选中）", SS_LEFT, 16, 144, 220,
                18, 0);
    typeList_ = CreateChild(hwnd_, instance_, L"LISTBOX", L"",
                            WS_BORDER | WS_VSCROLL | LBS_NOTIFY, 12, 164, 296, 100, kTypeList);
    statusText_ = CreateChild(hwnd_, instance_, L"STATIC",
                              L"装饰物层：左键放置，右键删除", SS_LEFT, 12, 272, 296, 28,
                              kStatusText);
}

void DoodadPalette::RebuildTypeList() {
    if (!typeList_) return;
    ::SendMessageW(typeList_, LB_RESETCONTENT, 0, 0);
    types_.clear();
    if (!table_) return;

    std::map<u16, int> counts;
    for (const Doodad& d : table_->Items()) counts[d.type]++;
    types_.assign(counts.begin(), counts.end());

    LRESULT selected = LB_ERR;
    for (std::size_t i = 0; i < types_.size(); i++) {
        wchar_t label[64];
        ::wsprintfW(label, L"类型 %u  × %d", static_cast<unsigned>(types_[i].first),
                    types_[i].second);
        const LRESULT at = ::SendMessageW(typeList_, LB_ADDSTRING, 0,
                                          reinterpret_cast<LPARAM>(label));
        if (at != LB_ERR) {
            ::SendMessageW(typeList_, LB_SETITEMDATA, static_cast<WPARAM>(at),
                           static_cast<LPARAM>(types_[i].first));
            if (types_[i].first == type_) selected = at;
        }
    }
    if (selected != LB_ERR) {
        ::SendMessageW(typeList_, LB_SETCURSEL, static_cast<WPARAM>(selected), 0);
    }
}

void DoodadPalette::SyncControls() {
    updating_ = true;
    wchar_t text[16];
    ::wsprintfW(text, L"%u", static_cast<unsigned>(type_));
    ::SetWindowTextW(typeEdit_, text);
    if (ownerCombo_) {
        ::SendMessageW(ownerCombo_, CB_SETCURSEL,
                       owner_ <= 7 ? static_cast<WPARAM>(owner_) : static_cast<WPARAM>(-1), 0);
    }
    SetCheck(disabledCheck_, disabled_);
    SetCheck(anywhereCheck_, allowAnywhere_);
    updating_ = false;
}

void DoodadPalette::NotifyParams() {
    if (!ownerWnd_) return;
    ::PostMessageW(ownerWnd_, kMsgParamsChanged, 0, 0);
}

void DoodadPalette::SelectTypeFromList() {
    if (!typeList_) return;
    const LRESULT sel = ::SendMessageW(typeList_, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return;
    const LRESULT data = ::SendMessageW(typeList_, LB_GETITEMDATA, static_cast<WPARAM>(sel), 0);
    if (data == LB_ERR) return;
    type_ = static_cast<u16>(data);
    updating_ = true;
    wchar_t text[16];
    ::wsprintfW(text, L"%u", static_cast<unsigned>(type_));
    ::SetWindowTextW(typeEdit_, text);
    updating_ = false;
    NotifyParams();
}

LRESULT CALLBACK DoodadPalette::WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* self = reinterpret_cast<DoodadPalette*>(cs->lpCreateParams);
        if (self) self->hwnd_ = hwnd;   // CreateWindowEx 返回前即可用于 DefWindowProc
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<DoodadPalette*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->WndProc(msg, wParam, lParam) : ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT DoodadPalette::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND: {
            if (updating_) return 0;
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);
            switch (id) {
                case kTypeEdit:
                    if (code == EN_CHANGE) {
                        wchar_t text[16] = L"";
                        ::GetWindowTextW(typeEdit_, text, 16);
                        const unsigned long value = std::wcstoul(text, nullptr, 10);
                        type_ = static_cast<u16>(value > 0xFFFF ? 0xFFFF : value);
                        NotifyParams();
                    }
                    break;
                case kOwnerCombo:
                    if (code == CBN_SELCHANGE) {
                        const LRESULT sel =
                            ::SendMessageW(ownerCombo_, CB_GETCURSEL, 0, 0);
                        if (sel != CB_ERR) {
                            const LRESULT data = ::SendMessageW(
                                ownerCombo_, CB_GETITEMDATA, static_cast<WPARAM>(sel), 0);
                            if (data != CB_ERR) owner_ = static_cast<u8>(data);
                            NotifyParams();
                        }
                    }
                    break;
                case kDisabledCheck:
                    if (code == BN_CLICKED) {
                        disabled_ = Checked(disabledCheck_);
                        NotifyParams();
                    }
                    break;
                case kAnywhereCheck:
                    if (code == BN_CLICKED) {
                        allowAnywhere_ = Checked(anywhereCheck_);
                        if (ownerWnd_) {
                            ::PostMessageW(ownerWnd_, kMsgAllowAnywhere,
                                           allowAnywhere_ ? 1 : 0, 0);
                        }
                    }
                    break;
                case kTypeList:
                    if (code == LBN_SELCHANGE || code == LBN_DBLCLK) SelectTypeFromList();
                    break;
                default:
                    break;
            }
            return 0;
        }
        case WM_CLOSE:
            ::DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            hwnd_ = nullptr;
            ownerWnd_ = nullptr;
            instance_ = nullptr;
            typeEdit_ = nullptr;
            ownerCombo_ = nullptr;
            disabledCheck_ = nullptr;
            anywhereCheck_ = nullptr;
            typeList_ = nullptr;
            statusText_ = nullptr;
            table_ = nullptr;
            return 0;
        default:
            break;
    }
    return ::DefWindowProcW(hwnd_, msg, wParam, lParam);
}

}  // namespace scmd
