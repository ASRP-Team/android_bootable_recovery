/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <liblp/builder.h>
#include "aera_image_flash_layout_types.hpp"
#include <set>
#include <string>
#include <vector>

namespace aera {
struct LogicalImageSize {
  std::string name;
  uint64_t bytes;
};

// Explicit full-ROM layout replacement only. Retain the device's geometry,
// block devices, other slot and shared partitions. Never publish the empty
// package table itself, whose extents/geometry need not match this device.
inline bool PlanRomImageLayout(android::fs_mgr::MetadataBuilder& builder,
        const RomImageLayout& layout, const std::string& suffix,
        const std::vector<LogicalImageSize>& images, std::string& error) {
  auto fail = [&](const std::string& message) { error = message; return false; };
  if ((suffix != "_a" && suffix != "_b") || layout.partitions.empty())
    return fail("ROM layout requires a known A/B slot");
  std::set<std::string> desired, groups, selected;
  for (const auto& image : images) selected.insert(image.name);
  for (const auto& group : layout.groups) {
    if (group.name.empty() || !group.maximum_bytes || !groups.insert(group.name + suffix).second)
      return fail("Invalid ROM partition group");
  }
  for (const auto& part : layout.partitions) {
    if (part.name.empty() || !desired.insert(part.name + suffix).second ||
        !selected.count(part.name + suffix) || !groups.count(part.group + suffix))
      return fail("Select every ROM logical image before applying its partition layout");
  }
  // A selected logical partition not in the template must never disappear.
  if (selected != desired) return fail("Selected logical images differ from the ROM layout");
  std::vector<std::string> obsolete;
  for (const auto& group : builder.ListGroups()) {
    for (const auto* part : builder.ListPartitionsInGroup(group)) {
      const auto& name = part->name();
      if (name.size() > suffix.size() && name.compare(name.size()-suffix.size(), suffix.size(), suffix) == 0 &&
          !desired.count(name)) obsolete.push_back(name);
    }
  }
  for (const auto& name : obsolete) builder.RemovePartition(name);
  for (const auto& group : layout.groups) {
    const auto name = group.name + suffix;
    if (!builder.FindGroup(name)) {
      if (!builder.AddGroup(name, group.maximum_bytes)) return fail("Cannot create ROM partition group");
    } else if (!builder.ChangeGroupSize(name, group.maximum_bytes)) return fail("Cannot set ROM group limit");
  }
  for (const auto& part : layout.partitions) {
    auto* entry = builder.FindPartition(part.name + suffix);
    if (!entry) entry = builder.AddPartition(part.name + suffix, part.group + suffix, part.attributes);
    if (!entry || !builder.ChangePartitionGroup(entry, part.group + suffix)) return fail("Cannot prepare ROM logical target");
    entry->set_attributes(part.attributes);
  }
  for (const auto& group : builder.ListGroups()) {
    if (group.size() > suffix.size() && group.compare(group.size()-suffix.size(), suffix.size(), suffix) == 0 &&
        !groups.count(group) && builder.ListPartitionsInGroup(group).empty()) builder.RemoveGroupAndPartitions(group);
  }
  return true;
}

// Plan only: callers publish Export() after every allocation succeeds. Keep
// unselected partitions, group limits and existing extents intact. Shrinking
// all selected partitions first lets a full group exchange space between them.
inline bool PlanImageFlashLayout(android::fs_mgr::MetadataBuilder& builder,
        const std::vector<LogicalImageSize>& images, const std::vector<std::string>& stale_cows,
        std::string& error) {
  std::set<std::string> names;
  for (const auto& image : images) {
    if (!image.bytes || image.bytes % 512 || !names.insert(image.name).second ||
        !builder.FindPartition(image.name)) {
      error = "Invalid or duplicate logical target: " + image.name;
      return false;
    }
  }
  for (const auto& name : stale_cows) {
    const auto* part = builder.FindPartition(name);
    if (!part || part->group_name() != "cow" || name.size() <= 4 ||
        name.compare(name.size() - 4, 4, "-cow") || names.count(name)) {
      error = "Invalid stale snapshot COW: " + name;
      return false;
    }
  }
  for (const auto& name : stale_cows) builder.RemovePartition(name);
  for (bool shrink : {true, false}) {
    for (const auto& image : images) {
      auto* part = builder.FindPartition(image.name);
      if (part->size() == image.bytes || (image.bytes < part->size()) != shrink) continue;
      if (!builder.ResizePartition(part, image.bytes)) {
        error = "Cannot fit " + image.name + " into its Super group after selected shrinks and stale-COW cleanup";
        return false;
      }
    }
  }
  return true;
}
} // namespace aera
