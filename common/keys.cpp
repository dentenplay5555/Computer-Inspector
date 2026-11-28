#include "keys.hpp"
#include <cctype>
#include <filesystem>
#include <fstream>
#include <vector>
#include <mbedtls/md.h>
#include <psa/crypto.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif

static std::string to_hex(const unsigned char* p,size_t n) {
  static const char* hx="0123456789abcdef";
  std::string s;
  for (size_t i=0;i<n;i++) {s+=hx[p[i]>>4]; s+=hx[p[i]&15];}
  return s;
}

std::string rand_hex(size_t n) {
  std::vector<unsigned char> b(n);
  if (psa_generate_random(b.data(),n)!=PSA_SUCCESS) return std::string();
  return to_hex(b.data(),n);
}

std::string hmac_hex(const std::string& key,const std::string& msg) {
  unsigned char out[32];
  if (mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),(const unsigned char*)key.data(),key.size(),
                      (const unsigned char*)msg.data(),msg.size(),out)!=0) return std::string();
  return to_hex(out,sizeof out);
}

// looks at every byte whatever it finds, so timing does not tell how much of a guess was right
bool same_secret(const std::string& a,const std::string& b) {
  if (a.empty()||a.size()!=b.size()) return false;
  volatile unsigned char d=0;
  for (size_t i=0;i<a.size();i++) d|=(unsigned char)(a[i]^b[i]);
  return d==0;
}

std::string make_join_key() {
  static const char* b32="ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   // no I, O, 0, 1: easy to type
  unsigned char b[17];
  if (psa_generate_random(b,sizeof b)!=PSA_SUCCESS) return std::string();
  std::string s;
  for (int i=0;i<26;i++) {                 // 26 x 5 bits = 130 bits from 136 random bits
    int bit=i*5,v=0;
    for (int k=0;k<5;k++) v=(v<<1)|((b[(bit+k)/8]>>(7-(bit+k)%8))&1);
    if (i&&i%4==0) s+='-';
    s+=b32[v];
  }
  return s;
}

std::string norm_join_key(const std::string& k) {
  std::string s;
  for (char c:k) if (c!='-'&&c!=' ') s+=(char)std::toupper((unsigned char)c);
  return s;
}

bool write_secret_file(const std::string& path,const std::string& data) {
  std::string tmp=path+".tmp";
  {
    std::ofstream f(tmp,std::ios::binary|std::ios::trunc);
    f<<data;
    f.close();
    if (!f) return false;
  }
#ifndef _WIN32
  chmod(tmp.c_str(),0600);
#endif
  std::error_code ec;
  std::filesystem::rename(tmp,path,ec);
  if (ec) {                                   // some windows libraries will not replace a file
    std::filesystem::remove(path,ec);
    std::filesystem::rename(tmp,path,ec);
  }
  if (ec) {std::filesystem::remove(tmp,ec); return false;}
  return true;
}
