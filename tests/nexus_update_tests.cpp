#include "util/nexus_update.hpp"
#include "util/json.hpp"
#include <cassert>
#include <iostream>
#include <fstream>

using namespace inst::update;
int main(int argc, char** argv) {
    using json=nlohmann::json;
    const std::string tag="v1.5.1-nexus.10";
    const std::string url=std::string("https://github.com/")+repo+"/releases/download/"+tag+"/"+assetName;
    json asset={{"name",assetName},{"size",16000000},{"digest","sha256:"+std::string(64,'b')},{"browser_download_url",url}};
    json j={{"draft",false},{"prerelease",false},{"tag_name",tag},{"assets",json::array({{{"name","pack.zip"}},asset})},{"body","Fixes"}};
    auto r=parseFeed(j.dump(),"1.5.1",9);
    assert(r && r->version=="1.5.1" && r->revision==10 && r->url==url);
    assert(!parseFeed(j.dump(),"1.5.1",10) && !parseFeed(j.dump(),"1.5.1",11));
    assert(!parseFeed(j.dump(),"1.6.0",1) && parseFeed(j.dump(),"1.4.12",300));
    auto bad=[&](json k){bool threw=false;try{parseFeed(k.dump(),"1.5.1",1);}catch(const std::exception&){threw=true;}assert(threw);};
    auto k=j;k["assets"][1]["browser_download_url"]="https://example.com/install";bad(k);
    k=j;k["assets"][1]["digest"]=nullptr;bad(k);
    k=j;k["assets"][1]["size"]=0;bad(k);
    k=j;k["tag_name"]="v1.5.2-nexus.0";bad(k);
    k=j;k["assets"]=json::array();bad(k);
    k=j;k["prerelease"]=true;assert(!parseFeed(k.dump(),"1.0.0",0));
    if(argc==2) {
        std::ifstream in(argv[1]);std::string s{std::istreambuf_iterator<char>(in),{}};
        auto first=parseFeed(s,"1.5.1",0);
        assert(first && first->version=="1.5.1" && first->revision==1);
        assert(!parseFeed(s,"1.5.1",1));
        std::cout<<"Live feed verified: "<<first->url<<"\n";
    }
    std::cout<<"Nexus update feed tests passed\n";
}
