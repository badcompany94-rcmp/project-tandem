#include "lobby_browser.h"
#include <cassert>
using namespace mkdd::online;
Json row(char id,const char* name,int count,int ping,const char* network="internet",const char* build="same",bool locked=false){return Json::dict({{"id",std::string(32,id)},{"name",name},{"code","23456789"},{"players",count},{"ping_us",ping},{"network",network},{"build",build},{"locked",locked},{"state","forming"}});}
int main(){
    LobbyBrowser browser;Json rows;rows.kind=Json::Array;
    rows.array={row('a',"Alpha",2,90000),row('b',"Beta",8,10000),row('c',"Charlie",4,-1),row('a',"Alpha",2,300,"lan"),row('d',"Delta",3,1000,"internet","old"),row('e',"Echo",1,5000,"internet","same",true)};
    browser.selected=std::string(32,'a');browser.update(rows,"same");assert(browser.rooms.size()==5);assert(browser.rooms.at(browser.selected).network=="lan");
    browser.query="ALp";assert(browser.visible().size()==1);browser.query="none";assert(browser.visible().empty());browser.query="";
    for(int sort=0;sort<6;++sort){browser.sort=LobbyBrowser::Sort(sort);auto list=browser.visible();assert(list.front().network=="lan");assert(list.back().network=="internet");}
    browser.sort=LobbyBrowser::NameZA;assert(browser.visible()[1].name=="Echo");browser.sort=LobbyBrowser::PlayersHigh;assert(browser.visible()[1].players==8);
    browser.sort=LobbyBrowser::PingHigh;assert(browser.visible().back().ping_us==-1);
    browser.joinable_only=true;assert(browser.visible().size()==2);assert(browser.rooms.at(std::string(32,'d')).status()=="DIFFERENT BUILD");
    rows.array={row('a',"Alpha",3,500,"lan")};browser.update(rows,"same");assert(browser.selected==std::string(32,'a'));assert(browser.query.empty());assert(browser.joinable_only);assert(browser.rooms.size()==1);
    assert(browser.visible().front().ping()=="<1 MS");
}
