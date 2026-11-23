#pragma once
#include <string>
#include <vector>

#include "common/types.h"

namespace scmd {

// iscript.bin 的一条指令。opcode 为 u8，操作数按 opcode 表读取。
struct IscriptInstruction {
    std::size_t offset = 0;
    u8 opcode = 0;
    std::vector<u8> operands;
};

// iscript 头：id + 类型 + 各动画的绝对文件偏移（0 = 该动画不存在）
struct IscriptHeader {
    u16 id = 0;
    u16 type = 0;
    std::size_t offset = 0;
    std::vector<u16> animOffsets;
};

// iscript 线性模拟结果（Play/测试用；不做图像绘制）
struct IscriptSimResult {
    int ops = 0;
    int waits = 0;
    int frames = 0;
    int loops = 0;
    u16 lastFrame = 0;
    u8 lastOp = 0;
    bool ended = false;
};

// iscript.bin：文件头为 (u16 id, u16 headerOffset) 索引表，以 headerOffset=0 结束；
// 每个头为 "SCPE" + u32 type + u16 动画偏移[动画数]，偏移是绝对文件位置。
class IscriptFile {
public:
    bool Load(const std::wstring& path);
    bool Parse(const Buffer& data);
    bool loaded() const { return loaded_; }
    std::size_t size() const { return data_.size(); }
    const std::vector<IscriptHeader>& entries() const { return entries_; }
    const IscriptHeader* Find(u16 id) const;

    // 反汇编一个动画；遇到 end/return/goto/ignorerest 停止
    bool Disassemble(const IscriptHeader& header, int animIndex,
                     std::vector<IscriptInstruction>& out) const;
    // 读取指定文件偏移处的一条指令（供 goto 跳转模拟使用）
    bool ReadInstruction(std::size_t offset, IscriptInstruction& out) const;
    // 线性模拟动画：处理 playfram/wait/goto/end/return/ignorerest，最多 maxOps 步
    bool Simulate(const IscriptHeader& header, int animIndex, int maxOps,
                  IscriptSimResult& out) const;

    // type -> 动画槽数量（0/1->2，2->4，12/13->14，14/15->16，20/21->22，
    // 23->24，24->26，26..29->28）
    static int AnimationCount(u16 type);
    static const char* AnimationName(int index);
    static const char* OpcodeName(u8 opcode);

private:
    // 固定操作数字节数；-1 表示变长（0x19/0x1c）或未知
    static int FixedOperandSize(u8 opcode);
    bool ReadOperands(std::size_t& pos, u8 opcode, std::vector<u8>& out) const;

    Buffer data_;
    std::vector<IscriptHeader> entries_;
    bool loaded_ = false;
};

// GRP 元数据（不做像素解码）：帧数 / 画布尺寸 / 每帧位置与大小
struct GrpFrameMeta {
    u8 x = 0;
    u8 y = 0;
    u8 width = 0;
    u8 height = 0;
};

class GrpFile {
public:
    bool Load(const std::wstring& path);
    bool loaded() const { return loaded_; }
    int frameCount() const { return frameCount_; }
    int width() const { return width_; }
    int height() const { return height_; }
    const std::vector<GrpFrameMeta>& frames() const { return frames_; }

private:
    int frameCount_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::vector<GrpFrameMeta> frames_;
    bool loaded_ = false;
};

}  // namespace scmd
