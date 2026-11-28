#pragma once
// who may send data: the join key and each pc's own key, kept in agents.json next to the
// executable (owner-only on mac / linux). see README "Joining and keys".
//
// agents.json: {"join_key":"K7QX-…","agents":{"A-01":{"key":"…","since":ms,"pending":"…"}}}
//   key      the pc's current key
//   since    when it was handed out (rotation is due when older than rotate_ms)
//   pending  a new key sent during a rotation; it becomes the key once the agent proves it
//            saved it, and the old key stays valid until then
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

class agent_keys {
public:
  enum class verdict {ok,bad_signature,not_joined,already_joined};

  explicit agent_keys(std::string path);
  // reads agents.json, or starts a new one with a fresh join key. false + err on failure.
  bool load(std::string& err);

  std::string join_key() const;
  std::string new_join_key();        // replaces it; already joined pcs are not affected

  // first connection of a pc: proof = hmac(join key, msg_join(nonce,id)).
  // ok: key is this pc's new key, to send to it once.
  verdict join(const std::string& id,const std::string& nonce,const std::string& proof,std::string& key);
  // every later connection: sig = hmac(pc key, msg_auth(nonce,id)). a signature made with
  // the pending key also counts (the agent saved it but its confirmation got lost).
  verdict auth(const std::string& id,const std::string& nonce,const std::string& sig);

  // true when the pc's key is older than rotate_ms
  bool rotation_due(const std::string& id,int64_t now) const;
  // makes (or repeats) the pending key for a rotation and returns it
  std::string start_rotation(const std::string& id);
  // sig = hmac(pending key, msg_rekey(nonce,id)): the pending key becomes the key
  bool finish_rotation(const std::string& id,const std::string& nonce,const std::string& sig,int64_t now);

  // forgets the pc's key: it must join again with the join key. false when not joined.
  bool revoke(const std::string& id);
  bool joined(const std::string& id) const;

  int64_t rotate_ms=24LL*60*60*1000;

private:
  struct pc {std::string key,pending; int64_t since=0;};
  bool save();                       // call with mu_ held
  std::string path_;
  mutable std::mutex mu_;
  std::string join_;
  std::map<std::string,pc> pcs_;
};
