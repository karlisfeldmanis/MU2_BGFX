// HTTPS by the system's libcurl: the Mac (and a Linux check). Windows has https_win.cpp.
#include "launcher/https.h"

#include <curl/curl.h>

#include <cstdio>
#include <mutex>

namespace mu::launcher {

namespace {

void once() {
    static std::once_flag flag;
    std::call_once(flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
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
    once();
    Reply reply;
    CURL* curl = curl_easy_init();
    if (!curl) {
        reply.error = "curl_easy_init";
        return reply;
    }
    curl_slist* lines = nullptr;
    for (const std::string& h : headers) lines = curl_slist_append(lines, h.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    if (!body.empty() || method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, long(body.size()));
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, lines);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, intoString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply.body);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kAgent);
    const CURLcode done = curl_easy_perform(curl);
    if (done == CURLE_OK) {
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        reply.status = int(status);
    } else {
        reply.error = curl_easy_strerror(done);
    }
    curl_slist_free_all(lines);
    curl_easy_cleanup(curl);
    return reply;
}

bool download(const std::string& url, const std::filesystem::path& to, bool resume,
              const std::function<void(uint64_t)>& got, const std::atomic<bool>& stop, std::string& error) {
    once();
    std::error_code ec;
    const uint64_t have = resume && std::filesystem::exists(to, ec) ? std::filesystem::file_size(to, ec) : 0;
    std::FILE* file = std::fopen(to.string().c_str(), have > 0 ? "ab" : "wb");
    if (!file) {
        error = "cannot write " + to.string();
        return false;
    }
    CURL* curl = curl_easy_init();
    Sink sink{file, &got, &stop};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, intoFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    // A line that stops moving for a minute is given up on; the next try resumes.
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kAgent);
    if (have > 0) curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, curl_off_t(have));
    const CURLcode done = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    std::fclose(file);
    if (done != CURLE_OK) {
        error = stop.load() ? "stopped" : curl_easy_strerror(done);
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
