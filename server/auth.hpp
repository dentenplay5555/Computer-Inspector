#pragma once
// the teacher's login (see README "Teacher login and audit log")
//  - auth.json next to the executable (owner-only on mac / linux):
//      {"pw":{"salt":"…","iter":600000,"hash":"…"}}   pbkdf2-hmac-sha256, never the password
//  - sessions: random tokens in an HttpOnly cookie, kept in memory (a restart logs everyone
//    out); 30 min without use or 12 h in all ends them
//  - wrong passwords: 5 from one address locks it out for 1 min, doubling up to 15 min
//  - audit.log next to the executable: one line per login, change, join, ...
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

class auth {
public:
  explicit auth(std::string dir);
  bool load(std::string& err);

  bool has_password() const;
  // "" when set; else why ("too short", "cannot save")
  std::string set_password(const std::string& pw);
  bool check_password(const std::string& pw) const;     // slow on purpose (pbkdf2)

  // a new session token, or "" with why: "wrong" or "locked" (retry_s: seconds to wait)
  std::string login(const std::string& ip,const std::string& pw,std::string& why,int& retry_s);
  std::string new_session();                            // after setting the first password
  bool session(const std::string& token,int64_t now);   // valid? (and counts as use)
  void logout(const std::string& token);
  void logout_others(const std::string& keep);          // after a password change
  int active(int64_t now,int64_t within_ms) const;      // dashboards seen recently

  // one line in audit.log: time, address, what happened
  void audit(const std::string& ip,const std::string& what);

  static constexpr int64_t IDLE_MS=30LL*60*1000, MAX_MS=12LL*60*60*1000;

private:
  struct sess {int64_t created=0,seen=0;};
  struct fails {int n=0; int64_t until=0, lock_ms=0;};
  bool save();                                          // call with mu_ held
  std::string dir_;
  mutable std::mutex mu_;
  std::string salt_,hash_;
  int iter_=0;
  std::map<std::string,sess> sess_;                     // by sha-256 of the token
  std::map<std::string,fails> fail_;                    // by address
  std::mutex log_;
};
