#pragma once
#include <cstddef>
#include <cstdint>
using u8=uint8_t; using u32=uint32_t; using u64=uint64_t; using Result=uint32_t;
using s64=int64_t;
struct NcmContentId { u8 bytes[16]; };
struct NcmPlaceHolderId { u8 bytes[16]; };
enum NcmStorageId { NcmStorageId_BuiltInUser=4,NcmStorageId_SdCard=5 };
struct Service { int id=0; };
struct NcmContentStorage { Service s; };
struct FsFileSystem {}; using FsDirEntryType=int;
constexpr int Module_Libnx=345,LibnxError_NotFound=1,LibnxError_BadInput=2;
constexpr unsigned FS_MAX_PATH=769,SfBufferAttr_HipcMapAlias=1,SfBufferAttr_In=2,SfBufferAttr_Out=4;
#define MAKERESULT(m,n) ((m)|((n)<<9))
#define R_FAILED(r) ((r)!=0)
#define R_SUCCEEDED(r) ((r)==0)
struct Params {
    unsigned buffer_attrs[2]{};
    struct {const void* data;size_t size;} buffers[2]{};
    bool in_send_pid=false;
    int out_num_objects=0;
    Service* out_objects=nullptr;
};
Result dispatch(unsigned, const void*, void*, Params);
#define serviceDispatchInOut(s,c,i,o,...) dispatch(c,&i,&o,Params{__VA_ARGS__})
#define tipcDispatchInOut serviceDispatchInOut
#define serviceDispatchOut(s,c,o,...) dispatch(c,nullptr,&o,Params{__VA_ARGS__})
#define serviceDispatchIn(s,c,i,...) dispatch(c,&i,nullptr,Params{__VA_ARGS__})
#define serviceDispatch(s,c,...) dispatch(c,nullptr,nullptr,Params{__VA_ARGS__})
inline bool hosversionAtLeast(int,int,int){return true;}
inline u64 smEncodeName(const char*){return 42;}
inline bool serviceIsActive(Service* s){return s->id!=0;}
inline void serviceClose(Service* s){s->id=0;}
inline Result smGetService(Service* s,const char*){s->id=1;return 0;}
Result ncmOpenContentStorage(NcmContentStorage*,NcmStorageId);
void ncmContentStorageClose(NcmContentStorage*);
Result ncmContentStorageGetSizeFromContentId(NcmContentStorage*,s64*,const NcmContentId*);
Result ncmContentStorageGeneratePlaceHolderId(NcmContentStorage*,NcmPlaceHolderId*);
Result ncmContentStorageCreatePlaceHolder(NcmContentStorage*,const NcmContentId*,const NcmPlaceHolderId*,s64);
Result ncmContentStorageReadContentIdFile(NcmContentStorage*,void*,size_t,const NcmContentId*,s64);
Result ncmContentStorageWritePlaceHolder(NcmContentStorage*,const NcmPlaceHolderId*,s64,const void*,size_t);
Result ncmContentStorageFlushPlaceHolder(NcmContentStorage*);
Result ncmContentStorageGetPlaceHolderPath(NcmContentStorage*,char*,size_t,const NcmPlaceHolderId*);
Result ncmContentStorageDeletePlaceHolder(NcmContentStorage*,const NcmPlaceHolderId*);
inline Result fsOpenSdCardFileSystem(FsFileSystem*){return 0;}
Result fsFsGetEntryType(FsFileSystem*,const char*,FsDirEntryType*);
inline void fsFsClose(FsFileSystem*){}
