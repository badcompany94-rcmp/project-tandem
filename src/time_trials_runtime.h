#pragma once
#include "time_trial_record.h"
namespace mkdd::trials {
Session& native_session(); // Guest/menu thread only.
void native_menu_boundary();
}
