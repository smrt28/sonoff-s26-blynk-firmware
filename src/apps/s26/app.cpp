#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS
#include "args.h"
#include "fingerprint_probe.h"

#include <Arduino.h>
#include <BlynkSimpleEsp8266_SSL.h>
#include <ESP8266WebServer.h> // Include the WebServer library
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266mDNS.h>

#include <LittleFS.h>
#include <SoftwareSerial.h>
#include <WiFiClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <iostream>
#include <string>
#include <vector>

#include "app.h"
#include "apps/config/app.h"
#include "logging.h"
#include "utils.h"

using namespace s28;
using namespace s28::utils;
#include "apps/s26/app.h"


namespace {

struct SetupCtl {
  static void create(StartupArgs &startup_args) {
    if (instance)
      return;
    instance = new SetupCtl(startup_args);
  }

  static void call_loop() {
    if (!instance)
      return;
    if (!instance->loop()) {
      delete instance;
      instance = nullptr;
    }
  }

  static bool will_enter() {
    if (!instance)
      return false;
    if (instance->action == ENTER)
      return true;
    return false;
  }

  static bool schedule_enter() {
    if (!instance)
      return false;
    instance->action = ENTER;
    return true;
  }

private:
  SetupCtl(StartupArgs &startup_args) : startup_args(startup_args) {}
  enum Action { NONE, ENTER, LEAVE };
  Action action = NONE;

  bool loop() {
    auto t = millis();

    if (t > 60000) {
      leave();
      return false;
    }

    if (t < 5000) {
      return true;
    }

    switch (action) {
    case NONE:
      break;
    case ENTER:
      enter();
      break;
    case LEAVE:
      leave();
      break;
    }

    action = NONE;
    return true;
  }

  void enter() {
    if (startup_args.is_entering_setup())
      return;
    startup_args.set_enter_setup(true);
    write_startup_args(&startup_args);
    digitalWrite(s28::gpio_13_led, LOW);
    flush_log_history();
  }

  void leave() {
    if (!startup_args.is_entering_setup())
      return;
    startup_args.set_enter_setup(false);
    write_startup_args(&startup_args);
    digitalWrite(s28::gpio_13_led, HIGH);
  }

  StartupArgs &startup_args;
  static SetupCtl *instance;
};

SetupCtl *SetupCtl::instance = nullptr;

bool check_args(const StartupArgs &args) {
  if (!args.ok) {
    log("invalid config mini-file. Please configure first!");
    return false;
  }
  if (args.ssid.isEmpty()) {
    log("SSID not configured");
    return false;
  }
  if (args.token.length() < 5) {
    log("Blynk token not configured");
    return false;
  }
  return true;
}

struct SonoffS26 : s28::App {
  SonoffS26(StartupArgs &startup_args) : startup_args(startup_args) {}
  StartupArgs &startup_args;
  int setup_sensors(const StartupArgs &args);
  void connect_blynk();
  bool setup() override;
  void loop() override;
  void timer_loop();
};

void SonoffS26::connect_blynk() {
  if (!startup_args.has_custom_blynk_server()) {
    log("connecting Blynk in cloud [%s]", startup_args.collector.c_str());
    Blynk.config(startup_args.token.c_str());
  } else {
    log("connecting custom Blynk server");
    IPAddress blinkIp;
    blinkIp.fromString(startup_args.collector);
    Blynk.config(startup_args.token.c_str(), blinkIp, 9443,
                 startup_args.fingerprint.c_str());
    log("connecting: %s", blinkIp.toString().c_str());
  }
  log("key: [%s]", startup_args.token.c_str());
  log("connecting blynk...");
  Blynk.connect();
}

bool SonoffS26::setup() {
  if (!check_args(startup_args)) { // vary basic args sanity check
    return false;
  }

  int status = WL_DISCONNECTED;
  WiFi.begin(startup_args.ssid.c_str(), startup_args.password.c_str());
  while ((status = WiFi.status()) != WL_CONNECTED) {
    if (SetupCtl::will_enter())
      return false;
    log("wifi not connected, retry %d", int(status));
    delay(1000);
  }

  log("WiFi connected, Gateway Ip: %s", WiFi.gatewayIP().toString().c_str());

  if (startup_args.has_custom_blynk_server()) {
    if (startup_args.fingerprint.length() < 5) {
      s28::Fingerprint fingerprint;
      for (;;) {
        if (s28::probe(startup_args.collector, 9443, &fingerprint) == 0) {
          log("? Fingerprint: [%s]", fingerprint.to_string().c_str());
          startup_args.fingerprint = fingerprint.to_string();
          write_startup_args(&startup_args);
          break;
        }

        if (SetupCtl::will_enter()) {
          return false;
        }

        log("Fingerprint probe failed!");
        delay(500);
      }
    } else {
      log("! Fingerprint: [%s]", startup_args.fingerprint.c_str());
    }
  } else {
    log("will check the cert");
  }
  connect_blynk();
  return true;
}

void SonoffS26::loop() { Blynk.run(); }

void ICACHE_RAM_ATTR handleInterrupt() {
  if (SetupCtl::schedule_enter()) {
    return;
  }
}


struct SwitchConfigProxyApp : public s28::App {
  SwitchConfigProxyApp(s28::App *parent)
      : parent(parent) {}

  static bool interupt_set;

  ~SwitchConfigProxyApp() { delete parent; }

  bool setup() override {
    if (!interupt_set) {
      interupt_set = true;
      attachInterrupt(digitalPinToInterrupt(0), handleInterrupt, RISING);
    }

    if (!parent) {
      log("fatal: app not created");
      return true;
    }

    if (!parent->setup()) {
      log("parent setup failed");
      delete parent;
      parent = nullptr;
    }
    return true;
  }

  void loop() {
    if (parent) {
      parent->loop();
    }
    SetupCtl::call_loop();
  }

  s28::App *parent = nullptr;
};

bool SwitchConfigProxyApp::interupt_set = false;

} // namespace

namespace s28 {
namespace s26 {

s28::App *create(StartupArgs &args) {
  SetupCtl::create(args);
  s28::App *mon = new SonoffS26(args);
  return new SwitchConfigProxyApp(mon);
}

} // namespace s26
} // namespace s28


// Blynk functions ---
BLYNK_CONNECTED() {
  s28::log("blynk sync");
  Blynk.syncVirtual(V1);
}

BLYNK_WRITE(V1) {
  int pinValue = param.asInt();
  s28::log("event: %d", pinValue);
  if (pinValue) {
    digitalWrite(
        s28::gpio_12_relay,
        HIGH); // Turn relay ON.  Power ON the devices connected to Sonoff
  } else {
    digitalWrite(
        s28::gpio_12_relay,
        LOW); // Turn relay ON.  Power ON the devices connected to Sonoff
  }
}