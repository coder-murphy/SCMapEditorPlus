#requires -Version 5.1
<#
  一键回归：四配置构建 + 对话框审计 + 无头功能测试

  用法：
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\regression.ps1
    powershell ... -SkipBuild                       # 跳过构建，仅跑测试
    powershell ... -Map "D:\maps\test.scx"          # 追加依赖输入地图的测试
    powershell ... -Configs Debug/Win32             # 只跑一个配置

  说明：
    - 依赖地图的测试（undo/trig/edit/player/...）在未提供 -Map 时自动跳过；
    - 所有输出写在 %TEMP%\scmdplus_regression_<pid>，结束后清理；
    - 任一构建/审计/测试失败则退出码非 0。
#>
param(
    [string]$MsBuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe',
    [string[]]$Configs = @('Debug/Win32', 'Release/Win32', 'Debug/x64', 'Release/x64'),
    [switch]$SkipBuild,
    [switch]$SkipAudit,
    [string]$Map = '',
    [string]$Ts = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrEmpty($Ts)) {
    $candidate = Join-Path $root 'ts'
    if (Test-Path -LiteralPath $candidate) { $Ts = $candidate }
}

if (-not (Test-Path -LiteralPath $MsBuild)) {
    $cmd = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($cmd) { $MsBuild = $cmd.Source } else { throw "找不到 MSBuild：$MsBuild" }
}

$tmp = Join-Path $env:TEMP ("scmdplus_regression_" + $PID)
New-Item -ItemType Directory -Path $tmp -Force | Out-Null
$diag = Join-Path $env:TEMP 'scmdplus_mpq.txt'
$results = New-Object System.Collections.ArrayList

function Add-Result([string]$name, [bool]$ok, [string]$detail) {
    [void]$results.Add([pscustomobject]@{ Name = $name; Ok = $ok; Detail = $detail })
    if ($ok) { Write-Host ("[PASS] {0}" -f $name) }
    else { Write-Host ("[FAIL] {0}  {1}" -f $name, $detail) }
}

function Invoke-Test([string]$exe, [string]$name, [string[]]$arguments) {
    if (Test-Path -LiteralPath $diag) { Remove-Item -LiteralPath $diag -Force }
    $p = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -Wait
    $ok = $p.ExitCode -eq 0
    $detail = ''
    if (-not $ok -and (Test-Path -LiteralPath $diag)) {
        $detail = ((Get-Content -LiteralPath $diag -Encoding Unicode) -join '; ')
        if ($detail.Length -gt 600) { $detail = $detail.Substring(0, 600) + '...' }
    }
    Add-Result $name $ok $detail
}

# 1) 构建
if (-not $SkipBuild) {
    foreach ($cfg in $Configs) {
        $parts = $cfg.Split('/')
        $config = $parts[0]
        $platform = $parts[1]
        Write-Host ("==== 构建 {0} {1} ====" -f $config, $platform)
        & $MsBuild (Join-Path $root 'SCMDPlus.sln') /t:Build "/p:Configuration=$config" `
            "/p:Platform=$platform" /m /v:m /nologo
        Add-Result ("build:{0}/{1}" -f $config, $platform) ($LASTEXITCODE -eq 0) ''
    }
}

# 2) 对话框审计（每个配置）
if (-not $SkipAudit) {
    $checkScript = Join-Path $root 'tools\dialog-ui-check.ps1'
    foreach ($cfg in $Configs) {
        $parts = $cfg.Split('/')
        $exe = Join-Path $root ("bin\{0}\{1}\SCMDPlus.exe" -f $parts[1], $parts[0])
        if (-not (Test-Path -LiteralPath $exe)) {
            Add-Result ("audit:{0}" -f $cfg) $false '缺少可执行文件'
            continue
        }
        Write-Host ("==== 审计 {0} ====" -f $cfg)
        & powershell -NoProfile -ExecutionPolicy Bypass -File $checkScript -Exe $exe
        Add-Result ("audit:{0}" -f $cfg) ($LASTEXITCODE -eq 0) ''
    }
}

# 3) 无头功能测试（用第一个配置的 exe）
$firstParts = $Configs[0].Split('/')
$exe = Join-Path $root ("bin\{0}\{1}\SCMDPlus.exe" -f $firstParts[1], $firstParts[0])
if (-not (Test-Path -LiteralPath $exe)) { throw "找不到可执行文件：$exe" }

$tsArgs = @()
if (-not [string]::IsNullOrEmpty($Ts)) { $tsArgs = @('--ts="' + $Ts + '"') }

Invoke-Test $exe 'unit-data' @('--unit-data-test')
Invoke-Test $exe 'unit-param' @('--unit-param-test="' + (Join-Path $tmp 'unit.scx') + '"')
Invoke-Test $exe 'clip' @('--clip-test="' + (Join-Path $tmp 'clip.scx') + '"')
Invoke-Test $exe 'doodad' @('--doodad-test="' + (Join-Path $tmp 'doodad.scx') + '"')
Invoke-Test $exe 'loc-draw' @('--loc-draw-test="' + (Join-Path $tmp 'locdraw.scx') + '"')
Invoke-Test $exe 'brush-pattern' @('--brush-pattern-test="' + (Join-Path $tmp 'brush.scx') + '"')
Invoke-Test $exe 'tileset-render' (@('--tileset-render-test="' + (Join-Path $tmp 'tsrender') + '"') + $tsArgs)
Invoke-Test $exe 'file' @('--file-test="' + (Join-Path $tmp 'file.scx') + '"')
Invoke-Test $exe 'mpq-compress' @('--mpq-compress-test="' + (Join-Path $tmp 'compress.scx') + '"')
Invoke-Test $exe 'edge' ($tsArgs + @('--edge-test'))
Invoke-Test $exe 'menu' @('--menu-test')

if (-not [string]::IsNullOrEmpty($Map)) {
    if (-not (Test-Path -LiteralPath $Map)) { throw "地图不存在：$Map" }
    $openArgs = @('--open="' + $Map + '"') + $tsArgs
    Invoke-Test $exe 'menu-cmd' ($openArgs + @('--menu-cmd-test'))
    Invoke-Test $exe 'undo' ($openArgs + @('--undo-test'))
    Invoke-Test $exe 'trig' ($openArgs + @('--trig-test="' + (Join-Path $tmp 'trig.scx') + '"'))
    Invoke-Test $exe 'edit' ($openArgs + @('--edit-test="' + (Join-Path $tmp 'edit.scx') + '"'))
    Invoke-Test $exe 'player' ($openArgs + @('--player-test="' + (Join-Path $tmp 'player.scx') + '"'))
    Invoke-Test $exe 'restriction' ($openArgs + @('--restriction-test="' + (Join-Path $tmp 'restriction.scx') + '"'))
    Invoke-Test $exe 'location' ($openArgs + @('--location-test="' + (Join-Path $tmp 'location.scx') + '"'))
    Invoke-Test $exe 'decor' ($openArgs + @('--decor-test="' + (Join-Path $tmp 'decor.scx') + '"'))
    Invoke-Test $exe 'sprite' ($openArgs + @('--sprite-test="' + (Join-Path $tmp 'sprite.scx') + '"'))
    Invoke-Test $exe 'radar' ($openArgs + @('--radar-test="' + (Join-Path $tmp 'radar.scx') + '"'))
    Invoke-Test $exe 'scenario-text' ($openArgs + @('--scenario-text-test="' + (Join-Path $tmp 'text.scx') + '"'))
    Invoke-Test $exe 'map-tools' ($openArgs + @('--map-tools-test="' + (Join-Path $tmp 'tools.scx') + '"'))
    Invoke-Test $exe 'extra' ($openArgs + @('--extra-test="' + (Join-Path $tmp 'extra.scx') + '"'))
    Invoke-Test $exe 'resize' ($openArgs + @('--resize-test="' + (Join-Path $tmp 'resize.scx') + '"'))
} else {
    Write-Host '未提供 -Map，跳过依赖输入地图的测试（undo/trig/edit/player/...）。'
}

# 4) 汇总
Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
$failed = @($results | Where-Object { -not $_.Ok })
Write-Host ('---- 汇总：{0} 项，失败 {1} 项 ----' -f $results.Count, $failed.Count)
foreach ($f in $failed) { Write-Host ("[FAIL] {0}  {1}" -f $f.Name, $f.Detail) }
if ($failed.Count -gt 0) { exit 1 }
exit 0
