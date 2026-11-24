#pragma once
#include <string>

namespace scmd {

// 写 %TEMP%\scmdplus_mpq.txt（与 GUI 回归共用同一份报告）
void WriteHeadlessDiag(const std::wstring& text);

// 无头回归入口（由 SCMDTests.exe 调用，不依赖窗口/对话框）
int RunEditTest(const std::wstring& mapPath, const std::wstring& outPath);
int RunFileTest(const std::wstring& outPath);
int RunMpqCompressTest(const std::wstring& outPath);
int RunEdgeTest(const std::wstring& tsDir);
int RunTrigTest(const std::wstring& mapPath, const std::wstring& outPath);
int RunExtraFileTest(const std::wstring& mapPath, const std::wstring& outPath);
int RunIscriptTest(const std::wstring& iscriptPath, const std::wstring& grpPath);
int RunGrpTest(const std::wstring& grpPath, const std::wstring& palPath,
               const std::wstring& bmpPath);
int RunUnitDataTest();
int RunResizeTest(const std::wstring& mapPath, const std::wstring& outPath);
int RunUndoTest(const std::wstring& mapPath);
int RunPlayerTest(const std::wstring& mapPath, const std::wstring& outPath);
int RunRestrictionTest(const std::wstring& mapPath, const std::wstring& outPath);
int RunLocationTest(const std::wstring& mapPath, const std::wstring& outPath);
int RunDecorTest(const std::wstring& mapPath, const std::wstring& outPath);
int RunSpriteTest(const std::wstring& mapPath, const std::wstring& outPath);
int RunRadarTest(const std::wstring& mapPath, const std::wstring& outPath);
int RunScenarioTextTest(const std::wstring& mapPath, const std::wstring& outPath);
int RunMapToolsTest(const std::wstring& mapPath, const std::wstring& outPath);
int RunRecentTest();
int RunBackupTest(const std::wstring& mapPath, const std::wstring& settingsDir);
int RunTilesetRenderTest(const std::wstring& tsDir, const std::wstring& outDir);
int RunSectionTest(const std::wstring& mapPath);
int RunMpqSelfTest(const std::wstring& path);

}  // namespace scmd
