#pragma once
#include "online_json.h"
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace mkdd::online {
// Presentation state survives refreshes. Identity, not row position, selects a
// lobby; LAN replaces an Internet observation of the same host.
struct LobbyBrowser {
    struct Row {
        std::string id,name,code,build,network,state;
        int players=0,capacity=16;int64_t ping_us=-1;
        bool locked=false,compatible=false;
        Json target;
        bool joinable()const{return compatible && !locked && players<capacity && state=="forming";}
        std::string status()const{return !compatible?"DIFFERENT BUILD":locked?"LOCKED":players>=capacity?"FULL":state!="forming"?"IN PROGRESS":"WAITING FOR PLAYERS";}
        std::string ping()const{return ping_us<0?"MEASURING...":ping_us<1000?"<1 MS":std::to_string((ping_us+500)/1000)+" MS";}
    };
    enum Sort {PingLow,PingHigh,NameAZ,NameZA,PlayersHigh,PlayersLow};
    std::string query,selected;
    Sort sort=PingLow;bool joinable_only=false;
    std::map<std::string,Row> rooms;
    static std::string fold(std::string s){for(char& c:s)if(c>='A' && c<='Z')c+=32;return s;}
    void update(const Json& values,const std::string& build){
        std::map<std::string,Row> next;
        for(const auto& value:values.list())try{
            Row row;row.id=value.at("id").str();row.name=value.at("name").str();row.code=value.at("code").str();row.build=value.at("build").str();row.network=value.at("network").str();row.state=value.at("state").str();
            if(row.id.size()!=32 || row.id.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos || row.name.empty() || row.name.size()>24 || row.code.size()!=8 || (row.network!="lan" && row.network!="internet"))continue;
            if(std::any_of(row.name.begin(),row.name.end(),[](unsigned char c){return c<32 || c>126;}))continue;
            row.capacity=value.has("capacity")?int(value.at("capacity").integer(8,16)):16;
            if(row.capacity!=8 && row.capacity!=16)continue;
            row.players=int(value.at("players").integer(1,row.capacity));row.locked=value.at("locked").boolean();row.compatible=row.build==build;
            if(value.has("ping_us"))row.ping_us=value.at("ping_us").integer(-1,60000000);
            row.target=value;
            auto old=next.find(row.id);
            if(old==next.end() || (old->second.network!="lan" && row.network=="lan"))next[row.id]=std::move(row);
        }catch(const std::exception&){}
        rooms=std::move(next);
    }
    std::vector<Row> visible()const{
        std::vector<Row> result;const auto search=fold(query);
        for(const auto& [id,row]:rooms)if(fold(row.name).find(search)!=std::string::npos && (!joinable_only || row.joinable()))result.push_back(row);
        std::sort(result.begin(),result.end(),[&](const Row& a,const Row& b){
            if(a.network!=b.network)return a.network=="lan";
            int order=0;
            if(sort==NameAZ || sort==NameZA){auto aa=fold(a.name),bb=fold(b.name);order=aa<bb?-1:aa>bb?1:0;if(sort==NameZA)order=-order;}
            else if(sort==PlayersHigh || sort==PlayersLow){order=a.players<b.players?-1:a.players>b.players?1:0;if(sort==PlayersHigh)order=-order;}
            else {if((a.ping_us<0)!=(b.ping_us<0))return a.ping_us>=0;order=a.ping_us<b.ping_us?-1:a.ping_us>b.ping_us?1:0;if(sort==PingHigh)order=-order;}
            if(order)return order<0;auto aa=fold(a.name),bb=fold(b.name);return aa==bb?a.id<b.id:aa<bb;
        });return result;
    }
    static const char* sort_label(Sort sort){const char* labels[]={"PING: LOWEST","PING: HIGHEST","NAME: A-Z","NAME: Z-A","PLAYERS: MOST","PLAYERS: FEWEST"};return labels[sort];}
};
}
