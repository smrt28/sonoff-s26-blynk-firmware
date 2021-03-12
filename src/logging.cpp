
#include <SoftwareSerial.h>
#include <stdarg.h>
#include <stdio.h>
#include <LittleFS.h>
#include "utils.h"
namespace s28 {

namespace {
struct History {
  History() {}

  ~History() {
    for (char *rec : history) {
      free(rec);
    }
  }

  void insert(const char *msg) {
    if (history.size() > 200) {
      return;
    }
    char *buf = strdup(msg);
    if (buf) {
      history.push_back(buf);
    }
  }

  void flush() {
    utils::LittleFSOpener opener;
    File f = LittleFS.open("/log", "w");
    if (!f) {
      Serial.printf("failed to open little file /log\n");
      return;
    }
    for (char *c: history) {
      f.println(c);
    }
    f.flush();
    f.close();
    Serial.println("Log written\n");
  }

private:
  std::vector<char *> history;
  size_t n = 0;
};

bool history_enabled = true;
History *log_history = nullptr;

void log_(int lvl, const char *format, va_list a) {
  if (history_enabled && log_history == 0) {
    log_history = new History();
  }

  va_list arg;
  va_copy(arg, a);
  char tmp[64];
  char *buffer = tmp;
  size_t len = vsnprintf(buffer, sizeof(tmp), format, arg);
  va_end(arg);
  if (len > sizeof(tmp) - 1) {
    buffer = new char[len + 1];
    if (!buffer) {
      return;
    }
    va_copy(arg, a);
    vsnprintf(buffer, len + 1, format, arg);
    va_end(arg);
  }

  if (len == 0) {
    if (buffer != tmp) {
      delete [] buffer;
    }
    return;
  }

  if (buffer[len - 1] == '\n') {
    len --;
    buffer[len] = 0;
  }

  if (log_history) {
    log_history->insert(buffer);
  }
  
  Serial.write((const uint8_t *)buffer, len);
  Serial.write("\n", 1);
  if (buffer != tmp) {
    delete[] buffer;
  }
}

} // namespace

void log(const char *format, ...) {
  va_list arg;
  va_start(arg, format);
  log_(0, format, arg);
}

void xlog(const char *format, ...) {
  va_list arg;
  va_start(arg, format);
  log_(1, format, arg);
}

void flush_log_history(bool dont_write) {
  if (!history_enabled) {
    return;
  }
  if (log_history) {
    if (!dont_write) {
      log_history->flush();
    }
    delete log_history;
  }
  log_history = nullptr;
  history_enabled = false;
  Serial.printf("flush_log_history\n");
}

} // namespace s28