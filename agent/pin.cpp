#include "pin.hpp"
#include "common/tlspin.hpp"
#include <cstring>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pem.h>
#include <mbedtls/ssl.h>

namespace {
struct tls {
  mbedtls_net_context net;
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config conf;
  mbedtls_entropy_context ent;
  mbedtls_ctr_drbg_context rng;
  tls() {mbedtls_net_init(&net); mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&conf); mbedtls_entropy_init(&ent); mbedtls_ctr_drbg_init(&rng);}
  ~tls() {mbedtls_net_free(&net); mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf); mbedtls_ctr_drbg_free(&rng); mbedtls_entropy_free(&ent);}
};
std::string why(int rc) {char b[160]; mbedtls_strerror(rc,b,sizeof b); return b;}
}  // namespace

pin_result fetch_pinned_cert(const std::string& host,int port,const std::string& pin,std::string& pem,std::string& info) {
  tls t;
  const char* pers="computer-inspector-pin";
  if (mbedtls_ctr_drbg_seed(&t.rng,mbedtls_entropy_func,&t.ent,(const unsigned char*)pers,std::strlen(pers))!=0) {info="random number setup failed"; return pin_result::unreachable;}
  std::string p=std::to_string(port);
  if (int rc=mbedtls_net_connect(&t.net,host.c_str(),p.c_str(),MBEDTLS_NET_PROTO_TCP)) {info="cannot reach "+host+":"+p+" ("+why(rc)+")"; return pin_result::unreachable;}
  mbedtls_ssl_config_defaults(&t.conf,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT);
  // no CA check here on purpose: the fingerprint comparison below is stricter
  mbedtls_ssl_conf_authmode(&t.conf,MBEDTLS_SSL_VERIFY_NONE);
  mbedtls_ssl_conf_rng(&t.conf,mbedtls_ctr_drbg_random,&t.rng);
  mbedtls_ssl_conf_read_timeout(&t.conf,5000);
  if (mbedtls_ssl_setup(&t.ssl,&t.conf)!=0) {info="TLS setup failed"; return pin_result::unreachable;}
  mbedtls_ssl_set_bio(&t.ssl,&t.net,mbedtls_net_send,mbedtls_net_recv,mbedtls_net_recv_timeout);
  int rc;
  while ((rc=mbedtls_ssl_handshake(&t.ssl))==MBEDTLS_ERR_SSL_WANT_READ||rc==MBEDTLS_ERR_SSL_WANT_WRITE) {}
  if (rc!=0) {info="TLS handshake with "+host+":"+p+" failed ("+why(rc)+")"; return pin_result::unreachable;}
  const mbedtls_x509_crt* c=mbedtls_ssl_get_peer_cert(&t.ssl);
  if (!c) {info="the server sent no certificate"; return pin_result::unreachable;}
  std::string fp=fp_sha256(c->raw.p,c->raw.len);
  if (!fp_matches(pin,fp)) {
    info="the server's certificate "+fp_pin(fp)+" does not match --pin "+pin+
         " (wrong pin, a reinstalled server, or someone pretending to be the server)";
    mbedtls_ssl_close_notify(&t.ssl);
    return pin_result::mismatch;
  }
  unsigned char buf[8192];
  size_t olen=0;
  if (mbedtls_pem_write_buffer("-----BEGIN CERTIFICATE-----\n","-----END CERTIFICATE-----\n",
                               c->raw.p,c->raw.len,buf,sizeof buf,&olen)!=0) {info="cannot encode the certificate"; return pin_result::unreachable;}
  pem.assign((const char*)buf);
  mbedtls_ssl_close_notify(&t.ssl);
  return pin_result::ok;
}

bool pin_from_cert_file(const std::string& path,std::string& pin,std::string& err) {
  mbedtls_x509_crt c;
  mbedtls_x509_crt_init(&c);
  bool ok=mbedtls_x509_crt_parse_file(&c,path.c_str())==0;
  if (ok) pin=fp_pin(fp_sha256(c.raw.p,c.raw.len));
  else err="cannot read the certificate "+path;
  mbedtls_x509_crt_free(&c);
  return ok;
}
