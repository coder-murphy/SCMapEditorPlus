#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <cwchar>
#include <string>

#include "app/app_settings.h"
#include "tests/headless_tests.h"

namespace {

std::wstring ValueOf(const wchar_t* prefix) {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring value;
    const std::size_t len = std::wcslen(prefix);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], prefix, len) == 0) value = argv[i] + len;
        }
        ::LocalFree(argv);
    }
    return value;
}

bool HasArg(const wchar_t* name) {
    int argc = 0;
    bool want = false;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcscmp(argv[i], name) == 0) want = true;
        }
        ::LocalFree(argv);
    }
    return want;
}

// --ts 优先；否则用 exe 旁的 ts，再回退工程根目录的 ts
std::wstring ResolveTsDir() {
    std::wstring dir = ValueOf(L"--ts=");
    if (!dir.empty()) return dir;
    wchar_t exePath[MAX_PATH] = L"";
    ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t* slash = std::wcsrchr(exePath, L'\\');
    if (slash) *slash = L'\0';
    const std::wstring exeDir(exePath);
    dir = exeDir + L"\\ts";
    if (::GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const std::wstring rootTs = exeDir + L"\\..\\..\\..\\ts";
        wchar_t full[MAX_PATH] = L"";
        if (::GetFullPathNameW(rootTs.c_str(), MAX_PATH, full, nullptr) > 0 &&
            ::GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES) {
            dir = full;
        }
    }
    return dir;
}

int NoMap() {
    scmd::WriteHeadlessDiag(L"no-map\n");
    return 2;
}

}  // namespace

int main() {
    const std::wstring settingsDir = ValueOf(L"--settings-dir=");
    if (!settingsDir.empty()) scmd::AppSettings::Instance().SetDirectory(settingsDir);
    scmd::AppSettings::Instance().Load();

    const std::wstring tsDir = ResolveTsDir();
    const std::wstring mapPath = ValueOf(L"--open=");

    // 与 GUI 版 App::Run 相同的顺序，便于逐项对照
    if (HasArg(L"--recent-test")) return scmd::RunRecentTest();
    if (HasArg(L"--backup-test")) {
        return scmd::RunBackupTest(mapPath, ValueOf(L"--settings-dir="));
    }
    if (const std::wstring value = ValueOf(L"--iscript-test="); !value.empty()) {
        return scmd::RunIscriptTest(value, ValueOf(L"--grp="));
    }
    if (const std::wstring value = ValueOf(L"--grp-test="); !value.empty()) {
        return scmd::RunGrpTest(value, ValueOf(L"--pal="), ValueOf(L"--grp-bmp="));
    }
    if (HasArg(L"--unit-data-test")) return scmd::RunUnitDataTest();
    if (HasArg(L"--edge-test")) return scmd::RunEdgeTest(tsDir);
    if (HasArg(L"--section-test")) {
        return mapPath.empty() ? NoMap() : scmd::RunSectionTest(mapPath);
    }
    if (const std::wstring out = ValueOf(L"--player-test="); !out.empty()) {
        return mapPath.empty() ? NoMap() : scmd::RunPlayerTest(mapPath, out);
    }
    if (const std::wstring out = ValueOf(L"--restriction-test="); !out.empty()) {
        return mapPath.empty() ? NoMap() : scmd::RunRestrictionTest(mapPath, out);
    }
    if (const std::wstring out = ValueOf(L"--location-test="); !out.empty()) {
        return mapPath.empty() ? NoMap() : scmd::RunLocationTest(mapPath, out);
    }
    if (const std::wstring out = ValueOf(L"--decor-test="); !out.empty()) {
        return mapPath.empty() ? NoMap() : scmd::RunDecorTest(mapPath, out);
    }
    if (const std::wstring out = ValueOf(L"--sprite-test="); !out.empty()) {
        return mapPath.empty() ? NoMap() : scmd::RunSpriteTest(mapPath, out);
    }
    if (const std::wstring out = ValueOf(L"--radar-test="); !out.empty()) {
        return mapPath.empty() ? NoMap() : scmd::RunRadarTest(mapPath, out);
    }
    if (const std::wstring out = ValueOf(L"--scenario-text-test="); !out.empty()) {
        return mapPath.empty() ? NoMap() : scmd::RunScenarioTextTest(mapPath, out);
    }
    if (const std::wstring out = ValueOf(L"--map-tools-test="); !out.empty()) {
        return mapPath.empty() ? NoMap() : scmd::RunMapToolsTest(mapPath, out);
    }
    if (const std::wstring out = ValueOf(L"--tileset-render-test="); !out.empty()) {
        return scmd::RunTilesetRenderTest(tsDir, out);
    }
    if (const std::wstring out = ValueOf(L"--extra-test="); !out.empty()) {
        return mapPath.empty() ? NoMap() : scmd::RunExtraFileTest(mapPath, out);
    }
    if (const std::wstring out = ValueOf(L"--trig-test="); !out.empty()) {
        return mapPath.empty() ? NoMap() : scmd::RunTrigTest(mapPath, out);
    }
    if (HasArg(L"--undo-test")) {
        return mapPath.empty() ? NoMap() : scmd::RunUndoTest(mapPath);
    }
    if (const std::wstring out = ValueOf(L"--edit-test="); !out.empty()) {
        return mapPath.empty() ? NoMap() : scmd::RunEditTest(mapPath, out);
    }
    if (const std::wstring out = ValueOf(L"--resize-test="); !out.empty()) {
        return mapPath.empty() ? NoMap() : scmd::RunResizeTest(mapPath, out);
    }
    if (const std::wstring out = ValueOf(L"--file-test="); !out.empty()) {
        return scmd::RunFileTest(out);
    }
    if (const std::wstring out = ValueOf(L"--mpq-compress-test="); !out.empty()) {
        return scmd::RunMpqCompressTest(out);
    }
    if (const std::wstring path = ValueOf(L"--mpq="); !path.empty()) {
        return scmd::RunMpqSelfTest(path);
    }

    std::printf("SCMDTests: no test flag specified\n");
    return 2;
}
