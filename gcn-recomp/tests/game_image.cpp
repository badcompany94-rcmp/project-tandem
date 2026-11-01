#include "game_image.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
using namespace game_image;
static int checks;
static void require(bool value,const char* message){++checks;if(!value){fprintf(stderr,"FAIL %s\n",message);exit(1);}}
static void touch(const fs::path& path){fs::create_directories(path.parent_path());std::ofstream(path)<<"fixture";}
int main() {
    auto temp=fs::temp_directory_path()/("tandem-image-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto package=temp/"Tandem spaces % moved.pc",cwd=temp/"unrelated";fs::create_directories(cwd);
    auto exe=package/"local-data/build-portable/mkdd.exe";touch(exe);touch(package/"package-manifest.json");
    auto result=resolve("Z:/missing/windows/game.iso",exe,cwd);require(result.image.empty(),"missing image returns diagnostic");
    const auto cache=package/"local-data/game-image.ciso";touch(cache);
    require(fs::path(resolve("Z:/missing/windows/game.iso",exe,cwd).image)==cache,"portable CISO found from foreign working directory");
    const auto selected=cwd/"manual.iso";touch(selected);
    require(fs::path(resolve(selected.string(),exe,cwd).image)==cache,"portable cache takes precedence over stale build-machine default");
    require(fs::path(resolve("",exe,cwd,selected.string()).image)==selected,"explicit path takes precedence");
    require(resolve("",exe,cwd,"not-here.iso").image.empty(),"invalid explicit path does not silently fall back");
    fs::remove(cache);require(fs::path(resolve(selected.string(),exe,cwd).image)==selected,"readable configured image remains supported");
    const auto rom=package/"rom/Disc Uppercase.ISO";touch(rom);
    require(fs::path(resolve("Z:/missing.iso",exe,cwd).image)==rom,"package ROM found with uppercase extension");
    fs::remove(rom);const auto relative=cwd/"rom/test.ciso";touch(relative);
    require(fs::path(resolve("",exe,cwd).image)==relative,"legacy working-folder ROM supported");
    require(fs::path(resolve("",exe,cwd,"rom/test.ciso").image)==relative,"relative explicit path resolves against working directory");
    const auto directory=package/"local-data/game-image.iso";fs::create_directories(directory);
    require(fs::path(resolve("",exe,cwd).image)==relative,"directory with image extension rejected");
    auto flat=package/"mkdd.exe";touch(flat);touch(package/"rom/flat.iso");
    require(fs::path(resolve("",flat,cwd).image)==package/"rom/flat.iso","flat portable executable layout supported");
    require(temp.filename().string().rfind("tandem-image-",0)==0 && temp.parent_path()==fs::temp_directory_path(),"fixture cleanup bounded");
    fs::remove_all(temp);printf("PASS %d portable image discovery checks\n",checks);
}
