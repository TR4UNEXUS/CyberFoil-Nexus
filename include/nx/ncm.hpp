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

#pragma once

#include "nx/nexus_usb.hpp"
#include <string>
#include <optional>

extern "C"
{
#include <switch/services/fs.h>
#include <switch/services/ncm.h>
}

#include "nx/ipc/tin_ipc.h"

namespace nx::ncm
{
    class ContentMeta;
    class ContentStorage final
    {
        private:
            NcmContentStorage m_contentStorage;
            nx::nexus::Target m_target;
            std::optional<NcmPlaceHolderId> m_usbPlaceholder;

        public:
            // Don't allow copying, or garbage may be closed by the destructor
            ContentStorage& operator=(const ContentStorage&) = delete;
            ContentStorage(const ContentStorage&) = delete;   

            ContentStorage(nx::nexus::Target storageId);
            ~ContentStorage();

            void CreatePlaceholder(const NcmContentId &contentId, const NcmPlaceHolderId &placeholderId, size_t size);
            void DeletePlaceholder(const NcmPlaceHolderId &placeholderId);
            void WritePlaceholder(const NcmPlaceHolderId &placeholderId, u64 offset, void *buffer, size_t bufSize);
            void Register(const NcmPlaceHolderId &placeholderId, const NcmContentId &registeredId);
            void Delete(const NcmContentId &registeredId);
            bool Has(const NcmContentId &registeredId);
            bool CanReuse(const NcmContentId &id);
            void CheckUsbSize(const NcmContentInfo &info);
            ContentMeta ReadContentMeta(const NcmContentId &id);
    };
}
