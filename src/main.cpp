#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS
#include "args.h"
#include "fingerprint_probe.h"

#include <Arduino.h>
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

#include "app_iface.h"
#include "apps/config/app.h"
#include "apps/s26/app.h"
#include "logging.h"
#include "utils.h"


using namespace s28;

namespace {
s28::App *app = nullptr;
StartupArgs startup_args;
} // namespace

void loop() {
  app->loop();
}

void setup() {
  Serial.begin(9600);

  // disconnect AP by default
  WiFi.softAPdisconnect(true);
  
  delay(100);
  log("starting...");
  read_startup_args(&startup_args);
  pinMode(gpio_13_led, OUTPUT);
  pinMode(gpio_12_relay, OUTPUT);
  
  if (startup_args.is_entering_setup()) {
    // don't maintain history during the setup
    s28::flush_log_history(true);
    log("entering setup...");
    startup_args.set_enter_setup(false);
    write_startup_args(&startup_args);
    digitalWrite(s28::gpio_13_led, LOW);
    app = s28::app_config::create(startup_args);
  } else {
    log("entering sonoff-s26 app...");
    digitalWrite(s28::gpio_13_led, HIGH);
    app = s28::s26::create(startup_args);
  }

  if (app) {
    if (!app->setup()) {
      log("setup failed");
      delete app;
      app = nullptr;
    }
  } else {
    log("no app initialized");
  }
}