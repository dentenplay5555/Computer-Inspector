#pragma once
// this pc's own key, given by the server when it joined and replaced at every rotation.
// kept in agent.key next to the agent (owner-only on mac / linux):
//   {"pin":"3fa2-…","id":"A-01","key":"…"}
// the key only counts for the server with that pin.
#include <string>

struct stored_key {std::string pin,id,key;};

// false when the file is missing or damaged
bool load_key(const std::string& path,stored_key& k);
// swaps the file in whole (temp file, then rename), so a crash never leaves half a key
bool save_key(const std::string& path,const stored_key& k);
// the same pin, ignoring case and separators
bool same_pin(const std::string& a,const std::string& b);
