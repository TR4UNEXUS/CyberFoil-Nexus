#pragma once
#include "nx/nexus_usb.hpp"
#include <vector>
#include <string>

namespace usbInstStuff {
    std::vector<std::string> OnSelected();
    void installTitleUsb(std::vector<std::string> ourNspList, nx::nexus::Target ourStorage);
}