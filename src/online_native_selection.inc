// Original orchestration of the disc's own character/kart/cup scenes. These
// scenes run locally; only authorized choices cross the room protocol boundary.
struct NativeSelection {
    struct Claim {
        enum State {Idle,Retry,Waiting,Apply} state=Idle;
        uint32_t scene=0;unsigned index=0,slot=0;
        Json characters;
        bool reset=false,cancel=false;
    } claim;
    bool initialize=false,finished=false,course=false,chosen=false,battle_menu=false,battle_chosen=false;
    unsigned target=1,cup=0,arena=52,first=0,second=0;
    std::string state;
    std::string notice;
    std::chrono::steady_clock::time_point notice_until{};
} native_ui;
void native_character_notice(std::string value) {
    native_ui.notice=std::move(value);native_ui.notice_until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
}
uint16_t menu_unlocks[2]{};bool menu_unlocks_saved=false;
void restore_menu_unlocks() {
    if(menu_unlocks_saved){mem_w16(0x803bc1c0+4,menu_unlocks[0]);mem_w16(0x803bc1c0+6,menu_unlocks[1]);menu_unlocks_saved=false;}
}
constexpr unsigned native_character_order[]={7,8,5,6,11,12,1,2,17,18,4,3,13,14,15,16,9,10,20,19};
const Json& own_member() {
    for(const auto& m:room_reply.at("room").at("members").list())if(m.at("you").boolean())return m;
    throw std::runtime_error("invalid-session");
}
unsigned native_character(unsigned index) {return index<20?native_character_order[index]:0;}
unsigned native_character_index(unsigned id) {
    for(unsigned i=0;i<20;++i)if(native_character_order[i]==id)return i;
    return 0;
}
const Json* native_character_owner(unsigned id) {
    if(!id || !room_reply.has("room"))return nullptr;
    for(const auto& member:room_reply.at("room").at("members").list())
        for(const auto& character:member.at("characters").list())if(character.integer(1,20)==id)return &member;
    return nullptr;
}
bool native_character_blocked(unsigned id) {
    const auto* owner=native_character_owner(id);return owner && !owner->at("you").boolean();
}
void native_character_reply(const std::string& error) {
    auto& claim=native_ui.claim;
    if(!native_selection || claim.state!=NativeSelection::Claim::Waiting)return;
    // Back also cancels a losing or stale in-flight pick. Release any earlier
    // rider reservation rather than leave it owned after the rejected request.
    if(claim.cancel && !error.empty()) {
        claim.characters=Json();claim.characters.kind=Json::Array;
        claim.reset=true;claim.cancel=false;claim.state=NativeSelection::Claim::Retry;return;
    }
    claim.state=error.empty()?NativeSelection::Claim::Apply:error=="stale-room"?NativeSelection::Claim::Retry:NativeSelection::Claim::Idle;
    if(error=="character-taken")native_character_notice("THAT CHARACTER WAS JUST CHOSEN. CHOOSE ANOTHER");
}
void native_character_request(uint32_t scene,Json characters,unsigned index=0,unsigned slot=0,bool reset=false) {
    auto& claim=native_ui.claim;claim={};claim.state=NativeSelection::Claim::Retry;
    claim.scene=scene;claim.characters=std::move(characters);claim.index=index;claim.slot=slot;claim.reset=reset;
}
void native_character_cancel(uint32_t scene) {
    if(native_ui.claim.state!=NativeSelection::Claim::Idle && native_ui.claim.state!=NativeSelection::Claim::Retry)native_ui.claim.cancel=true;
    else if(native_ui.claim.state==NativeSelection::Claim::Retry) {
        Json empty;empty.kind=Json::Array;native_character_request(scene,std::move(empty),0,0,true);
    }
    else if(!own_member().at("characters").list().empty()) {
        Json empty;empty.kind=Json::Array;native_character_request(scene,std::move(empty),0,0,true);
    }else if(room_reply.at("room").at("host").boolean() && can_act())submit(Work::Select,Json::dict({{"op","back"}}));
    else message="THE HOST CAN RETURN EVERYONE TO THE LOBBY";
}
// Consume a confirm until the authoritative room has reserved the character.
// The retail screen never accepts a duplicate that must later be unwound.
bool native_character_confirm(CPU* c,uint32_t scene,unsigned pad) {
    if(!native_selection || native_ui.state!="characters" || pad || !valid(scene,0x2200) || mem_r32(scene+0x2144)!=0)return false;
    if(native_ui.claim.state!=NativeSelection::Claim::Idle)return true;
    const auto first=native_character(mem_r32(scene+0x2138));
    const unsigned index=mem_r32(scene+0x212c),character=native_character(index);
    if(!character)return true;
    if(first && own_member().at("role").str()!="solo")return true;
    if(character==first || native_character_blocked(character)) {
        const auto* owner=native_character_owner(character);
        native_character_notice(owner?"CHOSEN BY "+owner->at("initials").str()+" - CHOOSE ANOTHER CHARACTER":"CHOOSE A DIFFERENT SECOND CHARACTER");
        sound(c,0x20005);return true;
    }
    Json characters;characters.kind=Json::Array;
    if(first)characters.array.emplace_back((int)first);
    characters.array.emplace_back((int)character);
    native_character_request(scene,std::move(characters),index,first?1:0);
    return true;
}
void native_character_claim_tick(CPU* c,uint32_t app) {
    auto& claim=native_ui.claim;
    if(claim.state==NativeSelection::Claim::Idle)return;
    if(claim.state==NativeSelection::Claim::Apply && claim.cancel) {
        Json empty;empty.kind=Json::Array;native_character_request(claim.scene,std::move(empty),0,0,true);
    }
    if(claim.state==NativeSelection::Claim::Retry && can_act()) {
        if(submit(Work::Character,Json::dict({{"op","character"},{"characters",claim.characters}})))claim.state=NativeSelection::Claim::Waiting;
    }
    if(claim.state!=NativeSelection::Claim::Apply || mem_r32(c->r[13]-30528)!=1 || mem_r32(app+0xc)!=claim.scene)return;
    if(own_member().at("characters").dump()!=claim.characters.dump())return;
    if(claim.reset) {
        claim={};native_ui.finished=false;native_ui.first=native_ui.second=0;native_ui.initialize=true;
        native_scene(c,1);message.clear();native_ui.notice.clear();return;
    }
    const auto scene=claim.scene,offset=claim.slot?0x213cu:0x2138u;
    if(mem_r32(scene+0x20e0)!=13)return;
    mem_w32(scene+0x212c,claim.index);
    const CPU saved=*c;c->r[3]=scene;c->r[4]=0;orig_fn_80162AE4(c);*c=saved;
    if(mem_r32(scene+offset)==claim.index) {
        fprintf(stderr,"[online-menu] reserved character=%u slot=%u\n",native_character(claim.index),claim.slot);
        claim={};message.clear();native_ui.notice.clear();
    }
}
void native_scene(CPU* c,unsigned kind) {
    if(kind!=0)classic_release(c);
    const CPU saved=*c;c->r[3]=kind;fn_801D9378(c);*c=saved;
}
void native_room_state(CPU* c) {
    if(!room_reply.has("room"))return;
    const auto& room=room_reply.at("room");
    const auto next=room.at("state").str();
    if(next=="mode") {
        if(page!=Page::Type && page!=Page::Engine && page!=Page::Battle)open(c,Page::Type);
        native_ui.state=next;
        if(mode_advance && !busy()){mode_advance=false;submit(Work::Start,Json::dict({{"op","start"}}));}
    }else if(next=="characters" && (!native_selection || native_ui.state!="characters")) {
        if(!menu_unlocks_saved){menu_unlocks[0]=mem_r16(0x803bc1c0+4);menu_unlocks[1]=mem_r16(0x803bc1c0+6);menu_unlocks_saved=true;}
        native_selection=true;native_ui={};native_ui.state=next;native_ui.initialize=true;
        page=Page::Native;native_scene(c,1);
        fprintf(stderr,"[online-menu] original character selection started\n");
    }else if(native_selection && next=="course" && native_ui.state!="course") {
        native_ui.state=next;native_ui.claim={};native_ui.notice.clear();
        {
            native_ui.course=true;native_ui.target=room.at("mode").str()=="grand-prix"?3:4;
            const auto app=mem_r32(c->r[13]-22016);
            if(valid(app,0x4c) && mem_r32(c->r[13]-30528)==1) {
                const auto scene=mem_r32(app+0xc);
                if(valid(scene,0x2200)){const CPU saved=*c;c->r[3]=scene;fn_80164E0C(c);*c=saved;}
            }
            mem_w32(0x803b145c+8,native_ui.target==3?2:room.at("mode").str()=="balloon"?4:room.at("mode").str()=="shine"?7:6);
            if(native_ui.target==3) {
                const std::string engine=room.at("rules").at("engine").str();
                mem_w32(0x803b145c+0x10,engine=="50cc"?0:engine=="100cc"?1:engine=="mirror"?3:2);
                native_scene(c,3);
            } else {native_scene(c,4);}
        }
        fprintf(stderr,"[online-menu] all characters confirmed; host selecting event\n");
    }else if(next=="forming" && (native_selection || page==Page::Type || page==Page::Engine || page==Page::Battle)) {
        const bool was_native=native_selection;native_selection=false;native_ui={};open(c,Page::Lobby);if(was_native)native_scene(c,0);
    }
}
void native_selection_upload(CPU* c,uint32_t scene) {
    if(busy() || queued_work!=Work::None || native_ui.claim.state!=NativeSelection::Claim::Idle || !room_reply.has("room"))return;
    const auto& room=room_reply.at("room");
    if(room.at("state").str()!="characters")return;
    const auto& own=own_member();const auto role=own.at("role").str();
    const unsigned stage=mem_r32(scene+0x2144),phase=mem_r32(scene+0x20e0);
    unsigned first=native_character(mem_r32(scene+0x2138)),second=native_character(mem_r32(scene+0x213c));
    const bool solo=role=="solo",passenger=role=="passenger";
    const auto& reserved=own.at("characters").list();
    if(phase==13 && ((!first && !reserved.empty()) || (solo && first && !second && reserved.size()==2))) {
        const auto character=(unsigned)reserved[first?1:0].integer(1,20);
        mem_w32(scene+0x212c,native_character_index(character));
        const CPU saved=*c;c->r[3]=scene;c->r[4]=0;orig_fn_80162AE4(c);*c=saved;return;
    }
    if(!solo && second) {
        const Json* partner=nullptr;
        for(const auto& m:room.at("members").list())if(!m.at("you").boolean() && m.at("kart").integer(0,7)==own.at("kart").integer(0,7))partner=&m;
        if(partner && (partner->at("characters").list().empty() || partner->at("characters").list()[0].integer(1,20)!=second)) {
            native_ui.finished=false;native_ui.initialize=true;native_scene(c,1);return;
        }
    }
    if(stage>=2 || phase>=14)native_ui.finished=true;
    if(!first || (solo && (!second || first==second)))return;
    Json chars;chars.kind=Json::Array;chars.array.emplace_back((int)first);if(solo)chars.array.emplace_back((int)second);
    if(chars.dump()!=own.at("characters").dump())return;
    if(!solo && !second) {
        const Json* partner=nullptr;
        for(const auto& m:room.at("members").list())if(!m.at("you").boolean() && m.at("kart").integer(0,7)==own.at("kart").integer(0,7))partner=&m;
        if(!partner || partner->at("characters").list().empty())return;
        const unsigned other=(unsigned)partner->at("characters").list()[0].integer(1,20);
        if(other==first)return;
        mem_w32(scene+0x212c,native_character_index(other));
        // This rider is already reserved by the teammate; only materialize it
        // in the local kart preview, without claiming it for this player.
        const CPU saved=*c;c->r[3]=scene;c->r[4]=0;orig_fn_80162AE4(c);*c=saved;
        return;
    }
    if(passenger && second && stage>=1) {
        if(!own.at("selected").boolean())submit(Work::Select,Json::dict({{"op","selection-done"}}));
        return;
    }
    if(!native_ui.finished)return;
    const unsigned db=mem_r32(0x803b145c+0x30);
    if(!valid(db,4))return;
    const unsigned kart=mem_r32(db);
    if(kart>20)return;
    if(own.at("choice").at("kart").integer(0,20)!=kart || !own.has("kart_selected") || !own.at("kart_selected").boolean()) {
        submit(Work::Select,Json::dict({{"op","selection-kart"},{"kart",(int)kart}}));return;
    }
    if(!own.at("selected").boolean())submit(Work::Select,Json::dict({{"op","selection-done"}}));
}
void native_selection_before(CPU* c,uint32_t app) {
    if(!native_selection)return;
    if(native_ui.state=="course" && !native_ui.battle_menu && room_reply.at("room").at("host").boolean() &&
       !native_ui.chosen && (mem_r32(0x803b4410+0x1c)&0x200) && can_act()) {
        submit(Work::Select,Json::dict({{"op","back"}}));return;
    }
    network_updates(c);
    if(!native_selection)return;
    if(native_ui.state=="characters" && valid(app,0x4c) && mem_r32(c->r[13]-30528)==1) {
        const auto scene=mem_r32(app+0xc);
        if(valid(scene,0x2200) && (mem_r32(0x803b442c)&0x200)) {
            mem_w32(0x803b442c,mem_r32(0x803b442c)&~0x200u);
            mem_w32(0x803b4440,mem_r32(0x803b4440)&~0x200u);
            native_character_cancel(scene);
        }
        native_character_claim_tick(c,app);
    }
    if(native_ui.battle_menu && can_act()) {
        const uint32_t pressed=mem_r32(0x803b4410+0x1c),repeat=mem_r32(0x803b4410+0x30);
        const char* modes[]={"balloon","shine","bob-omb"};
        if(native_ui.battle_chosen) {
            const auto& room=room_reply.at("room");
            if(room.at("mode").str()==modes[selection] && room.at("rules").at("rounds").integer(1,9)==draft_rounds) {
                native_ui.battle_menu=false;page=Page::Native;
                mem_w32(0x803b145c+8,selection==0?4:selection==1?7:6);native_scene(c,4);
            }
        }else {
            if(repeat&(8|0x08000000|4|0x04000000)){selection=(selection+((repeat&(8|0x08000000))?2:1))%3;sound(c,0x20000);}
            if(repeat&(1|0x01000000|2|0x02000000)){draft_rounds=(((draft_rounds-1)/2+((repeat&(1|0x01000000))?4:1))%5)*2+1;sound(c,0x20000);}
            if(pressed&0x100) {
                native_ui.battle_chosen=true;
                submit(Work::Select,Json::dict({{"op","rules"},{"mode",modes[selection]},{"rules",Json::dict({{"arena",52},{"rounds",draft_rounds}})}}));
            }else if(pressed&0x200)submit(Work::Select,Json::dict({{"op","back"}}));
        }
    }
    // The unlocks are local menu presentation state. Online race I/O disables
    // memory-card writes; the room never depends on a player's save unlocks.
    mem_w16(0x803bc1c0+4,0xffff);mem_w16(0x803bc1c0+6,0xffff);
    if(native_ui.chosen && !busy() && queued_work==Work::None) {
        const auto& room=room_reply.at("room");const auto& rules=room.at("rules");
        if(room.at("mode").str()=="grand-prix") {
            const char* cups[]={"mushroom","flower","star","special","all-cup"};
            if(rules.at("cup").str()!=cups[native_ui.cup]) {
                submit(Work::Select,Json::dict({{"op","rules"},{"rules",Json::dict({{"engine",rules.at("engine")},{"cup",cups[native_ui.cup]}})}}));return;
            }
        }else if(rules.at("arena").integer(52,59)!=native_ui.arena) {
            submit(Work::Select,Json::dict({{"op","rules"},{"rules",Json::dict({{"arena",(int)native_ui.arena},{"rounds",rules.at("rounds")}})}}));return;
        }
        submit(Work::Start,Json::dict({{"op","start"}}));
    }
    const uint32_t scene=mem_r32(app+0xc),kind=mem_r32(c->r[13]-30528);
    if(kind==1 && valid(scene,0x2200))native_selection_upload(c,scene);
    if(!busy() && std::chrono::steady_clock::now()>=next_poll) {
        if(native_ui.state=="characters" && kind==1 && valid(scene,0x2200))
            submit(Work::Poll,Json::dict({{"op","preview"},{"character",(int)native_character(mem_r32(scene+0x212c))}}));
        else if(native_ui.state=="course" && room_reply.at("room").at("host").boolean())submit(Work::Poll,native_course_preview(c,app));
        else submit(Work::Poll,Json::dict({{"op","state"}}));
    }
}
void native_selection_after(CPU* c,uint32_t app) {
    if(!native_selection)return;
    native_follow_course(c,app);
    const auto scene=mem_r32(app+0xc),kind=mem_r32(c->r[13]-30528);
    if(kind==1 && valid(scene,0x2200)) {
        if(native_ui.initialize) {
            mem_w32(scene+0x20e0,12);mem_w32(scene+0x20ec,0);native_ui.initialize=false;
        }else if(native_ui.state=="characters" && mem_r32(scene+0x20e0)>=14) {
            native_ui.finished=true;mem_w32(scene+0x20e0,13);
        }
        native_selection_upload(c,scene);
    }
}
bool native_race_selected(CPU* c) {
    if(!native_selection || !native_ui.course)return false;
    if(!room_reply.at("room").at("host").boolean())return true;
    native_ui.cup=std::min(4u,mem_r32(0x803b145c+0xc));
    // ResMgr's selected course is populated by the original map screen.
    native_ui.arena=mem_r32(c->r[13]-22236);
    native_ui.chosen=true;return true;
}
