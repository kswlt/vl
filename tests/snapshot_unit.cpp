#include "../board/src/snapshot_client.hpp"
#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <thread>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

int main() {
    signal(SIGPIPE,SIG_IGN);
    assert(curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK);
    int listener=socket(AF_INET,SOCK_STREAM,0);
    sockaddr_in address{}; address.sin_family=AF_INET; address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(listener,(sockaddr *)&address,sizeof address)==0);
    socklen_t length=sizeof address;
    assert(getsockname(listener,(sockaddr *)&address,&length)==0 && listen(listener,2)==0);
    std::thread server([&] {
        int fd=accept(listener,nullptr,nullptr);
        assert(fd>=0);
        for (int n=0;n<2;++n) {
            char buf[2048]; assert(recv(fd,buf,sizeof buf,0)>0);
            std::string response="HTTP/1.1 200 OK\r\ntransfer-encoding: chunked\r\n\r\n4\r\n";
            response.append("\xff\xd8\xff\xd9",4); response+="\r\n0\r\n\r\n";
            assert(send(fd,response.data(),response.size(),MSG_NOSIGNAL)==ssize_t(response.size()));
        }
        close(fd);
        fd=accept(listener,nullptr,nullptr); assert(fd>=0);
        char buf[2048]; assert(recv(fd,buf,sizeof buf,0)>0);
        std::string response="HTTP/1.1 200 OK\r\ncontent-length: 4\r\nconnection: close\r\n\r\n";
        response.append("\xff\xd8\xff\xd9",4);
        send(fd,response.data(),response.size(),MSG_NOSIGNAL); close(fd);
        fd=accept(listener,nullptr,nullptr); assert(fd>=0);
        assert(recv(fd,buf,sizeof buf,0)>0);
        const char *slow="HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n";
        send(fd,slow,strlen(slow),MSG_NOSIGNAL);
        for(int i=0;i<15;++i) { send(fd,"x",1,MSG_NOSIGNAL); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
        close(fd); close(listener);
    });
    HttpSnapshotClient client("http://127.0.0.1:"+std::to_string(ntohs(address.sin_port))+"/snapshot",120);
    std::vector<uint8_t> jpeg;
    assert(client.get(jpeg) && client.connections==1 && jpeg.size()==4);
    assert(client.get(jpeg) && client.connections==0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(client.get(jpeg) && jpeg.size()==4);
    auto begin=std::chrono::steady_clock::now();
    assert(!client.get(jpeg) && jpeg.empty() && client.result==CURLE_OPERATION_TIMEDOUT);
    double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    assert(elapsed<250);
    server.join();
    printf("PASS: keep-alive reuse, chunked/lowercase headers, Connection:close reconnect, slow drip total deadline %.1fms\n",elapsed);
}
