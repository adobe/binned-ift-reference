/*
Copyright 2023 Adobe
All Rights Reserved.

NOTICE: Adobe permits you to use, modify, and distribute this file in
accordance with the terms of the Adobe license agreement accompanying
it.
*/

/* The iftb::table_IFTB object has code to read, store, interact with,
   and (to a limited extent) update the contents of the IFTB table in
   an IFTB-encoded font. (Only the chunk set can be updated.)  This code
   is included in both the encoder and the client side.
 */

#include <map>
#include <array>
#include <string>
#include <set>
#include <vector>
#include <cassert>
#include <cstdint>

#include "streamhelp.h"
#include "cmap.h"

#pragma once

namespace iftb {
    class table_IFTB;
    class chunker;
}

class iftb::table_IFTB {
public:
    table_IFTB() {
        filesURI.reserve(257);
        filesURI.push_back(0);
        rangeFileURI.reserve(257);
        rangeFileURI.push_back(0);
    }
    friend class iftb::chunker;
    friend bool randtest(std::string &s, uint32_t iterations);
    uint16_t getChunkCount() { return (uint16_t) chunkCount; }
    uint32_t getChunkOffset(uint16_t cidx);
    std::pair<uint32_t, uint32_t> getChunkRange(uint16_t cidx);
    std::string &getRangeFileURI() { return rangeFileURI; }
    const char * getChunkURI(uint16_t idx);
    bool addcmap(std::istream &i, bool keepGIDMap = false) {
        bool r = readcmap(i, uniMap, &gidMap);
        if (r && !keepGIDMap)
            gidMap.clear();
        return r;
    }
    bool hasChunk(uint16_t cidx) {
        if (cidx >= chunkCount)
           return false;
        else if (cidx == 0)
            return true;
        else
            return chunkSet[cidx];
    }
    bool getMissingChunks(const std::vector<uint32_t> &unicodes,
                          const std::vector<uint32_t> &features,
                          std::set<uint16_t> &cl);
    void dumpChunkSet(std::ostream &os);
    void writeChunkSet(std::ostream &os, bool seekTo = false);
    void setChunkCount(uint32_t cc) {
        chunkCount = cc;
        chunkSet.clear();
        chunkSet.resize(chunkCount);
    }
    uint16_t readChunkIndex(std::istream &i) {
        uint8_t i8;
        uint16_t i16;
        if (chunkCount > 256) {
            readObject(i, i16);
            return i16;
        } else {
            readObject(i, i8);
            return i8;
        }
    }
    void writeChunkIndex(std::ostream &o, uint16_t idx) {
        uint8_t i8 = (uint8_t) idx;
        if (chunkCount > 256)
            writeObject(o, idx);
        else
            writeObject(o, i8);
    }
    uint32_t getGlyphCount() { return glyphCount; }
    unsigned int compile(std::ostream &o, uint32_t offset = 0);
    bool decompile(std::istream &i, uint32_t offset = 0);
    void dump(std::ostream &o, bool full = false);
    uint32_t getCharStringOffset() { return CFFCharStringsOffset; }
    bool updateChunkSet(std::set<uint16_t> &chunks) {
        for (auto idx: chunks) {
            if (idx >= chunkCount)
                return false;
            if (chunkSet[idx])
                return false;
            chunkSet[idx] = true;
        }
        return true;
    }
    uint32_t *getID() { return id; }
private:
    struct FeatureMap {
        friend class iftb::chunker;
        FeatureMap() {}
        FeatureMap(const FeatureMap &fm) = delete;
        FeatureMap(FeatureMap &&fm) {
            startIndex = fm.startIndex;
            ranges.swap(fm.ranges);
        }
        uint16_t startIndex = 0;
        std::vector<std::pair<uint16_t, uint16_t>> ranges;
    };
    bool error(const char *m) {
        std::cerr << "IFTB table error: " << m << std::endl;
        return false;
    }
    uint16_t majorVersion {0}, minorVersion {1}, flags {0};
    uint32_t id[4];
    uint32_t CFFCharStringsOffset {0};
    uint32_t chunkCount {0}, glyphCount {0};
    std::vector<bool> chunkSet;
    std::vector<uint16_t> gidMap;
    std::map<uint32_t, FeatureMap> featureMap;
    std::vector<uint32_t> chunkOffsets;
    std::string filesURI, rangeFileURI;
    std::array<char, 257> fURIbuf;
    std::unordered_map<uint32_t, uint16_t> uniMap;
};
