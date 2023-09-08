/*
Copyright 2023 Adobe
All Rights Reserved.

NOTICE: Adobe permits you to use, modify, and distribute this file in
accordance with the terms of the Adobe license agreement accompanying
it.
*/

#include <cassert>
#include <cstring>
#include <vector>
#include <limits>
#include <sstream>

#include <brotli/decode.h>
#include <woff2/decode.h>

#include "merger.h"

#include "streamhelp.h"
#include "tag.h"

bool iftb::merger::unpackChunks() {
    for (auto &i: chunkData) {
        if (!chunkAddRecs(i.first, i.second))
            return false;
    }
    return true;
}

bool iftb::merger::chunkAddRecs(uint16_t idx, const std::string &cd) {
    uint32_t glyphCount, table1, table2 = 0;
    uint32_t offset, lastOffset = 0;
    char *initialOffset;
    uint16_t i16;
    uint8_t i8, tableCount;
    std::vector<uint16_t> gids;
    glyphrec gr;
    std::istringstream is(cd);
    auto cdlen = cd.size();

    if (readObject<uint32_t>(is) != tag("IFTC"))
        return chunkError(idx, "Initial bytes of chunk must be \"IFTC\"");
    if (readObject<uint32_t>(is) != 0)
        return chunkError(idx, "Reserved bytes in chunk must be 0");
    if (readObject<uint32_t>(is) != id[0])
        return chunkError(idx, "ID mismatch (id0)");
    if (readObject<uint32_t>(is) != id[1])
        return chunkError(idx, "ID mismatch (id1)");
    if (readObject<uint32_t>(is) != id[2])
        return chunkError(idx, "ID mismatch (id2)");
    if (readObject<uint32_t>(is) != id[3])
        return chunkError(idx, "ID mismatch (id3)");
    if (readObject<uint32_t>(is) != idx)
        return chunkError(idx, "Chunk index mismatch");
    if (readObject<uint32_t>(is) != cd.size())
        return chunkError(idx, "Chunk index mismatch");
    readObject(is, glyphCount);
    if (glyphCount > std::numeric_limits<uint16_t>::max())
        return chunkError(idx, "Unreasonable glyph count");
    readObject(is, tableCount);
    if (!(tableCount == 1 || tableCount == 2))
        return chunkError(idx, "Chunk table count must be 1 or 2");
    for (int i = 0; i < glyphCount; i++)
        gids.push_back(readObject<uint16_t>(is));
    readObject(is, table1);
    if (tableCount == 2)
        readObject(is, table2);
    add_tables(table1, table2);
    readObject(is, lastOffset);
    for (auto i: gids) {
        readObject(is, offset);
        gr.offset = cd.data() + lastOffset;
        gr.length = offset - lastOffset;
        if (offset > cdlen)
            return chunkError(idx, "GID offset + length exceeds size");
        glyphMap1.emplace(i, gr);
        lastOffset = offset;
    }
    if (tableCount == 2) {
        for (auto i: gids) {
            readObject(is, offset);
            gr.offset = cd.data() + lastOffset;
            gr.length = offset - lastOffset;
            if (offset > cdlen)
                return chunkError(idx, "GID offset + length exceeds size");
            glyphMap2.emplace(i, gr);
            lastOffset = offset;
        }
    }
    if (is.fail())
        return chunkError(idx, "Decompile stream read failure");
    return true;
}

uint32_t iftb::merger::calcLengthDiff(std::istream &is, uint32_t glyphCount, 
                                      std::map<uint16_t, glyphrec> &glyphMap) {
    uint32_t ldiff = 0;
    uint32_t arrayOff = is.tellg();
    for (auto &i: glyphMap) {
        uint16_t gid = i.first;
        uint32_t start, end;
        is.seekg(arrayOff + 4 * gid);
        readObject(is, start);
        readObject(is, end);
        ldiff += i.second.length - (end - start);
    }
    return ldiff;
}

bool iftb::merger::copyGlyphData(std::iostream &s, uint32_t glyphCount,
                                 char *nbase, char *cbase, uint32_t ldiff,
                                 std::map<uint16_t, glyphrec> &glyphMap,
                                 uint32_t basediff) {
    uint32_t off, nextOff, clen;
    s.seekg(glyphCount * 4);
    readObject(s, nextOff);
    char *ctrailing = cbase + nextOff, *ntrailing = nbase + nextOff + ldiff;
    // XXX Could reduce the number of memmove calls (but not the amount of
    // data copied) by grouping the consecutive copies from ctrailing
    for (int64_t i = glyphCount-1; i >= 0; i--) {
        s.seekp((i + 1) * 4);
        writeObject(s, (uint32_t) (ntrailing - nbase));
        s.seekg(i * 4);
        readObject(s, off);
        ctrailing -= nextOff - off;
        auto j = glyphMap.find(i);
        if (j != glyphMap.end()) {
            ntrailing -= j->second.length;
            memmove(ntrailing, j->second.offset, j->second.length);
        } else {
            ntrailing -= nextOff - off;
            if (ntrailing != ctrailing)
                memmove(ntrailing, ctrailing, nextOff - off);
        }
        nextOff = off;
    }
    if (ctrailing - cbase != basediff || ntrailing - nbase != basediff) {
        std::cerr << "Logic error merging chunks" << std::endl;
        return false;
    }
    if (s.fail()) {
        std::cerr << "Stream failure merging chunks" << std::endl;
        return false;
    }
    return true;
}

uint32_t iftb::merger::calcLayout(iftb::sfnt &sf, uint32_t numg, uint32_t cso) {
    uint32_t ldiff;

    charStringOff = cso;
    glyphCount = numg;

    if ((t1off = sf.getTableOffset(t1tag = T_CFF, t1clen)) != 0) {
        has_cff = true;
    } else if ((t1off = sf.getTableOffset(t1tag = T_CFF2,
                                          t1clen)) != 0) {
        is_cff2 = true;
        has_cff = true;
    } else if ((t1off = sf.getTableOffset(t1tag = T_GVAR,
                                          t1clen)) != 0) {
        ;
    } else
        t1tag = 0;

    if (!has_cff) {
        glyfcoff = sf.getTableOffset(T_GLYF, glyfclen);
        if (glyfcoff == 0) {
            std::cerr << "Error: No CFF or glyf table to update" << std::endl;
            return false;
        }
        locacoff = sf.getTableOffset(T_LOCA, localen);
        if (locacoff == 0) {
            std::cerr << "Error: glyf table without loca table" << std::endl;
            return false;
        }
    }
    if (t1tag) {
        sf.getTableStream(ss, t1tag);
        if (has_cff) {
            ss.seekg(charStringOff + (is_cff2 ? 5 : 3));
        } else {  // gvar
            ss.seekg(16);
            readObject(ss, gvarDataOff);
        }
        ldiff = calcLengthDiff(ss, glyphCount, has_cff ? glyphMap1 : glyphMap2);
        t1nlen = t1clen + ldiff;
    }
    if (!has_cff) {
        if (t1tag)
            glyfnoff = ((t1off + t1nlen + 3) / 4) * 4;
        else
            glyfnoff = glyfcoff;

        sf.getTableStream(ss, T_LOCA);
        ss.seekg(0);
        ldiff = calcLengthDiff(ss, glyphCount, glyphMap1);
        glyfnlen = glyfclen + ldiff;
        locanoff = ((glyfnoff + glyfnlen + 3) / 4) * 4;
    }
    if (has_cff)
        fontend = ((t1off + t1nlen + 3) / 4) * 4;
    else
        fontend = ((locanoff + localen + 3) / 4) * 4;
    return fontend;
}

bool iftb::merger::merge(iftb::sfnt &sf, char *oldbuf, char *newbuf) {
    uint32_t cffOffOff = charStringOff + (is_cff2 ? 5 : 3);
    if (oldbuf != newbuf) {
        if (has_cff)
            memcpy(newbuf, oldbuf, t1off + cffOffOff + (glyphCount + 1) * 4);
        else if (t1tag)  // gvar
            memcpy(newbuf, oldbuf, t1off + 20 + (glyphCount + 1) * 4);
        else
            memcpy(newbuf, oldbuf, glyfcoff);
        sf.setBuffer(newbuf, fontend);
    } else {
        sf.setBuffer(oldbuf, fontend);
    }
    if (!has_cff) {
        memmove(newbuf + locanoff, oldbuf + locacoff, localen);
        ss.rdbuf()->pubsetbuf(newbuf + locanoff, localen);
        if (!copyGlyphData(ss, glyphCount, newbuf + glyfnoff,
                           oldbuf + glyfcoff, glyfnlen - glyfclen,
                           glyphMap1, 0))
            return false;
        for (uint32_t i = locanoff + localen; i < fontend; i++)
            *(newbuf + i) = 0;
        for (uint32_t i = glyfnoff + glyfnlen; i < locanoff; i++)
            *(newbuf + i) = 0;
        sf.adjustTable(T_LOCA, locanoff, localen, true);
        sf.adjustTable(T_GLYF, glyfnoff, glyfnlen, true);
    }
    if (t1tag) {
        uint32_t dataoff;
        for (uint32_t i = t1off + t1nlen; i < ((has_cff) ? fontend : glyfnoff);
             i++)
            *(newbuf + i) = 0;
        if (has_cff) {
            ss.rdbuf()->pubsetbuf(newbuf + t1off + cffOffOff,
                                  (glyphCount + 1) * 4);
            dataoff = t1off + cffOffOff + (glyphCount + 1) * 4 - 1;
        } else {  // gvar
            ss.rdbuf()->pubsetbuf(newbuf + t1off + 20, (glyphCount + 1) * 4);
            dataoff = t1off + gvarDataOff;
        }
        if (!copyGlyphData(ss, glyphCount, newbuf + dataoff,
                           oldbuf + dataoff, t1nlen - t1clen,
                           has_cff ? glyphMap1 : glyphMap2, has_cff ? 1 : 0))
            return false;
        sf.adjustTable(t1tag, t1off, t1nlen, true);
    }
    return true;
}


void iftb::dumpChunk(std::ostream &os, std::istream &is) {
    uint32_t u32, glyphCount, table1, table2 = 0, idx;
    uint32_t length, offset, lastOffset = 0;
    uint8_t tableCount;
    std::vector<uint16_t> gids;

    readObject(is, u32);
    if (u32 != tag("IFTC")) {
        std::cerr << "Unrecognized chunk type '" << otag(u32);
        std::cerr << "': can't display contents" << std::endl;
        return;
    }
    readObject(is, u32);
    if (u32 != 0) {
        std::cerr << "Reserved bytes in chunk must be 0: ";
        std::cerr << "can't display contents" << std::endl;
    }
    char c = os.fill();
    std::streamsize w = os.width();
    os << "ID: " << std::setfill('0') << std::setw(8) << std::right;
    os << std::hex << readObject<uint32_t>(is) << " ";
    os << readObject<uint32_t>(is) << " ";
    os << readObject<uint32_t>(is) << " ";
    os << readObject<uint32_t>(is) << std::dec << std::setfill(c);
    os << std::setw(w) << std::endl;
    readObject(is, idx);
    std::cerr << "Chunk index: " << idx << std::endl;
    readObject(is, length);
    std::cerr << "Uncompressed length: " << length << std::endl;
    readObject(is, glyphCount);
    std::cerr << "Count of included glyphs: " << glyphCount << std::endl;
    readObject(is, tableCount);
    std::cerr << "Table count (1 or 2): " << (int) tableCount << std::endl;
    std::cerr << "Gid list: ";
    for (int i = 0; i < glyphCount; i++) {
        if (i != 0)
            std::cerr << ", ";
        gids.push_back(readObject<uint16_t>(is));
        std::cerr << gids.back();
    }
    std::cerr << std::endl;
    readObject(is, table1);
    std::cerr << "Table 1: " << otag(table1);
    if (tableCount == 2) {
        readObject(is, table2);
        std::cerr << ", Table 2: " << otag(table2);
    }
    std::cerr << std::endl;
    readObject(is, lastOffset);
    std::cerr << "Table 1 lengths: ";
    for (int i = 0; i < glyphCount; i++) {
        if (i != 0)
            std::cerr << ", ";
        readObject(is, offset);
        std::cerr << (offset - lastOffset);
        lastOffset = offset;
    }
    std::cerr << std::endl;
    if (tableCount == 2) {
        std::cerr << "Table 2 lengths: ";
        for (int i = 0; i < glyphCount; i++) {
            if (i != 0)
                std::cerr << ", ";
            readObject(is, offset);
            std::cerr << (offset - lastOffset);
            lastOffset = offset;
        }
        std::cerr << std::endl;
    }
}
 
std::string iftb::decodeChunk(char *buf, size_t length) {
    uint32_t l;

    simpleistream sis;
    sis.rdbuf()->pubsetbuf(buf, length);
    sis.seekg(28);  // length offset
    readObject(sis, l);

    std::string r(l, 0);
    memcpy(r.data(), buf, 32);
    size_t decoded_size = l - 32;
    auto ok = BrotliDecoderDecompress(length - 32, (uint8_t *) buf + 32,
                                      &decoded_size,
                                      (uint8_t *) r.data() + 32);
    if (ok != BROTLI_DECODER_RESULT_SUCCESS && decoded_size == l - 32) {
        std::cerr << "Error: Could not decompress IFTZ chunk";
        r.clear();
    } else if (decoded_size != l - 32) {
        std::cerr << "Error: Wrong length encoded in chunk";
        r.clear();
    }

    r[3] = 'C';
    return r;
}


/* When buf != NULL the output should be copied into the string, which
   will be replaced.
   When buf == NULL the string will be treated as the input, which will
   only be changed if it needs to be decoded (decompressed)
 */
uint32_t iftb::decodeBuffer(char *buf, uint32_t length, std::string &s,
                           float reserveExtra) {
    bool is_woff2 = false, is_compressed_chunk = false;
    bool in_string = (buf == NULL);
    uint32_t tg; 

    assert(reserveExtra >= 0 && reserveExtra < 10.0);

    if (in_string) {
        buf = s.data();
        length = s.size();
    }
    tg = tag(buf);

    if (tg == tag("wOF2"))
        is_woff2 = true;
    else if (tg == tag("IFTZ"))
        is_compressed_chunk = true;

    if (is_woff2) {
        const uint8_t *iptr = reinterpret_cast<const uint8_t*>(buf);
        uint32_t sz = std::min(woff2::ComputeWOFF2FinalSize(iptr, length),
                               woff2::kDefaultMaxSize);
        uint32_t reserve = (uint32_t) ((float) sz * (1 + reserveExtra));
        std::string t;
        t.reserve(reserve);
        t.resize(sz);
        woff2::WOFF2StringOut o(&t);
        auto ok = woff2::ConvertWOFF2ToTTF(iptr, length, &o);
        if (ok) {
            s.swap(t);
            t.clear();
            tg = tag(s.data());
        } else {
            tg = 0;
        }
    } else if (is_compressed_chunk) {
        std::string t = iftb::decodeChunk(buf, length);
        if (t.size() > 4) {
            s.swap(t);
            t.clear();
            tg = tag(s.data());
        } else {
            tg = 0;
        }
    } else {
        if (tg == 0x00010000 || tg == tag("OTTO") || tg == tag("IFTB")) {
            uint32_t reserve = (uint32_t) ((float) length * (1 + reserveExtra));
            s.reserve(reserve);
        }
        s.resize(length);
        memcpy(s.data(), buf, length);
    }
    return tg;
}
