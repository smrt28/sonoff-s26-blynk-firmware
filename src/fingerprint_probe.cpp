#include <ESP8266WiFi.h> // https://github.com/esp8266/Arduino
#include <WiFiClient.h>  // https://github.com/esp8266/Arduino
#include <bearssl/bearssl.h>

#include "fingerprint_probe.h"

namespace {

struct ex_context {
  const br_x509_class *vtable;
  br_multihash_context mhash;
  br_sha1_context sha1_cert;
  bool done_cert = false;
  uint8_t *fingerprint;
  bool ok = false;
};

int sock_read(void *ctx, unsigned char *buf, size_t len) {
  WiFiClient *client = (WiFiClient *)ctx;

  int retr = 0;
  while (client->available() == 0) {
    delay(10);
    retr++;
    if (retr == 500) {
      Serial.printf("fingerprint-probe: socket_read failed!\n");
      return -1;
    }
  }

  Serial.printf("reading %d bytes:\n", (int)len);
  for (size_t i = 0; i < len; i++) {
    int x;
    retr = 0;

    for (;;) {
      x = client->read();
      if (x != -1)
        break;
      if (retr > 20) {
        return -1;
      }
      delay(10);
      retr++;
    }

    buf[i] = x;
  }

  return len;
}

int sock_write(void *ctx, const unsigned char *buf, size_t len) {
  WiFiClient *client = (WiFiClient *)ctx;
  int rv = client->write(buf, len);
  client->flush();
  return rv;
}

const br_x509_pkey *get_pkey(const br_x509_class *const *ctx,
                             unsigned *usages) {
  return 0;
}

static void ex_start_chain(const br_x509_class **ctx, const char *server_name) {
  ex_context *xc = (ex_context *)ctx;
  br_sha1_init(&xc->sha1_cert);
}

static void ex_append(const br_x509_class **ctx, const unsigned char *buf,
                      size_t len) {
  ex_context *xc = (ex_context *)ctx;
  if (xc->done_cert)
    return;
  br_sha1_update(&xc->sha1_cert, buf, len);
  xc->ok = true;
}

static void ex_end_cert(const br_x509_class **ctx) {
  ex_context *xc = (ex_context *)ctx;
  xc->done_cert = true;
  uint8_t res[20];
  memset(res, 0, sizeof(res));
  br_sha1_out(&xc->sha1_cert, res);
  memcpy(xc->fingerprint, res, 20);
}

unsigned end_chain(const br_x509_class **ctx) {
  return BR_ERR_X509_NOT_TRUSTED;
}

void start_cert(const br_x509_class **ctx, uint32_t length) {}

void br_ssl_client_init_full2(br_ssl_client_context *cc, ex_context *xc,
                              const br_x509_trust_anchor *trust_anchors,
                              size_t trust_anchors_num) {
  static const uint16_t suites[] = {
      BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
      BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
      BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
      BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
      BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
      BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
      BR_TLS_ECDHE_ECDSA_WITH_AES_128_CCM,
      BR_TLS_ECDHE_ECDSA_WITH_AES_256_CCM,
      BR_TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8,
      BR_TLS_ECDHE_ECDSA_WITH_AES_256_CCM_8,
      BR_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256,
      BR_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,
      BR_TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA384,
      BR_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384,
      BR_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA,
      BR_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
      BR_TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA,
      BR_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA,
      BR_TLS_ECDH_ECDSA_WITH_AES_128_GCM_SHA256,
      BR_TLS_ECDH_RSA_WITH_AES_128_GCM_SHA256,
      BR_TLS_ECDH_ECDSA_WITH_AES_256_GCM_SHA384,
      BR_TLS_ECDH_RSA_WITH_AES_256_GCM_SHA384,
      BR_TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA256,
      BR_TLS_ECDH_RSA_WITH_AES_128_CBC_SHA256,
      BR_TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA384,
      BR_TLS_ECDH_RSA_WITH_AES_256_CBC_SHA384,
      BR_TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA,
      BR_TLS_ECDH_RSA_WITH_AES_128_CBC_SHA,
      BR_TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA,
      BR_TLS_ECDH_RSA_WITH_AES_256_CBC_SHA,
      BR_TLS_RSA_WITH_AES_128_GCM_SHA256,
      BR_TLS_RSA_WITH_AES_256_GCM_SHA384,
      BR_TLS_RSA_WITH_AES_128_CCM,
      BR_TLS_RSA_WITH_AES_256_CCM,
      BR_TLS_RSA_WITH_AES_128_CCM_8,
      BR_TLS_RSA_WITH_AES_256_CCM_8,
      BR_TLS_RSA_WITH_AES_128_CBC_SHA256,
      BR_TLS_RSA_WITH_AES_256_CBC_SHA256,
      BR_TLS_RSA_WITH_AES_128_CBC_SHA,
      BR_TLS_RSA_WITH_AES_256_CBC_SHA,
      BR_TLS_ECDHE_ECDSA_WITH_3DES_EDE_CBC_SHA,
      BR_TLS_ECDHE_RSA_WITH_3DES_EDE_CBC_SHA,
      BR_TLS_ECDH_ECDSA_WITH_3DES_EDE_CBC_SHA,
      BR_TLS_ECDH_RSA_WITH_3DES_EDE_CBC_SHA,
      BR_TLS_RSA_WITH_3DES_EDE_CBC_SHA};

  static const br_hash_class *hashes[] = {&br_md5_vtable,    &br_sha1_vtable,
                                          &br_sha224_vtable, &br_sha256_vtable,
                                          &br_sha384_vtable, &br_sha512_vtable};

  br_ssl_client_zero(cc);
  br_ssl_engine_set_versions(&cc->eng, BR_TLS10, BR_TLS12);

  memset(xc, 0, sizeof(*xc));

  br_ssl_engine_set_suites(&cc->eng, suites,
                           (sizeof suites) / (sizeof suites[0]));

  for (int id = br_md5_ID; id <= br_sha512_ID; id++) {
    const br_hash_class *hc;

    hc = hashes[id - 1];
    br_ssl_engine_set_hash(&cc->eng, id, hc);
    br_multihash_setimpl(&xc->mhash, id, hc);
  }

  br_ssl_engine_set_x509(&cc->eng, &xc->vtable);
}

struct HeapVars {
  static const size_t iobuf_len = 837;
  br_ssl_client_context sc;
  ex_context xc;
  unsigned char iobuf[iobuf_len];
  br_sslio_context ioc;
  WiFiClient client;
};

char hex(int a) {
  static const char *h = "0123456789ABCDEF";
  if (a < 0 || a > 15) {
    Serial.printf("FATAL! hex failed\n");
    for (;;) {
      delay(1000);
    }
  }
  return h[a];
}

} // namespace

namespace s28 {

int probe(const String &host, int port, Fingerprint *fp) {
  std::unique_ptr<HeapVars> vars(new HeapVars());

  for (int retr = 1;; ++retr) {
    if (!vars->client.connect(host, port)) {
      Serial.printf("Failed connection... %s %d; attempt=%d\n", host.c_str(),
                    port, retr);
      vars->client.stopAll();
      if (retr == 15) {
        Serial.printf("probe, giving up on connection :-(");
        return -1;
      }
      delay(2000);
      continue;
    }
    break;
  }

  Serial.printf("looking for fingerprint...%s %d\n", host.c_str(), port);
  br_ssl_client_context &sc(vars->sc);
  ex_context &xc(vars->xc);
  br_sslio_context &ioc(vars->ioc);

  br_ssl_client_init_full2(&sc, &xc, 0, 0);

  br_x509_class vtable;
  xc.vtable = &vtable;
  vtable.start_cert = start_cert;
  vtable.start_chain = ex_start_chain;
  vtable.append = ex_append;
  vtable.end_cert = ex_end_cert;
  vtable.get_pkey = get_pkey;
  vtable.end_chain = end_chain;

  vars->xc.fingerprint = fp->raw;

  br_ssl_engine_set_buffer(&sc.eng, vars->iobuf, HeapVars::iobuf_len, 0);
  br_ssl_client_reset(&sc, host.c_str(), 0);
  br_sslio_init(&ioc, &sc.eng, sock_read, &vars->client, sock_write,
                &vars->client);
  br_sslio_flush(&ioc);

  int err = br_ssl_engine_last_error(&sc.eng);

  if (err == 62 && xc.ok) {
    return 0;
  }
  return -1;
}

String Fingerprint::to_string() {
  String s;
  for (size_t i = 0; i < 20; ++i) {
    if (i != 0)
      s += ' ';
    uint8_t c = raw[i];
    s += hex(c >> 4);
    s += hex(c & 0xf);
  }
  return s;
}

} // namespace s28