# 撤销 / 重做

对应源码：`src/map/edit_history.h/.cpp`、`src/ui/main_window.cpp`

## 设计选择：快照式（P1-6）

一次编辑保存**前后两份完整状态**：

```
EditCommand {
    label
    tilesBefore / tilesAfter        // MTXM 原始字节（192x192 = 73 728 B）
    unitsBefore / unitsAfter        // 单位向量（36 B/条）
}
```

理由：地图上限 256×256（MTXM 128 KB），单位通常数百条；快照让撤销“不可能出错”，
代价是每步约 150–300 KB，历史上限 **50 步**（最坏约 15 MB），对桌面编辑器可接受。
若日后要压内存，可换成增量记录（tile index + old/new、unit index + 记录）。

## 事务边界

| 操作 | 事务粒度 |
|---|---|
| 地形刷 | 按下鼠标 → 抬起鼠标为**一笔**，中途多次改格合并成一步 |
| 放置单位 | 一次点击一步 |
| 删除单位（右键/Delete） | 一次操作一步，无实际改动则不记录 |

提交时若地形与单位都未变化，直接丢弃，不会产生空历史。

## 行为

- `Undo` 恢复 `tilesBefore` + `unitsBefore`，命令移入重做栈；
- `Redo` 恢复 `tilesAfter` + `unitsAfter`，命令移回撤销栈；
- 新的编辑会**清空重做栈**（标准编辑器语义）；
- 打开新地图时清空历史。

快捷键：`Ctrl+Z` 撤销，`Ctrl+Y`（或 `Ctrl+Shift+Z`）重做。

## 验证（VERIFIED）

无头回归 `--undo-test`（Space Circuit）：

```
tileEdit=1 tileUndo=1 tileRedo=1
placeOk=1  placeUndo=1 placeRedo=1
removeUndo=1  undoCount=4  redoCleared=1
```

- 地形：刷 3 格后撤销，文档哈希回到初始值；重做后回到修改后的哈希；
- 单位：放置 2 个后撤销数量回落、重做恢复；删除 1 个后撤销连 `serial` 一起还原；
- 重做栈：撤销后再做新编辑，`CanRedo()` 为假。

GUI 实测（真实按键注入）：

```
放置后    145 个单位，已修改
Ctrl+Z -> 144 个单位，已修改
Ctrl+Y -> 145 个单位，已修改
```

注：第一次跑该测试时 `Ctrl+Y` 被误判失败，原因是脚本只等 700 ms 就读取标题，
而撤销会触发整图重渲；延长等待后稳定通过——**是测试时序问题，不是功能缺陷**。

## 已知限制

- 撤销会整图重渲（192×192 @8px 约 2.4 M 像素），大地图上有可见延迟；
  后续可只重绘受影响区域。
- 目前不含“文件级”撤销（例如打开/另存）。
