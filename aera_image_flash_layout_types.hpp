/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace aera {
struct RomImageGroup { std::string name; uint64_t maximum_bytes; };
struct RomImagePartition { std::string name, group; uint32_t attributes; };
struct RomImageLayout {
  std::vector<RomImageGroup> groups;
  std::vector<RomImagePartition> partitions;
};
}
