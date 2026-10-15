// HTTPS by the system's libcurl: the Mac, and Linux. Windows has https_win.cpp.
//
// On Linux libcurl is opened when it is first wanted, not linked (docs/plans/linux-release.md):
// distros build it apart -- OpenSSL or GnuTLS, symbols versioned or not -- and a program linked
// against Ubuntu's would not start on the others. A machine with no libcurl sends nothing and
// plays on. The Mac links its own, as it always has.
#include "launcher/https.h"

#include <curl/curl.h>

#include <cstdio>
#include <mutex>
#include <type_traits>

#ifdef __linux__
#include <dlfcn.h>
#endif

namespace mu::launcher {

namespace {

// The few calls made, by pointer, so Linux can fill them from dlopen.
struct Curl {
    bool ok = false;
    decltype(&curl_global_init) global_init = nullptr;
    decltype(&curl_easy_init) easy_init = nullptr;
    decltype(&curl_easy_setopt) easy_setopt = nullptr;
    decltype(&curl_easy_perform) easy_perform = nullptr;
    decltype(&curl_easy_getinfo) easy_getinfo = nullptr;
    decltype(&curl_easy_cleanup) easy_cleanup = nullptr;
    decltype(&curl_easy_strerror) easy_strerror = nullptr;
    decltype(&curl_slist_append) slist_append = nullptr;
    decltype(&curl_slist_free_all) slist_free_all = nullptr;
};

const Curl& curl() {
    static const Curl c = [] {
        Curl c;
#ifdef __linux__
        void* lib = nullptr;
        for (const char* name : {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so"}) {
            if ((lib = dlopen(name, RTLD_NOW | RTLD_LOCAL))) break;
        }
        if (!lib) return c;
        const auto take = [lib](auto& to, const char* name) {
            to = reinterpret_cast<std::remove_reference_t<decltype(to)>>(dlsym(lib, name));
            return to != nullptr;
        };
        c.ok = take(c.global_init, "curl_global_init") && take(c.easy_init, "curl_easy_init") &&
               take(c.easy_setopt, "curl_easy_setopt") && take(c.easy_perform, "curl_easy_perform") &&
               take(c.easy_getinfo, "curl_easy_getinfo") && take(c.easy_cleanup, "curl_easy_cleanup") &&
               take(c.easy_strerror, "curl_easy_strerror") && take(c.slist_append, "curl_slist_append") &&
               take(c.slist_free_all, "curl_slist_free_all");
#else
        c.global_init = &curl_global_init;
        c.easy_init = &curl_easy_init;
        c.easy_setopt = &curl_easy_setopt;
        c.easy_perform = &curl_easy_perform;
        c.easy_getinfo = &curl_easy_getinfo;
        c.easy_cleanup = &curl_easy_cleanup;
        c.easy_strerror = &curl_easy_strerror;
        c.slist_append = &curl_slist_append;
        c.slist_free_all = &curl_slist_free_all;
        c.ok = true;
#endif
        if (c.ok) c.global_init(CURL_GLOBAL_DEFAULT);
        return c;
    }();
    return c;
}

// Redirects followed over HTTPS only. CURLOPT_REDIR_PROTOCOLS_STR (10319) is libcurl 7.85's;
// an older one -- Ubuntu 22.04 has 7.81, whose header has not even the name -- takes the bitmask.
void httpsRedirectsOnly(CURL* handle) {
    const Curl& c = curl();
    if (c.easy_setopt(handle, CURLoption(10319), "https") != CURLE_OK) {
        c.easy_setopt(handle, CURLoption(182), long(1 << 1));  // CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS
    }
}

size_t intoString(char* data, size_t size, size_t count, void* to) {
    static_cast<std::string*>(to)->append(data, size * count);
    return size * count;
}

struct Sink {
    std::FILE* file;
    const std::function<void(uint64_t)>* got;
    const std::atomic<bool>* stop;
};

size_t intoFile(char* data, size_t size, size_t count, void* to) {
    Sink* sink = static_cast<Sink*>(to);
    if (sink->stop->load()) return 0;
    const size_t wrote = std::fwrite(data, 1, size * count, sink->file);
    if (*sink->got) (*sink->got)(wrote);
    return wrote;
}

}  // namespace

Reply request(const std::string& method, const std::string& url, const std::string& body,
              const std::vector<std::string>& headers) {
    const Curl& c = curl();
    Reply reply;
    if (!c.ok) {
        reply.error = "no libcurl on this machine";
        return reply;
    }
    CURL* curl = c.easy_init();
    if (!curl) {
        reply.error = "curl_easy_init";
        return reply;
    }
    curl_slist* lines = nullptr;
    for (const std::string& h : headers) lines = c.slist_append(lines, h.c_str());
    c.easy_setopt(curl, CURLOPT_URL, url.c_str());
    c.easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    if (!body.empty() || method == "POST") {
        c.easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        c.easy_setopt(curl, CURLOPT_POSTFIELDSIZE, long(body.size()));
    }
    c.easy_setopt(curl, CURLOPT_HTTPHEADER, lines);
    c.easy_setopt(curl, CURLOPT_WRITEFUNCTION, intoString);
    c.easy_setopt(curl, CURLOPT_WRITEDATA, &reply.body);
    c.easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    c.easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    c.easy_setopt(curl, CURLOPT_USERAGENT, kAgent);
    // A moved file is followed, as WinHTTP does on Windows, and only over HTTPS.
    c.easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    c.easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    httpsRedirectsOnly(curl);
    const CURLcode done = c.easy_perform(curl);
    if (done == CURLE_OK) {
        long status = 0;
        c.easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        reply.status = int(status);
    } else {
        reply.error = c.easy_strerror(done);
    }
    c.slist_free_all(lines);
    c.easy_cleanup(curl);
    return reply;
}

bool download(const std::string& url, const std::filesystem::path& to, bool resume,
              const std::function<void(uint64_t)>& got, const std::atomic<bool>& stop, std::string& error) {
    const Curl& c = curl();
    if (!c.ok) {
        error = "no libcurl on this machine";
        return false;
    }
    std::error_code ec;
    const uint64_t have = resume && std::filesystem::exists(to, ec) ? std::filesystem::file_size(to, ec) : 0;
    std::FILE* file = std::fopen(to.string().c_str(), have > 0 ? "ab" : "wb");
    if (!file) {
        error = "cannot write " + to.string();
        return false;
    }
    CURL* curl = c.easy_init();
    Sink sink{file, &got, &stop};
    c.easy_setopt(curl, CURLOPT_URL, url.c_str());
    c.easy_setopt(curl, CURLOPT_WRITEFUNCTION, intoFile);
    c.easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    c.easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    c.easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    // A line that stops moving for a minute is given up on; the next try resumes.
    c.easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    c.easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    c.easy_setopt(curl, CURLOPT_USERAGENT, kAgent);
    // A moved file is followed, as WinHTTP does on Windows, and only over HTTPS.
    c.easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    c.easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    httpsRedirectsOnly(curl);
    if (have > 0) c.easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, curl_off_t(have));
    const CURLcode done = c.easy_perform(curl);
    long status = 0;
    c.easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    c.easy_cleanup(curl);
    std::fclose(file);
    if (done != CURLE_OK) {
        error = stop.load() ? "stopped" : c.easy_strerror(done);
        return false;
    }
    // A server that ignored the Range and sent the whole file again from 0 appended it after what
    // was there: the hash check after catches that, and the next try starts clean.
    if (have > 0 && status == 200) {
        error = "the server sent the whole file again";
        return false;
    }
    return true;
}

}  // namespace mu::launcher
