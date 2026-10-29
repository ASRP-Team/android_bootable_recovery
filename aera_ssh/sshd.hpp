#pragma once

#ifdef OF_ENABLE_WLAN

#include <string>

namespace aera::ssh {

// Starts the bundled Dropbear server. Configuration (password, port, keys) is
// persisted through DataManager so it survives recovery restarts.
bool Start(int port);
bool Stop();
bool Running();
int Port();
std::string Address();
std::string Password();

// Empty password regenerates a random one.
bool SetPassword(const std::string& password);
bool SetPort(int port);

std::string AuthorizedKeys();
bool SetAuthorizedKeys(const std::string& keys);
int KeyCount();

}  // namespace aera::ssh

#endif  // OF_ENABLE_WLAN
