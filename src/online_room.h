#pragma once
#include "online_json.h"
#include <map>

namespace mkdd::online {
Json room_request(const Json& request); // bounded loopback HTTP, call off the guest thread
std::map<std::string,std::string> launch_environment(const Json& response,const std::string& build);
std::string executable_build();
void enter_race(const Json& response); // fresh process handoff at the room start boundary
void finish_event(const Json& ranking);
void return_to_room();
void race_ui_tick(bool fast_status=false);
void recover_room_disconnect();
}
