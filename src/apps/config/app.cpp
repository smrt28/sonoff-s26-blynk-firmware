#include <Arduino.h>
#include <ESP8266WebServer.h>
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

#include "args.h"
#include "logging.h"
#include "utils.h"

using namespace s28::utils;
using namespace s28;

namespace {

#include "bye_html.h"
#include "setup_html.h"

const char *ssid = "SonoffS26(8)";
const char *password = "SonoffFwSux";

IPAddress local_ip(192, 168, 100, 1);
IPAddress gateway(192, 168, 100, 1);
IPAddress subnet(255, 255, 255, 0);

ESP8266WebServer *server = nullptr;

String WidlCharVal(char c, StartupArgs &startup_args);
String SendHTML(StartupArgs &startup_args);

struct NetworkInfo {
  String ssid;
  bool open;
};

struct ServerArgsProxy : public IArgsMap {
  ServerArgsProxy(ESP8266WebServer *server) : server(server) {}
  String get(const char *name) override { return server->arg(name); }
  ESP8266WebServer *server;
};

struct AppConfig : public s28::App {
  AppConfig(StartupArgs &startup_args) : startup_args(startup_args) {}

  std::vector<NetworkInfo> networks;

  String networks_html() {
    String res;
    for (auto net : networks) {
      String tmp;

      tmp = String("<a href=\"javascript:setSsid('") +
            escape_html(net.ssid.c_str()) + "')\">" +
            escape_html(net.ssid.c_str()) + "</a>";

      res += String("<p>") + tmp + String("</p>");
    }
    return res;
  }

  String WidlCharVal(char c, StartupArgs &startup_args) {
    switch (c) {
    case 'F': {
      String s;
      gen_html_form_content(s, &startup_args);
      return s;
    }
    case 'n':
      return networks_html();
    }
    return String();
  }

  bool setup() override {
    server = new ESP8266WebServer(80);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    WiFi.scanNetworksAsync([this](int networksFound) {
      Serial.printf("%d network(s) found\n", networksFound);
      networks.clear();
      for (int i = 0; i < networksFound; i++) {
        Serial.printf("%d: %s, Ch:%d (%ddBm) %s\n", i + 1, WiFi.SSID(i).c_str(),
                      WiFi.channel(i), WiFi.RSSI(i),
                      WiFi.encryptionType(i) == ENC_TYPE_NONE ? "open" : "");
        NetworkInfo info;
        info.open = (WiFi.encryptionType(i) == ENC_TYPE_NONE);
        info.ssid = WiFi.SSID(i).c_str();
        networks.push_back(info);
      }
    });

    WiFi.softAP(ssid, password);
    WiFi.softAPConfig(local_ip, gateway, subnet);
    WiFi.setOutputPower(0);

    server->on("/", HTTP_GET, [this]() {
      String ptr;
      for (size_t i = 0; i < __assets_setup_html_len; ++i) {
        char c = __assets_setup_html[i];
        if (i == __assets_setup_html_len - 1) {
          ptr += char(c);
          continue;
        }

        if (c == '$') {
          ptr += WidlCharVal(__assets_setup_html[++i], startup_args);
          continue;
        }
        ptr += char(c);
      }
      server->send(200, "text/html", ptr);
    });

    server->on("/reset", HTTP_POST, []() { ESP.reset(); });
    server->on("/log", HTTP_GET, []() {
      utils::LittleFSOpener opener;
      File f = LittleFS.open("/log", "r");
      if (!f) {
        server->send(404, "text/plain", "log record found!");
        return;
      }

      char *data = new char[f.size()];
      if (!data) {
        server->send(400, "text/plain", "the log record is too large!");
        return;
      }

      f.read((uint8_t *)data, f.size());
      server->send(200, "text/plain", data, f.size());
    });
    server->on("/", HTTP_POST, []() {
      StartupArgs args;
      ServerArgsProxy proxy(server);
      update_startup_args(proxy, &args);
      args.flags = "0";

      if (write_startup_args(&args)) {
        String s;
        s.concat((const char *)___assets_bye_html, ___assets_bye_html_len);
        server->send(200, "text/html", s);
      } else {
        server->send(400, "text/html", "error");
      }
    });
    server->onNotFound([]() { server->send(404, "text/plain", "Not found"); });
    server->begin();
    Serial.println("HTTP server started");
    return true;
  }

  void loop() override { 
   // digitalWrite(s28::gpio_13_led, LOW);
    server->handleClient();
  }
  StartupArgs &startup_args;
};

} // namespace

namespace s28 {

namespace app_config {
s28::App *create(StartupArgs &startup_args) {
  return new AppConfig(startup_args);
}
} // namespace app_config
} // namespace s28