#pragma once
// the server's own tls identity: a self-signed certificate made on first start and kept
// next to the executable (server.crt, server.key), so agents can pin it
#include <string>

struct tls_id {
  std::string cert,key;   // file paths
  std::string fp;         // sha-256 of the certificate, hex (see common/tlspin.hpp)
};

// loads dir/server.crt + dir/server.key, or creates them (EC P-256, 10 years) when missing.
// false and err filled when they cannot be read or written.
bool load_or_make_cert(const std::string& dir,tls_id& out,std::string& err);
