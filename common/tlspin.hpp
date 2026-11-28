#pragma once
// certificate fingerprints, shared by the server (shows the pin) and the agent (checks it)
#include <cstddef>
#include <string>

// call once at program start, before any tls connection: mbedtls 3.6 needs psa crypto
// ready for tls 1.3, and ixwebsocket does not set it up
bool tls_global_init();
// sha-256 of a certificate's DER bytes, 64 lowercase hex characters
std::string fp_sha256(const unsigned char* der,size_t len);
// what the teacher types on each student pc: the first 128 bits of the fingerprint,
// in 8 groups of 4, e.g. "3fa2-91bc-0d4e-77a1-5c20-e9f3-1b86-44d0"
std::string fp_pin(const std::string& hexfp);
// true when pin (the short form above, the full fingerprint, any case, with or without
// '-' or ':') matches the start of hexfp; needs at least 32 hex characters
bool fp_matches(const std::string& pin,const std::string& hexfp);
