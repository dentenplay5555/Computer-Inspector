#include "tlscert.hpp"
#include "common/tlspin.hpp"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <vector>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/oid.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {
// frees every mbedtls object we use, however the function returns
struct ctx {
  mbedtls_entropy_context ent;
  mbedtls_ctr_drbg_context rng;
  mbedtls_pk_context key;
  mbedtls_x509write_cert w;
  mbedtls_x509_crt crt;
  ctx() {mbedtls_entropy_init(&ent); mbedtls_ctr_drbg_init(&rng); mbedtls_pk_init(&key); mbedtls_x509write_crt_init(&w); mbedtls_x509_crt_init(&crt);}
  ~ctx() {mbedtls_x509_crt_free(&crt); mbedtls_x509write_crt_free(&w); mbedtls_pk_free(&key); mbedtls_ctr_drbg_free(&rng); mbedtls_entropy_free(&ent);}
};

bool write_file(const std::string& p,const std::string& data,bool secret) {
  std::ofstream f(p,std::ios::binary|std::ios::trunc);
  f<<data;
  f.close();
  if (!f) return false;
#ifndef _WIN32
  if (secret) chmod(p.c_str(),0600);   // the private key: owner only
#else
  (void)secret;
#endif
  return true;
}

// "YYYYMMDDhhmmss" in UTC: a day ago (years<0, for clocks a little behind) or years from now
std::string stamp(int years) {
  std::time_t t=std::time(nullptr);
  if (years<0) t-=24*60*60;
  std::tm u{};
#ifdef _WIN32
  gmtime_s(&u,&t);
#else
  gmtime_r(&t,&u);
#endif
  if (years>0) {
    u.tm_year+=years;
    if (u.tm_mon==1&&u.tm_mday==29) u.tm_mday=28;   // 29 february may not exist then
  }
  char b[16];
  std::snprintf(b,sizeof b,"%04d%02d%02d%02d%02d%02d",u.tm_year+1900,u.tm_mon+1,u.tm_mday,u.tm_hour,u.tm_min,u.tm_sec);
  return b;
}

bool fingerprint_of(const std::string& crt_path,std::string& fp,std::string& err) {
  ctx c;
  if (mbedtls_x509_crt_parse_file(&c.crt,crt_path.c_str())!=0) {err="cannot read "+crt_path; return false;}
  fp=fp_sha256(c.crt.raw.p,c.crt.raw.len);
  return true;
}
}  // namespace

bool load_or_make_cert(const std::string& dir,tls_id& out,std::string& err) {
  out.cert=dir+"/server.crt";
  out.key=dir+"/server.key";
  std::error_code ec;
  bool have_crt=std::filesystem::exists(out.cert,ec), have_key=std::filesystem::exists(out.key,ec);
  if (have_crt&&have_key) return fingerprint_of(out.cert,out.fp,err);
  if (have_crt!=have_key) {err="only one of server.crt / server.key exists in "+dir+"; remove it to make a new pair"; return false;}

  ctx c;
  const char* pers="computer-inspector-cert";
  if (mbedtls_ctr_drbg_seed(&c.rng,mbedtls_entropy_func,&c.ent,(const unsigned char*)pers,std::strlen(pers))!=0) {err="random number setup failed"; return false;}
  if (mbedtls_pk_setup(&c.key,mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY))!=0||
      mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1,mbedtls_pk_ec(c.key),mbedtls_ctr_drbg_random,&c.rng)!=0) {err="key generation failed"; return false;}

  unsigned char serial[16];
  mbedtls_ctr_drbg_random(&c.rng,serial,sizeof serial);
  serial[0]&=0x7f;   // positive
  mbedtls_x509write_crt_set_version(&c.w,MBEDTLS_X509_CRT_VERSION_3);
  mbedtls_x509write_crt_set_md_alg(&c.w,MBEDTLS_MD_SHA256);
  mbedtls_x509write_crt_set_subject_key(&c.w,&c.key);
  mbedtls_x509write_crt_set_issuer_key(&c.w,&c.key);
  std::string nb=stamp(-1),na=stamp(10);
  // browsers check the address against subjectAltName; agents pin the certificate instead
  unsigned char lo[4]={127,0,0,1};
  const char* dns="localhost";
  mbedtls_x509_san_list ip{}, name{};
  ip.node.type=MBEDTLS_X509_SAN_IP_ADDRESS;
  ip.node.san.unstructured_name.p=lo; ip.node.san.unstructured_name.len=4;
  name.node.type=MBEDTLS_X509_SAN_DNS_NAME;
  name.node.san.unstructured_name.p=(unsigned char*)dns; name.node.san.unstructured_name.len=std::strlen(dns);
  ip.next=&name;
  if (mbedtls_x509write_crt_set_subject_name(&c.w,"CN=Computer Inspector server,O=Computer Inspector")!=0||
      mbedtls_x509write_crt_set_issuer_name(&c.w,"CN=Computer Inspector server,O=Computer Inspector")!=0||
      mbedtls_x509write_crt_set_serial_raw(&c.w,serial,sizeof serial)!=0||
      mbedtls_x509write_crt_set_validity(&c.w,nb.c_str(),na.c_str())!=0||
      mbedtls_x509write_crt_set_basic_constraints(&c.w,0,-1)!=0||
      mbedtls_x509write_crt_set_key_usage(&c.w,MBEDTLS_X509_KU_DIGITAL_SIGNATURE)!=0||
      mbedtls_x509write_crt_set_subject_alternative_name(&c.w,&ip)!=0) {err="certificate fields could not be set"; return false;}
  mbedtls_asn1_sequence eku{};
  eku.buf.tag=MBEDTLS_ASN1_OID;
  eku.buf.p=(unsigned char*)MBEDTLS_OID_SERVER_AUTH;
  eku.buf.len=MBEDTLS_OID_SIZE(MBEDTLS_OID_SERVER_AUTH);
  if (mbedtls_x509write_crt_set_ext_key_usage(&c.w,&eku)!=0) {err="certificate fields could not be set"; return false;}

  std::vector<unsigned char> pem(8192);
  if (mbedtls_x509write_crt_pem(&c.w,pem.data(),pem.size(),mbedtls_ctr_drbg_random,&c.rng)!=0) {err="certificate could not be written"; return false;}
  std::string crt_pem((const char*)pem.data());
  std::fill(pem.begin(),pem.end(),0);
  if (mbedtls_pk_write_key_pem(&c.key,pem.data(),pem.size())!=0) {err="key could not be written"; return false;}
  std::string key_pem((const char*)pem.data());
  std::fill(pem.begin(),pem.end(),0);

  if (!write_file(out.key,key_pem,true)||!write_file(out.cert,crt_pem,false)) {
    std::filesystem::remove(out.key,ec); std::filesystem::remove(out.cert,ec);
    err="cannot write server.crt / server.key in "+dir;
    return false;
  }
  return fingerprint_of(out.cert,out.fp,err);
}
