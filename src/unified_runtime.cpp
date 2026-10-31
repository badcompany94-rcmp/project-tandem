#include "unified_runtime.h"
#include <algorithm>
#include <cstdio>

namespace mkdd::eos {
using online::Json;
UnifiedRuntime::UnifiedRuntime(LanRuntime& l,PeerRuntime* n):local(l),internet(n){
    local.packet=[this](const auto& p,const auto& s,auto c,const auto& d){receive(p,s,c,d);};
    local.idle=[this]{pulse(false);};
    local.admit_identity=[this](const std::string& p){return !internet || !internet->members().count(p);};
    if(internet){internet->packet=local.packet;internet->idle=[this]{pulse(true);};}
}
UnifiedRuntime::~UnifiedRuntime(){local.packet={};local.idle={};local.admit_identity={};if(internet){internet->packet={};internet->idle={};}}
void UnifiedRuntime::pulse(bool from_internet){
    if(ticking || pumping)return;
    struct Guard{bool& b;Guard(bool& x):b(x){b=true;}~Guard(){b=false;}}guard(pumping);
    if(from_internet)local.tick();else if(internet)internet->tick();
    flush();if(idle)idle();
}
void UnifiedRuntime::tick(){
    if(ticking)return;
    struct Guard{bool& b;Guard(bool& x):b(x){b=true;}~Guard(){b=false;}}guard(ticking);
    if(internet)internet->tick();local.tick();
    if(mode!=Idle && owner().empty())leave();
    flush();if(idle)idle();
}
const std::string& UnifiedRuntime::self()const{return mode==Local || !internet?local.self():internet->self();}
const std::string& UnifiedRuntime::owner()const{return mode==Local || !internet || (mode==Host && internet->owner().empty())?local.owner():internet->owner();}
const std::string& UnifiedRuntime::control_socket()const{return mode==Local || !internet || (mode==Host && internet->control_socket().empty())?local.control_socket():internet->control_socket();}
void UnifiedRuntime::create(const std::string& code,const std::string& build){
    if(mode!=Idle)throw std::runtime_error("already-in-room");
    if(internet){internet->create(code,build);local.host_identity(internet->self(),internet->control_socket());}
    try{local.create(code,build);}catch(...){if(!internet)throw;fputs("LAN discovery unavailable; Internet lobby remains available\n",stderr);}
    mode=Host;
}
void UnifiedRuntime::join(const std::string& code,const std::string& build){
    if(mode!=Idle)throw std::runtime_error("already-in-room");
    try{local.join(code,build);mode=Local;return;}catch(const std::exception& e){
        if(std::string(e.what())!="lan-room-not-found" && std::string(e.what())!="lan-connect-failed")throw;
    }
    if(!internet)throw std::runtime_error("lan-room-not-found");
    internet->join(code,build);mode=Internet;
}
void UnifiedRuntime::join_target(const std::string& code,const std::string& build,const Json& target){
    if(mode!=Idle)throw std::runtime_error("already-in-room");
    if(target.at("network").str()=="lan"){local.join_target(code,build,target);mode=Local;}
    else {if(!internet)throw std::runtime_error("internet-unavailable");internet->join_target(code,build,target);mode=Internet;}
}
void UnifiedRuntime::leave(){local.leave();if(internet)internet->leave();mode=Idle;game.clear();game_members.clear();forwarded.clear();}
void UnifiedRuntime::kick(const std::string& peer){
    if(!hosting())throw std::runtime_error("host-only");
    if(local.members().count(peer))local.kick(peer);else if(internet)internet->kick(peer);
}
void UnifiedRuntime::refresh_invitation(const std::string& code){
    if(!hosting())throw std::runtime_error("host-only");
    if(internet && internet->hosting())internet->refresh_invitation(code);if(local.hosting())local.refresh_invitation(code);
}
std::set<std::string> UnifiedRuntime::members()const{
    auto result=mode==Internet && internet?internet->members():local.members();
    if(mode==Host && internet){auto remote=internet->members();result.insert(remote.begin(),remote.end());}
    return result;
}
bool UnifiedRuntime::authorized(const std::string& peer,const std::string& socket)const{
    if(peer==self())return false;
    return (!game.empty() && game==socket && game_members.count(peer)) ||
        (!control_socket().empty() && socket==control_socket() && members().count(peer));
}
void UnifiedRuntime::game_peers(std::string socket,std::set<std::string> peers){
    peers.erase(self());if(peers.size()>7)throw std::runtime_error("invalid-eos-roster");
    if(socket!=game)forwarded.clear();game=socket;game_members=std::move(peers);
    auto nearby=local.members();std::set<std::string> local_game;
    for(const auto& p:game_members)if(nearby.count(p))local_game.insert(p);
    local.game_peers(socket,std::move(local_game));
    if(internet){std::set<std::string> remote_game;auto remote=internet->members();for(const auto& p:game_members)if(remote.count(p))remote_game.insert(p);internet->game_peers(socket,std::move(remote_game));}
}
bool UnifiedRuntime::physical_send(const std::string& peer,const std::string& socket,uint8_t channel,const std::vector<uint8_t>& data){
    if(local.members().count(peer))return local.send(peer,socket,channel,data);
    return internet && internet->send(peer,socket,channel,data);
}
bool UnifiedRuntime::send(const std::string& peer,const std::string& socket,uint8_t channel,const std::vector<uint8_t>& data){
    if(!authorized(peer,socket))return false;
    auto direct=members();
    if(hosting() || direct.count(peer))return physical_send(peer,socket,channel,data);
    if(socket!=game || game.empty() || data.size()>1104 || channel>2 || owner().empty())return false;
    std::vector<uint8_t> wire{15};wire.insert(wire.end(),self().begin(),self().end());wire.insert(wire.end(),peer.begin(),peer.end());wire.push_back(channel);wire.insert(wire.end(),data.begin(),data.end());
    return physical_send(owner(),socket,1,wire);
}
void UnifiedRuntime::receive(const std::string& peer,const std::string& socket,uint8_t channel,const std::vector<uint8_t>& data){
    if(channel!=1 || data.empty() || data[0]!=15){if(authorized(peer,socket) && packet)packet(peer,socket,channel,data);return;}
    if(socket!=game || game.empty() || data.size()<=66 || data.size()>1170 || data[65]>2)return;
    const std::string from(data.begin()+1,data.begin()+33),to(data.begin()+33,data.begin()+65);
    if(!game_members.count(from) || (to!=self() && !game_members.count(to)) || from==to)return;
    if(hosting()){
        if(from!=peer)return; // A client may forward only its own input.
        if(to!=self()){
            if(!physical_send(to,socket,1,data) && data[66]!=14 && forwarded.size()<256)forwarded.push_back({to,socket,data});
            return;
        }
    }else if(peer!=owner() || to!=self())return;
    if(packet)packet(from,socket,data[65],{data.begin()+66,data.end()});
}
void UnifiedRuntime::flush(){
    unsigned count=0;for(auto it=forwarded.begin();it!=forwarded.end() && count++<64;){
        if(it->socket!=game || !game_members.count(it->to) || physical_send(it->to,it->socket,1,it->bytes))it=forwarded.erase(it);else ++it;
    }
}
std::string UnifiedRuntime::route()const{
    if(mode==Local){for(const auto& p:game_members)if(!local.members().count(p))return "mixed";return "lan";}
    if(mode==Host && internet && local.members().size()>1 && internet->members().size()>1)return "mixed";
    return internet?internet->route():"lan";
}
void UnifiedRuntime::publish(const Json& room,const std::string& build){if(!hosting())return;local.publish(room,build);if(internet)internet->publish(room,build);}
Json UnifiedRuntime::browse(const std::string& build){
    discovery_error.clear();
    auto result=local.browse(build);
    if(internet)try{auto remote=internet->browse(build);for(auto& row:remote.array){row.object["network"]="internet";result.array.push_back(std::move(row));}}catch(const std::exception&){discovery_error="INTERNET LIST UNAVAILABLE. LAN RESULTS ARE STILL AVAILABLE";fputs("Internet browser refresh failed; local results remain available\n",stderr);}
    std::map<std::string,Json> unique;
    for(auto& row:result.array){auto key=row.at("id").str();auto found=unique.find(key);if(found==unique.end() || row.at("network").str()=="lan")unique[key]=std::move(row);}
    result.array.clear();for(auto& [id,row]:unique)result.array.push_back(std::move(row));
    return result;
}
}
