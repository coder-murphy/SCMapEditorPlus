# 把最近构建的应用程序目录同步到项目 exe\（供本地手动运行检查）
# 由 .git/hooks/post-commit 调用；也可手动执行：
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\copy-app.ps1
#   powershell ... -File tools\copy-app.ps1 -Source bin\Win32\Debug   # 指定配置
param(
    [string]$Source = '',
    [string]$Dest = ''
)

$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrEmpty($Dest)) { $Dest = Join-Path $root 'exe' }

if ([string]::IsNullOrEmpty($Source)) {
    # 四配置里取 SCMDPlus.exe 最新的一份，保证 exe\ 跟最近一次构建一致
    $configs = @('Win32\Debug', 'Win32\Release', 'x64\Debug', 'x64\Release')
    $newest = $null
    foreach ($config in $configs) {
        $dir = Join-Path $root (Join-Path 'bin' $config)
        $exe = Join-Path $dir 'SCMDPlus.exe'
        if (-not (Test-Path -LiteralPath $exe)) { continue }
        $time = (Get-Item -LiteralPath $exe).LastWriteTimeUtc
        if ($null -eq $newest -or $time -gt $newest.Time) {
            $newest = [pscustomobject]@{ Dir = $dir; Time = $time; Config = $config }
        }
    }
    if ($null -eq $newest) {
        Write-Host '[copy-app] 未找到 bin\*\*\SCMDPlus.exe，请先构建'
        exit 2
    }
    $Source = $newest.Dir
    $label = $newest.Config
} else {
    $full = $Source
    if (-not [System.IO.Path]::IsPathRooted($full)) { $full = Join-Path $root $full }
    if (-not (Test-Path -LiteralPath (Join-Path $full 'SCMDPlus.exe'))) {
        Write-Host "[copy-app] 源目录缺少 SCMDPlus.exe：$full"
        exit 2
    }
    $Source = $full
    $label = $Source
}

New-Item -ItemType Directory -Force -Path $Dest | Out-Null
# /E 增量复制（不删除 exe\ 里用户自己放的文件），重复提交时只更新变化项
& robocopy.exe $Source $Dest /E /NFL /NDL /NJH /NJS /R:1 /W:1 | Out-Null
if ($LASTEXITCODE -ge 8) {
    Write-Host "[copy-app] robocopy 失败：$LASTEXITCODE"
    exit 1
}
$count = (Get-ChildItem -LiteralPath $Dest -Recurse -File | Measure-Object).Count
Write-Host "[copy-app] 已同步 $label -> exe\（$count 个文件）"
exit 0
