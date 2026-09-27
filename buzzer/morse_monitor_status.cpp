#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string>

static volatile sig_atomic_t running = 1;

static void stop(int) { running = 0; }

static std::string read_status() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return {};
    timeval timeout{3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        close(fd);
        return {};
    }
    static const char request[] =
        "GET /api/management/status HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    if (send(fd, request, sizeof(request) - 1, 0) < 0) {
        close(fd);
        return {};
    }
    std::string response;
    char buffer[4096];
    while (response.size() < 65536) {
        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        response.append(buffer, static_cast<size_t>(n));
    }
    close(fd);
    if (response.find("HTTP/1.1 200") == std::string::npos &&
        response.find("HTTP/1.0 200") == std::string::npos) return {};
    auto body = response.find("\r\n\r\n");
    return body == std::string::npos ? std::string{} : response.substr(body + 4);
}

static int healthy(const std::string& json, const char* sensor) {
    auto sensors = json.find("\"sensors\"");
    if (sensors == std::string::npos) return -1;
    auto key = json.find(std::string("\"") + sensor + "\"", sensors);
    if (key == std::string::npos) return -1;
    auto end = json.find('}', key);
    auto flag = json.find("\"healthy\"", key);
    if (end == std::string::npos || flag == std::string::npos || flag > end) return -1;
    auto value = json.find(':', flag);
    if (value == std::string::npos || value > end) return -1;
    ++value;
    while (value < end && json[value] == ' ') ++value;
    if (json.compare(value, 4, "true") == 0) return 1;
    if (json.compare(value, 5, "false") == 0) return 0;
    return -1;
}

static void log_state(const char* event, const std::string& code) {
    FILE* file = fopen("/tmp/mm.log", "a");
    if (!file) return;
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char clock[16]{};
    strftime(clock, sizeof(clock), "%H:%M:%S", &tm);
    fprintf(file, "[%s] %s state=%s\n", event, clock, code.c_str());
    fclose(file);
}

static void alarm(const std::string& code) {
    pid_t child = fork();
    if (child == 0) {
        execl("/userdata/xtapp/buzzer_morse", "buzzer_morse", code.c_str(), nullptr);
        _exit(127);
    }
    if (child > 0) {
        int status = 0;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    }
}

int main() {
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    sleep(20);
    std::string candidate, active;
    int streak = 0;
    std::time_t last_alarm = 0;
    while (running) {
        std::string status = read_status();
        int camera = healthy(status, "camera");
        int lidar = healthy(status, "lidar");
        if (camera < 0 || lidar < 0) { sleep(2); continue; }
        std::string code;
        if (!camera && !lidar) code = "RC";
        else if (!camera) code = "C";
        else if (!lidar) code = "R";
        if (code == candidate) ++streak;
        else { candidate = code; streak = 1; }
        if (code.empty()) {
            if (!active.empty()) log_state("ok", active);
            active.clear();
        } else if (streak >= 3) {
            std::time_t now = std::time(nullptr);
            if (code != active || now - last_alarm >= 20) {
                log_state("alarm", code);
                alarm(code);
                active = code;
                last_alarm = now;
            }
        }
        sleep(2);
    }
    return 0;
}
