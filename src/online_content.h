#pragma once
#include <algorithm>
#include <string>
#include <vector>

// Selection metadata only. Models, textures, layouts and course data are read
// from each player's disc. Online menus never consult or modify save unlocks.
namespace mkdd::online::content {
struct Entry { unsigned id; const char* name; };
inline constexpr Entry arenas[]={{52,"LUIGI'S MANSION"},{53,"NINTENDO GAMECUBE"},
    {54,"BLOCK CITY"},{56,"TILT-A-KART"},{58,"COOKIE LAND"},{59,"PIPE PLAZA"}};
inline constexpr Entry courses[]={{36,"LUIGI CIRCUIT"},{34,"PEACH BEACH"},{33,"BABY PARK"},{50,"DRY DRY DESERT"},
    {40,"MUSHROOM BRIDGE"},{37,"MARIO CIRCUIT"},{35,"DAISY CRUISER"},{42,"WALUIGI STADIUM"},
    {51,"SHERBET LAND"},{41,"MUSHROOM CITY"},{38,"YOSHI CIRCUIT"},{45,"DK MOUNTAIN"},
    {43,"WARIO COLOSSEUM"},{44,"DINO DINO JUNGLE"},{47,"BOWSER'S CASTLE"},{49,"RAINBOW ROAD"}};
inline constexpr const char* karts[]={"RED FIRE","DK JUMBO","TURBO YOSHI","KOOPA DASHER","HEART COACH",
    "GOO-GOO BUGGY","WARIO CAR","KOOPA KING","GREEN FIRE","BARREL TRAIN","TURBO BIRDO","PARA WING",
    "BLOOM COACH","RATTLE BUGGY","WALUIGI RACER","BULLET BLASTER","TOAD KART","TOADETTE KART",
    "BOO PIPES","PIRANHA PIPES","PARADE KART"};
inline constexpr const char* characters[]={"","BABY MARIO","BABY LUIGI","PARATROOPA","KOOPA TROOPA","PEACH",
    "DAISY","MARIO","LUIGI","WARIO","WALUIGI","YOSHI","BIRDO","DONKEY KONG","DIDDY KONG","BOWSER",
    "BOWSER JR.","TOAD","TOADETTE","KING BOO","PETEY PIRANHA"};
inline constexpr unsigned kart_weight[]={1,2,1,0,1,0,2,2,1,0,1,0,1,0,1,0,0,0,2,2,2};
inline constexpr unsigned char_weight[]={0,0,0,0,0,1,1,1,1,2,1,1,1,2,0,2,0,0,0,2,2};
inline bool arena(unsigned id) { for(const auto& e:arenas)if(e.id==id)return true;return false; }
inline bool course(unsigned id) { for(const auto& e:courses)if(e.id==id)return true;return false; }
inline const char* name(unsigned id,bool battle) {
    if(battle){for(const auto& e:arenas)if(e.id==id)return e.name;}
    else {for(const auto& e:courses)if(e.id==id)return e.name;}
    return "UNKNOWN";
}
inline std::vector<unsigned> cup_courses(const std::string& cup) {
    unsigned begin=cup=="flower"?4:cup=="star"?8:cup=="special"?12:0;
    std::vector<unsigned> out;
    for(unsigned i=begin;i<(cup=="all-cup"?16:begin+4);++i)out.push_back(courses[i].id);
    return out;
}
inline bool team(unsigned kart,unsigned driver,unsigned partner) {
    return kart<21 && driver>=1 && driver<=20 && partner>=1 && partner<=20 && driver!=partner &&
        std::max(char_weight[driver],char_weight[partner])<=kart_weight[kart];
}
}
