#ifndef s28_utils_h
#define s28_utils_h

#include <WString.h>

namespace s28 {
namespace utils {
    
String escape_html(const String &data);

struct LittleFSOpener {
  LittleFSOpener();
  ~LittleFSOpener();
};

} // namespace utils
} // namespace s28

#endif