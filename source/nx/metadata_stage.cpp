#include "nx/metadata_stage.hpp"
#include "util/error.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace nx::ncm {
MetadataStage::~MetadataStage() {
    if (owned) ncmContentStorageDeletePlaceHolder(&storage, &placeholder);
    if (serviceIsActive(&storage.s)) ncmContentStorageClose(&storage);
}

std::string MetadataStage::copyFrom(NcmContentStorage& pinnedUsb, const NcmContentId& id) {
    s64 size = 0;
    ASSERT_OK(ncmContentStorageGetSizeFromContentId(&pinnedUsb, &size, &id), "Could not locate the manifest archive on the selected USB drive");
    if (size < 0xc00 || size > 64 * 1024 * 1024)
        THROW_FORMAT("Unexpected USB manifest archive size: %lld bytes", static_cast<long long>(size));

    ASSERT_OK(ncmOpenContentStorage(&storage, NcmStorageId_BuiltInUser), "Could not open internal storage for a temporary manifest");
    ASSERT_OK(ncmContentStorageGeneratePlaceHolderId(&storage, &placeholder), "Could not allocate a temporary manifest ID");
    NcmContentId temporaryId{};
    static_assert(sizeof(temporaryId) == sizeof(placeholder));
    std::memcpy(&temporaryId, &placeholder, sizeof(temporaryId));
    ASSERT_OK(ncmContentStorageCreatePlaceHolder(&storage, &temporaryId, &placeholder, size), "Could not create the temporary manifest on internal storage");
    owned = true;
    std::vector<u8> buffer(std::min<s64>(256 * 1024, size));
    for (s64 offset = 0; offset < size;) {
        const size_t count = std::min<s64>(buffer.size(), size - offset);
        ASSERT_OK(ncmContentStorageReadContentIdFile(&pinnedUsb, buffer.data(), count, &id, offset), "Could not read the manifest from the selected USB drive");
        ASSERT_OK(ncmContentStorageWritePlaceHolder(&storage, &placeholder, offset, buffer.data(), count), "Could not write the temporary manifest");
        offset += count;
    }
    ASSERT_OK(ncmContentStorageFlushPlaceHolder(&storage), "Could not flush the temporary manifest");
    char path[FS_MAX_PATH]{};
    ASSERT_OK(ncmContentStorageGetPlaceHolderPath(&storage, path, sizeof(path), &placeholder), "Could not resolve the temporary manifest path");
    return path;
}

void MetadataStage::remove() {
    if (!owned) return;
    ASSERT_OK(ncmContentStorageDeletePlaceHolder(&storage, &placeholder), "Could not remove the temporary manifest");
    owned = false;
}
}
