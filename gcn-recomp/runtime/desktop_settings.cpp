#include "desktop_settings.h"
#include "desktop_controls.h"
#include "desktop_gamepad.h"
#include "desktop_display.h"
#include "desktop_sound.h"
#include "desktop_online_ui.h"
#include "desktop_pointer.h"
#include "display_config.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <SDL.h>
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#ifndef GCN_DEFAULT_AUDIO_VOLUME
#define GCN_DEFAULT_AUDIO_VOLUME 100
#endif

namespace {
using namespace desktop_controls;
SDL_Window* window;
bool ready=false,opened=false,dirty=false,select_mapping=false;
bool select_display=false,close_after_keep=false;
desktop_display::Settings display_draft;
int selected_port=0,listen_action=-1,listen_slot=0;
Configuration before;
desktop_sound::Settings sound_before;
std::string status;
uint64_t chord_start=0;bool chord_latched=false;

const char* device_label(const Port& p) {
    switch(p.device) {
    case Device::Disabled:return "Not connected";
    case Device::Automatic:return "Automatic controller";
    case Device::KeyboardMouse:return "Keyboard & mouse";
    case Device::Controller:return p.controller_name.c_str();
    }return "Unknown";
}
void close(bool discard=false) {
    if(desktop_display::pending()) {
        std::string error;desktop_display::revert(error);display_draft=desktop_display::current();
        if(!error.empty()){status=error;return;}
    }
    if(!discard && display_draft!=desktop_display::current()) {
        if(desktop_display::preview(display_draft,SDL_GetTicks64(),status))close_after_keep=true;
        select_display=true;return;
    }
    if(discard){configuration()=before;desktop_sound::preview(sound_before);}
    else if(!save(status) || !desktop_sound::save(status))return;
    opened=false;dirty=false;listen_action=-1;cancel_capture();block(false);
    SDL_ShowCursor(SDL_ENABLE);desktop_gamepad_clear_quit();
    fprintf(stderr,"[settings] closed\n");
}
void changed(){dirty=true;status.clear();}
void online_tab() {
    using namespace desktop_online_ui;
    const auto state=read();
    if(!state.available){ImGui::TextDisabled("Online controls are available from the main menu.");return;}
    ImGui::TextDisabled("%s",state.version.c_str());
    if(!state.code.empty())ImGui::Text("Room %s",state.code.c_str());
    if(state.can_refresh) {
        ImGui::SameLine();if(ImGui::Button("Refresh code"))ImGui::OpenPopup("Refresh room code?");
        if(ImGui::BeginPopupModal("Refresh room code?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Keep everyone connected and replace the invitation code? Share the new code with anyone joining later.");
            if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();
            ImGui::SameLine();if(ImGui::Button("Refresh")){submit({desktop_online_ui::Action::Refresh,{}});ImGui::CloseCurrentPopup();}
            ImGui::EndPopup();
        }
    }
    if(!state.status.empty())ImGui::TextWrapped("%s",state.status.c_str());
    static char name[8]{},code[9]{};
    static std::string last_name;
    if(last_name!=state.name){snprintf(name,sizeof(name),"%s",state.name.c_str());last_name=state.name;}
    ImGui::SeparatorText("Your name");
    ImGui::BeginDisabled(!state.can_edit);
    ImGui::SetNextItemWidth(240);
    ImGui::InputText("##Online name",name,sizeof(name),ImGuiInputTextFlags_CharsUppercase|ImGuiInputTextFlags_CharsNoBlank);
    ImGui::SameLine();if(ImGui::Button("Update name"))submit({desktop_online_ui::Action::Name,name});
    ImGui::EndDisabled();
    ImGui::TextDisabled("Up to 7 letters or numbers.");
    ImGui::SeparatorText("Quick join");
    ImGui::BeginDisabled(!state.can_join);
    ImGui::SetNextItemWidth(240);
    ImGui::InputText("Room code",code,sizeof(code),ImGuiInputTextFlags_CharsUppercase|ImGuiInputTextFlags_CallbackCharFilter,
        [](ImGuiInputTextCallbackData* data)->int{return data->EventChar>127 || !strchr("23456789ABCDEFGHJKLMNPQRSTUVWXYZ",(char)data->EventChar);});
    ImGui::BeginDisabled(strlen(code)!=8);
    if(ImGui::Button("Join room"))submit({desktop_online_ui::Action::Join,code});
    ImGui::EndDisabled();ImGui::EndDisabled();
    if(!state.can_join)ImGui::TextDisabled("Leave your current room or return to the main menu before joining another.");
    static Member kick;
    if(!state.members.empty()) {
        ImGui::SeparatorText("Players");
        for(const auto& member:state.members) {
            ImGui::PushID(int(member.seat));ImGui::Text("%s%s",member.name.c_str(),member.you?" (you)":member.host?" (host)":"");
            if(state.can_kick && !member.you && !member.host) {
                ImGui::SameLine();if(ImGui::Button("Kick")){kick=member;ImGui::OpenPopup("Confirm removal");}
                if(ImGui::BeginPopupModal("Confirm removal",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Remove %s from this room?",kick.name.c_str());
                    if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();
                    ImGui::SameLine();if(ImGui::Button("Remove player")){submit({desktop_online_ui::Action::Kick,kick.id,kick.seat});ImGui::CloseCurrentPopup();}
                    ImGui::EndPopup();
                }
            }
            ImGui::PopID();
        }
    }
    if(state.can_return) {
        if(ImGui::Button("Return to lobby"))ImGui::OpenPopup("Return to lobby?");
        if(ImGui::BeginPopupModal("Return to lobby?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("End this event and return everyone to the same room?");
            if(ImGui::Button("Keep playing"))ImGui::CloseCurrentPopup();
            ImGui::SameLine();if(ImGui::Button("Return everyone")){submit({desktop_online_ui::Action::Return,{}});ImGui::CloseCurrentPopup();}
            ImGui::EndPopup();
        }
    }
}
void choose(int p,Device type,const std::string& id={},const std::string& name={}){if(assign(p,type,id,name))changed();}
void device_combo(int p) {
    auto& port=configuration()[p];ImGui::SetNextItemWidth(-1);
    if(ImGui::BeginCombo("##Device",device_label(port))) {
        if(ImGui::Selectable("Not connected",port.device==Device::Disabled))choose(p,Device::Disabled);
        if(ImGui::Selectable("Automatic controller",port.device==Device::Automatic))choose(p,Device::Automatic);
        if(ImGui::Selectable("Keyboard & mouse",port.device==Device::KeyboardMouse))choose(p,Device::KeyboardMouse);
        for(const auto& d:devices())if(ImGui::Selectable(d.name.c_str(),port.device==Device::Controller && port.controller_id==d.id))choose(p,Device::Controller,d.id,d.name);
        ImGui::EndCombo();
    }
}
void preview_pad(int p) {
    const auto pad=preview()[p];
    if(!pad.connected){ImGui::TextDisabled("Device is disconnected");return;}
    ImGui::Text("Stick %4d, %4d    C-stick %4d, %4d",(int)pad.stick_x-128,(int)pad.stick_y-128,(int)pad.cstick_x-128,(int)pad.cstick_y-128);
    ImGui::Text("L %3d   R %3d   Buttons %04X",pad.trig_l,pad.trig_r,pad.buttons);
}
void ports_tab() {
    ImGui::TextWrapped("Assign one input device to each GameCube port. Use the game's original multiplayer menus for local play.");
    ImGui::Spacing();
    for(int p=0;p<4;++p) {
        ImGui::PushID(p);
        ImGui::SeparatorText(("GameCube port "+std::to_string(p+1)).c_str());
        ImGui::SetNextItemWidth(-160);
        ImGui::BeginGroup();device_combo(p);ImGui::EndGroup();
        if(ImGui::Button("Map controls",ImVec2(135,0))){selected_port=p;select_mapping=true;}
        ImGui::SameLine();auto& port=configuration()[p];
        if(port.device==Device::Automatic || port.device==Device::Controller) {
            bool keyboard=port.keyboard;
            if(ImGui::Checkbox("Also use keyboard & mouse",&keyboard)) {
                if(keyboard)for(auto& other:configuration()){other.keyboard=false;if(other.device==Device::KeyboardMouse)other.device=Device::Disabled;}
                port.keyboard=keyboard;changed();
            }
        }
        preview_pad(p);ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("A device assigned to another port is moved, so it cannot control two players.");
    ImGui::TextDisabled("Online Play uses port 1. Ports 2-4 are for local multiplayer.");
}
void bind_button(int action,int slot) {
    auto& binding=configuration()[selected_port].bindings[action][slot];
    ImGui::PushID(action*3+slot);
    if(ImGui::Button(binding_name(binding).c_str(),ImVec2(-1,0))) {
        listen_action=action;listen_slot=slot;begin_capture(selected_port,action,slot,SDL_GetTicks64());
        ImGui::OpenPopup("Assign input");
    }
    if(ImGui::BeginPopupContextItem("Binding actions")) {
        if(ImGui::MenuItem("Clear binding")){binding={};changed();}
        ImGui::EndPopup();
    }
    if(ImGui::BeginPopupModal("Assign input",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Port %d - %s",selected_port+1,action_name(action));
        if(!capture_ready(SDL_GetTicks64()) && capturing())ImGui::TextWrapped("Release the current buttons and center the sticks first.");
        else if(slot==0)ImGui::TextWrapped("Press a button or move an axis on this port's selected controller.");
        else ImGui::TextWrapped("Press a keyboard key, scroll the wheel, or choose a mouse input below.");
        ImGui::TextDisabled("Esc cancels. F1, F10 and F11 remain desktop shortcuts.");
        if(slot>0) {
            const struct {const char* name;Binding binding;} mouse[]={
                {"Mouse Left",{Source::MouseButton,1,1}},{"Mouse Right",{Source::MouseButton,3,1}},
                {"Mouse Middle",{Source::MouseButton,2,1}},{"Mouse X1",{Source::MouseButton,4,1}},{"Mouse X2",{Source::MouseButton,5,1}},
                {"Wheel Up",{Source::MouseWheel,0,1}},{"Wheel Down",{Source::MouseWheel,0,-1}},
                {"Move Left",{Source::MouseMotion,0,-1}},{"Move Right",{Source::MouseMotion,0,1}},
                {"Move Up",{Source::MouseMotion,1,-1}},{"Move Down",{Source::MouseMotion,1,1}}};
            for(int i=0;i<11;++i) {
                if(i%3)ImGui::SameLine();
                if(ImGui::Button(mouse[i].name,ImVec2(125,0))){binding=mouse[i].binding;cancel_capture();changed();}
            }
        }
        if(ImGui::Button("Clear",ImVec2(90,0))){binding={};cancel_capture();changed();}
        ImGui::SameLine();if(ImGui::Button("Cancel",ImVec2(90,0))){cancel_capture();listen_action=-1;}
        if(!capturing()) {
            if(listen_action>=0)changed();
            listen_action=-1;ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}
void mapping_tab() {
    for(int p=0;p<4;++p) {
        if(p)ImGui::SameLine();
        char name[24];snprintf(name,sizeof(name),"Port %d",p+1);
        if(ImGui::Selectable(name,p==selected_port,0,ImVec2(95,0)))selected_port=p;
    }
    auto& port=configuration()[selected_port];
    ImGui::Text("Input: %s",device_label(port));
    ImGui::TextDisabled("Select any binding to change it. Right-click a binding to clear it.");
    ImGui::TextDisabled("Controller buttons: a/b/x/y follow printed labels on Xbox and Switch.");
    ImGui::TextDisabled("PlayStation: a=Cross, b=Circle, x=Square, y=Triangle. rightshoulder=RB/R1/R.");
    ImGui::TextDisabled("Mouse movement captures the cursor during play. F1 releases it for settings.");
    ImGui::Spacing();
    const float footer=ImGui::GetFrameHeightWithSpacing()*4+12;
    if(ImGui::BeginTable("Bindings",4,ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY|ImGuiTableFlags_SizingStretchProp,ImVec2(0,-footer))) {
        ImGui::TableSetupColumn("GameCube",ImGuiTableColumnFlags_WidthFixed,125);
        ImGui::TableSetupColumn("Controller");ImGui::TableSetupColumn("Keyboard / mouse");ImGui::TableSetupColumn("Alternative");
        ImGui::TableSetupScrollFreeze(0,1);ImGui::TableHeadersRow();
        for(int a=0;a<ActionCount;++a) {
            ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(action_name(a));
            for(int s=0;s<3;++s){ImGui::TableSetColumnIndex(s+1);bind_button(a,s);}
        }
        ImGui::EndTable();
    }
    ImGui::SetNextItemWidth(180);if(ImGui::SliderFloat("Stick dead zone",&port.deadzone,.02f,.5f,"%.2f"))changed();
    ImGui::SameLine();ImGui::SetNextItemWidth(180);if(ImGui::SliderFloat("Mouse sensitivity",&port.mouse_sensitivity,.001f,.2f,"%.3f",ImGuiSliderFlags_Logarithmic))changed();
    preview_pad(selected_port);
    if(ImGui::Button("Reset this port's bindings")){port.bindings=defaults()[selected_port].bindings;port.deadzone=.12f;port.mouse_sensitivity=.025f;changed();}
}
void display_tab() {
    ImGui::TextWrapped("Choose the monitor, window mode and resolution for this PC.");
    ImGui::Spacing();ImGui::SetNextItemWidth(420);
    const auto monitors=desktop_display::monitors();
    std::string monitor_label="Unavailable monitor";
    for(const auto& m:monitors)if(m.index==display_draft.monitor)monitor_label=std::to_string(m.index+1)+": "+m.name;
    if(ImGui::BeginCombo("Monitor",monitor_label.c_str())) {
        for(const auto& m:monitors) {
            const auto label=std::to_string(m.index+1)+": "+m.name;
            if(ImGui::Selectable(label.c_str(),m.index==display_draft.monitor)) {
                display_draft.monitor=m.index;
                if(display_draft.mode==desktop_display::Mode::Fullscreen){display_draft.width=m.width;display_draft.height=m.height;}
                status.clear();
            }
        }ImGui::EndCombo();
    }
    const char* const modes[]={"Windowed","Fullscreen","Borderless fullscreen"};
    int mode=(int)display_draft.mode;ImGui::SetNextItemWidth(420);
    if(ImGui::Combo("Window mode",&mode,modes,3)) {
        display_draft.mode=(desktop_display::Mode)mode;status.clear();
        if(display_draft.mode==desktop_display::Mode::Fullscreen) {
            const auto supported=desktop_display::resolutions(display_draft.mode,display_draft.monitor);
            if(std::find(supported.begin(),supported.end(),desktop_display::Resolution{display_draft.width,display_draft.height})==supported.end() && !supported.empty()) {
                display_draft.width=supported.back().width;display_draft.height=supported.back().height;
            }
        }
    }
    if(display_draft.mode==desktop_display::Mode::Borderless) {
        std::string desktop="Desktop resolution";for(const auto& m:monitors)if(m.index==display_draft.monitor)desktop=std::to_string(m.width)+" x "+std::to_string(m.height)+" (desktop)";
        ImGui::BeginDisabled();ImGui::SetNextItemWidth(420);if(ImGui::BeginCombo("Resolution",desktop.c_str()))ImGui::EndCombo();ImGui::EndDisabled();
        ImGui::TextWrapped("Borderless uses your desktop resolution. Choose Windowed or Fullscreen to select a different resolution.");
    }else {
        const auto label=std::to_string(display_draft.width)+" x "+std::to_string(display_draft.height);
        ImGui::SetNextItemWidth(420);
        if(ImGui::BeginCombo("Resolution",label.c_str())) {
            for(const auto& r:desktop_display::resolutions(display_draft.mode,display_draft.monitor)) {
                const auto text=std::to_string(r.width)+" x "+std::to_string(r.height);
                if(ImGui::Selectable(text.c_str(),r.width==display_draft.width && r.height==display_draft.height)) {display_draft.width=r.width;display_draft.height=r.height;status.clear();}
            }ImGui::EndCombo();
        }
        ImGui::TextDisabled(display_draft.mode==desktop_display::Mode::Fullscreen?"Fullscreen switches this monitor to the selected resolution.":"Resolution sets the size of the game window.");
    }
    ImGui::Spacing();ImGui::BeginDisabled(display_draft==desktop_display::current() || desktop_display::pending());
    if(ImGui::Button("Apply display settings",ImVec2(240,0))) {close_after_keep=false;desktop_display::preview(display_draft,SDL_GetTicks64(),status);}
    ImGui::EndDisabled();ImGui::SameLine();
    if(ImGui::Button("Reset selection")){display_draft=desktop_display::current();status.clear();}
    ImGui::Spacing();ImGui::TextDisabled("Keep the new display mode within 15 seconds, or it will revert automatically.");
    ImGui::TextDisabled("The game keeps its proportions; extra screen space is letterboxed when needed.");
    ImGui::TextDisabled("F11 switches between windowed and borderless fullscreen.");
}
void sound_tab() {
    ImGui::TextWrapped("Adjust all game audio for this PC.");ImGui::Spacing();
    auto sound=desktop_sound::current();ImGui::SetNextItemWidth(420);
    if(ImGui::SliderInt("Master volume",&sound.volume,0,100,"%d%%")){desktop_sound::preview(sound);changed();}
    if(ImGui::Checkbox("Mute",&sound.muted)){desktop_sound::preview(sound);changed();}
    ImGui::TextDisabled("Changes can be heard immediately and are saved when this menu closes.");
    ImGui::TextDisabled("Discard changes restores the volume from when you opened Settings.");
}
}

bool desktop_settings_init(SDL_Window* win,void* context) {
    window=win;IMGUI_CHECKVERSION();ImGui::CreateContext();
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;
    // Font data is embedded in Dear ImGui; the default avoids any host font dependency.
    ImGui::StyleColorsDark();auto& style=ImGui::GetStyle();style.FontSizeBase=17.f;
    style.WindowRounding=10;style.FrameRounding=5;style.GrabRounding=5;
    style.WindowPadding=ImVec2(20,16);style.FramePadding=ImVec2(9,5);style.ItemSpacing=ImVec2(10,8);
    style.Colors[ImGuiCol_WindowBg]=ImVec4(.055f,.07f,.105f,.98f);
    style.Colors[ImGuiCol_TitleBgActive]=ImVec4(.10f,.18f,.29f,1);
    style.Colors[ImGuiCol_Button]=ImVec4(.12f,.23f,.36f,1);
    style.Colors[ImGuiCol_ButtonHovered]=ImVec4(.18f,.39f,.58f,1);
    style.Colors[ImGuiCol_CheckMark]=ImVec4(.45f,.85f,.94f,1);
    if(!ImGui_ImplSDL2_InitForOpenGL(win,context)){ImGui::DestroyContext();return false;}
    if(!ImGui_ImplOpenGL3_Init("#version 330 core")){ImGui_ImplSDL2_Shutdown();ImGui::DestroyContext();return false;}
    ready=true;
    const char* file=SDL_getenv("GCN_INPUT_CONFIG");
    if(!load(file?file:"saves/controllers.cfg",status))fprintf(stderr,"[input-settings] %s\n",status.c_str());
    const char* display_file=SDL_getenv("GCN_DISPLAY_CONFIG");std::string error;
    const auto display_path=display_file?std::filesystem::path(display_file):std::filesystem::path(path()).parent_path()/"display.cfg";
    if(!desktop_display::init(win,display_path.string(),error)) {status=error;fprintf(stderr,"[display-settings] %s\n",error.c_str());}
    display_draft=desktop_display::current();
    const char* sound_file=SDL_getenv("GCN_SOUND_CONFIG");
    const auto sound_path=sound_file?std::filesystem::path(sound_file):std::filesystem::path(path()).parent_path()/"sound.cfg";
    if(!desktop_sound::load(sound_path.string(),GCN_DEFAULT_AUDIO_VOLUME,error)){status=error;fprintf(stderr,"[sound-settings] %s\n",error.c_str());}
    return true;
}
bool desktop_settings_open(){return opened;}
void desktop_settings_show() {
    if(!ready || opened)return;
    before=configuration();sound_before=desktop_sound::current();display_draft=desktop_display::current();close_after_keep=false;dirty=false;opened=true;block(true);SDL_SetRelativeMouseMode(SDL_FALSE);SDL_ShowCursor(SDL_ENABLE);
    ImGui::GetIO().ClearInputKeys();
    fprintf(stderr,"[settings] opened\n");
}
void desktop_settings_toggle_fullscreen() {
    if(!ready)return;
    desktop_settings_show();select_display=true;
    auto next=desktop_display::current();next.mode=next.mode==desktop_display::Mode::Windowed?desktop_display::Mode::Borderless:desktop_display::Mode::Windowed;
    if(desktop_display::preview(next,SDL_GetTicks64(),status))display_draft=next;
}
bool desktop_settings_event(const SDL_Event& e) {
    if(!ready)return false;
    if(e.type==SDL_KEYDOWN && !e.key.repeat && e.key.keysym.scancode==SDL_SCANCODE_F1) {
        if(opened)close();else desktop_settings_show();return true;
    }
    if(opened && e.type==SDL_KEYDOWN && !e.key.repeat && e.key.keysym.scancode==SDL_SCANCODE_F11) {desktop_settings_toggle_fullscreen();return true;}
    if(opened) {
        if(e.type==SDL_KEYDOWN && !e.key.repeat && e.key.keysym.scancode==SDL_SCANCODE_ESCAPE) {
            if(capturing()){cancel_capture();listen_action=-1;}else close();return true;
        }
        // Binding presses belong only to the capture dialog, not its navigation.
        if(!capturing() || e.type==SDL_MOUSEBUTTONDOWN || e.type==SDL_MOUSEBUTTONUP || e.type==SDL_MOUSEMOTION)
            ImGui_ImplSDL2_ProcessEvent(&e);
        return e.type==SDL_KEYDOWN || e.type==SDL_KEYUP || e.type==SDL_TEXTINPUT || e.type==SDL_TEXTEDITING ||
            e.type==SDL_MOUSEBUTTONDOWN || e.type==SDL_MOUSEBUTTONUP || e.type==SDL_MOUSEMOTION || e.type==SDL_MOUSEWHEEL ||
            e.type==SDL_CONTROLLERBUTTONDOWN || e.type==SDL_CONTROLLERBUTTONUP || e.type==SDL_CONTROLLERAXISMOTION;
    }return false;
}
void desktop_settings_update() {
    if(desktop_display::tick(SDL_GetTicks64(),status)){display_draft=desktop_display::current();close_after_keep=false;}
    bool held=false;
    for(int i=0;i<desktop_gamepad_count();++i){auto* pad=desktop_gamepad_at(i);
        held|=SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_BACK) && SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_LEFTSTICK);}
    const auto now=SDL_GetTicks64();
    if(!held){chord_start=0;chord_latched=false;}
    else if(!chord_start)chord_start=now;
    else if(!chord_latched && now-chord_start>=600){chord_latched=true;if(opened)close();else desktop_settings_show();}
    if(opened)desktop_gamepad_clear_quit();
    const bool relative=wants_relative_mouse();
    if((SDL_GetRelativeMouseMode()==SDL_TRUE)!=relative)SDL_SetRelativeMouseMode(relative?SDL_TRUE:SDL_FALSE);
    desktop_pointer::update(window,opened);
}
void desktop_settings_draw() {
    if(!ready || (!opened && !display_settings_hint()))return;
    // The backend's manual mode borrows the existing handles and opens no duplicates.
    auto* pad=desktop_gamepad_active();ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_Manual,pad?&pad:nullptr,pad?1:0);
    ImGui_ImplOpenGL3_NewFrame();ImGui_ImplSDL2_NewFrame();ImGui::NewFrame();
    if(!opened) {
        const float pulse=.78f+.22f*std::sin(float(SDL_GetTicks64()%4000)*(6.2831853f/4000));
        ImGui::SetNextWindowPos(ImVec2(12,12));ImGui::SetNextWindowBgAlpha(.78f*pulse);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(8,5));
        ImGui::Begin("Settings shortcut",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoNav);
        ImGui::TextColored(ImVec4(1.f,.91f,.27f,pulse),"F1  Settings");ImGui::End();ImGui::PopStyleVar();
        ImGui::Render();ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());return;
    }
    const auto display=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(std::min(960.f,display.x-24),std::min(820.f,display.y-24)),ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(display.x*.5f,display.y*.5f),ImGuiCond_Always,ImVec2(.5f,.5f));
    bool visible=true;
    if(ImGui::Begin("Settings - F1",&visible,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoResize)) {
        ImGui::TextDisabled("Game continues while settings are open. Pause offline play first.");
        const float footer=ImGui::GetFrameHeightWithSpacing()*2+12;
        ImGui::BeginChild("Settings content",ImVec2(0,-footer));
        if(ImGui::BeginTabBar("Settings pages")) {
            if(ImGui::BeginTabItem("Controllers & ports")){ports_tab();ImGui::EndTabItem();}
            const auto flag=select_mapping?ImGuiTabItemFlags_SetSelected:0;select_mapping=false;
            if(ImGui::BeginTabItem("Control mapping",nullptr,flag)){mapping_tab();ImGui::EndTabItem();}
            const auto display_flag=select_display?ImGuiTabItemFlags_SetSelected:0;select_display=false;
            if(ImGui::BeginTabItem("Display",nullptr,display_flag)){display_tab();ImGui::EndTabItem();}
            if(ImGui::BeginTabItem("Sound")){sound_tab();ImGui::EndTabItem();}
            if(ImGui::BeginTabItem("Online")){online_tab();ImGui::EndTabItem();}
            ImGui::EndTabBar();
        }
        ImGui::EndChild();ImGui::Separator();
        if(ImGui::Button("Save & close",ImVec2(150,0)))close();
        ImGui::SameLine();if(ImGui::Button("Discard changes",ImVec2(160,0)))close(true);
        ImGui::SameLine();if(ImGui::Button("Exit to desktop"))ImGui::OpenPopup("Exit to desktop?");
        ImGui::TextDisabled("F1 or Esc closes settings   |   Select + left-stick click opens");
        if(ImGui::BeginPopupModal("Exit to desktop?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Close Project Tandem?");
            if(ImGui::Button("Keep playing",ImVec2(140,0)))ImGui::CloseCurrentPopup();
            ImGui::SameLine();if(ImGui::Button("Exit",ImVec2(100,0))){SDL_Event quit{};quit.type=SDL_QUIT;SDL_PushEvent(&quit);}
            ImGui::EndPopup();
        }
        if(!status.empty())ImGui::TextWrapped("%s",status.c_str());
        else ImGui::TextDisabled("%s",dirty || display_draft!=desktop_display::current()?"Changes will be saved when this menu closes.":"Settings are saved on this PC beside your game saves.");
    }
    ImGui::End();if(!visible)close();
    if(desktop_display::pending())ImGui::OpenPopup("Keep display settings?");
    if(ImGui::BeginPopupModal("Keep display settings?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Reverting in %u seconds.",desktop_display::seconds_left(SDL_GetTicks64()));
        if(ImGui::Button("Keep changes",ImVec2(150,0))) {
            if(desktop_display::keep(status)) {ImGui::CloseCurrentPopup();if(close_after_keep){close_after_keep=false;close();}}
        }
        ImGui::SameLine();if(ImGui::Button("Revert",ImVec2(110,0))) {desktop_display::revert(status);display_draft=desktop_display::current();close_after_keep=false;ImGui::CloseCurrentPopup();}
        if(!desktop_display::pending())ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::Render();ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
void desktop_settings_shutdown() {
    if(!ready)return;
    desktop_pointer::shutdown();
    desktop_display::shutdown();
    if(opened && dirty){std::string error;if(!save(error))fprintf(stderr,"[input-settings] %s\n",error.c_str());if(!desktop_sound::save(error))fprintf(stderr,"[sound-settings] %s\n",error.c_str());}
    desktop_controls::shutdown();ImGui_ImplOpenGL3_Shutdown();ImGui_ImplSDL2_Shutdown();ImGui::DestroyContext();ready=false;
}
