#pragma once

#include <switch.h>
#include <string>

namespace nx::ncm {
// Horizon's archive parser opens native NCM paths. Keep the placeholder alive
// until the parser has closed all its handles.
class MetadataStage final {
    NcmContentStorage storage{};
    NcmPlaceHolderId placeholder{};
    bool owned = false;
public:
    MetadataStage() = default;
    MetadataStage(const MetadataStage&) = delete;
    MetadataStage& operator=(const MetadataStage&) = delete;
    ~MetadataStage();
    std::string copyFrom(NcmContentStorage& pinnedUsb, const NcmContentId& id);
    void remove();
};
}
