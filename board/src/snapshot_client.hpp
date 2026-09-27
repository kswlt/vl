#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include <curl/curl.h>

class HttpSnapshotClient {
public:
    explicit HttpSnapshotClient(const std::string &url, long timeout_ms = 350) : handle_(curl_easy_init()) {
        if (!handle_) return;
        curl_easy_setopt(handle_, CURLOPT_URL, url.c_str());
        curl_easy_setopt(handle_, CURLOPT_PROXY, "");
        curl_easy_setopt(handle_, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(handle_, CURLOPT_CONNECTTIMEOUT_MS, std::min(150L, timeout_ms));
        curl_easy_setopt(handle_, CURLOPT_TIMEOUT_MS, timeout_ms);
        curl_easy_setopt(handle_, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(handle_, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
        curl_easy_setopt(handle_, CURLOPT_WRITEFUNCTION, write_body);
        curl_easy_setopt(handle_, CURLOPT_USERAGENT, "xt_camera_native_jpeg/2");
    }
    ~HttpSnapshotClient() { if (handle_) curl_easy_cleanup(handle_); }
    HttpSnapshotClient(const HttpSnapshotClient &) = delete;
    HttpSnapshotClient &operator=(const HttpSnapshotClient &) = delete;
    bool get(std::vector<uint8_t> &body) {
        body.clear();
        total_ms = first_byte_ms = connect_ms = 0;
        connections = status = 0;
        if (!handle_) return false;
        curl_easy_setopt(handle_, CURLOPT_WRITEDATA, &body);
        result = curl_easy_perform(handle_);
        double total=0, first=0, connect=0;
        curl_easy_getinfo(handle_, CURLINFO_TOTAL_TIME, &total);
        curl_easy_getinfo(handle_, CURLINFO_STARTTRANSFER_TIME, &first);
        curl_easy_getinfo(handle_, CURLINFO_CONNECT_TIME, &connect);
        curl_easy_getinfo(handle_, CURLINFO_NUM_CONNECTS, &connections);
        curl_easy_getinfo(handle_, CURLINFO_RESPONSE_CODE, &status);
        total_ms=total*1000; first_byte_ms=first*1000; connect_ms=connect*1000;
        bool valid = result == CURLE_OK && status == 200 && body.size() >= 4 &&
            body[0] == 0xff && body[1] == 0xd8 && body[body.size()-2] == 0xff && body.back() == 0xd9;
        if (!valid) body.clear();
        return valid;
    }
    double total_ms=0, first_byte_ms=0, connect_ms=0;
    long connections=0, status=0;
    CURLcode result=CURLE_OK;
private:
    CURL *handle_;
    static size_t write_body(char *data, size_t size, size_t count, void *ctx) {
        auto &body = *static_cast<std::vector<uint8_t> *>(ctx);
        constexpr size_t max = 2*1024*1024;
        if (size && count > (max-body.size())/size) return 0;
        const size_t bytes = size*count;
        try { body.insert(body.end(), data, data+bytes); }
        catch (...) { return 0; }
        return bytes;
    }
};
