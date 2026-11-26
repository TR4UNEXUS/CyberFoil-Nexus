#pragma once
#include <switch.h>
#include <string>
#include <vector>

namespace nx::nexus {
enum class Owner { Unknown, Native, Nexus };
enum class DriveState { Unavailable, Disconnected, Starting, Ready, Error };
struct Drive {
    DriveState state = DriveState::Unavailable;
    u64 generation = 0;
    u64 free = 0, total = 0;
};
Owner owner();
Drive drive(bool withSpace = false);
bool mountBrowser();
void closeBrowser();

struct Target {
    NcmStorageId logical;
    u64 generation;
    bool usb() const { return generation != 0; }
};
constexpr Target sd{NcmStorageId_SdCard, 0};
constexpr Target internal{NcmStorageId_BuiltInUser, 0};
class TargetChoices {
    const Drive media = drive();
public:
    std::vector<std::string> labels(std::string card, std::string memory) const;
    Target at(int option) const;
};
Result openStorage(NcmContentStorage& storage, Target target);
Result validate(Target target);
Result checkUsbWrite(Target target);
}
