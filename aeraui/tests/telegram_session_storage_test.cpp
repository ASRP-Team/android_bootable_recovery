// SPDX-License-Identifier: Apache-2.0
#include "telegram/session_storage.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
int main() {
  char tmp[]="/tmp/aera-telegram-state-XXXXXX";assert(mkdtemp(tmp));
  std::filesystem::path root(tmp), state=root/"session", outside=root/"downloads";
  std::filesystem::create_directory(state);std::filesystem::create_directory(outside);
  std::ofstream(outside/"keep.txt") << "saved download";
  std::ofstream(state/"client.conf") << "application configuration";
  for (const char *dir : {"database","files","avatars"}) {
    std::filesystem::create_directories(state/dir/"nested");
    std::ofstream(state/dir/"nested"/"private") << "cached session";
  }
  std::filesystem::create_directory_symlink(outside,state/"previews");
  std::filesystem::create_directory_symlink(outside,state/"database"/"external");
  int fd=open(state.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);assert(fd>=0);
  assert(aeraui::telegram::ResetLocalSessionAt(fd));
  assert(aeraui::telegram::ResetLocalSessionAt(fd));close(fd);
  for (const char *dir : {"database","files","avatars","previews"})
    assert(!std::filesystem::exists(state/dir));
  assert(std::filesystem::exists(state/"client.conf"));
  assert(std::filesystem::exists(outside/"keep.txt"));
  std::filesystem::remove_all(root);
}
