// Exercise the production menu update/draw code and real pointer hit regions.
// Only guest rendering/memory and asynchronous transport are fixtures; no game,
// live service, credentials, save files or physical input devices are needed.
#include "time_trial_client.h"
#include "time_trial_navigation.h"
#include "time_trials_runtime.h"
#include "desktop_pointer.h"
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <utility>
#include <new>
#ifdef TANDEM_MENU_SDL_INPUT_TEST
#include <SDL.h>
SDL_Window* window=nullptr;
float input_default(const char*,float fallback){return fallback;}
#endif

#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace mkdd::trials {
struct MockTransfer {
    bool busy=false;
    unsigned submitted=0,cancelled=0;
    std::optional<TrialRequest> request;
    std::optional<TrialResponse> response;
} transfer;
TrialHttpResponse trial_https(const TrialHttpRequest&,const std::function<bool()>&){std::abort();}
TrialClient::~TrialClient()=default;
bool TrialClient::submit(TrialRequest r,std::string,std::string){
    if(transfer.busy)return false;
    transfer.request=std::move(r);transfer.busy=true;++transfer.submitted;return true;
}
bool TrialClient::busy()const{return transfer.busy;}
void TrialClient::cancel(){transfer.response.reset();++transfer.cancelled;}
std::optional<TrialResponse> TrialClient::take(){return std::exchange(transfer.response,{});}
Session session;
Session& native_session(){return session;}
}
namespace mkdd::online {std::string executable_build(){return "fixture";}}
namespace desktop_engine {unsigned requested(){return 0;}}
struct CPU {uint32_t r[32]{};};
std::unordered_map<uint32_t,uint32_t> memory;
uint32_t mem_r32(uint32_t a){return memory[a];}
void mem_w32(uint32_t a,uint32_t v){memory[a]=v;}
unsigned mem_r8(uint32_t a){return mem_r32(a)&255;}
void mem_w8(uint32_t a,unsigned v){mem_w32(a,v);}
bool valid(uint32_t a,unsigned){return a>=0x80000000u;}
float read_float(uint32_t a){return float(mem_r32(a));}
void write_float(uint32_t a,float value){mem_w32(a,uint32_t(value));}
void sound(CPU*,unsigned){}
void classic_release(CPU*){}
void fp(CPU*,int,float){}
void call_indirect(CPU*,uint32_t){}
void fn_80030F4C(CPU*){}
void fn_80028E30(CPU*){}
void fn_801E0450(CPU*){}
void native_hit_pane(uint32_t pane,int action,float);
bool backgrounds(CPU*){return true;}
void text(CPU*,const char*,float,float,float=24,uint32_t=0xffffffff,bool=false){}
void label(CPU*,const std::string&,float,float=24,uint32_t=0xffffffff){}
void racing_text(CPU*,const std::string&,float,float,float,uint32_t,bool,bool){}
struct DrawBox {float x,y,w,h;uint32_t color;};
std::vector<DrawBox> drawn_boxes;
void box(CPU*,float x,float y,float w,float h,uint32_t color){drawn_boxes.push_back({x,y,w,h,color});}
void flat_polygon(CPU*,const float (*)[2],unsigned,uint32_t){}
bool native_selection=false;
std::string initials="P1";
bool save_online_name(const std::string& name,std::string& error){
    if(name.empty() || name.size()>7 || name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")!=std::string::npos){error="INVALID NAME";return false;}
    initials=name;error.clear();return true;
}
uint32_t trial_result_word(CPU*,const std::string&){return 0;}
bool trial_art_picture(CPU*,const std::string&,float,float,float,float,uint32_t=0xffffffff){return false;}
bool trial_art_word(CPU*,const std::string&,float,float,float,unsigned=1,bool=false){return false;}
bool native_pane_visible(uint32_t a){return valid(a,1);}
void native_menu_button(uint32_t b,uint32_t d=0){
    mem_w32(0x803b442c,mem_r32(0x803b442c)|b);
    mem_w32(0x803b4440,mem_r32(0x803b4440)|b|d);
}
bool course_pointer_scene(CPU* c,uint32_t app,uint32_t& scene,unsigned& kind){
    kind=mem_r32(c->r[13]-30528);scene=mem_r32(app+12);return kind==3;
}
void native_set_cup(CPU* c,uint32_t,unsigned cup){mem_w32(c->r[13]-23672,cup);mem_w32(c->r[13]-23668,0);}
std::vector<desktop_pointer::Region> clickable;
void hit(float x,float y,float w,float h,int a){clickable.push_back({x/608,y/448,w/608,h/448,a});}
void native_hit_pane(uint32_t pane,int action,float){
    if(action>=2300 && action<2306)hit(60,60+(action-2300)*50,488,46,action);
}
#include "time_trial_menu.inc"

CPU cpu;
constexpr uint32_t app=0x80100000,scene=0x80200000;
void reset(){
    trial.~TrialMenu();new (&trial) TrialMenu;
    trials::transfer={};trials::session={};memory.clear();
    initials="P1";
    cpu={};cpu.r[13]=0x80300000;cpu.r[2]=0x80310000;
    mem_w32(app+12,scene);mem_w32(cpu.r[13]-30528,1);
    mem_w32(scene+0x20e0,7);mem_w32(scene+0xd9c,0x80400000);
    mem_w32(0x803527f8,0x803b4410);
    desktop_pointer::context(false);desktop_pointer::context(true);
    desktop_pointer::state={true,-1,0,0};
}
void frame(unsigned pressed=0,unsigned repeat=0){
    mem_w32(0x803b442c,pressed);mem_w32(0x803b4440,repeat);
    CHECK(trial_menu_update(&cpu,app));
    drawn_boxes.clear();
    if(!trial_menu_draw(&cpu,app))CHECK(trial.native_flow || (trial.launch_ticks && !trial.online_launch));
}
desktop_pointer::Region region(int action){
    for(auto r:desktop_pointer::regions)if(r.action==action)return r;
    CHECK(false);return {};
}
bool has(int action){for(auto r:desktop_pointer::regions)if(r.action==action)return true;return false;}
void point(int action,bool click){
    const auto r=region(action);
#ifdef TANDEM_MENU_SDL_INPUT_TEST
    int w,h;SDL_GetWindowSize(window,&w,&h);
    const auto width=std::min(float(w),h*desktop_pointer::aspect),height=width/desktop_pointer::aspect;
    const int x=int((w-width)/2+(r.x+r.w/2)*width),y=int((h-height)/2+(r.y+r.h/2)*height);
    SDL_Event e{};e.type=SDL_MOUSEMOTION;e.motion.windowID=SDL_GetWindowID(window);e.motion.x=x;e.motion.y=y;
    desktop_pointer::event(e,false);CHECK(desktop_pointer::read().hover==action);
    if(click){
        e={};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);
        e.button.button=SDL_BUTTON_LEFT;e.button.x=x;e.button.y=y;desktop_pointer::event(e,false);
        CHECK(desktop_pointer::clicked==action);
        e.type=SDL_MOUSEBUTTONUP;desktop_pointer::event(e,false);
    }
#else
    desktop_pointer::state.x=r.x+r.w/2;desktop_pointer::state.y=r.y+r.h/2;
    // Resolve actual geometry in publication order, like SDL (last match wins).
    int found=-1;for(auto area:desktop_pointer::regions)
        if(desktop_pointer::state.x>=area.x && desktop_pointer::state.x<area.x+area.w &&
           desktop_pointer::state.y>=area.y && desktop_pointer::state.y<area.y+area.h)found=area.action;
    CHECK(found==action);desktop_pointer::state.hover=found;
    if(click)desktop_pointer::clicked=found;
#endif
}
void click(int action){point(action,true);frame();}
void board(unsigned count){
    reset();frame();click(2002);CHECK(trial.page==TrialPage::Hub);
    click(2101);CHECK(trial.page==TrialPage::Board);frame();
    CHECK(trials::transfer.request->action==trials::TrialAction::List);
    trials::TrialResponse response;response.course=trial.course;
    for(unsigned i=0;i<count;++i)response.entries.push_back({std::to_string(i),"PLAYER "+std::to_string(i),"hash",90000+i});
    trials::transfer.busy=false;trials::transfer.response=response;frame();
}
trials::Record ghost(unsigned course){
    trials::Record r{};r[0]=7;r[1]=8;r[3]=uint8_t(course);
    auto put=[&](unsigned at,unsigned value){for(unsigned i=0;i<4;++i)r[at+i]=uint8_t(value>>(24-8*i));};
    put(8,90000);put(12,6000);
    for(unsigned i=0;i<trials::laps(course)-1;++i)put(16+4*i,90000*(i+1)/trials::laps(course));
    for(unsigned i=0;i<6001;++i){r[40+2*i]=120;r[41+2*i]=1;}return r;
}
int main(){
#ifdef TANDEM_MENU_SDL_INPUT_TEST
    SDL_SetMainReady();CHECK(SDL_setenv("SDL_VIDEODRIVER","dummy",1)==0);
    CHECK(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)==0);
    window=SDL_CreateWindow("input fixture",0,0,1280,720,SDL_WINDOW_HIDDEN);CHECK(window);
    CHECK(std::string(SDL_GetCurrentVideoDriver())=="dummy");
#endif
    _putenv_s("TANDEM_TRIAL_CONFIG","fixture-only-no-file");
    _putenv_s("MKDD_ROOM_TOKEN","");_putenv_s("TANDEM_COURSE_INDEX","");_putenv_s("GCN_COURSE_OVERLAY","");
    // Only the one-player mode pane is replaced during its entrance animation.
    reset();mem_w32(scene+0x20e0,6);CHECK(trial_menu_draw(&cpu,app));
    mem_w32(scene+0xd9c,0);CHECK(!trial_menu_draw(&cpu,app));
    // All three mode rows, stick/D-pad, A/Start, Back, mouse and stationary hover.
    reset();frame();point(2000,false);frame(0,4);CHECK(trial.mode_row==1);
    frame();CHECK(trial.mode_row==1);frame(0,0x04000000);CHECK(trial.mode_row==2);
    frame(0x1000);CHECK(trial.page==TrialPage::Hub);
    frame(0,4);CHECK(trial.row==1);click(trial_back);CHECK(trial.page==TrialPage::None);
    click(2002);point(2100,false);frame();CHECK(trial.row==0);
    frame(0,4);frame();CHECK(trial.row==1); // idle mouse cannot steal back selection
    point(2100,false);desktop_pointer::state.x+=.001f;frame();CHECK(trial.row==0);
    click(trial_back);click(trial_back);CHECK(mem_r32(0x803b442c)&0x200);
    reset();frame();click(2000);CHECK(trial.launch_target==0 && trial.launch_ticks);
    reset();frame();click(2001);CHECK(trial.launch_target==2 && trial.launch_ticks);
    reset();frame();
    CHECK(std::none_of(clickable.begin(),clickable.end(),[](const auto& r){return r.action==2003 || r.action==2004;}));
    click(2000);frame();CHECK(trial.native_flow);click(2004);CHECK(trial.mode_row==0 && !trial.launch_ticks && (mem_r32(0x803b4440)&0x02000000));
    click(2003);CHECK(!trial.launch_ticks && (mem_r32(0x803b4440)&0x01000000));

    reset();frame();click(2002);frame(0x800);CHECK(trial.name_open);
    desktop_pointer::typed="\b\bracer\r";frame();CHECK(!trial.name_open && initials=="RACER");
    click(trial_edit_name);CHECK(trial.name_open);const auto name_generation=desktop_pointer::generation.load();frame();
    CHECK(desktop_pointer::generation==name_generation); // modal must not republish underlying page
    initials="F1NAME";click(trial_back);CHECK(initials=="F1NAME" && !trial.name_open);
    click(trial_edit_name);desktop_pointer::typed="\b\b\b\b\b\b\r";frame();CHECK(trial.name_open && !trial.name_error.empty());
    click(2600);click(2637);CHECK(initials=="A" && !trial.name_open); // mouse keyboard + Save

    // Every course is reachable with only mouse, including the second page.
    for(unsigned target=0;target<16;++target){
        board(12);click(2201);CHECK(trial.course_open);
        if(target>=8)click(trial_next);
        point(2400+target,false);frame();CHECK(trial.course_row==target);
        click(2400+target);CHECK(trial.course==trial_courses[target] && !trial.course_open);
        frame();CHECK(trials::transfer.request->course==trial_courses[target]);
    }
    board(12);click(2201);frame(0,8);CHECK(trial.course_row==15);
    frame(0,2);CHECK(trial.course_row==7);frame(0x100);CHECK(trial.course==42);
    // Dropdown is modal: opening/closing invalidates stale underlying actions.
    board(12);const auto before=desktop_pointer::generation.load();click(2201);
    CHECK(desktop_pointer::generation>before && !has(2100) && !has(2200));
    desktop_pointer::clicked=2100;frame();CHECK(trial.course_open && !trial.online_launch);
    click(trial_back);CHECK(!trial.course_open && trial.page==TrialPage::Board);
    frame(0x800);CHECK(trial.course_open);frame(0x800);CHECK(!trial.course_open);
    click(2201);click(2201);CHECK(!trial.course_open);

    // Full/partial pages, button-only wrap, empty board and repeated refresh.
    for(unsigned count:{0u,1u,5u,6u,10u,50u}){
        board(count);CHECK(trial.course_focus==(count==0));
        if(count){
            frame(0,8);CHECK(!trial.course_focus && trial.row==count-1);
            frame(0,4);CHECK(!trial.course_focus && trial.row==0);
            frame(0,4);CHECK(!trial.course_focus && trial.row==(1%count));
            frame(0,8);CHECK(!trial.course_focus && trial.row==0);
            while(has(trial_next))click(trial_next);
            CHECK(trial.row/trial_page_size==(count-1)/trial_page_size);
            if(count>trial_page_size){click(trial_previous);CHECK(trial.row/trial_page_size==(count-1)/trial_page_size-1);}
        }else{
            frame(0,4);frame(0x100);CHECK(trial.course_open);click(trial_back);
        }
        const auto requests=trials::transfer.submitted;click(2200);frame(0x400);
        CHECK(trials::transfer.submitted==requests+1);
        click(trial_back);CHECK(trial.page==TrialPage::Hub);
    }
    // Clicking a button downloads that run without a Ready page. Back stays usable
    // while busy; a cancelled worker's reply is discarded by the client contract.
    board(50);click(trial_next);click(trial_race_ghost+7);
    CHECK(trials::transfer.request->action==trials::TrialAction::Download && trials::transfer.request->entry.id=="7");
    const auto requests=trials::transfer.submitted;frame(0x100);CHECK(trials::transfer.submitted==requests);
    trials::TrialResponse reply;reply.action=trials::TrialAction::Download;reply.course=36;reply.record=ghost(36);
    trials::transfer.response=reply;trials::transfer.busy=false;frame();
    CHECK(trial.online_launch && trial.route.active());
    click(trial_back);CHECK(!trial.online_launch && !trial.launch_ticks);
    // Name/time areas are inert; only the button responds and gets highlighted.
    board(5);CHECK(!has(2100) && !has(2104));
    const auto previous_requests=trials::transfer.submitted;
    desktop_pointer::clicked=2100;frame();CHECK(trials::transfer.submitted==previous_requests);
    auto highlighted=[](){unsigned count=0;for(const auto& b:drawn_boxes){
        if(b.x==30 && b.w==548 && b.h==38)CHECK(b.color==0x062e60ee);
        if(b.x==430 && b.w==140 && b.color==0xffffffff)++count;
    }return count;};
    point(trial_race_ghost+1,false);frame();CHECK(highlighted()==1 && trial.row==1);
    desktop_pointer::state.hover=-1;desktop_pointer::state.x=.2f;frame();CHECK(highlighted()==0);
    frame(0,4);CHECK(highlighted()==1 && trial.row==2);
    frame();CHECK(highlighted()==1 && trial.row==2); // idle mouse cannot steal focus
    point(trial_race_ghost+4,false);frame();CHECK(highlighted()==1 && trial.row==4);
    // Every visible Race Ghost button targets its own entry, on every page.
    for(unsigned target=0;target<50;++target){
        board(50);while(trial.row/trial_page_size<target/trial_page_size)click(trial_next);
        CHECK(has(trial_race_ghost+target));
        point(trial_race_ghost+target,false);frame();
        CHECK(trial.row==target && !trial.course_focus);
        click(trial_race_ghost+target);
        CHECK(trials::transfer.request->entry.id==std::to_string(target));
    }
    board(6);frame(0x100);
    CHECK(trials::transfer.request->action==trials::TrialAction::Download && trials::transfer.request->entry.id=="0");
    board(10);click(trial_race_ghost);click(trial_back);CHECK(trial.page==TrialPage::Hub && !trial.online_launch);
    click(2100);CHECK(trial.online_launch); // cancelled HTTP cannot block Choose Course
    // A refresh cannot reinterpret an old mouse click as another player's run.
    board(10);click(2200);point(trial_race_ghost,true);
    trials::TrialResponse changed;changed.course=36;changed.entries.push_back({"new","NEW PLAYER","hash",89000});
    trials::transfer.busy=false;trials::transfer.response=changed;frame();
    CHECK(!trial.online_launch && trials::transfer.request->action==trials::TrialAction::List && !trial.course_focus);
    // Rapid course changes wait for the cancelled worker, then request latest.
    board(10);click(2200);click(2201);click(trial_next);click(2415);
    CHECK(trial.list_pending);click(2201);click(trial_previous);click(2401);
    const auto before_finish=trials::transfer.submitted;frame();CHECK(trials::transfer.submitted==before_finish);
    trials::transfer.busy=false;frame();CHECK(trials::transfer.request->course==34);
    // Course-specific late responses cannot launch the wrong race.
    trials::transfer.response=reply;frame();CHECK(!trial.online_launch);

    // Character-to-race automatic route accepts mouse Back as well as B.
    board(1);std::string error;CHECK(trials::session.choose(36,error));
    CHECK(trials::session.ghost(ghost(36),error));CHECK(trial.route.begin(36));
    mem_w32(cpu.r[13]-30528,3);trial_course_hint(&cpu,app);
    point(trial_back,true);mem_w32(0x803b442c,0);trial_course_guard(&cpu,app);
    CHECK(!trial.route.active() && (mem_r32(0x803b442c)&0x200));

    // Completed records wait for the persistent retail retry menu; never freeze
    // RaceApp early, auto-dismiss, or trigger native Save Ghost by mistake.
    reset();CHECK(trial_enter());CHECK(trials::session.choose(36,error));
    CHECK(trials::session.begin(36,true));CHECK(trials::session.finish(ghost(36),error));
    const uint32_t manager=0x80500000,result=0x80600000;cpu.r[3]=manager;
    mem_w32(manager+0x20,result);
    CHECK(!trial_result_update(&cpu));CHECK(!trial.result_open);
    mem_w8(cpu.r[13]-23800,1);mem_w32(cpu.r[13]-23792,2);mem_w32(result+0x198c,30);
    CHECK(trial_result_update(&cpu));CHECK(trial_result_draw(&cpu));
    for(unsigned n=0;n<1200;++n){CHECK(trial_result_update(&cpu));CHECK(trial.result_open);}
    point(2301,true);CHECK(trial_result_update(&cpu));CHECK(trial.name_open);CHECK(trial_result_draw(&cpu));
    desktop_pointer::typed="\b\bmyname\r";CHECK(trial_result_update(&cpu));CHECK(!trial.name_open && initials=="MYNAME");
    CHECK(trial_result_draw(&cpu));
    initials="LATEST";
    point(2304,true);CHECK(trial_result_update(&cpu));CHECK(trials::transfer.request->action==trials::TrialAction::Upload);
    CHECK(trials::transfer.request->display_name=="LATEST");CHECK(!(mem_r32(0x803b442c)&0x100));
    const auto uploads=trials::transfer.submitted;point(2304,true);CHECK(trial_result_update(&cpu));CHECK(trials::transfer.submitted==uploads);
    point(2305,true);CHECK(trial_result_update(&cpu));CHECK(trial.result_open); // busy upload is not abandoned
    trials::transfer.busy=false;
    trials::TrialResponse uploaded;uploaded.action=trials::TrialAction::Upload;
    trials::transfer.response=uploaded;CHECK(trial_result_update(&cpu));CHECK(trial.result_sent);
    point(2305,true);CHECK(trial_result_update(&cpu));CHECK(!trial.result_open && trial.result_exiting);
    CHECK(mem_r32(cpu.r[13]-23796)==8 && (mem_r32(0x803b442c)&0x100)); // retail Quit
    mem_w32(0x803b442c,0);CHECK(!trial_result_update(&cpu)); // cannot reopen during the native exit
#ifdef TANDEM_MENU_SDL_INPUT_TEST
    // Production SDL input relinquishes hover on button/stick activity. Stick
    // noise below the threshold cannot steal focus from the mouse.
    reset();frame();point(2002,false);SDL_Event e{};
    e.type=SDL_CONTROLLERAXISMOTION;e.caxis.value=4000;desktop_pointer::event(e,false);
    CHECK(desktop_pointer::read().hover==2002);
    e.caxis.value=24000;desktop_pointer::event(e,false);CHECK(desktop_pointer::read().hover==-1);
    point(2001,false);e={};e.type=SDL_CONTROLLERBUTTONDOWN;e.cbutton.button=SDL_CONTROLLER_BUTTON_A;
    desktop_pointer::event(e,false);CHECK(desktop_pointer::read().hover==-1);
    // Letterbox bars must not activate any row, and F1's blocked input cannot
    // click through into this menu. No OS mouse movement occurs in these tests.
    e={};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);e.button.button=SDL_BUTTON_LEFT;
    e.button.x=5;e.button.y=300;desktop_pointer::event(e,false);CHECK(!desktop_pointer::take());
    e.type=SDL_MOUSEBUTTONUP;desktop_pointer::event(e,false);
    e.type=SDL_MOUSEBUTTONDOWN;e.button.x=600;e.button.y=550;desktop_pointer::event(e,true);CHECK(!desktop_pointer::take());
    e.type=SDL_MOUSEBUTTONUP;desktop_pointer::event(e,false);
    // Same regions at ordinary 4:3 and resized widescreen sizes.
    for(auto size:{std::pair{640,480},std::pair{1920,1080}}){
        SDL_SetWindowSize(window,size.first,size.second);point(2002,false);CHECK(desktop_pointer::read().hover==2002);
    }
    desktop_pointer::shutdown();SDL_DestroyWindow(window);SDL_Quit();
    std::puts("PASS: real SDL events under dummy video, controller buttons/stick threshold, mouse viewport mapping, letterbox and blocked click-through");
#endif
    std::puts("PASS: production menu input + hit regions, all16 courses, 0/1/5/6/10/50 rows, mouse/controller switching, modal cancel, busy requests, ghost launch and upload/continue");
}
