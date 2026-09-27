#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>

#pragma comment(lib, "ws2_32.lib")

static void run_case(const char *label, const char *bind_ip) {
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  struct sockaddr_in target = {0};
  struct sockaddr_in local = {0};
  int local_len = sizeof(local);
  if (s == INVALID_SOCKET) { printf("%s: socket ERROR=%d\n", label, WSAGetLastError()); return; }
  if (bind_ip != NULL) {
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = inet_addr(bind_ip);
    if (bind(s, (struct sockaddr *)&local, sizeof(local)) == SOCKET_ERROR) {
      printf("%s: bind ERROR=%d\n", label, WSAGetLastError()); closesocket(s); return;
    }
  }
  target.sin_family = AF_INET;
  target.sin_port = htons(7447);
  inet_pton(AF_INET, "203.0.113.179", &target.sin_addr);
  if (connect(s, (struct sockaddr *)&target, sizeof(target)) == SOCKET_ERROR) {
    printf("%s: connect ERROR=%d\n", label, WSAGetLastError());
  } else if (getsockname(s, (struct sockaddr *)&local, &local_len) == 0) {
    printf("%s: OK local=%s:%u\n", label, inet_ntoa(local.sin_addr), ntohs(local.sin_port));
  }
  closesocket(s);
}

int main(void) {
  WSADATA data;
  if (WSAStartup(MAKEWORD(2,2), &data) != 0) return 1;
  run_case("winsock unbound", NULL);
  run_case("winsock bind ethernet", "203.0.113.201");
  run_case("winsock bind wifi", "198.51.100.134");
  WSACleanup();
  return 0;
}
