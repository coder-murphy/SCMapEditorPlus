#requires -Version 5.1
<#
  从原版 Scmdraft.exe（[CN]ScmDraft2.exe）导出全部 PE 资源到独立目录。

  用法：
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\dump_original.ps1
    powershell ... -Exe "F:\Games\单机游戏\starcraft\[CN]ScmDraft2.exe" -Out reverse\scmdraft2

  产物：
    raw\<类型>\<名称>_<语言>.bin     原始资源
    converted\bitmap\*.bmp           位图（补 BMP 文件头）
    converted\icon\*.ico             图标组（合并 RT_ICON）
    converted\cursor\*.cur           光标组（合并 RT_CURSOR + 热点）
    converted\string\*.txt           RT_STRING 文本（每块 16 条）
    converted\version\*.txt          版本资源的可读字符串（原始 BIN 仍保留）
    converted\manifest\*.xml         清单
    resources.csv                    资源清单（类型/名称/语言/大小/路径）
#>
param(
    [string]$Exe = 'F:\Games\单机游戏\starcraft\[CN]ScmDraft2.exe',
    [string]$Out = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrEmpty($Out)) { $Out = Join-Path $root 'reverse\scmdraft2' }
if (-not (Test-Path -LiteralPath $Exe)) { throw "找不到原版程序：$Exe" }

$source = @'
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

public static class ResDumper
{
    [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr LoadLibraryExW(string path, IntPtr hFile, uint flags);
    [DllImport("kernel32")] static extern bool FreeLibrary(IntPtr h);
    delegate bool EnumTypesProc(IntPtr h, IntPtr type, IntPtr lp);
    [DllImport("kernel32")] static extern bool EnumResourceTypesW(IntPtr h, EnumTypesProc cb, IntPtr lp);
    delegate bool EnumNamesProc(IntPtr h, IntPtr type, IntPtr name, IntPtr lp);
    [DllImport("kernel32")] static extern bool EnumResourceNamesW(IntPtr h, IntPtr type, EnumNamesProc cb, IntPtr lp);
    delegate bool EnumLangsProc(IntPtr h, IntPtr type, IntPtr name, ushort lang, IntPtr lp);
    [DllImport("kernel32")] static extern bool EnumResourceLanguagesW(IntPtr h, IntPtr type, IntPtr name, EnumLangsProc cb, IntPtr lp);
    [DllImport("kernel32")] static extern IntPtr FindResourceExW(IntPtr h, IntPtr type, IntPtr name, ushort lang);
    [DllImport("kernel32")] static extern IntPtr LoadResource(IntPtr h, IntPtr res);
    [DllImport("kernel32")] static extern IntPtr LockResource(IntPtr res);
    [DllImport("kernel32")] static extern uint SizeofResource(IntPtr h, IntPtr res);

    class Res
    {
        public IntPtr Type;
        public IntPtr Name;
        public ushort Lang;
        public byte[] Data;
    }

    static string Token(IntPtr p)
    {
        long v = p.ToInt64();
        if ((v & ~0xFFFFL) == 0) return "#" + v.ToString();
        return Marshal.PtrToStringUni(p);
    }

    static int NumericId(IntPtr p)
    {
        long v = p.ToInt64();
        if ((v & ~0xFFFFL) == 0) return (int)v;
        return -1;
    }

    static string TypeName(int id, string raw)
    {
        switch (id)
        {
            case 1: return "cursor";
            case 2: return "bitmap";
            case 3: return "icon";
            case 4: return "menu";
            case 5: return "dialog";
            case 6: return "string";
            case 9: return "accelerator";
            case 10: return "rcdata";
            case 12: return "group_cursor";
            case 14: return "group_icon";
            case 16: return "version";
            case 24: return "manifest";
            default: return "type_" + ((raw != null && raw.Length > 0) ? raw : id.ToString());
        }
    }

    static byte[] Read(IntPtr h, IntPtr type, IntPtr name, ushort lang)
    {
        IntPtr hr = FindResourceExW(h, type, name, lang);
        if (hr == IntPtr.Zero) return null;
        uint size = SizeofResource(h, hr);
        IntPtr loaded = LoadResource(h, hr);
        if (loaded == IntPtr.Zero) return null;
        IntPtr p = LockResource(loaded);
        if (p == IntPtr.Zero) return null;
        byte[] buf = new byte[size];
        Marshal.Copy(p, buf, 0, (int)size);
        return buf;
    }

    static byte[] DibToBmp(byte[] dib)
    {
        if (dib.Length < 40) return null;
        int biSize = BitConverter.ToInt32(dib, 0);
        int bitCount = BitConverter.ToInt16(dib, 14);
        int colorsUsed = BitConverter.ToInt32(dib, 32);
        int palette = (bitCount <= 8) ? (colorsUsed > 0 ? colorsUsed : (1 << bitCount)) * 4 : 0;
        int offBits = 14 + biSize + palette;
        byte[] bmp = new byte[14 + dib.Length];
        bmp[0] = (byte)'B'; bmp[1] = (byte)'M';
        Array.Copy(BitConverter.GetBytes(14 + dib.Length), 0, bmp, 2, 4);
        Array.Copy(BitConverter.GetBytes(offBits), 0, bmp, 10, 4);
        Array.Copy(dib, 0, bmp, 14, dib.Length);
        return bmp;
    }

    static string ExtractUtf16Strings(byte[] data)
    {
        var sb = new StringBuilder();
        var run = new StringBuilder();
        for (int i = 0; i + 1 < data.Length; i += 2)
        {
            char c = (char)(data[i] | (data[i + 1] << 8));
            if (c >= 0x20 && c < 0x7F || (c >= 0x4E00 && c <= 0x9FFF))
            {
                run.Append(c);
            }
            else
            {
                if (run.Length >= 4) sb.AppendLine(run.ToString());
                run.Length = 0;
            }
        }
        if (run.Length >= 4) sb.AppendLine(run.ToString());
        return sb.ToString();
    }

    static string Clean(string name)
    {
        foreach (char c in Path.GetInvalidFileNameChars()) name = name.Replace(c, '_');
        return name.Replace("#", "id");
    }

    public static string Dump(string exe, string outDir)
    {
        IntPtr h = LoadLibraryExW(exe, IntPtr.Zero, 0x2);
        if (h == IntPtr.Zero) throw new Exception("LoadLibraryEx 失败: " + Marshal.GetLastWin32Error());
        try
        {
            Directory.CreateDirectory(outDir);
            var types = new List<IntPtr>();
            EnumTypesProc typesCb = delegate(IntPtr m, IntPtr t, IntPtr l)
            {
                long v = t.ToInt64();
                // 命名类型的字符串指针只在回调期间有效，复制到稳定内存
                if ((v & ~0xFFFFL) == 0) types.Add(t);
                else types.Add(Marshal.StringToHGlobalUni(Marshal.PtrToStringUni(t)));
                return true;
            };
            EnumResourceTypesW(h, typesCb, IntPtr.Zero);

            var all = new List<Res>();
            foreach (IntPtr type in types)
            {
                var names = new List<IntPtr>();
                EnumNamesProc namesCb = delegate(IntPtr m, IntPtr t, IntPtr n, IntPtr l)
                {
                    long v = n.ToInt64();
                    if ((v & ~0xFFFFL) == 0) names.Add(n);
                    else names.Add(Marshal.StringToHGlobalUni(Marshal.PtrToStringUni(n)));
                    return true;
                };
                EnumResourceNamesW(h, type, namesCb, IntPtr.Zero);
                foreach (IntPtr name in names)
                {
                    var langs = new List<ushort>();
                    EnumLangsProc langsCb = delegate(IntPtr m, IntPtr t, IntPtr n, ushort lang, IntPtr l) { langs.Add(lang); return true; };
                    EnumResourceLanguagesW(h, type, name, langsCb, IntPtr.Zero);
                    foreach (ushort lang in langs)
                    {
                        byte[] data = Read(h, type, name, lang);
                        if (data == null) continue;
                        all.Add(new Res { Type = type, Name = name, Lang = lang, Data = data });
                    }
                }
            }

            var csv = new StringBuilder();
            csv.AppendLine("type_id,type_name,name,lang,bytes,raw,converted");
            int rawCount = 0, convCount = 0;

            // 索引：同语言、同 ID 的 icon/cursor，供组文件合并
            var byKey = new Dictionary<string, Res>();
            foreach (var r in all)
            {
                int tid = NumericId(r.Type);
                byKey[tid + ":" + Token(r.Name) + ":" + r.Lang] = r;
            }

            foreach (var r in all)
            {
                int tid = NumericId(r.Type);
                string typeName = TypeName(tid, Token(r.Type));
                string name = Token(r.Name);
                string baseName = Clean(name) + "_" + r.Lang;
                string rawDir = Path.Combine(outDir, "raw", typeName);
                Directory.CreateDirectory(rawDir);
                string rawPath = Path.Combine(rawDir, baseName + ".bin");
                File.WriteAllBytes(rawPath, r.Data);
                rawCount++;

                string converted = "";
                if (tid == 2)   // bitmap
                {
                    byte[] bmp = DibToBmp(r.Data);
                    if (bmp != null)
                    {
                        string dir = Path.Combine(outDir, "converted", "bitmap");
                        Directory.CreateDirectory(dir);
                        string p = Path.Combine(dir, baseName + ".bmp");
                        File.WriteAllBytes(p, bmp);
                        converted = p.Substring(outDir.Length).TrimStart('\\');
                        convCount++;
                    }
                }
                else if (tid == 6)   // string block
                {
                    string dir = Path.Combine(outDir, "converted", "string");
                    Directory.CreateDirectory(dir);
                    string p = Path.Combine(dir, baseName + ".txt");
                    var sb = new StringBuilder();
                    int pos = 0;
                    for (int i = 0; i < 16 && pos + 2 <= r.Data.Length; i++)
                    {
                        int len = r.Data[pos] | (r.Data[pos + 1] << 8);
                        pos += 2;
                        string text = "";
                        if (len > 0 && pos + len * 2 <= r.Data.Length)
                        {
                            text = Encoding.Unicode.GetString(r.Data, pos, len * 2);
                            pos += len * 2;
                        }
                        int id = (int)(r.Name.ToInt64() - 1) * 16 + i;
                        if (text.Length > 0) sb.AppendLine(id + "\t" + text);
                    }
                    File.WriteAllText(p, sb.ToString(), new UTF8Encoding(false));
                    converted = p.Substring(outDir.Length).TrimStart('\\');
                    convCount++;
                }
                else if (tid == 24)  // manifest
                {
                    string dir = Path.Combine(outDir, "converted", "manifest");
                    Directory.CreateDirectory(dir);
                    string p = Path.Combine(dir, baseName + ".xml");
                    File.WriteAllBytes(p, r.Data);
                    converted = p.Substring(outDir.Length).TrimStart('\\');
                    convCount++;
                }
                else if (tid == 16)  // version
                {
                    string dir = Path.Combine(outDir, "converted", "version");
                    Directory.CreateDirectory(dir);
                    string p = Path.Combine(dir, baseName + ".txt");
                    File.WriteAllText(p, ExtractUtf16Strings(r.Data), new UTF8Encoding(false));
                    converted = p.Substring(outDir.Length).TrimStart('\\');
                    convCount++;
                }
                else if (tid == 14 || tid == 12)  // icon/cursor group
                {
                    bool isCursor = tid == 12;
                    int count = (r.Data.Length >= 6) ? (r.Data[4] | (r.Data[5] << 8)) : 0;
                    int entrySize = isCursor ? 14 : 14;
                    if (count > 0 && r.Data.Length >= 6 + count * entrySize)
                    {
                        var images = new List<byte[]>();
                        var hot = new List<int[]>();
                        var meta = new List<int[]>();
                        for (int i = 0; i < count; i++)
                        {
                            int off = 6 + i * entrySize;
                            int w = r.Data[off];
                            int eh = r.Data[off + 1];
                            int color = r.Data[off + 2];
                            int planes = r.Data[off + 4] | (r.Data[off + 5] << 8);
                            int bits = r.Data[off + 6] | (r.Data[off + 7] << 8);
                            int bytes = BitConverter.ToInt32(r.Data, off + 8);
                            int id = r.Data[off + 12] | (r.Data[off + 13] << 8);
                            string key = (isCursor ? 1 : 3) + ":" + ("#" + id) + ":" + r.Lang;
                            Res img;
                            if (!byKey.TryGetValue(key, out img)) continue;
                            byte[] data = img.Data;
                            if (isCursor && data.Length >= 4)
                            {
                                hot.Add(new int[] { data[0] | (data[1] << 8), data[2] | (data[3] << 8) });
                                byte[] stripped = new byte[data.Length - 4];
                                Array.Copy(data, 4, stripped, 0, stripped.Length);
                                data = stripped;
                            }
                            else
                            {
                                hot.Add(new int[] { 0, 0 });
                            }
                            meta.Add(new int[] { w, eh, color, planes, bits, data.Length });
                            images.Add(data);
                        }
                        if (images.Count > 0)
                        {
                            string dir = Path.Combine(outDir, "converted", isCursor ? "cursor" : "icon");
                            Directory.CreateDirectory(dir);
                            string p = Path.Combine(dir, baseName + (isCursor ? ".cur" : ".ico"));
                            int header = 6 + images.Count * 16;
                            var outBuf = new List<byte>();
                            outBuf.AddRange(BitConverter.GetBytes((ushort)0));
                            outBuf.AddRange(BitConverter.GetBytes((ushort)(isCursor ? 2 : 1)));
                            outBuf.AddRange(BitConverter.GetBytes((ushort)images.Count));
                            int offset = header;
                            for (int i = 0; i < images.Count; i++)
                            {
                                outBuf.Add((byte)meta[i][0]);
                                outBuf.Add((byte)meta[i][1]);
                                outBuf.Add((byte)meta[i][2]);
                                outBuf.Add(0);
                                if (isCursor)
                                {
                                    outBuf.AddRange(BitConverter.GetBytes((ushort)hot[i][0]));
                                    outBuf.AddRange(BitConverter.GetBytes((ushort)hot[i][1]));
                                }
                                else
                                {
                                    outBuf.AddRange(BitConverter.GetBytes((ushort)meta[i][3]));
                                    outBuf.AddRange(BitConverter.GetBytes((ushort)meta[i][4]));
                                }
                                outBuf.AddRange(BitConverter.GetBytes(images[i].Length));
                                outBuf.AddRange(BitConverter.GetBytes(offset));
                                offset += images[i].Length;
                            }
                            foreach (var img in images) outBuf.AddRange(img);
                            File.WriteAllBytes(p, outBuf.ToArray());
                            converted = p.Substring(outDir.Length).TrimStart('\\');
                            convCount++;
                        }
                    }
                }

                csv.AppendLine(tid + "," + typeName + "," + name + "," + r.Lang + "," +
                                r.Data.Length + "," + rawPath.Substring(outDir.Length).TrimStart('\\') +
                                "," + converted);
            }

            File.WriteAllText(Path.Combine(outDir, "resources.csv"), csv.ToString(), new UTF8Encoding(false));
            return "resources=" + all.Count + " raw=" + rawCount + " converted=" + convCount;
        }
        finally
        {
            FreeLibrary(h);
        }
    }
}
'@

Add-Type -TypeDefinition $source -Language CSharp

$summary = [ResDumper]::Dump($Exe, $Out)

$hash = (Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash
$info = (Get-Item -LiteralPath $Exe).VersionInfo
$readme = @"
# 原版程序反解产物（Scmdraft 2）

- 来源：``$Exe``
- 大小：$((Get-Item -LiteralPath $Exe).Length) B
- SHA-256：``$hash``
- 版本：$($info.FileVersion)（$($info.FileDescription) / $($info.CompanyName)）
- 原始文件名：$($info.OriginalFilename)
- 导出汇总：$summary

## 目录

- ``raw\<类型>\<名称>_<语言>.bin``：PE 资源原始字节（menu/dialog/accelerator/rcdata 等）；
  自定义 ``BIN`` 类型输出到 ``raw\type_BIN\``（避免被 ``.gitignore`` 的 ``bin/`` 规则忽略）
- ``converted\bitmap\*.bmp``：位图（DIB + BMP 文件头）
- ``converted\icon\*.ico`` / ``converted\cursor\*.cur``：图标/光标组（合并 RT_ICON / RT_CURSOR）
- ``converted\string\*.txt``：字符串表（每块 16 条，``id<TAB>文本``）
- ``converted\version\*.txt``：版本资源里可读的 Unicode 字符串（原始 BIN 仍保留）
- ``converted\manifest\*.xml``：清单
- ``resources.csv``：全部资源清单（类型/名称/语言/大小/路径）

## 重新生成

``````powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\dump_original.ps1
``````

可读的菜单/对话框/加速键 ``.rc`` 文本已在 ``src/res/``（menu.rc / dialogs.rc / accel.rc）。
"@
$readmePath = Join-Path $Out 'README.md'
Set-Content -LiteralPath $readmePath -Value $readme -Encoding UTF8

Write-Host $summary
Write-Host "输出目录：$Out"
Write-Host "清单：$readmePath"
