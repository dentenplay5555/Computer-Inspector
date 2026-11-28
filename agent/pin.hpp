#pragma once
// the agent trusts exactly one server certificate: the one whose fingerprint matches --pin.
// it is fetched once over tls, checked, and then given to ixwebsocket as the only trusted
// certificate, so every later (re)connection is verified against it.
#include <string>

enum class pin_result {ok,mismatch,unreachable};

// pem: the server certificate when ok; info: what went wrong otherwise
pin_result fetch_pinned_cert(const std::string& host,int port,const std::string& pin,std::string& pem,std::string& info);

// the pin of a certificate file (e.g. the server's server.crt), for --pin-file on the server pc
bool pin_from_cert_file(const std::string& path,std::string& pin,std::string& err);
