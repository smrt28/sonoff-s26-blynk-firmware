#ifndef s28_apps_config_app_h
#define s28_apps_config_app_h

#include "app_iface.h"

namespace s28 {
namespace app_config {
s28::App * create(StartupArgs &startup_args);
} // namespace app_config
} // namespace s28

#endif