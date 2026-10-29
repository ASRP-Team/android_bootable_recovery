#pragma once

#include <string>

namespace aera::ftp {

// Plain FTP service for legacy clients. Credentials are shared with the SSH
// server (user "root", password from the SSH settings).
bool Start(int port);
bool Stop();
bool Running();
int Port();
bool SetPort(int port);
std::string Address();

}  // namespace aera::ftp
