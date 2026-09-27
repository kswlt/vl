/* vizd v5 - low-overhead browser visualization sidecar
 * /camera.mp4 copies H264 from RTSP into fragmented MP4; no decode/encode.
 * /radar serves an XTGR 160x80 radar grid; /health serves counters.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define VIZD_PORT 8081
#define UDP_PORT 17778
#define MAX_CONN 32
#define MAX_MP4_BOX (8u*1024u*1024u)
#define MAX_PC_FRAME (4u*1024u*1024u)
#define PC_CHUNK 1200u
#define GRID_W 160
#define GRID_H 80
#define GRID_X_MIN 0.0f
#define GRID_X_MAX 2.0f
#define GRID_Y_MIN -1.0f
#define GRID_Y_MAX 1.0f

static const char *RTSP_URL="rtsp://192.0.2.123:554/stream0?username=admin&password=123456";
static const char *FFMPEG="/userdata/xtapp/ffmpeg_fmp4";
static volatile sig_atomic_t g_running=1;
static volatile sig_atomic_t g_listen_fd=-1;
static atomic_uint g_clients=0;
static atomic_ullong g_udp_pkts=0,g_udp_frames=0,g_render_ok=0;
static atomic_ullong g_cam_fragments=0,g_ffmpeg_starts=0;

static int send_all(int fd,const void *data,size_t len){
    const uint8_t *p=data;
    while(len){ssize_t n=send(fd,p,len,MSG_NOSIGNAL);if(n<0&&errno==EINTR)continue;if(n<=0)return -1;p+=n;len-=(size_t)n;}
    return 0;
}
static int send_chunk(int fd,const void *data,size_t len){
    char h[32];int hn=snprintf(h,sizeof(h),"%zx\r\n",len);
    return send_all(fd,h,(size_t)hn)||send_all(fd,data,len)||send_all(fd,"\r\n",2)?-1:0;
}
static int read_full(int fd,void *data,size_t len){
    uint8_t *p=data;
    while(len&&g_running){ssize_t n=read(fd,p,len);if(n<0&&errno==EINTR)continue;if(n<=0)return -1;p+=n;len-=(size_t)n;}
    return len?-1:0;
}
static uint32_t be32(const uint8_t *p){return((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static uint64_t be64(const uint8_t *p){return((uint64_t)be32(p)<<32)|be32(p+4);}

/* Camera init segment plus latest complete keyframe fragment. */
static pthread_mutex_t g_cm=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cc=PTHREAD_COND_INITIALIZER;
static uint8_t *g_init=NULL,*g_frag=NULL;
static size_t g_init_len=0,g_frag_len=0;
static uint64_t g_cam_seq=0,g_cam_epoch=0;

static int append_buf(uint8_t **dst,size_t *len,const void *src,size_t n,size_t limit){
    if(n>limit||*len>limit-n)return -1;uint8_t *p=realloc(*dst,*len+n);if(!p)return -1;
    memcpy(p+*len,src,n);*dst=p;*len+=n;return 0;
}
static void camera_reset(void){
    pthread_mutex_lock(&g_cm);free(g_init);free(g_frag);g_init=g_frag=NULL;g_init_len=g_frag_len=0;g_cam_epoch++;pthread_cond_broadcast(&g_cc);pthread_mutex_unlock(&g_cm);
}
static void publish_init(const uint8_t *p,size_t n){
    pthread_mutex_lock(&g_cm);free(g_init);g_init=malloc(n);if(g_init){memcpy(g_init,p,n);g_init_len=n;}else g_init_len=0;pthread_cond_broadcast(&g_cc);pthread_mutex_unlock(&g_cm);
}
static void publish_fragment(const uint8_t *p,size_t n){
    if(!n)return;pthread_mutex_lock(&g_cm);uint8_t *next=malloc(n);
    if(next){memcpy(next,p,n);free(g_frag);g_frag=next;g_frag_len=n;g_cam_seq++;atomic_fetch_add(&g_cam_fragments,1);pthread_cond_broadcast(&g_cc);}pthread_mutex_unlock(&g_cm);
}
static void *ffmpeg_thread(void *unused){
    (void)unused;
    while(g_running){
        int fds[2];if(pipe(fds)){sleep(2);continue;}pid_t pid=fork();
        if(pid==0){prctl(PR_SET_PDEATHSIG,SIGTERM);dup2(fds[1],STDOUT_FILENO);close(fds[0]);close(fds[1]);
            execl(FFMPEG,"ffmpeg","-hide_banner","-loglevel","error","-rtsp_transport","tcp","-i",RTSP_URL,"-map","0:v:0","-an","-c:v","copy","-movflags","frag_keyframe+empty_moov+default_base_moof","-f","mov","pipe:1",(char*)NULL);_exit(127);}
        close(fds[1]);if(pid<0){close(fds[0]);sleep(2);continue;}atomic_fetch_add(&g_ffmpeg_starts,1);camera_reset();
        uint8_t *init=NULL,*frag=NULL;size_t ilen=0,flen=0;int seen_moof=0;
        while(g_running){
            uint8_t head[16];if(read_full(fds[0],head,8))break;uint64_t boxlen=be32(head);size_t hlen=8;
            if(boxlen==1){if(read_full(fds[0],head+8,8))break;boxlen=be64(head+8);hlen=16;}
            if(boxlen<hlen||boxlen>MAX_MP4_BOX)break;uint8_t *box=malloc((size_t)boxlen);if(!box)break;memcpy(box,head,hlen);
            if(read_full(fds[0],box+hlen,(size_t)boxlen-hlen)){free(box);break;}
            int is_moof=memcmp(box+4,"moof",4)==0;
            if(is_moof){if(seen_moof&&flen)publish_fragment(frag,flen);else if(ilen)publish_init(init,ilen);free(frag);frag=NULL;flen=0;seen_moof=1;}
            int bad=seen_moof?append_buf(&frag,&flen,box,(size_t)boxlen,MAX_MP4_BOX*2u):append_buf(&init,&ilen,box,(size_t)boxlen,MAX_MP4_BOX*2u);free(box);if(bad)break;
        }
        free(init);free(frag);close(fds[0]);kill(pid,SIGTERM);
        for(int i=0;i<10&&waitpid(pid,NULL,WNOHANG)==0;i++)usleep(100000);
        if(waitpid(pid,NULL,WNOHANG)==0){kill(pid,SIGKILL);waitpid(pid,NULL,0);}camera_reset();if(g_running)sleep(2);
    }
    return NULL;
}
static void serve_camera(int fd){
    const char *hdr="HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\nTransfer-Encoding: chunked\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    if(send_all(fd,hdr,strlen(hdr)))return;uint64_t seq=0,epoch=0;int sent_init=0;
    while(g_running){
        uint8_t *copy=NULL;size_t n=0;pthread_mutex_lock(&g_cm);if(!epoch)epoch=g_cam_epoch;
        while(g_running&&epoch==g_cam_epoch&&((!sent_init&&!g_init_len)||(sent_init&&g_cam_seq==seq))){struct timespec ts;clock_gettime(CLOCK_REALTIME,&ts);ts.tv_sec++;pthread_cond_timedwait(&g_cc,&g_cm,&ts);}
        if(!g_running||epoch!=g_cam_epoch){pthread_mutex_unlock(&g_cm);break;}
        if(!sent_init&&g_init_len){n=g_init_len;copy=malloc(n);if(copy)memcpy(copy,g_init,n);sent_init=1;}
        else if(g_frag_len&&g_cam_seq!=seq){n=g_frag_len;copy=malloc(n);if(copy)memcpy(copy,g_frag,n);seq=g_cam_seq;}
        pthread_mutex_unlock(&g_cm);if(!copy)continue;int bad=send_chunk(fd,copy,n);free(copy);if(bad)break;
    }
}

/* Minimal bounds-checked protobuf pointcloud decoder. */
typedef struct{const uint8_t*d;size_t n,pos;int err;}PB;
static uint64_t varint(PB*r){uint64_t v=0;unsigned sh=0;while(r->pos<r->n&&sh<64){uint8_t b=r->d[r->pos++];v|=(uint64_t)(b&0x7f)<<sh;if(!(b&0x80))return v;sh+=7;}r->err=1;return 0;}
static int64_t zig(uint64_t v){return(int64_t)(v>>1)^-(int64_t)(v&1);}
static int skip_field(PB*r,unsigned w){if(w==0){(void)varint(r);return r->err?-1:0;}if(w==1){if(r->n-r->pos<8)return-1;r->pos+=8;return 0;}if(w==5){if(r->n-r->pos<4)return-1;r->pos+=4;return 0;}if(w==2){uint64_t n=varint(r);if(r->err||n>r->n-r->pos)return-1;r->pos+=(size_t)n;return 0;}return-1;}
static pthread_mutex_t g_gm=PTHREAD_MUTEX_INITIALIZER;
static uint8_t g_grid[GRID_W*GRID_H];static uint32_t g_grid_seq=0;static int g_grid_have=0;
static void pc_render(const uint8_t*data,size_t len){
    if(len>4&&data[0]==0x0a&&(data[1]&0x80)){
        PB outer={data,len,0,0};uint64_t tag=varint(&outer),n=varint(&outer);
        if(!outer.err&&(tag>>3)==1&&(tag&7)==2&&n>2&&n<=len-outer.pos&&data[outer.pos]==0x0a){data+=outer.pos;len=(size_t)n;}
    }
    PB r={data,len,0,0};uint8_t grid[GRID_W*GRID_H]={0};int64_t scaler=1000;int have=0;
    while(r.pos<r.n&&!r.err){uint64_t tag=varint(&r);unsigned f=(unsigned)(tag>>3),w=tag&7;if(r.err)break;
        if(f==1&&w==2){uint64_t n=varint(&r);if(r.err||n>r.n-r.pos)break;PB h={r.d+r.pos,(size_t)n,0,0};r.pos+=(size_t)n;
            while(h.pos<h.n&&!h.err){uint64_t t=varint(&h);unsigned hf=t>>3,hw=t&7;if(hw==0){uint64_t v=varint(&h);if(hf==3)scaler=zig(v);}else if(skip_field(&h,hw))h.err=1;}}
        else if(f==2&&w==2){uint64_t n=varint(&r);if(r.err||n>r.n-r.pos)break;PB p={r.d+r.pos,(size_t)n,0,0};r.pos+=(size_t)n;int64_t px=0,py=0;unsigned xy=0;uint32_t intensity=0;
            while(p.pos<p.n&&!p.err){uint64_t t=varint(&p);unsigned pf=t>>3,pw=t&7;if(pw==0){uint64_t v=varint(&p);if(pf==1){px=zig(v);xy|=1;}else if(pf==2){py=zig(v);xy|=2;}else if(pf==4)intensity=(uint32_t)v;}else if(skip_field(&p,pw))p.err=1;}
            if(!p.err&&xy==3&&scaler){float x=(float)px/scaler,y=(float)py/scaler;int gx=(int)((x-GRID_X_MIN)/(GRID_X_MAX-GRID_X_MIN)*GRID_W),gy=(int)((y-GRID_Y_MIN)/(GRID_Y_MAX-GRID_Y_MIN)*GRID_H);if(gx>=0&&gx<GRID_W&&gy>=0&&gy<GRID_H){uint8_t v=intensity>255?255:(uint8_t)intensity;if(v>grid[gy*GRID_W+gx])grid[gy*GRID_W+gx]=v;have=1;}}}
        else if(skip_field(&r,w))break;
    }
    if(have){pthread_mutex_lock(&g_gm);memcpy(g_grid,grid,sizeof(grid));g_grid_seq++;g_grid_have=1;pthread_mutex_unlock(&g_gm);atomic_fetch_add(&g_render_ok,1);}
}
static int seq_newer(uint32_t a,uint32_t b){return(int32_t)(a-b)>0;}
static void*udp_thread(void*unused){
    (void)unused;int fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0)return NULL;int on=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));struct sockaddr_in sa={0};sa.sin_family=AF_INET;sa.sin_port=htons(UDP_PORT);sa.sin_addr.s_addr=htonl(INADDR_ANY);if(bind(fd,(struct sockaddr*)&sa,sizeof(sa))<0){close(fd);return NULL;}struct timeval tv={1,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
    uint8_t pkt[65536],*acc=NULL,*seen=NULL;uint32_t cur=0,total=0,cnt=0,got=0;int active=0;
    while(g_running){ssize_t n=recvfrom(fd,pkt,sizeof(pkt),0,NULL,NULL);if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))continue;if(n<24||memcmp(pkt,"XTPC",4))continue;uint32_t seq,tl,idx,cc,cl;memcpy(&seq,pkt+4,4);memcpy(&tl,pkt+8,4);memcpy(&idx,pkt+12,4);memcpy(&cc,pkt+16,4);memcpy(&cl,pkt+20,4);atomic_fetch_add(&g_udp_pkts,1);
        if(!tl||tl>MAX_PC_FRAME||!cc||cc>4096||idx>=cc||cl!=(uint32_t)n-24||cl>PC_CHUNK)continue;size_t off=(size_t)idx*PC_CHUNK;if(off>=tl||cl>tl-off)continue;
        if(!active||seq_newer(seq,cur)){free(acc);free(seen);acc=calloc(1,tl);seen=calloc(cc,1);if(!acc||!seen){free(acc);free(seen);acc=seen=NULL;active=0;continue;}cur=seq;total=tl;cnt=cc;got=0;active=1;}
        if(!active||seq!=cur||tl!=total||cc!=cnt||seen[idx])continue;memcpy(acc+off,pkt+24,cl);seen[idx]=1;got++;if(got==cnt){atomic_fetch_add(&g_udp_frames,1);pc_render(acc,total);free(acc);free(seen);acc=seen=NULL;active=0;}
    }free(acc);free(seen);close(fd);return NULL;
}
static void serve_radar(int fd){
    uint8_t body[28+GRID_W*GRID_H];uint32_t seq=0;int have;pthread_mutex_lock(&g_gm);have=g_grid_have;if(have){memcpy(body+28,g_grid,sizeof(g_grid));seq=g_grid_seq;}pthread_mutex_unlock(&g_gm);
    if(!have){const char*b="XTGRNODATA";char h[256];int n=snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",strlen(b));send_all(fd,h,n);send_all(fd,b,strlen(b));return;}
    memcpy(body,"XTGR",4);body[4]=GRID_W;body[5]=GRID_H;memcpy(body+6,&seq,4);body[10]=body[11]=0;float ranges[4]={GRID_X_MIN,GRID_X_MAX,GRID_Y_MIN,GRID_Y_MAX};memcpy(body+12,ranges,sizeof(ranges));char h[256];int n=snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",sizeof(body));send_all(fd,h,n);send_all(fd,body,sizeof(body));
}
static void serve_health(int fd){
    char b[256],h[256];unsigned gs;pthread_mutex_lock(&g_gm);gs=g_grid_seq;pthread_mutex_unlock(&g_gm);int bn=snprintf(b,sizeof(b),"ok udp_pkts=%llu udp_frames=%llu render=%llu grid_seq=%u camera_fragments=%llu ffmpeg_starts=%llu clients=%u\n",(unsigned long long)atomic_load(&g_udp_pkts),(unsigned long long)atomic_load(&g_udp_frames),(unsigned long long)atomic_load(&g_render_ok),gs,(unsigned long long)atomic_load(&g_cam_fragments),(unsigned long long)atomic_load(&g_ffmpeg_starts),atomic_load(&g_clients));int hn=snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",bn);send_all(fd,h,hn);send_all(fd,b,bn);
}
static void*conn_thread(void*arg){
    int fd=(int)(intptr_t)arg;struct timeval tv={5,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));char req[1024];ssize_t n=recv(fd,req,sizeof(req)-1,0);
    if(n>0){req[n]=0;if(!strncmp(req,"GET /camera",11))serve_camera(fd);else if(!strncmp(req,"GET /radar",10))serve_radar(fd);else if(!strncmp(req,"GET /health",11))serve_health(fd);else{const char*r="HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";send_all(fd,r,strlen(r));}}close(fd);atomic_fetch_sub(&g_clients,1);return NULL;
}
static void on_signal(int sig){(void)sig;g_running=0;if(g_listen_fd>=0)close(g_listen_fd);}
int main(void){
    signal(SIGPIPE,SIG_IGN);signal(SIGTERM,on_signal);signal(SIGINT,on_signal);pthread_t cam,udp;if(pthread_create(&cam,NULL,ffmpeg_thread,NULL)||pthread_create(&udp,NULL,udp_thread,NULL))return 1;
    int s=socket(AF_INET,SOCK_STREAM,0);if(s<0)return 1;g_listen_fd=s;int on=1;setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));struct sockaddr_in sa={0};sa.sin_family=AF_INET;sa.sin_port=htons(VIZD_PORT);sa.sin_addr.s_addr=htonl(INADDR_ANY);if(bind(s,(struct sockaddr*)&sa,sizeof(sa))||listen(s,MAX_CONN))return 1;
    while(g_running){int fd=accept(s,NULL,NULL);if(fd<0){if(errno==EINTR)continue;usleep(100000);continue;}unsigned old=atomic_load(&g_clients);while(old<MAX_CONN&&!atomic_compare_exchange_weak(&g_clients,&old,old+1)){}if(old>=MAX_CONN){const char*r="HTTP/1.1 503 Busy\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";send_all(fd,r,strlen(r));close(fd);continue;}pthread_t t;if(pthread_create(&t,NULL,conn_thread,(void*)(intptr_t)fd)){close(fd);atomic_fetch_sub(&g_clients,1);}else pthread_detach(t);}
    close(s);return 0;
}
