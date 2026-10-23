#include "desktop_sound.h"
#include "audio_gain.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdlib>
static int checks;
static void require(bool ok,const char* why){++checks;if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
int main(int argc,char** argv) {
    require(argc==2,"fresh directory required");const std::filesystem::path root(argv[1]);require(!std::filesystem::exists(root),"fresh directory");std::filesystem::create_directories(root);
    const auto file=(root/"sound.cfg").string();std::string error;
    require(desktop_sound::load(file,25,error) && desktop_sound::current().volume==25 && audio_gain::get()==25,"first launch uses supplied quiet default");
    std::array<int16_t,8> samples{32767,-32768,10000,-10000,4,-4,0,1};
    audio_gain::apply(samples.data(),4);require(samples==std::array<int16_t,8>{8191,-8192,2500,-2500,1,-1,0,0},"quarter gain for both channels including negative and full-scale samples");
    desktop_sound::preview({80,true});samples.fill(32000);audio_gain::apply(samples.data(),4);require(samples==std::array<int16_t,8>{},"muting silences the complete output");
    require(!std::filesystem::exists(file),"preview does not save");require(desktop_sound::save(error),"save sound settings");
    require(desktop_sound::load(file,25,error) && desktop_sound::current()==desktop_sound::Settings{80,true} && audio_gain::get()==0,"volume and mute persist independently");
    desktop_sound::preview({100,false});samples.fill(-32768);audio_gain::apply(samples.data(),4);require(samples[0]==-32768 && samples[7]==-32768,"full volume preserves original samples");
    desktop_sound::preview({250,false});require(audio_gain::get()==100,"preview cannot amplify above original level");
    desktop_sound::preview({-2,false});require(audio_gain::get()==0,"negative preview is silent");
    {std::ofstream out(file);out<<"GCN_SOUND 1\n101 0\n";}
    require(!desktop_sound::load(file,25,error) && !error.empty() && audio_gain::get()==25,"invalid saved gain falls back safely before audio starts");
    {std::ofstream out(file);out<<"GCN_SOUND 1\n50 0 garbage\n";}
    require(!desktop_sound::load(file,25,error) && audio_gain::get()==25,"trailing invalid settings rejected");
    require(desktop_sound::load((root/"generic.cfg").string(),100,error) && audio_gain::get()==100,"shared runtime supports unchanged defaults for other games");
    printf("PASS: %d sound gain, startup defaults, mute and persistence checks\n",checks);return 0;
}
