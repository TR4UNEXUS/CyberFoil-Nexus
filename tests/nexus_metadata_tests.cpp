#include "nx/metadata_stage.hpp"
#include "util/install_diagnostics.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace inst::config { bool verboseInstallLogging = false; }
namespace {
enum class Fault { None, MissingSource, Create, ReadSecondChunk, Write, Flush, Resolve, Parse, DeleteOnce };
Fault fault{};
std::vector<u8> archive(550123), staged;
NcmContentStorage source{{42}};
NcmContentId content{{19}};
const NcmPlaceHolderId placeholder{{77}};
bool owned, flushed;
int opened, closed, deletes;
void checkPlaceholder(const NcmPlaceHolderId* id) { assert(!std::memcmp(id,&placeholder,sizeof(*id))); }
}
Result ncmContentStorageGetSizeFromContentId(NcmContentStorage* s,s64* size,const NcmContentId* id) {
    assert(s==&source && id==&content); *size=archive.size();
    return fault==Fault::MissingSource ? 0x202 : 0;
}
Result ncmOpenContentStorage(NcmContentStorage* s,NcmStorageId location) {
    assert(location==NcmStorageId_BuiltInUser); ++opened; s->s.id=8;return 0;
}
void ncmContentStorageClose(NcmContentStorage* s){assert(s->s.id==8);++closed;s->s.id=0;}
Result ncmContentStorageGeneratePlaceHolderId(NcmContentStorage*,NcmPlaceHolderId* id){*id=placeholder;return 0;}
Result ncmContentStorageCreatePlaceHolder(NcmContentStorage*,const NcmContentId* id,const NcmPlaceHolderId* ph,s64 size) {
    checkPlaceholder(ph); assert(!owned && std::memcmp(id,&content,sizeof(content)));
    if(fault==Fault::Create)return 1;
    staged.assign(size,0);owned=true;return 0;
}
Result ncmContentStorageReadContentIdFile(NcmContentStorage* s,void* data,size_t n,const NcmContentId* id,s64 off) {
    assert(s==&source && id==&content && n<=256*1024 && off+n<=archive.size());
    if(fault==Fault::ReadSecondChunk && off>0)return 1;
    std::memcpy(data,archive.data()+off,n);return 0;
}
Result ncmContentStorageWritePlaceHolder(NcmContentStorage* s,const NcmPlaceHolderId* id,s64 off,const void* data,size_t n) {
    checkPlaceholder(id);assert(owned && s->s.id==8 && off+n<=staged.size());
    if(fault==Fault::Write)return 1;
    std::memcpy(staged.data()+off,data,n);return 0;
}
Result ncmContentStorageFlushPlaceHolder(NcmContentStorage*) {flushed=true;return fault==Fault::Flush ? 1 : 0;}
Result ncmContentStorageGetPlaceHolderPath(NcmContentStorage*,char* path,size_t n,const NcmPlaceHolderId* id) {
    checkPlaceholder(id);assert(flushed);std::snprintf(path,n,"@UserContent:/placehld/temp.nca");return fault==Fault::Resolve ? 0x202 : 0;
}
Result ncmContentStorageDeletePlaceHolder(NcmContentStorage*,const NcmPlaceHolderId* id) {
    checkPlaceholder(id);assert(owned);++deletes;
    if(fault==Fault::DeleteOnce && deletes==1)return 1;
    owned=false;return 0;
}
int main() {
    for(size_t i=0;i<archive.size();++i)archive[i]=static_cast<u8>(i%253);
    for(Fault f:{Fault::None,Fault::MissingSource,Fault::Create,Fault::ReadSecondChunk,Fault::Write,
                 Fault::Flush,Fault::Resolve,Fault::Parse,Fault::DeleteOnce}){
        fault=f;owned=flushed=false;opened=closed=deletes=0;
        bool failed=false;
        try {
            nx::ncm::MetadataStage stage;
            auto path=stage.copyFrom(source,content);
            assert(path=="@UserContent:/placehld/temp.nca" && owned && staged==archive);
            if(fault==Fault::Parse)throw std::runtime_error("archive parser rejected manifest");
            stage.remove();
        }catch(const std::runtime_error&){failed=true;}
        assert(failed==(f!=Fault::None) && !owned && opened==closed);
        assert(deletes==((f==Fault::MissingSource||f==Fault::Create)?0:f==Fault::DeleteOnce?2:1));
    }
    const auto failure=inst::diag::ClassifyFailure("OpenFileSystemWithId:124: Failed to open file system with id: @SdCardContent://registered/000000F1/037d691e2b3cdc92cbdb3f66613534a9.nca.  Error code: 0x00000202");
    assert(failure.code=="0x00000202" && failure.category=="Storage path not found");
    assert(failure.summary.find("firmware")==std::string::npos);
    assert(inst::diag::ClassifyFailure("OpenFileSystemWithId: Unknown result 0x00000402").category=="Content archive");
    assert(inst::diag::ClassifyFailure("Could not flush the temporary manifest 0x00000402").category=="Content manifest");
    std::puts("USB manifest staging, cleanup and reported storage error regression tests passed");
}
