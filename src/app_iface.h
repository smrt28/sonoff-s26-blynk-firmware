#ifndef s28_app_iface_h
#define s28_app_iface_h

namespace s28 {

// sonoff s26 related GPIO's
constexpr int gpio_13_led = 13;
constexpr int gpio_12_relay = 12;

struct App {  
  virtual ~App() {}
  virtual void loop() = 0;
  virtual bool setup() = 0;
};

}

#endif