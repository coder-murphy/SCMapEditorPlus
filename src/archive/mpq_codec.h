#pragma once
#include <cstddef>

#include "common/types.h"

namespace scmd {
namespace mpq {

// MPQ 加密表：0x500 项的 hash/crypt 表（0x000..0x4FF）
std::uint32_t HashString(const char* text, int hashType);

// 原地解密/加密一段按 u32 小端组织的数据（长度不是 4 的倍数时按 4 补零处理）
void DecryptWords(u32* data, std::size_t dwordCount, u32 key);
void EncryptWords(u32* data, std::size_t dwordCount, u32 key);

// DEFLATE 解压（raw，无 zlib 头），expected 用于预分配与截断
bool InflateRaw(const u8* in, std::size_t inLen, Buffer& out, std::size_t expected);

// PKWARE DCL implode（MPQ 压缩类型 0x08）
bool Explode(const u8* in, std::size_t inLen, Buffer& out, std::size_t expected);

// 按 MPQ 扇区首字节的压缩类型分发：0x00 原样 / 0x02 zlib / 0x08 implode
bool DecompressSector(const u8* in, std::size_t inLen, std::size_t expected, Buffer& out);

// 最近一次 DecompressSector 的状态：0xFF 未调用；0x00/0x02/0x08 为压缩类型；
// 0xE1..0xE4 表示 PKWARE implode 内部失败点（便于定位损坏数据）
u8 LastCompressType();

}  // namespace mpq
}  // namespace scmd
