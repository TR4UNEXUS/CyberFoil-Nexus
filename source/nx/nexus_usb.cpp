#include "nx/nexus_usb.hpp"
#include <sys/iosupport.h>
#include <dirent.h>
#include <fcntl.h>
#include <algorithm>
#include <charconv>
#include <cerrno>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace nx::nexus {
namespace {
constexpr Result missing = MAKERESULT(Module_Libnx, LibnxError_NotFound);
constexpr Result invalid = MAKERESULT(Module_Libnx, LibnxError_BadInput);
struct Status { u8 mounted, ready, state, flags; u32 error; u64 generation; };
struct Info { u64 size; u32 cluster; u8 directory, pad[3]; };
struct Entry { char name[256]; u64 size; u8 directory, pad[7]; };
struct Space { u64 free, total; };
static_assert(sizeof(Status)==16 && sizeof(Info)==16 && sizeof(Entry)==272 && sizeof(Space)==16);
Service session{};
std::mutex ipc;
bool browserMounted = false;

Result query(Status& status) {
    if (!serviceIsActive(&session)) {
        if (owner() != Owner::Nexus) return missing;
        Result rc = smGetService(&session, "ams:usb");
        if (R_FAILED(rc)) return rc;
    }
    Result rc = serviceDispatchOut(&session, 0, status);
    if (R_FAILED(rc)) serviceClose(&session);
    return rc;
}
bool ready(const Status& s) {
    return s.mounted && s.ready && s.state == 4 && !s.error && s.generation;
}
int error(struct _reent* r, int number) { r->_errno = number; return -1; }
int ioError(struct _reent* r, Result rc) {
    if (rc == MAKERESULT(2,1) || rc == MAKERESULT(140,205)) return error(r, ENOENT);
    return error(r, rc == missing ? ENODEV : EIO);
}
struct Path {
    char name[FS_MAX_PATH];
    u64 generation;
};
struct File {
    Path path;
    off_t size, position;
    size_t chunk;
};
struct Directory {
    Path path;
    u64 offset;
    size_t index, count;
    bool end;
    Entry page[16];
};
Result openPath(const char* path, Path& out, Info& info, size_t* chunk = nullptr) {
    if (!std::strncmp(path, "nexus:", 6)) path += 6;
    // The selected drive travels with every queued source path. Reject a replacement
    // even when it contains a package with the same filename.
    if (*path != '/' || strnlen(path, 18) < 17) return invalid;
    u64 expected = 0;
    auto parsed = std::from_chars(path + 1, path + 17, expected, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != path + 17 || !expected ||
        (path[17] && path[17] != '/')) return invalid;
    path = path[17] ? path + 17 : "/";
    const size_t length = strnlen(path, sizeof(out.name));
    if (*path != '/' || length == sizeof(out.name)) return invalid;
    memcpy(out.name, path, length + 1);
    std::lock_guard lock(ipc);
    Status s{};
    Result rc = query(s);
    if (R_FAILED(rc) || !ready(s) || s.generation != expected) return R_FAILED(rc) ? rc : missing;
    out.generation = s.generation;
    if (chunk) *chunk = (s.flags & 1) ? 256*1024 : 64*1024;
    if (!strcmp(path, "/")) { info = {}; info.directory = 1; return 0; }
    return serviceDispatchInOut(&session, 6, out.generation, info,
        .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_In},
        .buffers = {{out.name, length + 1}});
}
void fillStat(struct stat* st, bool directory, u64 size) {
    *st = {};
    st->st_mode = directory ? S_IFDIR | 0555 : S_IFREG | 0444;
    st->st_nlink = 1;
    st->st_size = size;
}
int fileOpen(struct _reent* r, void* state, const char* path, int flags, int) {
    if ((flags & O_ACCMODE) != O_RDONLY || (flags & (O_TRUNC | O_CREAT))) return error(r, EROFS);
    auto& f = *static_cast<File*>(state);
    Info info{};
    Result rc = openPath(path, f.path, info, &f.chunk);
    if (R_FAILED(rc)) return ioError(r, rc);
    if (info.directory) return error(r, EISDIR);
    if (info.size > INT64_MAX) return error(r, EOVERFLOW);
    f.size = info.size;
    f.position = 0;
    return 0;
}
ssize_t fileRead(struct _reent* r, void* state, char* data, size_t length) {
    auto& f = *static_cast<File*>(state);
    if (f.position >= f.size) return 0;
    length = std::min<u64>(f.size - f.position, std::min<size_t>(length, SSIZE_MAX));
    size_t done = 0;
    while (done < length) {
        const size_t ask = std::min(length - done, f.chunk);
        const struct { u64 offset, generation; } request{static_cast<u64>(f.position), f.path.generation};
        u64 got = 0;
        std::lock_guard lock(ipc);
        if (!serviceIsActive(&session)) return done ? static_cast<ssize_t>(done) : error(r, ENODEV);
        Result rc = serviceDispatchInOut(&session, 7, request, got,
            .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_In, SfBufferAttr_HipcMapAlias | SfBufferAttr_Out},
            .buffers = {{f.path.name, strlen(f.path.name)+1}, {data + done, ask}});
        if (R_FAILED(rc) || got > ask || !got) return done ? static_cast<ssize_t>(done) : error(r, EIO);
        done += got;
        f.position += got;
    }
    return done;
}
off_t fileSeek(struct _reent* r, void* state, off_t pos, int origin) {
    auto& f = *static_cast<File*>(state);
    off_t base = 0, next;
    if (origin == SEEK_CUR) base = f.position;
    else if (origin == SEEK_END) base = f.size;
    else if (origin != SEEK_SET) return error(r, EINVAL);
    if (__builtin_add_overflow(pos, base, &next) || next < 0) return error(r, EINVAL);
    return f.position = next;
}
int pathStat(struct _reent* r, const char* name, struct stat* st) {
    Path path{}; Info info{};
    Result rc = openPath(name, path, info);
    if (R_FAILED(rc)) return ioError(r, rc);
    fillStat(st, info.directory, info.size);
    return 0;
}
DIR_ITER* dirOpen(struct _reent* r, DIR_ITER* it, const char* name) {
    auto& d = *static_cast<Directory*>(it->dirStruct);
    Info info{};
    Result rc = openPath(name, d.path, info);
    if (R_FAILED(rc)) { ioError(r, rc); return nullptr; }
    if (!info.directory) { error(r, ENOTDIR); return nullptr; }
    d.offset = d.count = d.index = 0;
    d.end = false;
    return it;
}
int dirNext(struct _reent* r, DIR_ITER* it, char* name, struct stat* st) {
    auto& d = *static_cast<Directory*>(it->dirStruct);
    if (d.index == d.count) {
        if (d.end) return error(r, ENOENT);
        const struct { u64 skip, generation; u8 directories, files, pad[6]; } request{d.offset, d.path.generation, 1, 1, {}};
        u64 count = 0;
        std::lock_guard lock(ipc);
        if (!serviceIsActive(&session)) return error(r, ENODEV);
        Result rc = serviceDispatchInOut(&session, 8, request, count,
            .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_In, SfBufferAttr_HipcMapAlias | SfBufferAttr_Out},
            .buffers = {{d.path.name, strlen(d.path.name)+1}, {d.page, sizeof(d.page)}});
        if (R_FAILED(rc) || count > std::size(d.page)) return error(r, EIO);
        d.count = count; d.index = 0; d.offset += count; d.end = count < std::size(d.page);
        if (!count) return error(r, ENOENT);
    }
    const auto& e = d.page[d.index++];
    const size_t length = strnlen(e.name, sizeof(e.name));
    if (!length || length >= sizeof(e.name) || length > NAME_MAX || strchr(e.name,'/')) return error(r, EIO);
    memcpy(name, e.name, length + 1);
    fillStat(st, e.directory, e.size);
    return 0;
}
const devoptab_t device{
    .name = "nexus", .structSize = sizeof(File), .open_r = fileOpen,
    .close_r = [](struct _reent*,void*) { return 0; },
    .read_r = fileRead, .seek_r = fileSeek,
    .fstat_r = [](struct _reent*,void* p,struct stat* s){fillStat(s,false,static_cast<File*>(p)->size);return 0;},
    .stat_r = pathStat, .dirStateSize = sizeof(Directory), .diropen_r = dirOpen,
    .dirreset_r = [](struct _reent*,DIR_ITER* it){auto& d=*static_cast<Directory*>(it->dirStruct);d.offset=d.count=d.index=0;d.end=false;return 0;},
    .dirnext_r = dirNext, .dirclose_r = [](struct _reent*,DIR_ITER*){return 0;}, .lstat_r = pathStat
};
}
Owner owner() {
    const auto name = smEncodeName("ams:usb");
    u8 present = 0;
    Result rc = hosversionAtLeast(12,0,0) ? tipcDispatchInOut(smGetServiceSessionTipc(),65100,name,present)
                                          : serviceDispatchInOut(smGetServiceSession(),65100,name,present);
    return R_FAILED(rc) ? Owner::Unknown : present ? Owner::Nexus : Owner::Native;
}
Drive drive(bool withSpace) {
    std::lock_guard lock(ipc);
    Status s{};
    Drive d{};
    if (R_FAILED(query(s))) return d;
    if (!ready(s)) {
        if (s.state == 5 || s.error) d.state = DriveState::Error;
        else if (s.state == 1 || s.state == 3) d.state = DriveState::Starting;
        else d.state = DriveState::Disconnected;
        return d;
    }
    d.state = DriveState::Ready;
    d.generation = s.generation;
    if (withSpace) {
        Space space{}; Status after{};
        if (R_SUCCEEDED(serviceDispatchOut(&session,9,space)) && R_SUCCEEDED(query(after)) &&
            ready(after) && after.generation == d.generation && space.free <= space.total) {
            d.free = space.free; d.total = space.total;
        } else {
            d.state = DriveState::Error; d.generation = 0;
        }
    }
    return d;
}
bool mountBrowser() {
    if (!browserMounted) browserMounted = AddDevice(&device) >= 0;
    return browserMounted;
}
void closeBrowser() {
    if (browserMounted) RemoveDevice("nexus:");
    browserMounted = false;
    std::lock_guard lock(ipc);
    serviceClose(&session);
}
std::vector<std::string> TargetChoices::labels(std::string card, std::string memory) const {
    std::vector<std::string> options{std::move(card), std::move(memory)};
    if (media.state == DriveState::Ready) options.push_back("USB Drive");
    return options;
}
Target TargetChoices::at(int option) const {
    if (option == 0) return sd;
    if (option == 1) return internal;
    if (option == 2 && media.generation) return {NcmStorageId_SdCard, media.generation};
    throw std::invalid_argument("Invalid installation destination");
}
Result validate(Target t) {
    return !t.usb() || drive().generation == t.generation ? 0 : missing;
}
Result checkUsbWrite(Target t) {
    if (!t.usb()) return 0;
    Result rc = validate(t);
    if (R_FAILED(rc)) return rc;
    // Repair journals are stored on physical SD. A virtual-SD session cannot inspect them safely.
    {
        std::lock_guard lock(ipc);
        const u64 pid = 0;
        struct { u32 magic, version, flags, reserved; u64 serial, generation; } view{};
        rc = serviceDispatchInOut(&session,70,pid,view,.in_send_pid=true);
        if (R_FAILED(rc)) return rc;
        if (view.magic != 0x31445356 || view.version != 1 || (view.flags & 6)) return invalid;
    }
    FsFileSystem fs{};
    rc = fsOpenSdCardFileSystem(&fs);
    if (R_FAILED(rc)) return rc;
    for (auto journal : {"/atmosphere/usb_content-repair.pending", "/atmosphere/usb-import.pending"}) {
        FsDirEntryType type;
        char path[FS_MAX_PATH]{};
        std::strcpy(path, journal);
        rc = fsFsGetEntryType(&fs,path,&type);
        if (rc != MAKERESULT(2,1)) {
            fsFsClose(&fs);
            return R_FAILED(rc) ? rc : MAKERESULT(505,900);
        }
    }
    fsFsClose(&fs);
    return 0;
}
Result openStorage(NcmContentStorage& storage, Target t) {
    if (t.usb()) {
        return serviceDispatchIn(ncmGetServiceSession(),65031,t.generation,.out_num_objects=1,.out_objects=&storage.s);
    }
    if (t.logical == NcmStorageId_SdCard) {
        const auto who = owner();
        if (who == Owner::Unknown) return missing;
        if (who == Owner::Nexus) return serviceDispatch(ncmGetServiceSession(),65030,.out_num_objects=1,.out_objects=&storage.s);
    }
    return ncmOpenContentStorage(&storage,t.logical);
}
}
