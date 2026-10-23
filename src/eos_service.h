#pragma once
#include <string>
namespace mkdd::eos {
int serve(const std::string& config,unsigned upstream,const std::string& ready,unsigned seconds=0,bool fixture=false);
}
