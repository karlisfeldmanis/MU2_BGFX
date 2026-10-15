// The game's pass (docs/plans/security.md): what a game the launcher started says where an account's
// key goes, an Account's or a Hello's, in place of the website's sign-in token.
//
// The launcher asks the website for a ticket and its secret over HTTPS (POST /api/ticket) and hands
// both to the game. Over the game's line, which is not encrypted, only the ticket goes, a count, and a
// proof: HMAC-SHA256 of the ticket and the count under the secret. The server learns the secret once
// from the website (/internal/ticket, which gives each ticket only once) and from then on checks
// every line's proof itself, each count above the last. Someone who reads the line has a ticket and
// proofs already spent: he cannot open a line of his own, and the 90-day token never crosses it.
#pragma once

#include <cstdint>
#include <string>

namespace mu::net {

struct Pass {
    std::string ticket;  // 32 hex digits: whose pass, said in the open
    uint64_t count = 0;  // this line's, 1 for the first; each line counts one more
    std::string proof;   // 64 hex digits: passProof(secret, ticket, count)
};

bool isTicket(const std::string& ticket);  // 32 hex digits
bool isSecret(const std::string& secret);  // 64 hex digits

// HMAC-SHA256 under `secret` of "mu2 pass <ticket> <count>", in hex.
std::string passProof(const std::string& secret, const std::string& ticket, uint64_t count);
// What a line says for its key: "<ticket>.<count in hex>.<proof>".
std::string sealPass(const std::string& ticket, const std::string& secret, uint64_t count);
// True when `said` has a pass's shape.
bool parsePass(const std::string& said, Pass& out);
// True when the pass's proof is the secret's, compared in time that does not tell how near it came.
bool proven(const Pass& pass, const std::string& secret);

}  // namespace mu::net
