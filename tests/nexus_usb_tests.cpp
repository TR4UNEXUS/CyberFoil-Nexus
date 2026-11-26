#include "nx/nexus_usb.hpp"
#include <sys/iosupport.h>
#include <fcntl.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
u8 provider=1, state=4, flags=1;
u64 generation=21;
bool pending=false, smError=false;
int virtualFlags=0, largestRead=0;
unsigned lastStorage=0;
const devoptab_t* device=nullptr;
constexpr size_t fileSize=600000;
}
int AddDevice(const devoptab_t* d){device=d;return 3;}
int RemoveDevice(const char*){device=nullptr;return 0;}
Result ncmOpenContentStorage(NcmContentStorage*,NcmStorageId id){lastStorage=id;return 0;}
Result fsFsGetEntryType(FsFileSystem*,const char*,FsDirEntryType*){return pending?0:MAKERESULT(2,1);}
Result dispatch(unsigned cmd,const void* input,void* output,Params p) {
    auto words=static_cast<const u64*>(input);
    switch(cmd) {
        case 65100: *static_cast<u8*>(output)=provider; return smError?1:0;
        case 0: {
            struct {u8 mounted,ready,state,flags;u32 error;u64 generation;} s{1,1,state,flags,0,generation};
            memcpy(output,&s,sizeof(s)); return 0;
        }
        case 9: {
            const u64 space[]{100,400};memcpy(output,space,sizeof(space));return 0;
        }
        case 70: {
            const u32 view[8]{0x31445356,1,static_cast<u32>(virtualFlags)};
            memcpy(output,view,sizeof(view));return 0;
        }
        case 65030: case 65031:
            lastStorage=cmd;
            if(cmd==65031 && (*words!=generation || state!=4))return 1;
            p.out_objects->id=3;return 0;
        case 6: {
            assert(*words==generation);
            if(strcmp(static_cast<const char*>(p.buffers[0].data),"/game.nsp"))return MAKERESULT(140,205);
            const u64 info[]{fileSize,0};memcpy(output,info,sizeof(info));return 0;
        }
        case 7: {
            if(words[1]!=generation)return 1;
            const size_t n=p.buffers[1].size;
            assert(words[0]+n<=fileSize);
            largestRead=std::max<int>(largestRead,n);
            char* data=(char*)p.buffers[1].data;
            for(size_t i=0;i<n;++i)data[i]=(words[0]+i)%127;
            *static_cast<u64*>(output)=n;return 0;
        }
        case 8: {
            if(words[1]!=generation)return 1;
            struct Entry {char name[256];u64 size;u8 dir,pad[7];};
            auto entries=(Entry*)p.buffers[1].data;
            const size_t n=std::min<u64>(p.buffers[1].size/sizeof(Entry),19-words[0]);
            for(size_t i=0;i<n;++i){
                entries[i]={};std::snprintf(entries[i].name,256,"file%zu.nsp",size_t(words[0])+i);
            }
            *static_cast<u64*>(output)=n;return 0;
        }
    }
    assert(false);return 1;
}
int main() {
    using namespace nx::nexus;
    TargetChoices choices;
    auto labels=choices.labels("SD Card","Internal Storage");
    assert(labels.size()==3 && labels[2]=="USB Drive");
    assert(drive(true).free==100 && drive(true).total==400);
    NcmContentStorage storage{};
    assert(!openStorage(storage,choices.at(0)) && lastStorage==65030);
    assert(!openStorage(storage,choices.at(1)) && lastStorage==NcmStorageId_BuiltInUser);
    const Target usb=choices.at(2);
    assert(!openStorage(storage,usb) && lastStorage==65031);
    pending=true;assert(checkUsbWrite(usb));pending=false;
    virtualFlags=4;assert(checkUsbWrite(usb));virtualFlags=0;assert(!checkUsbWrite(usb));
    assert(mountBrowser());
    _reent r{};struct stat st{};
    void* f=calloc(1,device->structSize);
    const char* file="nexus:/0000000000000015/game.nsp";
    assert(device->open_r(&r,f,file,O_WRONLY,0)==-1 && r._errno==EROFS);
    assert(!device->open_r(&r,f,file,O_RDONLY,0));
    assert(!device->fstat_r(&r,f,&st) && st.st_size==fileSize);
    std::vector<char> data(fileSize+1);
    assert(device->read_r(&r,f,data.data(),data.size())==fileSize && largestRead==262144);
    for(size_t i=0;i<fileSize;i++)assert(data[i]==char(i%127));
    assert(device->read_r(&r,f,data.data(),1)==0);
    assert(device->seek_r(&r,f,-2,SEEK_END)==fileSize-2);
    assert(device->read_r(&r,f,data.data(),5)==2);
    DIR_ITER dir{calloc(1,device->dirStateSize)};
    assert(device->diropen_r(&r,&dir,"nexus:/0000000000000015"));
    char name[256];
    for (int i=0;i<19;i++) {
        assert(!device->dirnext_r(&r,&dir,name,&st));
        assert(std::string(name)=="file"+std::to_string(i)+".nsp");
    }
    assert(device->dirnext_r(&r,&dir,name,&st)==-1 && r._errno==ENOENT);
    ++generation;
    assert(validate(usb) && openStorage(storage,usb) && lastStorage==65031);
    assert(!device->dirreset_r(&r,&dir));
    assert(device->dirnext_r(&r,&dir,name,&st)==-1);
    assert(device->seek_r(&r,f,0,SEEK_SET)==0 && device->read_r(&r,f,data.data(),1)==-1);
    assert(device->open_r(&r,f,file,O_RDONLY,0)==-1);
    state=2; assert(drive(true).state==DriveState::Disconnected);
    TargetChoices absent;assert(absent.labels("SD","NAND").size()==2);
    bool rejected=false;try{absent.at(2);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
    free(f);free(dir.dirStruct);closeBrowser();
    provider=0;assert(!openStorage(storage,sd) && lastStorage==NcmStorageId_SdCard);
    smError=true;assert(openStorage(storage,sd));
    puts("Nexus USB picker, routing, capacity, paged browser, seek/read and drive change tests passed");
}
