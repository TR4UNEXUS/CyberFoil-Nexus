#include "util/nexus_update.hpp"
#include "util/json.hpp"
#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <string_view>

namespace inst::update {
namespace {
bool number(std::string_view part, unsigned& value) {
    if (part.empty() || (part.size()>1 && part.front()=='0')) return false;
    const auto end=part.data()+part.size();
    const auto parsed=std::from_chars(part.data(),end,value);
    return parsed.ec==std::errc{} && parsed.ptr==end;
}
Version version(std::string_view text, unsigned revision) {
    Version v{0,0,0,revision};
    for (size_t i=0; i<3; ++i) {
        const size_t dot=text.find('.');
        if ((i<2) != (dot!=text.npos) || !number(text.substr(0,dot),v[i]))
            throw std::runtime_error("Unrecognized CyberFoil version.");
        if (i<2) text.remove_prefix(dot+1);
    }
    return v;
}
}
std::optional<Release> parseFeed(const std::string& json, const std::string& installed, unsigned revision) {
    const auto j=nlohmann::json::parse(json);
    if(j.at("draft").get<bool>() || j.at("prerelease").get<bool>()) return {};
    const auto tag=j.at("tag_name").get<std::string>();
    const auto split=tag.find("-nexus.");
    unsigned build{};
    if(!tag.starts_with('v') || split==tag.npos ||
       !number(std::string_view(tag).substr(split+7),build) || !build)
        throw std::runtime_error("The release has no valid Nexus revision.");
    const auto base=tag.substr(1,split-1);
    if(version(base,build)<=version(installed,revision)) return {};
    const auto url=std::string("https://github.com/")+repo+"/releases/download/"+tag+"/"+assetName;
    for(const auto& a:j.at("assets")){
        if(a.value("name",std::string{})!=assetName) continue;
        const auto size=a.at("size").get<int64_t>();
        const auto digest=a.at("digest").get<std::string>();
        if(a.at("browser_download_url").get<std::string>()!=url || size<0x4000 || size>96*1024*1024 ||
           digest.size()!=71 || !digest.starts_with("sha256:") ||
           digest.find_first_not_of("0123456789abcdef",7)!=std::string::npos)
            throw std::runtime_error("GitHub returned an invalid update asset.");
        std::string notes;
        if(j.contains("body") && j["body"].is_string()) notes=j["body"].get<std::string>().substr(0,3500);
        return Release{base,tag,notes,url,digest.substr(7),build,static_cast<uint64_t>(size)};
    }
    throw std::runtime_error("The release is missing the CyberFoil Nexus NRO.");
}
}
