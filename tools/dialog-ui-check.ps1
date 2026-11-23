#requires -Version 5.1
<#
  对话框 UI 快照回归（CI / 本地）

  用法：
    powershell -NoProfile -ExecutionPolicy Bypass -File tools/dialog-ui-check.ps1
    powershell ... -File tools/dialog-ui-check.ps1 -Exe bin\x64\Release\SCMDPlus.exe
    powershell ... -File tools/dialog-ui-check.ps1 -Mode update            # 更新通用基准
    powershell ... -File tools/dialog-ui-check.ps1 -Mode update -PerEnvironment  # 更新本环境基准

  基准选择：
    1. 若存在 doc\05-ui\<基准名>.<环境标签>.txt，优先使用；
    2. 否则退回 doc\05-ui\<基准名>.txt。
    环境标签默认 win<主版本>.<构建号>（如 win10.19045），可用 -EnvTag 覆盖。
  基准文件：
    dialog-snapshot-baseline.txt     控件状态（28 个对话框）
    dialog-interaction-baseline.txt  交互前后状态（167/179/198/199，8 对）
  通过条件：进程退出码 0（审计断言全部通过且两份快照都与基准一致）。
#>
param(
    [string]$Exe = '',
    [ValidateSet('check', 'update')]
    [string]$Mode = 'check',
    [string]$EnvTag = '',
    [switch]$PerEnvironment
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrEmpty($Exe)) {
    $Exe = Join-Path $root 'bin\Win32\Debug\SCMDPlus.exe'
} elseif (-not [System.IO.Path]::IsPathRooted($Exe)) {
    $Exe = Join-Path $root $Exe
}

# 环境标签：Windows 主版本 + 构建号；字体只影响像素，不影响快照文本
if ([string]::IsNullOrEmpty($EnvTag)) {
    $v = [System.Environment]::OSVersion.Version
    $EnvTag = "win$($v.Major).$($v.Build)"
}
$EnvTag = $EnvTag -replace '[\\/:*?"<>|]', '_'

$baselineDir = Join-Path $root 'doc\05-ui'
$diag = Join-Path $env:TEMP 'scmdplus_mpq.txt'
$snapshotDiff = Join-Path $env:TEMP 'scmdplus_dialog_snapshot.diff.txt'
$interactionDiff = Join-Path $env:TEMP 'scmdplus_dialog_interaction.diff.txt'

function Select-Baseline([string]$baseName) {
    $variant = Join-Path $baselineDir "$baseName.$EnvTag.txt"
    if (Test-Path -LiteralPath $variant) { return $variant }
    return Join-Path $baselineDir "$baseName.txt"
}

if (-not (Test-Path -LiteralPath $Exe)) {
    throw "找不到可执行文件：$Exe（请先构建对应配置）"
}
foreach ($f in @($diag, $snapshotDiff, $interactionDiff)) {
    if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f -Force }
}

if ($Mode -eq 'update') {
    if ($PerEnvironment) {
        $snapshotTarget = Join-Path $baselineDir "dialog-snapshot-baseline.$EnvTag.txt"
        $interactionTarget = Join-Path $baselineDir "dialog-interaction-baseline.$EnvTag.txt"
    } else {
        $snapshotTarget = Join-Path $baselineDir 'dialog-snapshot-baseline.txt'
        $interactionTarget = Join-Path $baselineDir 'dialog-interaction-baseline.txt'
    }
    $testArgs = @(
        '--dialog-audit-test',
        "--dialog-snapshot-baseline=$snapshotTarget",
        "--dialog-interaction-baseline=$interactionTarget"
    )
} else {
    $snapshotTarget = Select-Baseline 'dialog-snapshot-baseline'
    $interactionTarget = Select-Baseline 'dialog-interaction-baseline'
    if (-not (Test-Path -LiteralPath $snapshotTarget)) {
        throw "缺少基准快照：$snapshotTarget"
    }
    $testArgs = @(
        '--dialog-audit-test',
        "--dialog-snapshot-diff=$snapshotTarget"
    )
    if (Test-Path -LiteralPath $interactionTarget) {
        $testArgs += "--dialog-interaction-diff=$interactionTarget"
    } else {
        Write-Host "未找到交互基准，跳过对比：$interactionTarget"
    }
}

Write-Host "环境标签：$EnvTag"
Write-Host "控件基准：$snapshotTarget"
Write-Host "交互基准：$interactionTarget"
$p = Start-Process -FilePath $Exe -ArgumentList $testArgs -PassThru -Wait
if (Test-Path -LiteralPath $diag) {
    Get-Content -LiteralPath $diag -Encoding Unicode
}

if ($Mode -eq 'update') {
    if ($p.ExitCode -ne 0) { throw "基准生成失败（exit=$($p.ExitCode)）" }
    Write-Host "已更新基准：$snapshotTarget"
    Write-Host "已更新基准：$interactionTarget"
    exit 0
}

if ($p.ExitCode -ne 0) {
    if (Test-Path -LiteralPath $snapshotDiff) {
        Write-Host '---- 控件快照差异 ----'
        Get-Content -LiteralPath $snapshotDiff -Encoding Unicode
    }
    if (Test-Path -LiteralPath $interactionDiff) {
        Write-Host '---- 交互快照差异 ----'
        Get-Content -LiteralPath $interactionDiff -Encoding Unicode
    }
    throw "对话框快照与基准不一致（exit=$($p.ExitCode)）"
}
Write-Host '对话框快照与基准一致。'
