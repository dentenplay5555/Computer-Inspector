#include "tlspin.hpp"
#include <cctype>
#include <mbedtls/sha256.h>
#include <mbedtls/threading.h>
#include <psa/crypto.h>

#if defined(MBEDTLS_THREADING_ALT)
// visual studio: mbedtls's mutexes as windows critical sections (see threading_alt.h)
static void mx_init(mbedtls_threading_mutex_t* m) {InitializeCriticalSection(&m->cs); m->is_valid=1;}
static void mx_free(mbedtls_threading_mutex_t* m) {if (m->is_valid) DeleteCriticalSection(&m->cs); m->is_valid=0;}
static int mx_lock(mbedtls_threading_mutex_t* m) {
  if (!m->is_valid) return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
  EnterCriticalSection(&m->cs);
  return 0;
}
static int mx_unlock(mbedtls_threading_mutex_t* m) {
  if (!m->is_valid) return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
  LeaveCriticalSection(&m->cs);
  return 0;
}
#endif

bool tls_global_init() {
  // once per program, before any other mbedtls call
  static bool ok=[]{
#if defined(MBEDTLS_THREADING_ALT)
    mbedtls_threading_set_alt(mx_init,mx_free,mx_lock,mx_unlock);
#endif
    return psa_crypto_init()==PSA_SUCCESS;
  }();
  return ok;
}

std::string fp_sha256(const unsigned char* der,size_t len) {
  unsigned char h[32];
  mbedtls_sha256(der,len,h,0);
  static const char* hx="0123456789abcdef";
  std::string s;
  for (unsigned char c:h) {s+=hx[c>>4]; s+=hx[c&15];}
  return s;
}

std::string fp_pin(const std::string& hexfp) {
  std::string s;
  for (size_t i=0;i<32&&i<hexfp.size();i++) {
    if (i&&i%4==0) s+='-';
    s+=hexfp[i];
  }
  return s;
}

bool fp_matches(const std::string& pin,const std::string& hexfp) {
  std::string p;
  for (char c:pin) {
    if (c=='-'||c==':'||c==' ') continue;
    if (!std::isxdigit((unsigned char)c)) return false;
    p+=(char)std::tolower((unsigned char)c);
  }
  return p.size()>=32&&p.size()<=hexfp.size()&&hexfp.compare(0,p.size(),p)==0;
}
