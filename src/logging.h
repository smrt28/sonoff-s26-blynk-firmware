#ifndef s28_logging_h
#define s28_logging_h

namespace s28 {
void log(const char *format, ...);
void flush_log_history(bool dont_write = false);
} // namespace s28

#endif