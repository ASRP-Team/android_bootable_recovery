/* SPDX-License-Identifier: Apache-2.0 */
#include "../../twrpinstall/splash_preservation.hpp"
#include <cassert>
#include <map>
#include <tuple>
int main() {
  using aera::preservation::FindSplashWith;
  using Block = std::tuple<uint64_t, uint64_t, bool>;
  std::map<std::string, Block> blocks;
  auto find = [&] {
    return FindSplashWith([&](const std::string &p) { return blocks.count(p); },
      [&](const std::string &p, uint64_t *bytes, uint64_t *device) {
        const auto &b = blocks.at(p);
        *bytes = std::get<0>(b); *device = std::get<1>(b);
        return std::get<2>(b);
      });
  };
  const std::string root = "/dev/block/by-name/";
  assert(!find().bytes);
  blocks[root + "splash"] = {1024, 1, true};
  assert(find().bytes == 1024 && find().second.empty());
  blocks[root + "splash_a"] = {1024, 2, true};
  assert(!find().bytes); // No fallback from a partial pair to the shared alias.
  blocks[root + "splash_b"] = {1024, 3, true};
  assert(find().bytes == 1024 && find().second == root + "splash_b");
  blocks[root + "splash_b"] = {2048, 3, true};
  assert(!find().bytes);
  blocks[root + "splash_b"] = {1024, 2, true};
  assert(!find().bytes); // Two aliases to the same device are not an A/B pair.
  blocks[root + "splash_b"] = {1024, 3, false};
  assert(!find().bytes);
  blocks.clear(); blocks[root + "splash"] = {0, 1, true};
  assert(!find().bytes);
}
