#include "util/nexus_update.hpp"
#include "util/curl.hpp"
#include <switch.h>
#include <curl/curl.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <future>
#include <memory>
#include <stdexcept>
#include <unistd.h>

namespace inst::update {
namespace fs = std::filesystem;
namespace {
std::future<std::optional<Release>> startup;
fs::path launched, staged;
using File = std::unique_ptr<FILE, decltype(&std::fclose)>;

bool nexusNro(const fs::path& path, const std::string& expected = {}) {
    std::error_code ec;
    const auto length=fs::file_size(path,ec);
    File file(std::fopen(path.c_str(),"rb"),std::fclose);
    if(ec || !file || length<0x4000 || length>96*1024*1024) return false;
    auto read=[&](uint64_t offset, void* dest,size_t size) {
        return offset<=length && size<=length-offset && !fseeko(file.get(),offset,SEEK_SET)
            && std::fread(dest,1,size,file.get())==size;
    };
    char magic[4],name[0x200],display[16];
    uint32_t aset{};
    uint64_t offset{},size{};
    if(!read(0x10,magic,4) || memcmp(magic,"NRO0",4) || !read(0x18,&aset,4) ||
       !read(aset,magic,4) || memcmp(magic,"ASET",4) || !read(uint64_t(aset)+24,&offset,8) ||
       !read(uint64_t(aset)+32,&size,8) || size!=0x4000 || offset>length-aset ||
       size>length-aset-offset) return false;
    const auto nacp=aset+offset;
    return read(nacp,name,sizeof(name)) && !strncmp(name,"CyberFoil Nexus",sizeof(name)) &&
           read(nacp+0x3060,display,sizeof(display)) &&
           (expected.empty() || !strncmp(display,expected.c_str(),sizeof(display)));
}
struct Transfer {
    std::string* json=nullptr;
    FILE* file=nullptr;
    const std::function<bool(unsigned)>* progress=nullptr;
    Sha256Context hash{};
    size_t received=0, limit=0;
    unsigned previous=101;
    bool cancel=false;
};
size_t receive(char* data,size_t unit,size_t count,void* ctx) noexcept {
    auto& t=*static_cast<Transfer*>(ctx);
    if(!unit || count>(t.limit-t.received)/unit) return 0;
    const size_t n=unit*count;
    try {
        if(t.json) t.json->append(data,n);
        else {
            if(std::fwrite(data,1,n,t.file)!=n) return 0;
            sha256ContextUpdate(&t.hash,data,n);
        }
        t.received+=n;
        if(t.progress) {
            unsigned pct=static_cast<unsigned>(100ULL*t.received/t.limit);
            if(pct!=t.previous) {
                t.previous=pct;
                if(!(*t.progress)(pct)){t.cancel=true;return 0;}
            }
        }
        return n;
    } catch(...) { return 0; }
}
void download(const std::string& url,Transfer& transfer) {
    if(!inst::curl::initialize()) throw std::runtime_error("Could not start the network client.");
    std::unique_ptr<CURL,decltype(&curl_easy_cleanup)> c(curl_easy_init(),curl_easy_cleanup);
    if(!c) throw std::runtime_error("Could not start the GitHub request.");
    auto opt=[&](CURLoption option,auto value){
        if(curl_easy_setopt(c.get(),option,value)!=CURLE_OK) throw std::runtime_error("Could not configure the GitHub request.");
    };
    opt(CURLOPT_URL,url.c_str());
    opt(CURLOPT_USERAGENT,"CyberFoil-Nexus/" APP_VERSION);
    opt(CURLOPT_PROTOCOLS,static_cast<long>(CURLPROTO_HTTPS));
    opt(CURLOPT_REDIR_PROTOCOLS,static_cast<long>(CURLPROTO_HTTPS));
    opt(CURLOPT_FOLLOWLOCATION,1L); opt(CURLOPT_MAXREDIRS,5L);
    opt(CURLOPT_SSL_VERIFYPEER,1L); opt(CURLOPT_SSL_VERIFYHOST,2L);
    opt(CURLOPT_CAINFO,"romfs:/nexus-ca-bundle.crt");
    opt(CURLOPT_NOSIGNAL,1L); opt(CURLOPT_CONNECTTIMEOUT,8L);
    opt(CURLOPT_TIMEOUT,transfer.json?15L:300L);
    opt(CURLOPT_LOW_SPEED_LIMIT,1L); opt(CURLOPT_LOW_SPEED_TIME,25L);
    opt(CURLOPT_FAILONERROR,1L); opt(CURLOPT_WRITEFUNCTION,&receive); opt(CURLOPT_WRITEDATA,&transfer);
    const auto rc=curl_easy_perform(c.get());
    long status=0; curl_easy_getinfo(c.get(),CURLINFO_RESPONSE_CODE,&status);
    if(transfer.cancel) throw std::runtime_error("Update cancelled.");
    if(rc!=CURLE_OK || status!=200)
        throw std::runtime_error("GitHub download failed (HTTP "+std::to_string(status)+", "+curl_easy_strerror(rc)+").");
}
}
void setLaunchPath(const char* argv0) {
    if(!argv0) return;
    std::string s=argv0;
    if(s.starts_with("fat:/")) s.replace(0,4,"sdmc:");
    if(!s.starts_with("sdmc:/") || fs::path(s).extension()!=".nro" || !nexusNro(s)) return;
    launched=s;
    // The previous NRO remains launchable if power is lost between the two renames.
    const std::string suffix=".nexus-previous.nro";
    if(s.ends_with(suffix)) {
        const fs::path original=s.substr(0,s.size()-suffix.size());
        std::error_code ec;
        if(!fs::exists(original,ec) && !ec){
            fs::rename(launched,original,ec);
            if(!ec){launched=original;return;}
        }
        launched.clear();
    } else {
        fs::path previous=s+suffix;
        if(nexusNro(previous)){std::error_code ignored;fs::remove(previous,ignored);}
    }
}
std::optional<Release> check() {
    std::string json;
    Transfer t; t.json=&json; t.limit=512*1024;
    download(feed,t);
    return parseFeed(json,APP_VERSION,NEXUS_REVISION);
}
void startBootCheck() { startup=std::async(std::launch::async,check); }
std::optional<Release> pollBootCheck() {
    if(!startup.valid() || startup.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return {};
    try{return startup.get();}catch(const std::exception&){return {};}
}
void finishBootCheck(){if(startup.valid()) startup.wait();}
void install(const Release& release,const std::function<bool(unsigned)>& progress) {
    if(launched.empty() || !nexusNro(launched))
        throw std::runtime_error("Open CyberFoil Nexus from its NRO on your microSD to install this update.");
    const fs::path temp=launched.string()+".nexus-download";
    try {
        File file(std::fopen(temp.c_str(),"wb"),std::fclose);
        if(!file) throw std::runtime_error("Could not create the update file on the microSD.");
        Transfer t; t.file=file.get(); t.progress=&progress; t.limit=release.bytes;
        sha256ContextCreate(&t.hash);
        download(release.url,t);
        unsigned char digest[32];sha256ContextGetHash(&t.hash,digest);
        char hex[65]{};
        for(size_t i=0;i<32;++i) std::snprintf(hex+2*i,3,"%02x",digest[i]);
        if(t.received!=release.bytes || release.sha256!=hex) throw std::runtime_error("The download failed its size or SHA-256 check.");
        if(std::fflush(file.get()) || fsync(fileno(file.get())) || std::fclose(file.release()))
            throw std::runtime_error("Could not finish writing the update to the microSD.");
        if(!nexusNro(temp,release.version)) throw std::runtime_error("The download is not the expected CyberFoil Nexus version.");
        staged=temp;
    } catch(...) {std::error_code ignored; fs::remove(temp,ignored);throw;}
}
void activatePending() {
    if(staged.empty()) return;
    const fs::path previous=launched.string()+".nexus-previous.nro";
    if(fs::exists(previous)) throw std::runtime_error("A previous recovery NRO exists. Reopen CyberFoil Nexus and retry.");
    fs::rename(launched,previous);
    try {fs::rename(staged,launched);}
    catch(...) {fs::rename(previous,launched);throw;}
    std::string quoted="\""+launched.string()+"\"";
    envSetNextLoad(launched.c_str(),quoted.c_str());
}
}
