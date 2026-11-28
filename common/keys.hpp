#pragma once
// keys shared by the server and the agents (see README "Joining and keys"):
//  - the join key: typed once on each new pc, e.g. "K7QX-3MPA-…"
//  - each pc's own key: 32 random bytes as hex, handed out on join and rotated
// keys are never sent to prove something; the agent signs the server's random nonce
// with hmac-sha256 instead.
#include <string>

// n random bytes as lowercase hex (psa_crypto_init must have run: tls_global_init)
std::string rand_hex(size_t n);
// hmac-sha256(key, msg) as hex
std::string hmac_hex(const std::string& key,const std::string& msg);
// constant-time equality for signatures
bool same_secret(const std::string& a,const std::string& b);

// a new join key: 128 random bits in base32, grouped in 4s ("K7QX-3MPA-…")
std::string make_join_key();
// the canonical form used for signing: upper case, no '-' or spaces
std::string norm_join_key(const std::string& k);

// what is signed (nonce: from the server's hello for this connection)
inline std::string msg_join(const std::string& nonce,const std::string& id) {return "ci-join|"+nonce+"|"+id;}
inline std::string msg_auth(const std::string& nonce,const std::string& id) {return "ci-auth|"+nonce+"|"+id;}
inline std::string msg_rekey(const std::string& nonce,const std::string& id) {return "ci-rekey|"+nonce+"|"+id;}

// writes a secret file: to a temp file, then swapped in (old file replaced), owner-only
// on mac / linux. false when it could not be written.
bool write_secret_file(const std::string& path,const std::string& data);
