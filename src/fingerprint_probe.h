#ifndef fingerprint_probe_h
#define fingerprint_probe_h

namespace s28 {

struct Fingerprint {
    uint8_t raw[20];
    String to_string();
};

int probe(const String &host, int port, Fingerprint *fp);

} // namespace s28

#endif /* fingerprint_probe_h */