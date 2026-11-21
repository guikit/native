#include "png_decoder.hpp"

#include "keel/bits.h"
#include "keel/math.h"
#include "keel/memory.h"

namespace guikit::native::detail {

namespace {

static constexpr keel::u8 kPngSig[8] = {0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au, 0x1Au, 0x0Au};
static constexpr keel::u32 kMaxPngDim = 4096u;
static constexpr keel::usize kMaxIdatBytes = 64u * 1024u * 1024u;

[[nodiscard]] inline keel::u32 ReadBe32(const keel::u8* p) noexcept {
    return (static_cast<keel::u32>(p[0]) << 24) |
           (static_cast<keel::u32>(p[1]) << 16) |
           (static_cast<keel::u32>(p[2]) << 8) |
           static_cast<keel::u32>(p[3]);
}

[[nodiscard]] inline keel::u16 ReadBe16(const keel::u8* p) noexcept {
    return static_cast<keel::u16>((static_cast<keel::u32>(p[0]) << 8) | static_cast<keel::u32>(p[1]));
}

[[nodiscard]] inline keel::u32 PackRgba(keel::u8 r, keel::u8 g, keel::u8 b, keel::u8 a) noexcept {
    return static_cast<keel::u32>(r) |
           (static_cast<keel::u32>(g) << 8) |
           (static_cast<keel::u32>(b) << 16) |
           (static_cast<keel::u32>(a) << 24);
}

[[nodiscard]] inline keel::u8 PaethPredictor(keel::i32 a, keel::i32 b, keel::i32 c) noexcept {
    const keel::i32 p = a + b - c;
    const keel::i32 pa = keel::abs(p - a);
    const keel::i32 pb = keel::abs(p - b);
    const keel::i32 pc = keel::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<keel::u8>(a);
    if (pb <= pc) return static_cast<keel::u8>(b);
    return static_cast<keel::u8>(c);
}

// ============================================================================
// RFC 1951 DEFLATE Inflater (Canonical Huffman + 9-bit Fast Lookup Table)
// ============================================================================

struct BitReader {
    const keel::u8* ptr{nullptr};
    const keel::u8* end{nullptr};
    keel::u64 buf{0};
    keel::i32 count{0};

    inline void Fill(keel::i32 needed) noexcept {
        while (count < needed && ptr < end) {
            buf |= static_cast<keel::u64>(*ptr++) << count;
            count += 8;
        }
    }

    [[nodiscard]] inline bool ReadBits(keel::i32 n, keel::u32& out) noexcept {
        Fill(n);
        if (count < n) {
            out = 0u;
            return false;
        }
        out = static_cast<keel::u32>(buf & ((1ULL << n) - 1ULL));
        buf >>= n;
        count -= n;
        return true;
    }

    inline void AlignByteAndRewind() noexcept {
        const keel::i32 drop = count & 7;
        buf >>= drop;
        count -= drop;
        const keel::i32 wholeBytes = count >> 3;
        ptr -= wholeBytes;
        buf = 0u;
        count = 0;
    }
};

struct HuffTable {
    static constexpr keel::u32 kMaxBits = 15u;
    static constexpr keel::u32 kFastBits = 9u;
    static constexpr keel::u32 kFastSize = 1u << kFastBits;

    keel::u16 counts[kMaxBits + 1u]{};
    keel::u16 firstCode[kMaxBits + 1u]{};
    keel::u16 firstSymbol[kMaxBits + 1u]{};
    keel::u16 symbols[320]{};
    keel::u16 fastLut[kFastSize]{}; // bits [0..8]=symbol, bits [9..15]=bitLen (0 if slow)

    [[nodiscard]] static inline keel::u32 ReverseBits(keel::u32 v, keel::u32 n) noexcept {
        keel::u32 r = 0u;
        for (keel::u32 i = 0u; i < n; ++i) {
            r = (r << 1) | (v & 1u);
            v >>= 1;
        }
        return r;
    }

    bool Build(const keel::u8* lengths, keel::u32 numSyms) noexcept {
        if (numSyms > 320u) return false;
        keel::mem_zero(counts, sizeof(counts));
        keel::mem_zero(fastLut, sizeof(fastLut));

        for (keel::u32 i = 0u; i < numSyms; ++i) {
            if (lengths[i] > kMaxBits) return false;
            if (lengths[i] > 0u) {
                ++counts[lengths[i]];
            }
        }

        keel::u32 code = 0u;
        keel::u32 symOffset = 0u;
        keel::i32 left = 1;
        for (keel::u32 bits = 1u; bits <= kMaxBits; ++bits) {
            code <<= 1;
            left <<= 1;
            left -= static_cast<keel::i32>(counts[bits]);
            if (left < 0) return false; // Over-subscribed Huffman tree
            firstCode[bits] = static_cast<keel::u16>(code);
            firstSymbol[bits] = static_cast<keel::u16>(symOffset);
            code += counts[bits];
            symOffset += counts[bits];
        }

        keel::u16 nextPos[kMaxBits + 1u]{};
        for (keel::u32 bits = 1u; bits <= kMaxBits; ++bits) {
            nextPos[bits] = firstSymbol[bits];
        }

        for (keel::u32 sym = 0u; sym < numSyms; ++sym) {
            const keel::u32 len = lengths[sym];
            if (len == 0u) continue;
            const keel::u32 idx = nextPos[len]++;
            symbols[idx] = static_cast<keel::u16>(sym);
            if (len <= kFastBits) {
                const keel::u32 canonicalCode = static_cast<keel::u32>(firstCode[len]) + (idx - firstSymbol[len]);
                const keel::u32 rev = ReverseBits(canonicalCode, len);
                const keel::u32 step = 1u << len;
                const keel::u16 entry = static_cast<keel::u16>((sym & 0x1FFu) | (len << 9));
                for (keel::u32 k = rev; k < kFastSize; k += step) {
                    fastLut[k] = entry;
                }
            }
        }
        return true;
    }

    [[nodiscard]] bool Decode(BitReader& br, keel::u32& outSym) const noexcept {
        br.Fill(static_cast<keel::i32>(kMaxBits));
        if (br.count <= 0) return false;

        const keel::u16 fast = fastLut[br.buf & (kFastSize - 1u)];
        const keel::u32 fastLen = static_cast<keel::u32>(fast >> 9);
        if (fastLen != 0u && br.count >= static_cast<keel::i32>(fastLen)) {
            br.buf >>= fastLen;
            br.count -= static_cast<keel::i32>(fastLen);
            outSym = static_cast<keel::u32>(fast & 0x1FFu);
            return true;
        }

        keel::u32 code = 0u;
        for (keel::u32 bits = 1u; bits <= kMaxBits; ++bits) {
            if (br.count <= 0) {
                br.Fill(1);
                if (br.count <= 0) return false;
            }
            code = (code << 1) | static_cast<keel::u32>(br.buf & 1u);
            br.buf >>= 1;
            --br.count;
            const keel::u32 fc = firstCode[bits];
            const keel::u32 cnt = counts[bits];
            if (code >= fc && (code - fc) < cnt) {
                outSym = symbols[firstSymbol[bits] + (code - fc)];
                return true;
            }
        }
        return false;
    }
};

bool InflateZlib(
    keel::ArrayRef<keel::u8> zlibData,
    keel::usize expectedBytes,
    keel::Array<keel::u8>& out
) {
    if (zlibData.size() < 6u) return false;
    const keel::u8 cmf = zlibData[0];
    const keel::u8 flg = zlibData[1];
    if ((cmf & 0x0Fu) != 8u) return false;
    if ((((static_cast<keel::u32>(cmf) << 8) | flg) % 31u) != 0u) return false;
    if ((flg & 0x20u) != 0u) return false; // Preset dictionary not valid in PNG

    out.clear();
    out.reserve(expectedBytes);

    BitReader br{};
    br.ptr = zlibData.data() + 2u;
    br.end = zlibData.data() + zlibData.size();

    static constexpr keel::u16 kLenBase[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
    };
    static constexpr keel::u8 kLenExtra[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
    };
    static constexpr keel::u16 kDistBase[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
        8193, 12289, 16385, 24577
    };
    static constexpr keel::u8 kDistExtra[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
    };
    static constexpr keel::u8 kCodeLenOrder[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };

    HuffTable litTable{};
    HuffTable distTable{};
    HuffTable codeTable{};

    keel::u32 bfinal = 0u;
    do {
        keel::u32 btype = 0u;
        if (!br.ReadBits(1, bfinal) || !br.ReadBits(2, btype)) {
            return false;
        }

        if (btype == 0u) {
            br.AlignByteAndRewind();
            if (br.end - br.ptr < 4) return false;
            const keel::u16 len = static_cast<keel::u16>(br.ptr[0] | (static_cast<keel::u32>(br.ptr[1]) << 8));
            const keel::u16 nlen = static_cast<keel::u16>(br.ptr[2] | (static_cast<keel::u32>(br.ptr[3]) << 8));
            br.ptr += 4;
            if (static_cast<keel::u16>(len ^ 0xFFFFu) != nlen) return false;
            if (static_cast<keel::usize>(br.end - br.ptr) < len) return false;
            if (out.size() + len > expectedBytes) return false;
            for (keel::u32 i = 0u; i < len; ++i) {
                out.push_back(br.ptr[i]);
            }
            br.ptr += len;
            continue;
        }

        if (btype == 1u) {
            keel::u8 litLens[288]{};
            for (keel::u32 i = 0u; i <= 143u; ++i) litLens[i] = 8u;
            for (keel::u32 i = 144u; i <= 255u; ++i) litLens[i] = 9u;
            for (keel::u32 i = 256u; i <= 279u; ++i) litLens[i] = 7u;
            for (keel::u32 i = 280u; i <= 287u; ++i) litLens[i] = 8u;
            keel::u8 distLens[32]{};
            for (keel::u32 i = 0u; i < 32u; ++i) distLens[i] = 5u;
            if (!litTable.Build(litLens, 288u) || !distTable.Build(distLens, 32u)) {
                return false;
            }
        } else if (btype == 2u) {
            keel::u32 hlit = 0u;
            keel::u32 hdist = 0u;
            keel::u32 hclen = 0u;
            if (!br.ReadBits(5, hlit) || !br.ReadBits(5, hdist) || !br.ReadBits(4, hclen)) {
                return false;
            }
            hlit += 257u;
            hdist += 1u;
            hclen += 4u;
            if (hlit > 286u || hdist > 30u) return false;

            keel::u8 codeLens[19]{};
            for (keel::u32 i = 0u; i < hclen; ++i) {
                keel::u32 v = 0u;
                if (!br.ReadBits(3, v)) return false;
                codeLens[kCodeLenOrder[i]] = static_cast<keel::u8>(v);
            }
            if (!codeTable.Build(codeLens, 19u)) return false;

            keel::u8 allLens[320]{};
            const keel::u32 total = hlit + hdist;
            keel::u32 idx = 0u;
            while (idx < total) {
                keel::u32 sym = 0u;
                if (!codeTable.Decode(br, sym)) return false;
                if (sym < 16u) {
                    allLens[idx++] = static_cast<keel::u8>(sym);
                } else {
                    keel::u32 rep = 0u;
                    keel::u8 val = 0u;
                    if (sym == 16u) {
                        if (idx == 0u || !br.ReadBits(2, rep)) return false;
                        rep += 3u;
                        val = allLens[idx - 1u];
                    } else if (sym == 17u) {
                        if (!br.ReadBits(3, rep)) return false;
                        rep += 3u;
                    } else if (sym == 18u) {
                        if (!br.ReadBits(7, rep)) return false;
                        rep += 11u;
                    } else {
                        return false;
                    }
                    if (idx + rep > total) return false;
                    for (keel::u32 r = 0u; r < rep; ++r) {
                        allLens[idx++] = val;
                    }
                }
            }
            if (!litTable.Build(allLens, hlit) || !distTable.Build(allLens + hlit, hdist)) {
                return false;
            }
        } else {
            return false;
        }

        for (;;) {
            keel::u32 sym = 0u;
            if (!litTable.Decode(br, sym)) return false;
            if (sym < 256u) {
                if (out.size() >= expectedBytes) return false;
                out.push_back(static_cast<keel::u8>(sym));
            } else if (sym == 256u) {
                break;
            } else {
                const keel::u32 lenIdx = sym - 257u;
                if (lenIdx >= 29u) return false;
                keel::u32 extraLen = 0u;
                if (kLenExtra[lenIdx] > 0u && !br.ReadBits(kLenExtra[lenIdx], extraLen)) {
                    return false;
                }
                const keel::u32 matchLen = kLenBase[lenIdx] + extraLen;

                keel::u32 distSym = 0u;
                if (!distTable.Decode(br, distSym) || distSym >= 30u) return false;
                keel::u32 extraDist = 0u;
                if (kDistExtra[distSym] > 0u && !br.ReadBits(kDistExtra[distSym], extraDist)) {
                    return false;
                }
                const keel::u32 dist = kDistBase[distSym] + extraDist;
                if (dist == 0u || dist > out.size()) return false;
                if (out.size() + matchLen > expectedBytes) return false;

                keel::usize srcPos = out.size() - dist;
                for (keel::u32 k = 0u; k < matchLen; ++k) {
                    out.push_back(out[srcPos++]);
                }
            }
        }
    } while (bfinal == 0u);

    return out.size() == expectedBytes;
}

[[nodiscard]] keel::u32 ChannelsForColorType(keel::u8 colorType) noexcept {
    switch (colorType) {
        case 0u: return 1u; // Grayscale
        case 2u: return 3u; // RGB
        case 3u: return 1u; // Indexed
        case 4u: return 2u; // Grayscale + Alpha
        case 6u: return 4u; // RGBA
        default: return 0u;
    }
}

[[nodiscard]] bool ValidateBitDepth(keel::u8 colorType, keel::u8 bitDepth) noexcept {
    switch (colorType) {
        case 0u: return bitDepth == 1u || bitDepth == 2u || bitDepth == 4u || bitDepth == 8u || bitDepth == 16u;
        case 2u: return bitDepth == 8u || bitDepth == 16u;
        case 3u: return bitDepth == 1u || bitDepth == 2u || bitDepth == 4u || bitDepth == 8u;
        case 4u: return bitDepth == 8u || bitDepth == 16u;
        case 6u: return bitDepth == 8u || bitDepth == 16u;
        default: return false;
    }
}

[[nodiscard]] keel::usize RowByteCount(keel::u32 width, keel::u32 channels, keel::u8 bitDepth) noexcept {
    const keel::u64 bits = static_cast<keel::u64>(width) * channels * bitDepth;
    return static_cast<keel::usize>((bits + 7u) / 8u);
}

struct PngDecodeMeta {
    keel::u32 width{0};
    keel::u32 height{0};
    keel::u8 bitDepth{0};
    keel::u8 colorType{0};
    keel::u32 channels{0};
    keel::u8 palette[256][3]{};
    keel::u32 paletteCount{0};
    keel::u8 paletteAlpha[256]{};
    bool hasTrnsColor{false};
    keel::u16 trnsGray{0};
    keel::u16 trnsR{0};
    keel::u16 trnsG{0};
    keel::u16 trnsB{0};
};

[[nodiscard]] keel::u8 ExpandSubByteSample(keel::u32 rawVal, keel::u8 bitDepth) noexcept {
    switch (bitDepth) {
        case 1u: return rawVal ? 255u : 0u;
        case 2u: return static_cast<keel::u8>(rawVal * 85u);
        case 4u: return static_cast<keel::u8>(rawVal * 17u);
        default: return static_cast<keel::u8>(rawVal & 0xFFu);
    }
}

bool UnfilterAndBlitPass(
    const keel::u8* src,
    keel::usize srcAvail,
    keel::usize& srcConsumed,
    keel::u32 passW,
    keel::u32 passH,
    keel::u32 xOffset,
    keel::u32 yOffset,
    keel::u32 xStep,
    keel::u32 yStep,
    const PngDecodeMeta& meta,
    keel::Array<keel::u32>& dstRgba
) {
    if (passW == 0u || passH == 0u) return true;

    const keel::usize rowBytes = RowByteCount(passW, meta.channels, meta.bitDepth);
    const keel::usize bpp = keel::max<keel::usize>(1u, (meta.channels * meta.bitDepth + 7u) / 8u);
    const keel::usize needed = static_cast<keel::usize>(passH) * (1u + rowBytes);
    if (srcConsumed + needed > srcAvail) return false;

    keel::Array<keel::u8> prevRow(rowBytes, 0u);
    keel::Array<keel::u8> curRow(rowBytes, 0u);

    for (keel::u32 py = 0u; py < passH; ++py) {
        const keel::u8 filter = src[srcConsumed++];
        const keel::u8* rawRow = src + srcConsumed;
        srcConsumed += rowBytes;

        switch (filter) {
            case 0u:
                keel::mem_copy(curRow.data(), rawRow, rowBytes);
                break;
            case 1u:
                for (keel::usize i = 0u; i < rowBytes; ++i) {
                    const keel::u8 a = (i >= bpp) ? curRow[i - bpp] : 0u;
                    curRow[i] = static_cast<keel::u8>(rawRow[i] + a);
                }
                break;
            case 2u:
                for (keel::usize i = 0u; i < rowBytes; ++i) {
                    curRow[i] = static_cast<keel::u8>(rawRow[i] + prevRow[i]);
                }
                break;
            case 3u:
                for (keel::usize i = 0u; i < rowBytes; ++i) {
                    const keel::u32 a = (i >= bpp) ? curRow[i - bpp] : 0u;
                    const keel::u32 b = prevRow[i];
                    curRow[i] = static_cast<keel::u8>(rawRow[i] + ((a + b) >> 1));
                }
                break;
            case 4u:
                for (keel::usize i = 0u; i < rowBytes; ++i) {
                    const keel::i32 a = (i >= bpp) ? static_cast<keel::i32>(curRow[i - bpp]) : 0;
                    const keel::i32 b = static_cast<keel::i32>(prevRow[i]);
                    const keel::i32 c = (i >= bpp) ? static_cast<keel::i32>(prevRow[i - bpp]) : 0;
                    curRow[i] = static_cast<keel::u8>(rawRow[i] + PaethPredictor(a, b, c));
                }
                break;
            default:
                return false;
        }

        const keel::u32 dstY = yOffset + py * yStep;
        keel::u32* dstRow = dstRgba.data() + static_cast<keel::usize>(dstY) * meta.width;

        for (keel::u32 px = 0u; px < passW; ++px) {
            const keel::u32 dstX = xOffset + px * xStep;
            keel::u8 r = 0u;
            keel::u8 g = 0u;
            keel::u8 b = 0u;
            keel::u8 a = 255u;

            if (meta.colorType == 6u) {
                if (meta.bitDepth == 8u) {
                    const keel::usize off = static_cast<keel::usize>(px) * 4u;
                    r = curRow[off];
                    g = curRow[off + 1u];
                    b = curRow[off + 2u];
                    a = curRow[off + 3u];
                } else {
                    const keel::usize off = static_cast<keel::usize>(px) * 8u;
                    r = curRow[off];
                    g = curRow[off + 2u];
                    b = curRow[off + 4u];
                    a = curRow[off + 6u];
                }
            } else if (meta.colorType == 2u) {
                if (meta.bitDepth == 8u) {
                    const keel::usize off = static_cast<keel::usize>(px) * 3u;
                    r = curRow[off];
                    g = curRow[off + 1u];
                    b = curRow[off + 2u];
                    if (meta.hasTrnsColor && r == meta.trnsR && g == meta.trnsG && b == meta.trnsB) {
                        a = 0u;
                    }
                } else {
                    const keel::usize off = static_cast<keel::usize>(px) * 6u;
                    const keel::u16 r16 = ReadBe16(&curRow[off]);
                    const keel::u16 g16 = ReadBe16(&curRow[off + 2u]);
                    const keel::u16 b16 = ReadBe16(&curRow[off + 4u]);
                    r = static_cast<keel::u8>(r16 >> 8);
                    g = static_cast<keel::u8>(g16 >> 8);
                    b = static_cast<keel::u8>(b16 >> 8);
                    if (meta.hasTrnsColor && r16 == meta.trnsR && g16 == meta.trnsG && b16 == meta.trnsB) {
                        a = 0u;
                    }
                }
            } else if (meta.colorType == 3u) {
                keel::u32 idxVal = 0u;
                if (meta.bitDepth == 8u) {
                    idxVal = curRow[px];
                } else {
                    const keel::u32 perByte = 8u / meta.bitDepth;
                    const keel::u8 byteVal = curRow[px / perByte];
                    const keel::u32 shift = (perByte - 1u - (px % perByte)) * meta.bitDepth;
                    const keel::u32 mask = (1u << meta.bitDepth) - 1u;
                    idxVal = (byteVal >> shift) & mask;
                }
                if (idxVal < meta.paletteCount) {
                    r = meta.palette[idxVal][0];
                    g = meta.palette[idxVal][1];
                    b = meta.palette[idxVal][2];
                    a = meta.paletteAlpha[idxVal];
                } else {
                    a = 0u;
                }
            } else if (meta.colorType == 4u) {
                if (meta.bitDepth == 8u) {
                    const keel::usize off = static_cast<keel::usize>(px) * 2u;
                    r = g = b = curRow[off];
                    a = curRow[off + 1u];
                } else {
                    const keel::usize off = static_cast<keel::usize>(px) * 4u;
                    r = g = b = curRow[off];
                    a = curRow[off + 2u];
                }
            } else if (meta.colorType == 0u) {
                if (meta.bitDepth == 16u) {
                    const keel::u16 gv = ReadBe16(&curRow[static_cast<keel::usize>(px) * 2u]);
                    r = g = b = static_cast<keel::u8>(gv >> 8);
                    if (meta.hasTrnsColor && gv == meta.trnsGray) a = 0u;
                } else if (meta.bitDepth == 8u) {
                    const keel::u8 gv = curRow[px];
                    r = g = b = gv;
                    if (meta.hasTrnsColor && gv == meta.trnsGray) a = 0u;
                } else {
                    const keel::u32 perByte = 8u / meta.bitDepth;
                    const keel::u8 byteVal = curRow[px / perByte];
                    const keel::u32 shift = (perByte - 1u - (px % perByte)) * meta.bitDepth;
                    const keel::u32 mask = (1u << meta.bitDepth) - 1u;
                    const keel::u32 rawVal = (byteVal >> shift) & mask;
                    r = g = b = ExpandSubByteSample(rawVal, meta.bitDepth);
                    if (meta.hasTrnsColor && rawVal == meta.trnsGray) a = 0u;
                }
            }

            dstRow[dstX] = PackRgba(r, g, b, a);
        }

        keel::mem_copy(prevRow.data(), curRow.data(), rowBytes);
    }

    return true;
}

} // namespace

bool InspectPngDimensions(
    keel::ArrayRef<keel::u8> encodedBytes,
    keel::u32& outWidth,
    keel::u32& outHeight
) noexcept {
    outWidth = 0u;
    outHeight = 0u;
    if (encodedBytes.size() < 33u) return false;
    if (keel::mem_cmp(encodedBytes.data(), kPngSig, sizeof(kPngSig)) != 0) return false;

    const keel::u32 ihdrLen = ReadBe32(encodedBytes.data() + 8u);
    if (ihdrLen != 13u) return false;
    if (keel::mem_cmp(encodedBytes.data() + 12u, "IHDR", 4u) != 0) return false;

    const keel::u32 w = ReadBe32(encodedBytes.data() + 16u);
    const keel::u32 h = ReadBe32(encodedBytes.data() + 20u);
    if (w == 0u || h == 0u || w > kMaxPngDim || h > kMaxPngDim) return false;

    outWidth = w;
    outHeight = h;
    return true;
}

bool DecodePngFromMemory(
    keel::ArrayRef<keel::u8> encodedBytes,
    keel::u32& outWidth,
    keel::u32& outHeight,
    keel::Array<keel::u32>& outRgbaPixels
) {
    outWidth = 0u;
    outHeight = 0u;
    if (encodedBytes.size() < 33u) return false;
    if (keel::mem_cmp(encodedBytes.data(), kPngSig, sizeof(kPngSig)) != 0) return false;

    PngDecodeMeta meta{};
    for (keel::u32 i = 0u; i < 256u; ++i) {
        meta.paletteAlpha[i] = 255u;
    }

    bool haveIhdr = false;
    keel::u8 interlace = 0u;
    keel::Array<keel::u8> idat;

    keel::usize pos = 8u;
    while (pos + 12u <= encodedBytes.size()) {
        const keel::u32 chunkLen = ReadBe32(encodedBytes.data() + pos);
        const keel::u8* chunkType = encodedBytes.data() + pos + 4u;
        pos += 8u;
        if (chunkLen > encodedBytes.size() - pos || (encodedBytes.size() - pos - chunkLen) < 4u) {
            return false;
        }
        const keel::u8* chunkData = encodedBytes.data() + pos;
        pos += static_cast<keel::usize>(chunkLen) + 4u; // skip data + CRC

        if (keel::mem_cmp(chunkType, "IHDR", 4u) == 0) {
            if (chunkLen != 13u || haveIhdr) return false;
            meta.width = ReadBe32(chunkData);
            meta.height = ReadBe32(chunkData + 4u);
            meta.bitDepth = chunkData[8];
            meta.colorType = chunkData[9];
            const keel::u8 compression = chunkData[10];
            const keel::u8 filterMethod = chunkData[11];
            interlace = chunkData[12];

            if (meta.width == 0u || meta.height == 0u || meta.width > kMaxPngDim || meta.height > kMaxPngDim) {
                return false;
            }
            if (compression != 0u || filterMethod != 0u || interlace > 1u) {
                return false;
            }
            meta.channels = ChannelsForColorType(meta.colorType);
            if (meta.channels == 0u || !ValidateBitDepth(meta.colorType, meta.bitDepth)) {
                return false;
            }
            haveIhdr = true;
        } else if (keel::mem_cmp(chunkType, "PLTE", 4u) == 0) {
            if ((chunkLen % 3u) != 0u || chunkLen > 768u) return false;
            meta.paletteCount = chunkLen / 3u;
            for (keel::u32 i = 0u; i < meta.paletteCount; ++i) {
                meta.palette[i][0] = chunkData[i * 3u];
                meta.palette[i][1] = chunkData[i * 3u + 1u];
                meta.palette[i][2] = chunkData[i * 3u + 2u];
            }
        } else if (keel::mem_cmp(chunkType, "tRNS", 4u) == 0) {
            if (meta.colorType == 3u) {
                const keel::u32 n = keel::min<keel::u32>(chunkLen, 256u);
                for (keel::u32 i = 0u; i < n; ++i) {
                    meta.paletteAlpha[i] = chunkData[i];
                }
            } else if (meta.colorType == 0u && chunkLen == 2u) {
                meta.hasTrnsColor = true;
                meta.trnsGray = ReadBe16(chunkData);
            } else if (meta.colorType == 2u && chunkLen == 6u) {
                meta.hasTrnsColor = true;
                meta.trnsR = ReadBe16(chunkData);
                meta.trnsG = ReadBe16(chunkData + 2u);
                meta.trnsB = ReadBe16(chunkData + 4u);
            }
        } else if (keel::mem_cmp(chunkType, "IDAT", 4u) == 0) {
            if (!haveIhdr) return false;
            if (idat.size() + chunkLen > kMaxIdatBytes) return false;
            const keel::usize oldSize = idat.size();
            idat.resize(oldSize + chunkLen);
            if (chunkLen > 0u) {
                keel::mem_copy(idat.data() + oldSize, chunkData, chunkLen);
            }
        } else if (keel::mem_cmp(chunkType, "IEND", 4u) == 0) {
            break;
        }
    }

    if (!haveIhdr || idat.empty()) return false;
    if (meta.colorType == 3u && meta.paletteCount == 0u) return false;

    static constexpr keel::u32 kAdam7X0[7] = {0u, 4u, 0u, 2u, 0u, 1u, 0u};
    static constexpr keel::u32 kAdam7Y0[7] = {0u, 0u, 4u, 0u, 2u, 0u, 1u};
    static constexpr keel::u32 kAdam7XS[7] = {8u, 8u, 4u, 4u, 2u, 2u, 1u};
    static constexpr keel::u32 kAdam7YS[7] = {8u, 8u, 8u, 4u, 4u, 2u, 2u};

    keel::usize expectedRawBytes = 0u;
    if (interlace == 0u) {
        const keel::usize rowBytes = RowByteCount(meta.width, meta.channels, meta.bitDepth);
        if (!keel::checked_mul(static_cast<keel::usize>(meta.height), 1u + rowBytes, expectedRawBytes)) {
            return false;
        }
    } else {
        for (keel::u32 p = 0u; p < 7u; ++p) {
            const keel::u32 pw = (meta.width > kAdam7X0[p]) ? ((meta.width - kAdam7X0[p] + kAdam7XS[p] - 1u) / kAdam7XS[p]) : 0u;
            const keel::u32 ph = (meta.height > kAdam7Y0[p]) ? ((meta.height - kAdam7Y0[p] + kAdam7YS[p] - 1u) / kAdam7YS[p]) : 0u;
            if (pw == 0u || ph == 0u) continue;
            const keel::usize rowBytes = RowByteCount(pw, meta.channels, meta.bitDepth);
            keel::usize passBytes = 0u;
            if (!keel::checked_mul(static_cast<keel::usize>(ph), 1u + rowBytes, passBytes)) {
                return false;
            }
            expectedRawBytes += passBytes;
        }
    }

    keel::Array<keel::u8> rawScanlines;
    if (!InflateZlib(idat.as_ref(), expectedRawBytes, rawScanlines)) {
        return false;
    }

    keel::usize totalPx = 0u;
    if (!keel::checked_mul(static_cast<keel::usize>(meta.width), static_cast<keel::usize>(meta.height), totalPx)) {
        return false;
    }
    outRgbaPixels.resize(totalPx, 0u);

    keel::usize consumed = 0u;
    if (interlace == 0u) {
        if (!UnfilterAndBlitPass(
                rawScanlines.data(), rawScanlines.size(), consumed,
                meta.width, meta.height, 0u, 0u, 1u, 1u, meta, outRgbaPixels)) {
            outRgbaPixels.clear();
            return false;
        }
    } else {
        for (keel::u32 p = 0u; p < 7u; ++p) {
            const keel::u32 pw = (meta.width > kAdam7X0[p]) ? ((meta.width - kAdam7X0[p] + kAdam7XS[p] - 1u) / kAdam7XS[p]) : 0u;
            const keel::u32 ph = (meta.height > kAdam7Y0[p]) ? ((meta.height - kAdam7Y0[p] + kAdam7YS[p] - 1u) / kAdam7YS[p]) : 0u;
            if (!UnfilterAndBlitPass(
                    rawScanlines.data(), rawScanlines.size(), consumed,
                    pw, ph, kAdam7X0[p], kAdam7Y0[p], kAdam7XS[p], kAdam7YS[p], meta, outRgbaPixels)) {
                outRgbaPixels.clear();
                return false;
            }
        }
    }

    outWidth = meta.width;
    outHeight = meta.height;
    return true;
}

} // namespace guikit::native::detail
