/*
Copyright (c) 2017-2018 Adubbz

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "nx/ncm.hpp"
#include "nx/metadata_stage.hpp"
#include "util/file_util.hpp"
#include "util/error.hpp"
#include <algorithm>
#include <cstring>
#include <vector>

namespace nx::ncm
{
    ContentStorage::ContentStorage(nx::nexus::Target storageId) : m_target(storageId)
    {
        ASSERT_OK(nx::nexus::openStorage(m_contentStorage, storageId), "Failed to open NCM ContentStorage");
    }

    ContentStorage::~ContentStorage()
    {
        if (m_usbPlaceholder) ncmContentStorageDeletePlaceHolder(&m_contentStorage, &*m_usbPlaceholder);
        serviceClose(&m_contentStorage.s);
    }

    void ContentStorage::CreatePlaceholder(const NcmContentId &contentId, const NcmPlaceHolderId &placeholderId, size_t size)
    {
        NcmPlaceHolderId actual = placeholderId;
        if (m_target.usb()) {
            if (m_usbPlaceholder) THROW_FORMAT("USB write already in progress");
            ASSERT_OK(ncmContentStorageGeneratePlaceHolderId(&m_contentStorage, &actual), "Could not allocate a USB placeholder");
        }
        ASSERT_OK(ncmContentStorageCreatePlaceHolder(&m_contentStorage, &contentId, &actual, size), "Failed to create placeholder");
        if (m_target.usb()) m_usbPlaceholder = actual;
    }

    void ContentStorage::DeletePlaceholder(const NcmPlaceHolderId &placeholderId)
    {
        if (m_target.usb() && !m_usbPlaceholder) return;
        const auto& actual = m_target.usb() ? *m_usbPlaceholder : placeholderId;
        ASSERT_OK(ncmContentStorageDeletePlaceHolder(&m_contentStorage, &actual), "Failed to delete placeholder");
        m_usbPlaceholder.reset();
    }

    void ContentStorage::WritePlaceholder(const NcmPlaceHolderId &placeholderId, u64 offset, void *buffer, size_t bufSize)
    {
        if (m_target.usb() && !m_usbPlaceholder) THROW_FORMAT("USB placeholder is closed");
        const auto& actual = m_target.usb() ? *m_usbPlaceholder : placeholderId;
        ASSERT_OK(ncmContentStorageWritePlaceHolder(&m_contentStorage, &actual, offset, buffer, bufSize), "Failed to write to placeholder");
    }

    void ContentStorage::Register(const NcmPlaceHolderId &placeholderId, const NcmContentId &registeredId)
    {
        if (m_target.usb() && !m_usbPlaceholder) THROW_FORMAT("USB placeholder is closed");
        const auto& actual = m_target.usb() ? *m_usbPlaceholder : placeholderId;
        ASSERT_OK(ncmContentStorageRegister(&m_contentStorage, &registeredId, &actual), "Failed to register placeholder NCA");
        m_usbPlaceholder.reset();
    }

    void ContentStorage::Delete(const NcmContentId &registeredId)
    {
        ASSERT_OK(ncmContentStorageDelete(&m_contentStorage, &registeredId), "Failed to delete registered NCA");
    }

    bool ContentStorage::Has(const NcmContentId &registeredId)
    {
        bool hasNCA = false;
        ASSERT_OK(ncmContentStorageHas(&m_contentStorage, &hasNCA, &registeredId), "Failed to check if NCA is present");
        return hasNCA;
    }

    bool ContentStorage::CanReuse(const NcmContentId& id)
    {
        if (!Has(id)) return false;
        if (!m_target.usb()) return true;
        ASSERT_OK(nx::nexus::validate(m_target), "USB drive changed");

        // The normal SD view exposes a USB file only when it has a routing entry.
        // A separate physical SD lookup disambiguates copies present on both devices.
        bool onCard = false, routed = false;
        NcmContentStorage card{}, normal{};
        ASSERT_OK(nx::nexus::openStorage(card, nx::nexus::sd), "Could not check physical SD");
        Result rc = ncmContentStorageHas(&card, &onCard, &id);
        ncmContentStorageClose(&card);
        ASSERT_OK(rc, "Could not check physical SD content");
        if (!onCard) {
            ASSERT_OK(ncmOpenContentStorage(&normal, NcmStorageId_SdCard), "Could not check the USB routing index");
            rc = ncmContentStorageHas(&normal, &routed, &id);
            ncmContentStorageClose(&normal);
            ASSERT_OK(rc, "Could not check the registered USB content");
            if (routed) return true;
        }

        // For an unconfirmed location, authenticate the file before adding its route.
        s64 size = 0;
        ASSERT_OK(ncmContentStorageGetSizeFromContentId(&m_contentStorage,&size,&id), "Could not size existing USB content");
        if (size <= 0) THROW_FORMAT("The existing USB file is empty");
        std::vector<u8> buffer(256 * 1024);
        Sha256Context sha{};
        sha256ContextCreate(&sha);
        for (s64 offset = 0; offset < size;) {
            const size_t count = std::min<s64>(buffer.size(), size - offset);
            ASSERT_OK(ncmContentStorageReadContentIdFile(&m_contentStorage,buffer.data(),count,&id,offset), "Could not verify existing USB content");
            sha256ContextUpdate(&sha,buffer.data(),count);
            offset += count;
        }
        u8 digest[32]{};
        sha256ContextGetHash(&sha,digest);
        if (std::memcmp(digest,&id,sizeof(id))) THROW_FORMAT("Could not confirm this existing USB file. Review it with Nexus Manager's USB repair option.");
        const struct { NcmContentId id; u64 generation; s64 size; } request{id,m_target.generation,size};
        ASSERT_OK(nx::nexus::checkUsbWrite(m_target), "USB drive changed or Nexus repair is pending");
        ASSERT_OK(serviceDispatchIn(ncmGetServiceSession(),65032,request), "Could not register the verified USB file");
        return true;
    }

    void ContentStorage::CheckUsbSize(const NcmContentInfo& info)
    {
        s64 size = 0;
        ASSERT_OK(ncmContentStorageGetSizeFromContentId(&m_contentStorage,&size,&info.content_id), "A required file is missing from the selected USB drive");
        u64 expected = 0;
        ncmContentInfoSizeToU64(&info, &expected);
        if (size < 0 || static_cast<u64>(size) != expected)
            THROW_FORMAT("A file on the USB drive has a different size from the title manifest");
    }

    ContentMeta ContentStorage::ReadContentMeta(const NcmContentId &id)
    {
        if (m_target.usb()) {
            ASSERT_OK(nx::nexus::validate(m_target), "The selected USB drive is no longer connected");
            MetadataStage temporary;
            auto metadata = tin::util::GetContentMetaFromNCA(temporary.copyFrom(m_contentStorage, id));
            temporary.remove();
            ASSERT_OK(nx::nexus::validate(m_target), "The USB drive changed while reading the manifest");
            return metadata;
        }
        char pathBuf[FS_MAX_PATH] = {0};
        ASSERT_OK(ncmContentStorageGetPath(&m_contentStorage, pathBuf, sizeof(pathBuf), &id), "Could not resolve the installed manifest path");
        return tin::util::GetContentMetaFromNCA(pathBuf);
    }
}
