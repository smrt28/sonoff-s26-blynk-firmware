
#include "args.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <stdarg.h>

#include "logging.h"
#include "utils.h"

using namespace s28::utils;
namespace s28 {
namespace {

static const char *config_file_name = "/xconfig";

struct ArgsIO {
  virtual void io(const char *key, String &val) = 0;
  virtual bool check(const char *key) = 0;
};

struct ArgsGetter : public ArgsIO {
  ArgsGetter(DynamicJsonDocument &json) : json(json) {}

  void io(const char *key, String &val) {
    JsonVariant tmp = json[key];
    if (tmp.isNull() || tmp.isUndefined()) {
      val = "";
      return;
    }
    val = tmp.as<String>();
  }

  bool check(const char *key) {
    if (!json[key].is<String>()) {
      log("key: [%s] has invalid type", key);
      return false;
    } else {
      log("key: [%s] has good type", key);
    }
    return true;
  }

  DynamicJsonDocument &json;
};

struct ArgsSetter : public ArgsIO {
  ArgsSetter(DynamicJsonDocument &json) : json(json) {}

  void io(const char *key, String &val) { json[key] = val; }

  bool check(const char *key) { return true; }

  DynamicJsonDocument &json;
};

struct Arg {
  const char *id;
  const char *long_name;
  String StartupArgs::*val;
  enum ArgEntryType { TITLE, SECTION, ARG, ARG_ID, END };
  ArgEntryType type;

  static const bool is_arg(ArgEntryType at) {
    if (at == ARG || at == ARG_ID) {
      return true;
    }
    return false;
  }
};

namespace {
static const Arg arg_list[] = {
    {"WiFi", nullptr, nullptr, Arg::TITLE},

    {"ssid", "SSID", &StartupArgs::ssid, Arg::ARG},
    {"password", "password", &StartupArgs::password, Arg::ARG},
    //---
    {"Blynk server", nullptr, nullptr, Arg::TITLE},

    {"collector", "server ip", &StartupArgs::collector, Arg::ARG},
    {"token", "token", &StartupArgs::token, Arg::ARG},
    {"fingerprint", "fingerprint", &StartupArgs::fingerprint, Arg::ARG},

    {nullptr, nullptr, nullptr, Arg::END}};
} // namespace

void handle_args(ArgsIO &aio, StartupArgs *args) {
  // explicit arguments
  aio.io("flags", args->flags);
  aio.io("version", args->version);
  // from arg_list
  for (const Arg &a : arg_list) {
    if (Arg::is_arg(a.type)) {
      aio.io(a.id, args->*(a.val));
    }
  }
}

} // namespace

void update_startup_args(IArgsMap &m, StartupArgs *args) {
  for (const Arg &a : arg_list) {
    if (!Arg::is_arg(a.type)) {
      continue;
    }
    String val = m.get(a.id);
    log("%s <- %s", a.id, val.c_str());
    args->*(a.val) = m.get(a.id);
  }
}
void read_startup_args(StartupArgs *args) {
  LittleFSOpener opener;
  args->ok = false;
  DynamicJsonDocument json(2048);
  std::vector<uint8_t> raw;
  do {
    {
      File f = LittleFS.open(config_file_name, "r");
      raw.resize(f.size() + 1);
      if (f.read(raw.data(), f.size()) != f.size()) {
        log("read config file failed");
        break;
      }
      raw[f.size()] = 0;
      log("config json=[%s]", raw.data());
      if (deserializeJson(json, raw.data()) != DeserializationError::Ok) {
        log("json parse failed");
        break;
      }
    }

    ArgsGetter ag(json);
    handle_args(ag, args);
    args->ok = true;
    return;
  } while (false);

  LittleFS.format();
  *args = StartupArgs();
  args->ok = false;
}

bool write_startup_args(StartupArgs *args) {
  LittleFSOpener opener;
  DynamicJsonDocument json(2048);
  ArgsSetter ag(json);
  handle_args(ag, args);
  String s;
  serializeJson(json, s);
  log("writing config: %s\n", s.c_str());
  File f = LittleFS.open(config_file_name, "w");
  f.write(s.c_str());
  return true;
}

void visit_args(StartupArgs *args, ArgVisitor *visitor) {
  visitor->start();
  for (const Arg &a : arg_list) {
    if (Arg::is_arg(a.type)) {
      visitor->arg(a.id, a.long_name, args->*a.val);
      continue;
    }
    switch (a.type) {
    case Arg::END:
      break;
    case Arg::TITLE:
      visitor->title(a.id);
      break;
    case Arg::SECTION:
      break;
    default:
      log("warn: something not handled in visit_args");
      break;
    }
  }
  visitor->end();
}

struct HtmlFormGenerator : public ArgVisitor {
  HtmlFormGenerator(String &s) : s(s) {}

  void arg(const String &id, const String &label, const String &val) override {
    s = s + "<div class=\"label\">" + label + ":</div>" +
        "<input type=\"text\" name=\"" + id + "\" value=\"" +
        s28::utils::escape_html(val) + "\"";

    if (id == "ssid") {
      s += " id=\"ssid\"";
    }

    s += ">\n";
  }

  void title(const String &label) override {
    s = s + "<h1>" + label + "</h1>\n";
  }

  void start() override {}

  void end() override {}

private:
  String &s;
};

void gen_html_form_content(String &s, StartupArgs *args) {
  HtmlFormGenerator g(s);
  visit_args(args, &g);
}
} // namespace s28