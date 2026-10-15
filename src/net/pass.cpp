#include "net/pass.h"

#include <cstdio>
#include <cstring>

#include "launcher/sha256.h"

namespace mu::net {

namespace {

bool hexOf(const std::string& s, size_t size) {
    if (s.size() != size) return false;
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

}  // namespace

bool isTicket(const std::string& ticket) { return hexOf(ticket, 32); }
bool isSecret(const std::string& secret) { return hexOf(secret, 64); }

std::string passProof(const std::string& secret, const std::string& ticket, uint64_t count) {
    // RFC 2104 with a 64-byte block; the secret is 64 letters, used as they are.
    uint8_t key[64] = {};
    if (secret.size() > sizeof key) {
        launcher::Sha256 h;
        h.add(secret.data(), secret.size());
        h.digest(key);
    } else {
        std::memcpy(key, secret.data(), secret.size());
    }
    uint8_t inner[64], outer[64];
    for (int i = 0; i < 64; ++i) {
        inner[i] = uint8_t(key[i] ^ 0x36);
        outer[i] = uint8_t(key[i] ^ 0x5c);
    }
    const std::string message = "mu2 pass " + ticket + " " + std::to_string(count);
    uint8_t first[32], mac[32];
    launcher::Sha256 in;
    in.add(inner, sizeof inner);
    in.add(message.data(), message.size());
    in.digest(first);
    launcher::Sha256 out;
    out.add(outer, sizeof outer);
    out.add(first, sizeof first);
    out.digest(mac);
    char text[65];
    for (int i = 0; i < 32; ++i) std::snprintf(text + i * 2, 3, "%02x", mac[i]);
    return std::string(text, 64);
}

std::string sealPass(const std::string& ticket, const std::string& secret, uint64_t count) {
    char number[17];
    std::snprintf(number, sizeof number, "%llx", (unsigned long long)count);
    return ticket + "." + number + "." + passProof(secret, ticket, count);
}

bool parsePass(const std::string& said, Pass& out) {
    const size_t first = said.find('.');
    const size_t second = first == std::string::npos ? std::string::npos : said.find('.', first + 1);
    if (second == std::string::npos) return false;
    const std::string ticket = said.substr(0, first);
    const std::string number = said.substr(first + 1, second - first - 1);
    const std::string proof = said.substr(second + 1);
    if (!isTicket(ticket) || !isSecret(proof) || number.empty() || number.size() > 16 ||
        number.find_first_not_of("0123456789abcdef") != std::string::npos) {
        return false;
    }
    out.ticket = ticket;
    out.count = std::strtoull(number.c_str(), nullptr, 16);
    out.proof = proof;
    return out.count > 0;
}

bool proven(const Pass& pass, const std::string& secret) {
    const std::string expected = passProof(secret, pass.ticket, pass.count);
    if (pass.proof.size() != expected.size()) return false;
    unsigned differ = 0;
    for (size_t i = 0; i < expected.size(); ++i) differ |= unsigned(uint8_t(pass.proof[i] ^ expected[i]));
    return differ == 0;
}

}  // namespace mu::net
