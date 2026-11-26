#pragma once

#include "nx/nexus_usb.hpp"
#include <cstdint>

namespace inst::mtp {

bool StartInstallServer(nx::nexus::Target storage_choice);
void StopInstallServer();
bool IsInstallServerRunning();

}
