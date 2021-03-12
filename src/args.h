#ifndef SRC_ARGS_H
#define SRC_ARGS_H

#include <Arduino.h>

namespace s28 {

struct StartupArgs {
  constexpr static const char * VERSION = "1001";

  String version = VERSION;
  String flags;
  String ssid;
  String password;
  String id;
  
  String collector; // blynk server
  String token;
  String fingerprint; // blybk server fingerprint

  bool has_custom_blynk_server() {
    if (collector.isEmpty() || collector == "*") {
      return false;
    }
    return true;
  }

  bool is_entering_setup() {
    if (flags ==  "1") {
      return true;
    }
    return false;
  }

  void set_enter_setup(bool b) {
    if (b) {
      flags = "1";
    } else {
      flags = "0";
    }
  }

  bool ok = false;
};



struct ArgVisitor {
  virtual void arg(const String &id, const String &label, const String &val) = 0;
  virtual void title(const String &label) = 0;
  virtual void start() = 0;
  virtual void end() = 0;
};

struct IArgsMap {
  virtual String get(const char *name) = 0;
};

void read_startup_args(StartupArgs *args);
bool write_startup_args(StartupArgs *args);
void gen_html_form_content(String &s, StartupArgs *args);
void update_startup_args(IArgsMap &m, StartupArgs *args);
void visit_args(StartupArgs *args, ArgVisitor *visitor);

}

#endif