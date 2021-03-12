#include "utils.h"
#include "logging.h"
#include <LittleFS.h>

namespace s28 {
namespace utils {

String escape_html(const String &data) {
  String res;

  for (size_t pos = 0; pos != data.length(); ++pos) {
    switch (data[pos]) {
    case '&':
      res += "&amp;";
      break;
    case '\"':
      res += "&quot;";
      break;
    case '\'':
      res += "&apos;";
      break;
    case '<':
      res += "&lt;";
      break;
    case '>':
      res += "&gt;";
      break;
    default:
      res += char(data[pos]);
      break;
    }
  }
  return res;
}

LittleFSOpener::LittleFSOpener() {
  if (!LittleFS.begin()) {
    log("LittleFS.begin failed");
  }
}
LittleFSOpener::~LittleFSOpener() { LittleFS.end(); }

} // namespace utils
} // namespace s28