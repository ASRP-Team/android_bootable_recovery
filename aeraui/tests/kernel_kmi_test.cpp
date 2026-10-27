/* SPDX-License-Identifier: Apache-2.0 */
#include "../features/root/kernel_kmi.hpp"
#include <cassert>
#include <string>
#include <vector>
int main() {
  using namespace std::string_literals;
  using aeraui::root::KernelKmiFromBanner;
  using aeraui::root::ResolveKernelKmiFromBanner;
  using aeraui::root::ResolveKernelKmiFromRelease;
  assert(KernelKmiFromBanner("Linux version 6.6.89-android15-8-g123 (builder)\n") == "android15-6.6");
  assert(KernelKmiFromBanner("Linux version 5.10.228-android12-9-gabc (builder)\n") == "android12-5.10");
  assert(KernelKmiFromBanner("Linux version 6.1.75-android14-11-g123 (builder)\n") == "android14-6.1");
  assert(KernelKmiFromBanner("Linux version 4.19.157-perf+ (builder)\n").empty());
  assert(KernelKmiFromBanner("Linux version 6.6.89-android15").empty());
  assert(KernelKmiFromBanner("6.6.89-android15-8 random data").empty());
  const std::string binary(40, '\0');
  assert(KernelKmiFromBanner(binary + "Linux version 6.6.89-android15-8\0"s) == "android15-6.6");

  const std::vector<std::string> supported = {
      "android12-5.10", "android13-5.10", "android14-6.1", "android15-6.6"};
  assert(ResolveKernelKmiFromBanner(
             "Linux version 6.1.75-perf+ (builder)\n", supported) == "android14-6.1");
  assert(ResolveKernelKmiFromRelease("6.1.177~Chidori-v2.2+", supported) ==
         "android14-6.1");
  assert(ResolveKernelKmiFromRelease("6.6.89-gki", supported) == "android15-6.6");
  assert(ResolveKernelKmiFromRelease("5.10.228-perf+", supported).empty());
  assert(ResolveKernelKmiFromRelease("4.19.157-perf+", supported).empty());
  assert(ResolveKernelKmiFromRelease("6.1.75-android15-broken", supported) ==
         "android15-6.1");
  assert(ResolveKernelKmiFromRelease(
             "6.1.75-perf+", {"android14-6.1", "android14-6.1"}) == "android14-6.1");
  assert(ResolveKernelKmiFromRelease(
             "6.1.75-perf+", {"androidfoo-6.1", "android14-6.1"}) == "android14-6.1");
  assert(KernelKmiFromBanner(
             "Linux version invalid (old)\nLinux version 6.1.75-android14-11-g123 (builder)\n") ==
         "android14-6.1");
}
