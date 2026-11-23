#include "archive/mpq_codec.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace scmd {
namespace mpq {

namespace {
u8 g_lastCompressType = 0xFF;
int g_explodeLit = -1;
int g_explodeDict = -1;
}  // namespace

// ───────────────────────── 加密表 ─────────────────────────
namespace {

struct CryptTable {
    u32 v[0x500];
    CryptTable() {
        u32 seed = 0x00100001;
        for (int i = 0; i < 0x100; i++) {
            int idx = i;
            for (int k = 0; k < 5; k++) {
                seed = (seed * 125 + 3) % 0x2AAAAB;
                const u32 hi = (seed & 0xFFFF) << 16;
                seed = (seed * 125 + 3) % 0x2AAAAB;
                const u32 lo = seed & 0xFFFF;
                v[idx] = hi | lo;
                idx += 0x100;
            }
        }
    }
};

const CryptTable& Crypt() {
    static const CryptTable table;
    return table;
}

inline u32 KeyRot(u32 key) {
    return ((~key * 2097152u) + 0x11111111u) | (key >> 11);
}

}  // namespace

std::uint32_t HashString(const char* text, int hashType) {
    u32 s1 = 0x7FED7FED;
    u32 s2 = 0xEEEEEEEE;
    const u32* table = Crypt().v;
    for (const char* p = text; *p; ++p) {
        unsigned c = static_cast<unsigned char>(*p);
        if (c >= 'a' && c <= 'z') c -= 0x20;          // MPQ 的 hash 只对 ASCII 做大写折叠
        s1 = table[(hashType << 8) + c] ^ (s1 + s2);
        s2 = c + s1 + s2 + (s2 << 5) + 3;
    }
    return s1;
}

void DecryptWords(u32* data, std::size_t n, u32 key) {
    const u32* table = Crypt().v;
    u32 seed = 0xEEEEEEEE;
    for (std::size_t i = 0; i < n; i++) {
        seed += table[0x400 + (key & 0xFF)];
        const u32 enc = data[i];
        const u32 ch = enc ^ (key + seed);
        data[i] = ch;
        key = KeyRot(key);
        seed = ch + seed + (seed << 5) + 3;
    }
}

void EncryptWords(u32* data, std::size_t n, u32 key) {
    const u32* table = Crypt().v;
    u32 seed = 0xEEEEEEEE;
    for (std::size_t i = 0; i < n; i++) {
        seed += table[0x400 + (key & 0xFF)];
        const u32 plain = data[i];
        data[i] = plain ^ (key + seed);
        key = KeyRot(key);
        seed = plain + seed + (seed << 5) + 3;
    }
}

// ───────────────────────── DEFLATE 压缩（固定 Huffman + LZ77） ─────────────────────────
namespace {

// 位写入：字节内按低位先填，Huffman 码按高位先写
struct BitWriter {
    Buffer& out;
    u32 buf = 0;
    int cnt = 0;

    explicit BitWriter(Buffer& dst) : out(dst) {}

    void Put(u32 value, int bits) {
        buf |= (value & ((1u << bits) - 1u)) << cnt;
        cnt += bits;
        while (cnt >= 8) {
            out.push_back(static_cast<u8>(buf & 0xFFu));
            buf >>= 8;
            cnt -= 8;
        }
    }

    void PutCode(u32 code, int bits) {
        for (int i = bits - 1; i >= 0; --i) Put((code >> i) & 1u, 1);
    }

    void Flush() {
        if (cnt > 0) out.push_back(static_cast<u8>(buf & 0xFFu));
        buf = 0;
        cnt = 0;
    }
};

// RFC 1951 固定 Huffman 字面量/长度码
void PutSymbol(BitWriter& bw, int sym) {
    if (sym <= 143) bw.PutCode(0x30u + static_cast<u32>(sym), 8);
    else if (sym <= 255) bw.PutCode(0x190u + static_cast<u32>(sym - 144), 9);
    else if (sym <= 279) bw.PutCode(static_cast<u32>(sym - 256), 7);
    else bw.PutCode(0xC0u + static_cast<u32>(sym - 280), 8);
}

struct LengthCode {
    u8 extra;
    u16 base;
};

// 符号 257..285
const LengthCode kLengthCodes[29] = {
    {0, 3},   {0, 4},   {0, 5},   {0, 6},   {0, 7},   {0, 8},   {0, 9},   {0, 10},
    {1, 11},  {1, 13},  {1, 15},  {1, 17},  {2, 19},  {2, 23},  {2, 27},  {2, 31},
    {3, 35},  {3, 43},  {3, 51},  {3, 59},  {4, 67},  {4, 83},  {4, 99},  {4, 115},
    {5, 131}, {5, 163}, {5, 195}, {5, 227}, {0, 258},
};

const u16 kEncDistBase[30] = {
    1,     2,     3,     4,     5,     7,     9,     13,    17,    25,
    33,    49,    65,    97,    129,   193,   257,   385,   513,   769,
    1025,  1537,  2049,  3073,  4097,  6145,  8193,  12289, 16385, 24577,
};

const u8 kEncDistExtra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5,  5,  6,  6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
};

void PutMatch(BitWriter& bw, int len, int dist) {
    int li = 28;
    while (li > 0 && len < static_cast<int>(kLengthCodes[li].base)) li--;
    PutSymbol(bw, 257 + li);
    const int lextra = kLengthCodes[li].extra;
    if (lextra > 0) bw.Put(static_cast<u32>(len - kLengthCodes[li].base), lextra);

    int di = 29;
    while (di > 0 && dist < static_cast<int>(kEncDistBase[di])) di--;
    bw.PutCode(static_cast<u32>(di), 5);
    const int dextra = kEncDistExtra[di];
    if (dextra > 0) bw.Put(static_cast<u32>(dist - kEncDistBase[di]), dextra);
}

}  // namespace

bool CompressRaw(const u8* in, std::size_t inLen, Buffer& out) {
    out.clear();
    if (in == nullptr || inLen < 16) return false;
    out.reserve(inLen / 2 + 16);

    BitWriter bw(out);
    bw.Put(1, 1);   // BFINAL
    bw.Put(1, 2);   // BTYPE = 01（固定 Huffman）

    constexpr int kWindow = 32768;
    constexpr int kMinMatch = 3;
    constexpr int kMaxMatch = 258;
    constexpr int kHashBits = 15;
    constexpr int kHashSize = 1 << kHashBits;
    constexpr int kMaxChain = 32;

    std::vector<int> head(kHashSize, -1);
    std::vector<int> prev(inLen, -1);

    auto hash3 = [&](std::size_t p) -> unsigned {
        const unsigned h = (static_cast<unsigned>(in[p]) << 10) ^
                           (static_cast<unsigned>(in[p + 1]) << 5) ^
                           static_cast<unsigned>(in[p + 2]);
        return h & (kHashSize - 1);
    };

    std::size_t pos = 0;
    while (pos < inLen) {
        int bestLen = 0;
        int bestDist = 0;
        if (pos + kMinMatch <= inLen) {
            const unsigned h = hash3(pos);
            int cand = head[h];
            int chain = 0;
            const std::size_t maxLen = std::min<std::size_t>(kMaxMatch, inLen - pos);
            while (cand >= 0 && chain++ < kMaxChain) {
                const std::size_t dist = pos - static_cast<std::size_t>(cand);
                if (dist > static_cast<std::size_t>(kWindow)) break;
                std::size_t len = 0;
                while (len < maxLen && in[static_cast<std::size_t>(cand) + len] == in[pos + len]) {
                    len++;
                }
                if (static_cast<int>(len) > bestLen) {
                    bestLen = static_cast<int>(len);
                    bestDist = static_cast<int>(dist);
                    if (len == maxLen) break;
                }
                cand = prev[static_cast<std::size_t>(cand)];
            }
            prev[pos] = head[h];
            head[h] = static_cast<int>(pos);
        }

        if (bestLen >= kMinMatch) {
            PutMatch(bw, bestLen, bestDist);
            for (int k = 1; k < bestLen; k++) {
                const std::size_t p = pos + static_cast<std::size_t>(k);
                if (p + kMinMatch <= inLen) {
                    const unsigned h = hash3(p);
                    prev[p] = head[h];
                    head[h] = static_cast<int>(p);
                }
            }
            pos += static_cast<std::size_t>(bestLen);
        } else {
            PutSymbol(bw, in[pos]);
            pos++;
        }
    }
    PutSymbol(bw, 256);   // 块结束
    bw.Flush();
    return true;
}

// ───────────────────────── 位流与 Huffman ─────────────────────────
namespace {

struct BitReader {
    const u8* p;
    std::size_t n;
    std::size_t pos = 0;
    u32 buf = 0;
    int cnt = 0;

    BitReader(const u8* data, std::size_t len) : p(data), n(len) {}

    bool Bits(int need, u32& out) {
        u32 v = buf;
        while (cnt < need) {
            if (pos >= n) return false;
            v |= static_cast<u32>(p[pos++]) << cnt;
            cnt += 8;
        }
        buf = (need >= 32) ? 0 : (v >> need);
        cnt -= need;
        out = v & ((1u << need) - 1);
        return true;
    }

    // 丢弃当前位缓冲，回到字节边界
    void Align() {
        buf = 0;
        cnt = 0;
    }

    bool ReadBytes(u8* dst, std::size_t count) {
        if (pos + count > n) return false;
        std::memcpy(dst, p + pos, count);
        pos += count;
        return true;
    }
};

struct Huffman {
    int count[16] = {0};
    std::vector<int> sym;

    void Build(const u8* lens, int n) {
        for (int i = 1; i < 16; i++) count[i] = 0;
        for (int i = 0; i < n; i++) count[lens[i]]++;
        count[0] = 0;
        int offs[16] = {0};
        for (int i = 1; i < 15; i++) offs[i + 1] = offs[i] + count[i];
        sym.assign(n, 0);
        for (int i = 0; i < n; i++) {
            if (lens[i]) sym[offs[lens[i]]++] = i;
        }
    }

    // 返回符号，-1 表示码流非法
    int Decode(BitReader& br) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; len++) {
            u32 bit = 0;
            if (!br.Bits(1, bit)) return -1;
            code |= static_cast<int>(bit);
            const int c = count[len];
            if (code - first < c) return sym[static_cast<std::size_t>(index + (code - first))];
            index += c;
            first = (first + c) << 1;
            code <<= 1;
        }
        return -1;
    }
};

const u32 kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                          35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const u8 kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                          3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const u32 kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                           257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const u8 kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                           7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void BuildFixed(Huffman& lit, Huffman& dist) {
    u8 lengths[288];
    for (int i = 0; i < 144; i++) lengths[i] = 8;
    for (int i = 144; i < 256; i++) lengths[i] = 9;
    for (int i = 256; i < 280; i++) lengths[i] = 7;
    for (int i = 280; i < 288; i++) lengths[i] = 8;
    lit.Build(lengths, 288);
    u8 d[30];
    for (int i = 0; i < 30; i++) d[i] = 5;
    dist.Build(d, 30);
}

bool InflateBlockData(BitReader& br, const Huffman& lit, const Huffman& dist, Buffer& out) {
    for (;;) {
        const int sym = lit.Decode(br);
        if (sym < 0) return false;
        if (sym < 256) {
            out.push_back(static_cast<u8>(sym));
            continue;
        }
        if (sym == 256) return true;
        const int li = sym - 257;
        if (li < 0 || li >= 29) return false;
        u32 extra = 0;
        if (kLenExtra[li] && !br.Bits(kLenExtra[li], extra)) return false;
        const u32 length = kLenBase[li] + extra;

        const int ds = dist.Decode(br);
        if (ds < 0 || ds >= 30) return false;
        u32 dextra = 0;
        if (kDistExtra[ds] && !br.Bits(kDistExtra[ds], dextra)) return false;
        const u32 distance = kDistBase[ds] + dextra;
        if (distance == 0 || distance > out.size()) return false;

        const std::size_t start = out.size() - distance;
        for (u32 k = 0; k < length; k++) out.push_back(out[start + k]);
    }
}

}  // namespace

bool InflateRaw(const u8* in, std::size_t inLen, Buffer& out, std::size_t expected) {
    BitReader br(in, inLen);
    out.clear();
    out.reserve(expected ? expected : inLen * 4);

    for (;;) {
        u32 last = 0, type = 0;
        if (!br.Bits(1, last) || !br.Bits(2, type)) return false;

        if (type == 0) {                      // 存储块
            br.Align();
            u8 head[4];
            if (!br.ReadBytes(head, 4)) return false;
            const u32 len = head[0] | (head[1] << 8);
            const u32 nlen = head[2] | (head[3] << 8);
            if ((len ^ 0xFFFF) != nlen) return false;
            const std::size_t at = out.size();
            out.resize(at + len);
            if (!br.ReadBytes(out.data() + at, len)) return false;
        } else if (type == 1) {               // 固定 Huffman
            Huffman lit, dist;
            BuildFixed(lit, dist);
            if (!InflateBlockData(br, lit, dist, out)) return false;
        } else if (type == 2) {               // 动态 Huffman
            u32 hlit = 0, hdist = 0, hclen = 0;
            if (!br.Bits(5, hlit) || !br.Bits(5, hdist) || !br.Bits(4, hclen)) return false;
            const int nlit = static_cast<int>(hlit) + 257;
            const int ndist = static_cast<int>(hdist) + 1;
            const int nclen = static_cast<int>(hclen) + 4;
            if (nlit > 288 || ndist > 30) return false;

            static const int kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5,
                                           11, 4, 12, 3, 13, 2, 14, 1, 15};
            u8 clen[19] = {0};
            for (int i = 0; i < nclen; i++) {
                u32 v = 0;
                if (!br.Bits(3, v)) return false;
                clen[kOrder[i]] = static_cast<u8>(v);
            }
            Huffman clh;
            clh.Build(clen, 19);

            std::vector<u8> lens(static_cast<std::size_t>(nlit + ndist), 0);
            int idx = 0;
            while (idx < nlit + ndist) {
                const int sym = clh.Decode(br);
                if (sym < 0) return false;
                if (sym < 16) {
                    lens[static_cast<std::size_t>(idx++)] = static_cast<u8>(sym);
                } else if (sym == 16) {
                    if (idx == 0) return false;
                    u32 rep = 0;
                    if (!br.Bits(2, rep)) return false;
                    const u8 prev = lens[static_cast<std::size_t>(idx - 1)];
                    for (u32 k = 0; k < 3 + rep && idx < nlit + ndist; k++) lens[static_cast<std::size_t>(idx++)] = prev;
                } else if (sym == 17) {
                    u32 rep = 0;
                    if (!br.Bits(3, rep)) return false;
                    idx += static_cast<int>(3 + rep);
                } else if (sym == 18) {
                    u32 rep = 0;
                    if (!br.Bits(7, rep)) return false;
                    idx += static_cast<int>(11 + rep);
                } else {
                    return false;
                }
                if (idx > nlit + ndist) return false;
            }

            Huffman lit, dist;
            lit.Build(lens.data(), nlit);
            dist.Build(lens.data() + nlit, ndist);
            if (!InflateBlockData(br, lit, dist, out)) return false;
        } else {
            return false;                     // 保留类型
        }

        if (last) break;
        if (out.size() > (expected ? expected * 64u + 65536u : (1u << 26))) return false;   // 防爆内存
    }

    if (expected && out.size() > expected) out.resize(expected);
    return true;
}

// ───────────────────────── PKWARE DCL implode ─────────────────────────
namespace {

const int kMaxBits = 13;const u8 kLitLen[] = {
    11, 124, 8, 7, 28, 7, 188, 13, 76, 4, 10, 8, 12, 10, 12, 10, 8, 23, 8,
    9, 7, 6, 7, 8, 7, 6, 55, 8, 23, 24, 12, 11, 7, 9, 11, 12, 6, 7, 22, 5,
    7, 24, 6, 11, 9, 6, 7, 22, 7, 11, 38, 7, 9, 8, 25, 11, 8, 11, 9, 12,
    8, 12, 5, 38, 5, 38, 5, 11, 7, 5, 6, 21, 6, 10, 53, 8, 7, 24, 10, 27,
    44, 253, 253, 253, 252, 252, 252, 13, 12, 45, 12, 45, 12, 61, 12, 45,
    44, 173};
const u8 kLenLen[] = {2, 35, 36, 53, 38, 23};
const u8 kDistLen[] = {2, 20, 53, 230, 247, 151, 248};
const u16 kPkBase[16] = {3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264};
const u8 kPkExtra[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8};

struct PkHuffman {
    int count[kMaxBits + 1] = {0};
    std::vector<int> sym;
};

PkHuffman PkConstruct(const u8* rep, int n) {
    std::vector<u8> lengths;
    int i = 0, remain = n;
    for (;;) {
        const u8 b = rep[i++];
        const int left = (b >> 4) + 1;
        const u8 ln = b & 15;
        for (int k = 0; k < left; k++) lengths.push_back(ln);
        if (--remain == 0) break;
    }
    PkHuffman h;
    for (u8 l : lengths) h.count[l]++;
    int offs[kMaxBits + 1] = {0};
    for (int ln = 1; ln < kMaxBits; ln++) offs[ln + 1] = offs[ln] + h.count[ln];
    h.sym.assign(lengths.size(), 0);
    for (std::size_t s = 0; s < lengths.size(); s++) {
        const u8 l = lengths[s];
        if (l) h.sym[static_cast<std::size_t>(offs[l]++)] = static_cast<int>(s);
    }
    return h;
}

int PkDecode(BitReader& br, const PkHuffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= kMaxBits; len++) {
        u32 bit = 0;
        if (!br.Bits(1, bit)) return -1;
        code |= static_cast<int>(bit ^ 1);
        const int c = h.count[len];
        if (code < first + c) return h.sym[static_cast<std::size_t>(index + (code - first))];
        index += c;
        first = (first + c) << 1;
        code <<= 1;
    }
    return -1;
}

}  // namespace

bool Explode(const u8* in, std::size_t inLen, Buffer& out, std::size_t expected) {    static const PkHuffman kLit = PkConstruct(kLitLen, sizeof(kLitLen));
    static const PkHuffman kLen = PkConstruct(kLenLen, sizeof(kLenLen));
    static const PkHuffman kDist = PkConstruct(kDistLen, sizeof(kDistLen));

    BitReader br(in, inLen);
    out.clear();
    out.reserve(expected ? expected : inLen * 4);

    u32 lit = 0, dictBits = 0;
    if (!br.Bits(8, lit) || !br.Bits(8, dictBits)) { g_lastCompressType = 0xE1; return false; }
    g_explodeLit = static_cast<int>(lit);
    g_explodeDict = static_cast<int>(dictBits);

    while (!expected || out.size() < expected) {
        u32 flag = 0;
        if (!br.Bits(1, flag)) return false;
        if (flag) {
            const int sym = PkDecode(br, kLen);
            if (sym < 0 || static_cast<std::size_t>(sym) >= kLen.sym.size()) {
                g_lastCompressType = 0xE2;
                return false;
            }
            u32 extra = 0;
            if (kPkExtra[sym] && !br.Bits(kPkExtra[sym], extra)) return false;
            const u32 length = kPkBase[sym] + extra;
            if (length == 519) break;                     // 结束标记
            const int s = (length == 2) ? 2 : static_cast<int>(dictBits);
            const int dsym = PkDecode(br, kDist);
            if (dsym < 0 || static_cast<std::size_t>(dsym) >= kDist.sym.size()) {
                g_lastCompressType = 0xE3;
                return false;
            }
            u32 dextra = 0;
            if (s > 0 && !br.Bits(s, dextra)) return false;
            const u32 dist = (static_cast<u32>(dsym) << s) + dextra + 1;
            if (dist == 0 || dist > out.size()) { g_lastCompressType = 0xE4; return false; }
            const std::size_t start = out.size() - dist;
            for (u32 k = 0; k < length; k++) out.push_back(out[start + k]);
        } else {
            if (lit) {
                const int sym = PkDecode(br, kLit);
                if (sym < 0 || static_cast<std::size_t>(sym) >= kLit.sym.size()) return false;
                out.push_back(static_cast<u8>(sym));
            } else {
                u32 v = 0;
                if (!br.Bits(8, v)) return false;
                out.push_back(static_cast<u8>(v));
            }
        }
        if (out.size() > (expected ? expected + 65536u : (1u << 26))) return false;
    }
    if (expected && out.size() > expected) out.resize(expected);
    return true;
}

u8 LastCompressType() { return g_lastCompressType; }
int LastExplodeLiteralMode() { return g_explodeLit; }
int LastExplodeDictBits() { return g_explodeDict; }

bool DecompressSector(const u8* in, std::size_t inLen, std::size_t expected, Buffer& out) {
    if (inLen == 0) { out.clear(); return true; }
    const u8 type = in[0];
    g_lastCompressType = type;
    switch (type) {
        case 0x00:                       // 未压缩
            out.assign(in + 1, in + inLen);
            if (expected && out.size() > expected) out.resize(expected);
            return true;
        case 0x02: {                     // zlib（带 2 字节头）
            const u8* d = in + 1;
            std::size_t len = inLen - 1;
            const bool zlibHeader =
                len >= 2 && (d[0] & 0x0F) == 8 &&
                ((static_cast<unsigned>(d[0]) << 8 | d[1]) % 31) == 0;
            if (zlibHeader) {
                d += 2;
                len -= 2;
            }
            if (InflateRaw(d, len, out, expected)) return true;
            if (zlibHeader) return InflateRaw(in + 1, inLen - 1, out, expected);   // 回退尝试裸流
            return false;
        }
        case 0x08:                       // PKWARE implode
            return Explode(in + 1, inLen - 1, out, expected);
        default:
            return false;                // 0x01 huffman / 0x10 bz2 等暂不支持
    }
}

}  // namespace mpq
}  // namespace scmd
