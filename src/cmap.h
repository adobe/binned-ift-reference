/*
Copyright 2023 Adobe
All Rights Reserved.

NOTICE: Adobe permits you to use, modify, and distribute this file in
accordance with the terms of the Adobe license agreement accompanying
it.
*/

/* Contains code for reading formats 4 and 12 of an OpenType cmap table.
   Included in both the encoder and the client side.
 */

#include <iostream>
#include <vector>
#include <unordered_map>
#include <cstdint>

#pragma once

namespace iftb {
    bool readcmap(std::istream &is,
                  std::unordered_map<uint32_t, uint16_t> &uniMap,
                  std::vector<uint16_t> *gidMap);
}
