#include "tlspin.hpp"
#include <cctype>
#include <mbedtls/sha256.h>
#include <psa/crypto.h>

bool tls_global_init() {return psa_crypto_init()==PSA_SUCCESS;}

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
