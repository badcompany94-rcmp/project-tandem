// Private Online Time Trials: native J2D font/background and ordinary retail
// character/course selection. Enabled only by a scoped tester configuration.
namespace trials=mkdd::trials;
enum class TrialPage {None,Hub,Board};
struct TrialMenu {
    trials::TrialClient client;
    TrialPage page=TrialPage::None;
    unsigned mode_row=0,row=0,course=36,launch_ticks=0,launch_target=2;
    bool mode_seen=false,native_flow=false,active=false,result_open=false,result_sent=false;
    bool online_launch=false,list_pending=false,course_open=false,course_focus=true;
    bool name_open=false,return_board=false,result_exiting=false;
    uint32_t result_object=0;
    unsigned name_key=0;
    unsigned course_row=0;
    trials::GhostCourseRoute route;
    trials::MenuPointerFocus pointer;
    uint64_t shown_result=0;
    std::string status,rival,build,config;
    std::string name_draft,name_error,upload_name;
    std::vector<trials::TrialEntry> entries;
} trial;
constexpr int trial_back=2500,trial_previous=2501,trial_next=2502;
constexpr int trial_edit_name=2503;
int trial_hover(unsigned pressed,unsigned repeat,const std::optional<int>& click){
    const auto p=desktop_pointer::read();
    return trial.pointer.hover(p.hover,p.x,p.y,(pressed|repeat)!=0,click.has_value());
}
bool trial_clicked_back(const std::optional<int>& click){return click && (*click==-2 || *click==trial_back);}
constexpr unsigned trial_courses[]={36,34,33,50,40,37,35,42,51,41,38,45,43,44,47,49};
constexpr const char* trial_names[]={"LUIGI CIRCUIT","PEACH BEACH","BABY PARK","DRY DRY DESERT",
    "MUSHROOM BRIDGE","MARIO CIRCUIT","DAISY CRUISER","WALUIGI STADIUM",
    "SHERBET LAND","MUSHROOM CITY","YOSHI CIRCUIT","DK MOUNTAIN",
    "WARIO COLOSSEUM","DINO DINO JUNGLE","BOWSER'S CASTLE","RAINBOW ROAD"};
const char* trial_course_name(unsigned course){for(unsigned i=0;i<16;++i)if(trial_courses[i]==course)return trial_names[i];return "CHOOSE A COURSE";}
std::string trial_time(unsigned time){char b[32];std::snprintf(b,sizeof(b),"%u:%02u.%03u",time/60000,time/1000%60,time%1000);return b;}
bool trial_enabled(){
#ifdef TANDEM_PLUS_PROBE
    return false;
#else
    const auto path=getenv("TANDEM_TRIAL_CONFIG");return path && *path && !native_selection && !getenv("MKDD_ROOM_TOKEN");
#endif
}
bool trial_mode_scene(CPU* c,uint32_t app,uint32_t& scene){
    if(!trial_enabled() || !valid(app,0x4c) || mem_r32(c->r[13]-30528)!=1)return false;
    scene=mem_r32(app+0xc);
    return valid(scene,0x2200) && mem_r32(scene+0x20e0)==7 && native_pane_visible(mem_r32(scene+0xd9c));
}
void trial_change(CPU* c,TrialPage next,unsigned row=0){
    trial.page=next;trial.row=row;trial.status.clear();sound(c,0x20002);
    desktop_pointer::publish("trial-changing",{});
}
void trial_leave(){
    trial.client.cancel();trial.active=false;trial.page=TrialPage::None;
    trial.result_open=false;trial.rival.clear();trials::native_session().leave();
    trial.route.clear();trial.online_launch=false;trial.list_pending=false;trial.course_open=false;
    trial.launch_ticks=0;trial.return_board=false;trial.result_exiting=false;trial.result_object=0;
    trial.name_open=false;desktop_pointer::text_enabled=false;
}
void trial_launch(){
    trial.launch_target=2;trial.launch_ticks=180;trial.online_launch=true;
    trial.native_flow=false;trial.course_open=false;
    desktop_pointer::publish("trial-launching",{});
}
void trial_load_board(unsigned course){
    trial.client.cancel();trial.course=course;trial.row=0;trial.entries.clear();
    trial.course_open=false;trial.list_pending=true;trial.status="GETTING LATEST TIMES...";
    desktop_pointer::publish("trial-board-changing",{});
}
bool trial_enter(){
    std::string error;
    const bool stock=desktop_engine::requested()==0 && !getenv("TANDEM_COURSE_INDEX") && !getenv("GCN_COURSE_OVERLAY");
    if(!stock){trial.status="TURN OFF EXTRA CC AND COURSE MODS FOR ONLINE TIME TRIALS";return false;}
    // Retail does not construct a recorder when Ghost is OFF. Preserve the
    // player's preference rather than silently changing their memory-card data.
    if(mem_r32(0x803bc1c0)&8){trial.status="TURN GHOST ON IN THE GAME'S OPTIONS FIRST";return false;}
    if(!trials::native_session().enter(stock,error)){trial.status=error;return false;}
    trial.active=true;trial.config=getenv("TANDEM_TRIAL_CONFIG");trial.build=mkdd::online::executable_build();
    trial.rival.clear();trial.result_sent=false;return true;
}
void trial_request(trials::TrialAction action){
    trials::TrialRequest request;request.action=action;request.course=trial.course;
    if(action==trials::TrialAction::Download){
        if(trial.row>=trial.entries.size())return;request.entry=trial.entries[trial.row];trial.rival=request.entry.name;
    }else if(action==trials::TrialAction::Upload){
        const auto record=trials::native_session().result();if(!record)return;
        request.record=*record;request.course=(*record)[3];
        request.display_name=initials;trial.upload_name=initials;
    }
    if(trial.client.submit(std::move(request),trial.config,trial.build))
        trial.status=action==trials::TrialAction::List?"GETTING LATEST TIMES...":action==trials::TrialAction::Download?"DOWNLOADING GHOST...":"UPLOADING YOUR GHOST...";
    else trial.status="FINISHING THE PREVIOUS REQUEST. PLEASE TRY AGAIN";
}
void trial_poll(){
    // Rapid course changes coalesce to the latest selection. A cancelled worker
    // finishes outside the frame loop; no join or retry loop occurs here.
    if(trial.list_pending && !trial.client.busy()){
        trial.list_pending=false;trial_request(trials::TrialAction::List);
    }
    const auto response=trial.client.take();if(!response)return;
    if(response->action!=trials::TrialAction::Upload &&
       (trial.page!=TrialPage::Board || response->course!=trial.course))return;
    if(getenv("MKDD_NATIVE_MENU_TRACE"))fprintf(stderr,"[trial-service] action=%u success=%u entries=%zu\n",unsigned(response->action),response->error.empty(),response->entries.size());
    if(!response->error.empty()){trial.status=response->error;return;}
    if(response->action==trials::TrialAction::List){
        trial.entries=response->entries;trial.row=0;trial.course_focus=true;
        // A refreshed table may reorder players. Discard a queued click on the
        // previous table rather than downloading a different player's ghost.
        desktop_pointer::publish("trial-board-updated",{});
        trial.status=trial.entries.empty()?"NO TIMES YET. BE THE FIRST TO UPLOAD!":"SELECT A PLAYER TO RACE THEIR GHOST";
    }
    else if(response->action==trials::TrialAction::Download){
        std::string error;auto& session=trials::native_session();
        if(session.choose(response->course,error) && session.ghost(response->record,error)){
            if(trial.route.begin(response->course))trial_launch();
            else trial.status="THIS GHOST HAS AN UNSUPPORTED COURSE";
        }else trial.status=error;
    }else{trial.result_sent=true;trial.status="UPLOADED AS "+trial.upload_name+"! YOUR FRIEND CAN REFRESH";}
}
void trial_clear_buttons();
void trial_native_button(uint32_t button,uint32_t direction);
// Keep retail's resource loading and confirmation path, but select the already
// chosen ghost course automatically. Never change race or replay state here.
void trial_course_guard(CPU* c,uint32_t app){
    if(!trial.active || !trials::native_session().ghost())return;
    uint32_t scene;unsigned kind;
    if(!course_pointer_scene(c,app,scene,kind) || kind!=3)return;
    const auto cup=mem_r32(c->r[13]-23672),index=mem_r32(c->r[13]-23668);
    if(!trial.route.active()){
        if(cup>=4 || index>=4 || trial_courses[cup*4+index]!=trial.course)
            for(unsigned offset:{0x18u,0x1cu,0x20u,0x2cu,0x30u})
                mem_w32(0x803b4410+offset,mem_r32(0x803b4410+offset)&~0x1100u);
        return;
    }
    const bool back=(mem_r32(0x803b442c)&0x200)!=0 || trial_clicked_back(desktop_pointer::take());
    const auto phase=mem_r32(scene+0x384);
    const bool ready=mem_r32(scene+0x338)==1 && !mem_r32(scene+0x388) &&
        read_float(scene+0x14)==read_float(c->r[2]-24576);
    const auto action=trial.route.step(cup,index,phase,ready,back);
    trial_clear_buttons();
    using A=trials::GhostCourseRoute::Action;
    switch(action){
        case A::SetCup:native_set_cup(c,scene,trial.route.cup());break;
        case A::SetCourse:mem_w32(c->r[13]-23668,trial.route.index());break;
        case A::Confirm:trial_native_button(0x100,0);break;
        case A::Cancelled:trial_native_button(0x200,0);break;
        case A::Failed:
            trial.route.clear();trial.status="COULD NOT SELECT THE GHOST COURSE. GO BACK AND TRY AGAIN";
            break;
        case A::Wait:break;
    }
}
void trial_course_hint(CPU* c,uint32_t app){
    if(!trial.active || !trials::native_session().ghost())return;
    uint32_t scene;unsigned kind;if(!course_pointer_scene(c,app,scene,kind) || kind!=3)return;
    const CPU saved=*c;const auto ortho=mem_r32(scene+4);
    c->r[3]=ortho;call_indirect(c,mem_r32(mem_r32(ortho)+0x14));
    if(trial.route.active()){
        clickable.clear();
        const auto back=mem_r32(c->r[13]-21904);
        if(valid(back,0x120)){c->r[3]=back;c->r[4]=ortho;fp(c,1,0);fp(c,2,0);fn_80030F4C(c);}
        label(c,trial_course_name(trial.course),172,25,0xffed83ff);
        label(c,"RACING "+trial.rival+"'S GHOST",222,18);
        label(c,"LOADING...",298,14,0xace7ffff);
        text(c,"B: BACK",35,420,12,0xffed83ff);hit(25,410,120,32,trial_back);
        desktop_pointer::publish("trial-course-loading",clickable);*c=saved;return;
    }
    box(c,12,58,584,28,0x052b55eb);
    label(c,"GHOST: "+trial.rival+" - "+trial_course_name(trial.course),65,12,0xffed83ff);
    *c=saved;
}
void trial_clear_buttons(){
    for(unsigned offset:{0x18u,0x1cu,0x20u,0x2cu,0x30u})mem_w32(0x803b4410+offset,0);
}
struct TrialPadRestore {
    const unsigned offsets[5]={0x18,0x1c,0x20,0x2c,0x30};
    const bool full=trial_enabled();
    uint32_t values[5]{};
    TrialPadRestore(){for(unsigned i=0;i<5;++i)values[i]=mem_r32(0x803b4410+offsets[i]);}
    ~TrialPadRestore(){for(unsigned i=0;i<5;++i)if(full || i==1 || i==4)mem_w32(0x803b4410+offsets[i],values[i]);}
};
void trial_native_button(uint32_t button,uint32_t direction=0){
    native_menu_button(button,direction);
    const auto pad=mem_r32(0x803527f8);
    if(valid(pad,0x40)){
        mem_w32(pad+0x1c,mem_r32(pad+0x1c)|button);
        mem_w32(pad+0x30,mem_r32(pad+0x30)|button|direction);
    }
}
void trial_edit_profile(){
    trial.name_open=true;trial.name_draft=initials;trial.name_error.clear();trial.name_key=0;
    desktop_pointer::take_text();desktop_pointer::text_enabled=true;
    desktop_pointer::publish("trial-name",{});
}
void trial_name_update(){
    unsigned pressed=mem_r32(0x803b442c),repeat=mem_r32(0x803b4440);
    const auto click=desktop_pointer::take();const auto hover=trial_hover(pressed,repeat,click);
    if(hover>=2600 && hover<2638)trial.name_key=unsigned(hover-2600);
    const bool up=repeat&(8|0x08000000u),down=repeat&(4|0x04000000u);
    const bool left=repeat&(1|0x01000000u),right=repeat&(2|0x02000000u);
    auto& key=trial.name_key;
    if(left || right){const unsigned first=key<36?key/6*6:36,count=key<36?6:2;key=first+(key-first+(left?count-1:1))%count;}
    if(up)key=key>=36?30+(key-36)*5:key<6?36+key/3:key-6;
    if(down)key=key>=36?(key-36)*5:key>=30?36+(key%6)/3:key+6;
    if(click && *click>=2600 && *click<2638){key=*click-2600;pressed|=0x100;}
    if(trial_clicked_back(click))pressed|=0x200;
    for(unsigned char ch:desktop_pointer::take_text()){
        if(ch=='\b'){if(!trial.name_draft.empty())trial.name_draft.pop_back();}
        else if(ch=='\r')pressed|=0x1000;
        else{if(ch>='a' && ch<='z')ch-=32;if(trial.name_draft.size()<7 &&
            ((ch>='A' && ch<='Z') || (ch>='0' && ch<='9')))trial.name_draft+=char(ch);}
    }
    trial_clear_buttons();
    if(pressed&0x200)trial.name_open=false;
    else if((pressed&0x400) || ((pressed&0x100) && key==36)){
        if(!trial.name_draft.empty())trial.name_draft.pop_back();
    }else if((pressed&0x1000) || ((pressed&0x100) && key==37)){
        if(save_online_name(trial.name_draft,trial.name_error))trial.name_open=false;
    }else if((pressed&0x100) && key<36 && trial.name_draft.size()<7)
        trial.name_draft+="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"[key];
    desktop_pointer::text_enabled=trial.name_open;
    if(!trial.name_open)desktop_pointer::publish("trial-name-closed",{});
}
void trial_name_draw(CPU* c){
    clickable.clear();desktop_pointer::text_enabled=true;
    box(c,20,58,568,365,0x032b53ff);
    label(c,"EDIT ONLINE NAME",75,25,0xffff83ff);
    label(c,trial.name_draft+"_",116,24,0xffff83ff);
    for(unsigned i=0;i<36;++i){
        const float x=110+(i%6)*65,y=157+(i/6)*31;
        if(i==trial.name_key)box(c,x-9,y-4,44,29,0x1e82bdff);
        char value[2]={"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"[i],0};text(c,value,x,y,21);
        hit(x-9,y-4,44,29,2600+i);
    }
    text(c,"DELETE",125,353,18,trial.name_key==36?0xffff83ff:0xffffffff);hit(100,345,160,30,2636);
    text(c,"SAVE NAME",352,353,18,trial.name_key==37?0xffff83ff:0xffffffff);hit(330,345,175,30,2637);
    label(c,trial.name_error.empty()?"A: LETTER   X: DELETE   START: SAVE":trial.name_error,390,10,0xffed83ff);
    text(c,"B: CANCEL",35,426,12,0xffed83ff);hit(25,416,120,30,trial_back);
    desktop_pointer::publish("trial-name",clickable);
}
// A nonzero return means this menu handled pointer input. Native animation still
// runs, with navigation consumed, and no waiting on network operations.
bool trial_menu_update(CPU* c,uint32_t app){
    uint32_t scene=0;const bool modes=trial_mode_scene(c,app,scene);
    if(!trial_enabled())return false;
    trial_poll();
    if(!modes){
        trial.mode_seen=false;
        const auto kind=mem_r32(c->r[13]-30528);
        if(trial.return_board && kind==1 && valid(mem_r32(app+0xc),0x2200)){
            const auto phase=mem_r32(mem_r32(app+0xc)+0x20e0);
            if(phase==13){trial_clear_buttons();trial_native_button(0x200);return true;}
        }
        if(kind==1 && valid(mem_r32(app+0xc),0x2200) && mem_r32(mem_r32(app+0xc)+0x20e0)==13){
            if(trial.online_launch || trial.native_flow)classic_release(c);
            trial.native_flow=false;
            trial.online_launch=false;trial.launch_ticks=0;
        }
        if(trial.active && (kind==0 || (kind==1 && valid(mem_r32(app+0xc),0x2200) && mem_r32(mem_r32(app+0xc)+0x20e0)<6)))trial_leave();
        uint32_t course_scene;unsigned course_kind;
        if(trial.route.active() && course_pointer_scene(c,app,course_scene,course_kind) && course_kind==3)return true;
        return false;
    }
    if(trial.return_board){
        trial.return_board=false;trial.mode_seen=true;trial.native_flow=false;trial.mode_row=2;
        trial.page=TrialPage::Board;trial_load_board(trial.course);classic_release(c);
    }
    if(trial.name_open){trial_name_update();return true;}
    if(!trial.mode_seen)trial.native_flow=false;
    if(trial.native_flow){
        // Retail still owns engine selection and the memory-card initials.
        // Keep our three-row screen visible while forwarding its editing input.
        const auto click=desktop_pointer::take();
        if(trial_clicked_back(click))trial_native_button(0x200);
        else if(click){
            if(*click==2003 || *click==150)trial_native_button(0,0x01000000u);
            else if(*click==2004 || *click==151)trial_native_button(0,0x02000000u);
            else if(*click==153 || *click==154)trial_native_button(0,*click==153?0x08000000u:0x04000000u);
            else if(*click==152 || *click==155 || *click==2000)trial_native_button(0x100);
        }
        if(mem_r32(0x803b442c)&0x200){trial.native_flow=false;trial.mode_seen=true;}
        return true;
    }
    if(trial.launch_ticks){
        const auto click=desktop_pointer::take();
        if((mem_r32(0x803b442c)&0x200) || trial_clicked_back(click)){
            trial.launch_ticks=0;trial.online_launch=false;trial.route.clear();
            trial.status.clear();trial_clear_buttons();return true;
        }
        if(getenv("MKDD_NATIVE_MENU_TRACE") && (trial.launch_ticks==60 || trial.launch_ticks==30 || trial.launch_ticks==1))fprintf(stderr,"[trial-launch] ticks=%u selected=%u target=%u pad=%08x scene=%08x r3=%08x edit=%08x counters=%u/%u/%u dialog=%u\n",trial.launch_ticks,mem_r32(scene+0x2100),trial.launch_target,mem_r32(0x803527f8),scene,c->r[3],mem_r32(scene+0x2108),mem_r32(scene+0x210c),mem_r32(scene+0x2110),mem_r32(scene+0x2114),mem_r32(mem_r32(scene+0x22b4)+0x14));
        if(mem_r32(scene+0x2100)!=trial.launch_target)mem_w8(scene+0x2108,0);
        // Move through retail's normal TT selection before accepting; it owns
        // the selected-mode globals and all character/race setup.
        if(mem_r32(scene+0x2100)==trial.launch_target){
            trial_clear_buttons();trial_native_button(0x100);
            if(trial.online_launch){
                if(!--trial.launch_ticks){trial.online_launch=false;trial.route.clear();trial.status="COULD NOT OPEN CHARACTER SELECT. PLEASE TRY AGAIN";}
            }else{trial.launch_ticks=0;trial.native_flow=true;}
        }
        else {
            trial_clear_buttons();trial_native_button(0,trial.launch_target?0x04000000u:0x08000000u);
            if(!--trial.launch_ticks){trial.online_launch=false;trial.route.clear();trial.status="COULD NOT OPEN CHARACTER SELECT. PLEASE TRY AGAIN";}
        }
        return true;
    }
    if(!trial.mode_seen){trial.mode_seen=true;trial.mode_row=mem_r32(scene+0x2100)==2?1:0;if(trial.active){trial.page=TrialPage::Hub;trial.row=0;trial.route.clear();}}
    unsigned pressed=mem_r32(0x803b442c),repeat=mem_r32(0x803b4440);
    if(getenv("MKDD_NATIVE_MENU_TRACE") && (pressed || repeat))fprintf(stderr,"[trial-input] page=%u row=%u pressed=%x repeat=%x\n",unsigned(trial.page),trial.mode_row,pressed,repeat);
    const auto click=desktop_pointer::take();const auto hover=trial_hover(pressed,repeat,click);
    const bool up=(repeat&(8|0x08000000u))!=0,down=(repeat&(4|0x04000000u))!=0;
    const bool left=(repeat&(1|0x01000000u))!=0,right=(repeat&(2|0x02000000u))!=0;
    if(trial_clicked_back(click))pressed|=0x200;
    if(trial.page==TrialPage::None){
        if(hover>=2000 && hover<2003)trial.mode_row=unsigned(hover-2000);
        if(up || down){trial.mode_row=(trial.mode_row+(up?2:1))%3;sound(c,0x20000);}
        if(click && *click>=2000 && *click<2003){trial.mode_row=unsigned(*click-2000);pressed|=0x100;}
        // Retail GP engine arrows remain available at the first row.
        trial_clear_buttons();
        if(pressed&0x200){classic_release(c);trial_leave();trial_native_button(0x200);return true;}
        // Like retail, engine selection starts only after choosing Grand Prix.
        if(pressed&0x1100){
            if(trial.mode_row==2){if(trial_enter())trial_change(c,TrialPage::Hub);}
            else {const unsigned target=trial.mode_row==1?2:0;trial_leave();trial.launch_target=target;trial.launch_ticks=60;}
        }
        return true;
    }
    trial_clear_buttons();
    if(trial.page==TrialPage::Hub && ((pressed&0x800) || (click && *click==trial_edit_name))){trial_edit_profile();return true;}
    if(trial.page==TrialPage::Board && trial.course_open){
        if((pressed&(0x200|0x800)) || (click && *click==2201)){trial.course_open=false;return true;}
        if(hover>=2400 && hover<2416)trial.course_row=unsigned(hover-2400);
        if(up || down)trial.course_row=(trial.course_row+(up?15:1))%16;
        if(left || right || (click && (*click==trial_previous || *click==trial_next)))trial.course_row=(trial.course_row+8)%16;
        if(click && *click>=2400 && *click<2416){trial.course_row=*click-2400;pressed|=0x100;}
        if(pressed&0x1100){trial_load_board(trial_courses[trial.course_row]);trial.course_focus=true;}
        return true;
    }
    if(pressed&0x200){
        trial.client.cancel();trial.list_pending=false;
        if(trial.page==TrialPage::Hub){trial_leave();trial.mode_row=2;}
        else trial_change(c,TrialPage::Hub);
        return true;
    }
    if(trial.page==TrialPage::Board && ((pressed&0x800) || (click && *click==2201))){
        trial.course_open=true;trial.course_focus=true;trial.course_row=trials::course_slot(trial.course).value_or(0);return true;
    }
    const unsigned count=trial.page==TrialPage::Hub?2:unsigned(trial.entries.size());
    if(trial.page==TrialPage::Board && hover==2201)trial.course_focus=true;
    if(hover>=2100 && unsigned(hover-2100)<count){trial.row=unsigned(hover-2100);trial.course_focus=false;}
    if(count && (up || down)){
        if(trial.page==TrialPage::Board && trial.course_focus){trial.course_focus=false;trial.row=up?count-1:0;}
        else if(trial.page==TrialPage::Board && ((up && !trial.row) || (down && trial.row==count-1)))trial.course_focus=true;
        else trial.row=(trial.row+(up?count-1:1))%count;
        sound(c,0x20000);
    }
    const bool previous=left || (click && *click==trial_previous),next=right || (click && *click==trial_next);
    if(trial.page==TrialPage::Board && (previous || next) && count){
        trial.row=std::min(count-1,unsigned(std::max(0,int(trial.row)+(previous?-6:6))));trial.course_focus=false;
    }
    if(click && *click>=2100 && unsigned(*click-2100)<count){trial.row=unsigned(*click-2100);trial.course_focus=false;pressed|=0x100;}
    if(click && *click==2200)pressed|=0x400;
    if(trial.page==TrialPage::Board && trial.course_focus && (pressed&0x1100)){
        trial.course_open=true;trial.course_row=trials::course_slot(trial.course).value_or(0);return true;
    }
    if(trial.page==TrialPage::Board && (trial.client.busy() || trial.list_pending))return true;
    if(trial.page==TrialPage::Board && (pressed&0x400)){trial_request(trials::TrialAction::List);return true;}
    if(!(pressed&0x1100))return true;
    if(trial.page==TrialPage::Hub){
        if(trial.row){trial_change(c,TrialPage::Board);trial.course_focus=true;trial_load_board(trial.course);}
        else {std::string error;if(trials::native_session().enter(true,error)){trial.rival.clear();trial.route.clear();trial_launch();}else trial.status=error;}
    }else if(trial.page==TrialPage::Board && count)trial_request(trials::TrialAction::Download);
    return true;
}
void trial_row(CPU* c,const std::string& title,float y,bool selected,int action,float size=36){
    const auto color=selected?0x99e8ffff:0x1686b8cc;
    // Retail's broad translucent selection band sits behind its wordmark.
    const float band[][2]={{34,y+17},{578,y+17},{578,y+62},{29,y+62},{48,y+30}};
    flat_polygon(c,band,5,selected?0x2487c8a0:0x14669370);
    const float tail[][2]={{43,y+20},{58,y+20},{38,y+54},{23,y+54}};
    if(!trial_art_picture(c,"bar2",0,y+18,580,52,color)){
        box(c,0,y+20,58,6,color);flat_polygon(c,tail,4,color);box(c,23,y+52,557,5,color);
    }
    trial_art_picture(c,"corsor_maru1",178,y-7,60,60,selected?0xffdf58ff:0xc99b27dd);
    if(!trial_art_word(c,title,84,y-1,size,selected?1:2))
        racing_text(c,title,84,y-1,size,selected?0xffff86ff:0xe7b73fff,false,true);
    hit(25,y-10,555,80,action);
}
// Reuse complete retail pane assemblies: their leaves, flag deformation, clock
// hands/eyes and animation are not present in any single texture.
bool trial_native_decoration(CPU* c,uint32_t scene,uint64_t tag,float scale,float center_y,float target_y){
    uint32_t pane=0;
    for(unsigned at=8;at<0x2100;at+=4){
        const auto candidate=mem_r32(scene+at);
        if(valid(candidate,0x100) && mem_r32(candidate+0x10)==uint32_t(tag>>32) && mem_r32(candidate+0x14)==uint32_t(tag)){
            pane=candidate;break;
        }
    }
    if(!pane)return false;
    const auto link=mem_r32(pane+0xec),parent=link>=0x800000dc?link-0xdc:0;
    if(!valid(parent,0xb4))return false;
    float saved[8];for(unsigned i=0;i<8;++i){saved[i]=read_float(parent+0x80+i*4);write_float(parent+0x80+i*4,saved[i]*scale);}
    write_float(parent+0x8c,480+(saved[3]-480)*scale);
    write_float(parent+0x9c,target_y+(saved[7]-center_y)*scale);
    const CPU old=*c;
    c->r[3]=pane;c->r[4]=mem_r32(scene+4);c->r[5]=0;c->r[6]=1;fp(c,1,0);fp(c,2,0);fn_80028E30(c);
    for(unsigned i=0;i<8;++i)write_float(parent+0x80+i*4,saved[i]);
    c->r[3]=mem_r32(scene+4);call_indirect(c,mem_r32(mem_r32(c->r[3])+0x14));*c=old;
    return true;
}
void trial_background(CPU* c,uint32_t scene,const char* heading){
    const auto ortho=mem_r32(scene+4),back=mem_r32(c->r[13]-21904),line=mem_r32(c->r[13]-21896);
    c->r[3]=ortho;call_indirect(c,mem_r32(mem_r32(ortho)+0x14));
    c->r[3]=back;c->r[4]=ortho;fp(c,1,0);fp(c,2,0);fn_80030F4C(c);
    const auto title=mem_r32(line+0x120);const auto visible=mem_r8(title+0xb0);mem_w8(title+0xb0,0);
    c->r[3]=line+8;c->r[4]=ortho;fp(c,1,0);fp(c,2,0);fn_80030F4C(c);mem_w8(title+0xb0,visible);
    c->r[3]=ortho;call_indirect(c,mem_r32(mem_r32(ortho)+0x14));
    if(!trial_art_word(c,heading,304,14,40,0,true))racing_text(c,heading,304,14,40,0xffff9aff,true,true);
}
bool trial_menu_draw(CPU* c,uint32_t app){
    uint32_t scene=0;const bool modes=trial_mode_scene(c,app,scene);
    if(!modes && trial_enabled() && valid(app,0x4c) && mem_r32(c->r[13]-30528)==1){
        scene=mem_r32(app+0xc);
        if(!valid(scene,0x2200))return false;
        const auto phase=mem_r32(scene+0x20e0);
        if(phase==6 && !native_pane_visible(mem_r32(scene+0xd9c)))return false;
        // Cover the retail entrance and exit animations too, while keeping
        // input owned by retail until the fully open phase (7).
        if(phase!=6 && !((trial.online_launch || trial.native_flow) && phase>=7 && phase<13))return false;
    }else if(!modes)return false;
    if(!backgrounds(c))return false;
    const CPU saved=*c;clickable.clear();
    trial_background(c,scene,trial.page==TrialPage::None?"SELECT MODE":trial.page==TrialPage::Board?"LEADERBOARDS":"ONLINE TIME TRIALS");
    if(trial.name_open){trial_name_draw(c);*c=saved;return true;}
    if(trial.page==TrialPage::None){
        trial_native_decoration(c,scene,0x00004e5f53746f6bull,1,0,0); // N_Stok
        trial_row(c,"GRAND PRIX",127,trial.mode_row==0,2000);
        const bool engine_edit=trial.mode_row==0 && (trial.native_flow || trial.launch_ticks);
        if(engine_edit){
        const unsigned engine=std::min(3u,mem_r32(scene+0x2104));const char* engines[]={"50cc","100cc","150cc","MIRROR"};
        trial_art_picture(c,"entry_engine"+std::to_string(engine+1),442,119,56,48);
        if(!trial_art_picture(c,"power_mozi."+std::to_string(engine+1),424,162,92,23,0xffff9aff))text(c,engines[engine],450,164,16,0xffff9aff);
        text(c,"<",404,143,20,0xffed83ff);hit(390,119,36,65,2003);
        text(c,">",530,143,20,0xffed83ff);hit(520,119,36,65,2004);
        }else trial_native_decoration(c,scene,0x004d4e6770503131ull,.82f,180,147); // MNgpP11
        trial_row(c,"TIME TRIALS",218,trial.mode_row==1,2001);
        trial_native_decoration(c,scene,0x004d4e7461503131ull,.82f,320,238); // MNtaP11
        trial_row(c,"ONLINE TIME TRIALS",309,trial.mode_row==2,2002,30);
        trial_native_decoration(c,scene,0x004d4e7461503131ull,.82f,320,329);
        if(trial.native_flow){
            clickable.clear();
            if(trial.mode_row==0){
                hit(390,119,36,65,2003);hit(520,119,36,65,2004);hit(85,119,300,65,2000);
            }else{
                // Draw the original initials panel/drums above this background.
                // Its original coordinates and helper state also own hit testing.
                const auto pane=mem_r32(scene+0xf58),ortho=mem_r32(scene+4);
                const auto link=valid(pane,0xf0)?mem_r32(pane+0xec):0;
                const auto parent=link>=0x800000dc?link-0xdc:0;
                const unsigned offsets[]={0x34,0x3c,0x9c};float old_y[3]{};
                if(valid(parent,0xa0))for(unsigned i=0;i<3;++i){
                    old_y[i]=read_float(parent+offsets[i]);write_float(parent+offsets[i],old_y[i]-91);
                }
                if(native_pane_visible(pane)){
                    c->r[3]=pane;c->r[4]=ortho;c->r[5]=0;c->r[6]=1;fp(c,1,0);fp(c,2,0);fn_80028E30(c);
                    const float x=read_float(pane+0x8c),y=read_float(pane+0x9c);
                    hit(x-22,y-80,44,35,153);hit(x-22,y+25,44,40,154);
                    for(unsigned i=0;i<3;++i)native_hit_pane(mem_r32(scene+0xf64+i*12),155,9);
                }
                c->r[3]=mem_r32(scene+0x22b4);c->r[4]=ortho;fn_801E0450(c);
                if(valid(parent,0xa0))for(unsigned i=0;i<3;++i)write_float(parent+offsets[i],old_y[i]);
                const auto ok=mem_r32(scene+0x10c0);
                if(native_pane_visible(ok)){
                    c->r[3]=ok;c->r[4]=ortho;c->r[5]=0;c->r[6]=1;fp(c,1,0);fp(c,2,0);fn_80028E30(c);
                    native_hit_pane(ok,152,5);
                }
            }
        }
    }else if(trial.page==TrialPage::Hub){
        label(c,"POST AS: "+initials+"   Y: EDIT NAME",88,13,0xffed83ff);hit(110,78,388,30,trial_edit_name);
        trial_row(c,"CHOOSE COURSE",146,trial.row==0,2100);
        trial_row(c,"LEADERBOARDS",257,trial.row==1,2101);
    }else if(trial.page==TrialPage::Board){
        box(c,30,83,548,36,trial.course_focus?0x1266aaff:0x073a6aff);
        text(c,"COURSE",40,94,12,0xace7ffff);
        text(c,trial_course_name(trial.course),127,91,17,0xffed83ff);
        text(c,"v",551,91,17,0xffed83ff);hit(30,83,548,36,2201);
        text(c,"RANK",41,130,12,0xace7ffff);text(c,"ONLINE PLAYER",101,130,12,0xace7ffff);
        text(c,"COURSE TIME",424,130,12,0xace7ffff);
        box(c,30,150,548,1,0x3183baff);
        const unsigned start=trial.row/6*6;
        for(unsigned i=start;i<trial.entries.size() && i<start+6;++i){
            const auto& entry=trial.entries[i];const float y=162+(i-start)*31;
            if(i==trial.row && !trial.course_focus)box(c,30,y-4,548,29,0x1e82bdcc);
            text(c,(std::to_string(i+1)+".").c_str(),41,y,17,0xffed83ff);
            const float name_size=std::min(17.f,300.f/std::max(1.f,float(entry.name.size())*.65f));
            text(c,entry.name.c_str(),101,y,name_size,i==trial.row && !trial.course_focus?0xffff83ff:0xffffffff);
            text(c,trial_time(entry.time_ms).c_str(),424,y,17,0xffed83ff);hit(30,y-4,548,29,2100+i);
        }
        if(start){text(c,"< PREV",40,360,12,0xffed83ff);hit(30,350,110,32,trial_previous);}
        if(start+6<trial.entries.size()){text(c,"NEXT >",477,360,12,0xffed83ff);hit(465,350,113,32,trial_next);}
        const auto pages=std::max(1u,(unsigned(trial.entries.size())+5)/6);
        label(c,"PAGE "+std::to_string(start/6+1)+" / "+std::to_string(pages),360,12);
        label(c,"COMMUNITY TIMES - NOT COMPETITION VERIFIED",387,9,0xace7ffff);
        if(trial.course_open){
            clickable.clear();hit(30,83,548,36,2201);box(c,30,120,548,264,0x042950ff);
            const unsigned first=trial.course_row/8*8;
            for(unsigned i=first;i<first+8;++i){
                const float y=128+(i-first)*29;
                if(i==trial.course_row)box(c,34,y-3,540,28,0x1e82bdff);
                text(c,trial_names[i],48,y,17,i==trial.course_row?0xffff83ff:0xffffffff);
                hit(34,y-3,540,28,2400+i);
            }
            text(c,"< PREV",43,365,12,0xffed83ff);hit(34,357,110,30,trial_previous);
            text(c,"NEXT >",477,365,12,0xffed83ff);hit(465,357,109,30,trial_next);
            label(c,"COURSES "+std::to_string(first+1)+" - "+std::to_string(first+8)+" / 16",365,12);
        }
    }
    if(trial.online_launch){clickable.clear();trial.status="OPENING CHARACTER SELECT...";}
    if(!trial.status.empty())label(c,trial.status,401,10,0xffed83ff);
    text(c,trial.course_open?"B: CANCEL":"B: BACK",35,426,12,0xffed83ff);hit(25,416,120,30,trial_back);
    if(!trial.online_launch){
        label(c,trial.page==TrialPage::Board?"Y: COURSE   A: SELECT":"A: SELECT",426,11,0xace7ffff);
        if(trial.page==TrialPage::Board && !trial.course_open){
            text(c,"X: REFRESH",465,426,12,0xffed83ff);hit(455,416,130,30,2200);
        }
    }
    std::string key="trial-"+std::to_string(unsigned(trial.page));
    if(trial.page==TrialPage::Board)key+="-"+std::to_string(trial.course)+
        (trial.course_open?"-courses-"+std::to_string(trial.course_row/8):"-rows-"+std::to_string(trial.row/6));
    if(trial.online_launch)key+="-launching";
    desktop_pointer::publish(key,clickable);
    *c=saved;return true;
}
// Retail Result2D's six rows: Retry, Course, Characters, Replay, Save, Quit.
// Reuse their animation and transition machinery; only the online actions differ.
constexpr unsigned trial_result_choices[]={1,2,4,7,10,8};
bool trial_result_update(CPU* c){
    if(!trial.active)return false;
    auto& session=trials::native_session();
    const auto manager=c->r[3];
    if(!session.result() || !valid(manager,0x3c)){trial.result_open=false;return false;}
    const auto result=mem_r32(manager+0x20);
    // Wait for the actual retry menu, including its entrance animation. Merely
    // having captured a ghost does not mean the native results flow is ready.
    const bool ready=valid(result,0x1c18) && mem_r8(c->r[13]-23800) &&
        mem_r32(c->r[13]-23792)==2 && mem_r32(result+0x198c)>=30 &&
        read_float(result+0x1988)==read_float(c->r[2]-26840) &&
        !mem_r32(result+0x1980) && !mem_r32(manager+0x28) && !mem_r32(manager+0x34);
    if(getenv("MKDD_NATIVE_MENU_TRACE")){
        static unsigned last_state=~0u;
        const auto state=mem_r32(c->r[13]-23792);
        if(last_state!=state){last_state=state;fprintf(stderr,"[trial-result] state=%u draw=%u animation=%u progress=%.2f target=%.2f exit=%u manager=%08x\n",state,mem_r8(c->r[13]-23800),mem_r32(result+0x198c),read_float(result+0x1988),read_float(c->r[2]-26840),mem_r32(result+0x1980),manager);}
        static bool was_ready=false;
        if(ready!=was_ready){was_ready=ready;fprintf(stderr,"[trial-result] native retry menu ready=%u object=%08x serial=%llu\n",ready,result,(unsigned long long)session.result_serial());}
    }
    if(!ready){trial.result_open=false;trial.result_exiting=false;return false;}
    if(session.result_serial()!=trial.shown_result){
        trial.shown_result=session.result_serial();trial.result_sent=false;trial.result_exiting=false;
        trial.status.clear();trial.row=0;trial.route.clear();
        desktop_pointer::context(true);desktop_pointer::publish("trial-result",{});
    }
    if(trial.result_exiting)return false;
    trial.result_open=true;trial.result_object=result;
    desktop_pointer::context(true);trial_poll();
    if(trial.name_open){trial_name_update();return true;}
    unsigned pressed=mem_r32(0x803b442c),repeat=mem_r32(0x803b4440);
    const auto click=desktop_pointer::take();const auto hover=trial_hover(pressed,repeat,click);
    if(hover>=2300 && hover<2306)trial.row=hover-2300;
    if(repeat&(8|4|0x0c000000u))trial.row=(trial.row+((repeat&(8|0x08000000u))?5:1))%6;
    if(click && *click>=2300 && *click<2306){trial.row=*click-2300;pressed|=0x100;}
    // No timeout, and B/Start do not silently discard the completed run.
    trial_clear_buttons();mem_w32(c->r[13]-23796,trial_result_choices[trial.row]);
    if(!(pressed&0x100))return true;
    if(trial.row==1){if(!trial.client.busy())trial_edit_profile();return true;}
    if(trial.row==4){
        if(!trial.client.busy() && !trial.result_sent)trial_request(trials::TrialAction::Upload);
        return true;
    }
    if(trial.client.busy()){trial.status="PLEASE WAIT FOR YOUR UPLOAD TO FINISH";return true;}
    if(trial.row==2){trial.return_board=true;trial.course=session.result_info().course;}
    trial.result_open=false;trial.result_exiting=true;
    desktop_pointer::publish("trial-result-leaving",{});
    // The last draw restored the retail textures. Free our descriptors while
    // their race heap still exists, before retail can destroy it on a transition.
    classic_release(c);
    // Let retail perform the fade, resource teardown, retry/replay and app switch.
    trial_native_button(0x100);return true;
}
bool trial_result_draw(CPU* c){
    if(!trial.active || !trial.result_open)return false;
    const CPU saved=*c;clickable.clear();
    if(trial.name_open || !trial.status.empty()){
        // The last native result row leaves its translated position matrix in
        // GX. Restore Result2D's screen context before drawing a full-screen
        // name editor or upload status, so pixels and hit regions agree.
        const auto ortho=mem_r32(trial.result_object+4);
        if(valid(ortho,0x40)){c->r[3]=ortho;call_indirect(c,mem_r32(mem_r32(ortho)+0x14));}
    }
    if(trial.name_open){trial_name_draw(c);*c=saved;return true;}
    for(unsigned row=0;row<6;++row){
        const auto pane=mem_r32(trial.result_object+0xc8+row*0x6c);
        native_hit_pane(pane,2300+row,4);
    }
    if(!trial.status.empty())label(c,trial.status,420,10,0xffed83ff);
    desktop_pointer::publish("trial-result",clickable);
    *c=saved;return true;
}

// Texture substitutions are scoped to this draw. Keep all original result
// resources intact for offline Time Trials and subsequent native menus.
struct TrialResultLabels {
    CPU* cpu;uint32_t panes[3]{},previous[3]{};
    TrialResultLabels(CPU* c):cpu(c){
        if(!trial.active || !trial.result_open || !valid(trial.result_object,0x1c18))return;
        const uint32_t drawing_pane=c->r[3];
        const CPU saved=*c;
        const unsigned rows[]={1,2,4};const char* words[]={"CHANGE NAME","TO LEADERBOARDS","UPLOAD GHOST"};
        for(unsigned i=0;i<3;++i){
            // Result2D assigns its stock wordmarks immediately before drawing
            // each row. Substitute at that row boundary, after the assignment.
            if(drawing_pane!=mem_r32(trial.result_object+0x50+rows[i]*4))continue;
            const auto pane=mem_r32(trial.result_object+0xc8+rows[i]*0x6c);
            if(!valid(pane,0x168))continue;
            const auto resource=trial_result_word(c,words[i]);if(!resource)continue;
            panes[i]=pane;c->r[3]=pane;c->r[4]=resource;c->r[5]=0;
            call_indirect(c,mem_r32(mem_r32(pane)+0x110));previous[i]=c->r[3];
            if(getenv("MKDD_NATIVE_MENU_TRACE")){
                static bool traced[3]{};
                if(!traced[i]){traced[i]=true;fprintf(stderr,"[trial-result] label row=%u pane=%08x replacement=%08x original=%08x\n",rows[i],pane,resource,previous[i]);}
            }
        }
        *c=saved;
    }
    ~TrialResultLabels(){
        const CPU saved=*cpu;
        for(unsigned i=0;i<3;++i)if(panes[i] && valid(previous[i],0x20)){
            cpu->r[3]=panes[i];cpu->r[4]=previous[i];cpu->r[5]=0;
            call_indirect(cpu,mem_r32(mem_r32(panes[i])+0x110));
        }
        *cpu=saved;
    }
};
