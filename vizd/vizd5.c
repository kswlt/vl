/* vizd - self-contained browser visualization on the RK board.
 * The browser uses fragmented MP4 carrying the camera's H.264 bitstream
 * without a JPEG worker. Radar points are rendered from the optional mirror.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
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
#ifndef VIZD_NO_CURL
#include <curl/curl.h>
#endif

#ifndef VIZD_PORT
#define VIZD_PORT 8081
#endif
#ifndef UDP_PORT
#define UDP_PORT 17778
#endif
#define MAX_CONN 32
#define MAX_MP4_BOX (8u*1024u*1024u)
#define MAX_PC_FRAME (4u*1024u*1024u)
#define PC_CHUNK 1200u
#define GRID_W 160
#define GRID_H 80
#define MAX_POINTS 30000u
#define POINT_STRIDE 16u
#define GRID_X_MIN 0.0f
#define GRID_X_MAX 2.0f
#define GRID_Y_MIN -1.0f
#define GRID_Y_MAX 1.0f

static const char *RTSP_URL="rtsp://192.0.2.123:554/stream0?username=admin&password=123456";
static const char *FFMPEG="/userdata/xtapp/ffmpeg_fmp4";
static const char *SNAPSHOT_URL="http://192.0.2.123/cgi-bin/snapshot.cgi?stream=1&username=admin&password=E10ADC3949BA59ABBE56E057F20F883E";
static volatile sig_atomic_t g_running=1;
static volatile sig_atomic_t g_listen_fd=-1;
static atomic_uint g_clients=0;
static atomic_ullong g_udp_pkts=0,g_udp_frames=0,g_render_ok=0;
static atomic_ullong g_cam_fragments=0,g_ffmpeg_starts=0;
static atomic_ullong g_jpeg_frames=0,g_jpeg_errors=0;
static atomic_int g_camera_visualization=1,g_radar_visualization=1;
static atomic_int g_camera_copy_pid=0,g_camera_jpeg_pid=0;
#ifndef VIZD_CONFIG_PATH
#define VIZD_CONFIG_PATH "/userdata/xtapp/visualization.conf"
#endif
static const char *VISUALIZATION_CONFIG=VIZD_CONFIG_PATH;

static void load_visualization_config(void){
    FILE*f=fopen(VISUALIZATION_CONFIG,"r");if(!f)return;char line[64];
    while(fgets(line,sizeof(line),f)){if(!strncmp(line,"CAMERA=0",8))atomic_store(&g_camera_visualization,0);else if(!strncmp(line,"CAMERA=1",8))atomic_store(&g_camera_visualization,1);else if(!strncmp(line,"RADAR=0",7))atomic_store(&g_radar_visualization,0);else if(!strncmp(line,"RADAR=1",7))atomic_store(&g_radar_visualization,1);}
    fclose(f);
}
static int save_visualization_config(int camera,int radar){
    char tmp[320];int path_len=snprintf(tmp,sizeof(tmp),"%s.new",VISUALIZATION_CONFIG);if(path_len<0||path_len>=(int)sizeof(tmp))return -1;FILE*f=fopen(tmp,"w");if(!f)return -1;
    int ok=fprintf(f,"CAMERA=%d\nRADAR=%d\n",camera,radar)>0&&fflush(f)==0&&fsync(fileno(f))==0;
    if(fclose(f)!=0)ok=0;if(!ok||rename(tmp,VISUALIZATION_CONFIG)!=0){unlink(tmp);return -1;}return 0;
}

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

/* Camera init segment, joinable GOP, and a short ordered fragment history. */
#define CAMERA_RING_SIZE 256u
typedef struct {uint8_t *data;size_t len;uint64_t seq;} CameraFragment;
static pthread_mutex_t g_cm=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cc=PTHREAD_COND_INITIALIZER;
static uint8_t *g_init=NULL,*g_gop=NULL;
static size_t g_init_len=0,g_gop_len=0;
static CameraFragment g_camera_ring[CAMERA_RING_SIZE];
static uint64_t g_cam_seq=0,g_cam_epoch=0;

static int append_buf(uint8_t **dst,size_t *len,const void *src,size_t n,size_t limit){
    if(n>limit||*len>limit-n)return -1;uint8_t *p=realloc(*dst,*len+n);if(!p)return -1;
    memcpy(p+*len,src,n);*dst=p;*len+=n;return 0;
}
static void camera_reset(void){
    pthread_mutex_lock(&g_cm);free(g_init);free(g_gop);g_init=g_gop=NULL;g_init_len=g_gop_len=0;
    for(size_t i=0;i<CAMERA_RING_SIZE;i++){free(g_camera_ring[i].data);g_camera_ring[i]=(CameraFragment){0};}
    g_cam_epoch++;pthread_cond_broadcast(&g_cc);pthread_mutex_unlock(&g_cm);
}
static void publish_init(const uint8_t *p,size_t n){
    pthread_mutex_lock(&g_cm);free(g_init);g_init=malloc(n);if(g_init){memcpy(g_init,p,n);g_init_len=n;}else g_init_len=0;pthread_cond_broadcast(&g_cc);pthread_mutex_unlock(&g_cm);
}
/* The camera's MP4 samples use four-byte AVCC NAL lengths. An IDR starts a
 * independently decodable group for clients joining between keyframes. */
static int fragment_has_idr(const uint8_t *p,size_t n){
    size_t off=0;
    while(off+8<=n){
        uint64_t boxlen=be32(p+off);size_t head=8;
        if(boxlen==1){if(off+16>n)return 0;boxlen=be64(p+off+8);head=16;}
        if(boxlen<head||boxlen>n-off)return 0;
        if(!memcmp(p+off+4,"mdat",4)){
            size_t pos=off+head,end=off+(size_t)boxlen;
            while(pos+4<=end){uint32_t nal=be32(p+pos);pos+=4;
                if(!nal||nal>end-pos)break;
                if((p[pos]&31)==5)return 1;
                pos+=nal;
            }
        }
        off+=(size_t)boxlen;
    }
    return 0;
}
static void publish_fragment(const uint8_t *p,size_t n){
    if(!n)return;int idr=fragment_has_idr(p,n);pthread_mutex_lock(&g_cm);
    uint8_t *next=malloc(n);
    if(next){
        memcpy(next,p,n);uint64_t next_seq=g_cam_seq+1;
        CameraFragment *slot=&g_camera_ring[next_seq%CAMERA_RING_SIZE];
        free(slot->data);*slot=(CameraFragment){next,n,next_seq};
        if(idr){free(g_gop);g_gop=NULL;g_gop_len=0;}
        if(idr||g_gop_len){
            if(append_buf(&g_gop,&g_gop_len,p,n,MAX_MP4_BOX*2u)){
                free(g_gop);g_gop=NULL;g_gop_len=0;
            }
        }
        g_cam_seq=next_seq;atomic_fetch_add(&g_cam_fragments,1);pthread_cond_broadcast(&g_cc);
    }
    pthread_mutex_unlock(&g_cm);
}
static void *ffmpeg_thread(void *unused){
    (void)unused;
    while(g_running){
        if(!atomic_load(&g_camera_visualization)){usleep(200000);continue;}
        int fds[2];if(pipe(fds)){sleep(2);continue;}pid_t pid=fork();
        if(pid==0){prctl(PR_SET_PDEATHSIG,SIGTERM);dup2(fds[1],STDOUT_FILENO);close(fds[0]);close(fds[1]);
            /* This FFmpeg build only has the MOV muxer. Override its default
             * QuickTime brand so the output is accepted as ISO MP4 by browsers. */
            execl(FFMPEG,"ffmpeg","-hide_banner","-loglevel","error","-rtsp_transport","tcp","-i",RTSP_URL,"-map","0:v:0","-an","-c:v","copy","-movflags","frag_every_frame+empty_moov+default_base_moof","-brand","isom","-f","mov","pipe:1",(char*)NULL);_exit(127);}
        close(fds[1]);if(pid<0){close(fds[0]);sleep(2);continue;}atomic_store(&g_camera_copy_pid,pid);atomic_fetch_add(&g_ffmpeg_starts,1);camera_reset();
        uint8_t *init=NULL,*frag=NULL;size_t ilen=0,flen=0;int seen_moof=0;
        while(g_running&&atomic_load(&g_camera_visualization)){
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
        if(waitpid(pid,NULL,WNOHANG)==0){kill(pid,SIGKILL);waitpid(pid,NULL,0);}atomic_store(&g_camera_copy_pid,0);camera_reset();if(g_running&&atomic_load(&g_camera_visualization))sleep(2);
    }
    return NULL;
}
static void serve_camera(int fd){
    const char *hdr="HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\nTransfer-Encoding: chunked\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    if(send_all(fd,hdr,strlen(hdr)))return;uint64_t seq=0,epoch=0;int sent_init=0,sent_gop=0;
    while(g_running&&atomic_load(&g_camera_visualization)){
        uint8_t *copy=NULL;size_t n=0;int overrun=0;pthread_mutex_lock(&g_cm);if(!epoch)epoch=g_cam_epoch;
        while(g_running&&epoch==g_cam_epoch&&((!sent_init&&!g_init_len)||(sent_init&&!sent_gop&&!g_gop_len)||(sent_gop&&g_cam_seq==seq))){struct timespec ts;clock_gettime(CLOCK_REALTIME,&ts);ts.tv_sec++;pthread_cond_timedwait(&g_cc,&g_cm,&ts);}
        if(!g_running||epoch!=g_cam_epoch){pthread_mutex_unlock(&g_cm);break;}
        if(!sent_init&&g_init_len){n=g_init_len;copy=malloc(n);if(copy)memcpy(copy,g_init,n);sent_init=1;}
        else if(!sent_gop&&g_gop_len){n=g_gop_len;copy=malloc(n);if(copy){memcpy(copy,g_gop,n);seq=g_cam_seq;sent_gop=1;}}
        else if(g_cam_seq!=seq){
            CameraFragment *slot=&g_camera_ring[(seq+1)%CAMERA_RING_SIZE];
            if(slot->seq!=seq+1)overrun=1;
            else {n=slot->len;copy=malloc(n);if(copy){memcpy(copy,slot->data,n);seq++;}}
        }
        pthread_mutex_unlock(&g_cm);if(overrun)break;if(!copy)continue;int bad=send_chunk(fd,copy,n);free(copy);if(bad)break;
    }
}

/* Camera's native JPEG snapshot API avoids browser fragmented-MP4 buffering. */
#ifndef VIZD_NO_CURL
typedef struct {uint8_t *data;size_t len,cap;} JpegBuffer;
#endif
static pthread_mutex_t g_jm=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_jc=PTHREAD_COND_INITIALIZER;
static uint8_t *g_jpeg=NULL;static size_t g_jpeg_len=0;static uint64_t g_jpeg_seq=0;
#ifndef VIZD_NO_CURL
static size_t jpeg_write(char *ptr,size_t size,size_t count,void *userdata){
    JpegBuffer *b=userdata;if(size&&count>(2u*1024u*1024u-b->len)/size)return 0;
    size_t n=size*count;if(b->len+n>b->cap){size_t cap=b->cap?b->cap*2:262144;
        while(cap<b->len+n)cap*=2;uint8_t *p=realloc(b->data,cap);if(!p)return 0;b->data=p;b->cap=cap;}
    memcpy(b->data+b->len,ptr,n);b->len+=n;return n;
}
static void *snapshot_thread(void *unused){
    (void)unused;CURL *curl=curl_easy_init();if(!curl)return NULL;
    curl_easy_setopt(curl,CURLOPT_URL,SNAPSHOT_URL);
    curl_easy_setopt(curl,CURLOPT_PROXY,"");
    curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT_MS,300L);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT_MS,900L);
    curl_easy_setopt(curl,CURLOPT_TCP_KEEPALIVE,1L);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,jpeg_write);
    while(g_running&&atomic_load(&g_camera_visualization)){
        struct timespec begin,end;clock_gettime(CLOCK_MONOTONIC,&begin);
        JpegBuffer b={0};curl_easy_setopt(curl,CURLOPT_WRITEDATA,&b);
        CURLcode result=curl_easy_perform(curl);long status=0;
        curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
        if(result==CURLE_OK&&status==200&&b.len>4&&b.data[0]==0xff&&b.data[1]==0xd8&&b.data[b.len-2]==0xff&&b.data[b.len-1]==0xd9){
            pthread_mutex_lock(&g_jm);free(g_jpeg);g_jpeg=b.data;g_jpeg_len=b.len;g_jpeg_seq++;b.data=NULL;
            pthread_cond_broadcast(&g_jc);pthread_mutex_unlock(&g_jm);atomic_fetch_add(&g_jpeg_frames,1);
        }else atomic_fetch_add(&g_jpeg_errors,1);
        free(b.data);clock_gettime(CLOCK_MONOTONIC,&end);
        long elapsed=(end.tv_sec-begin.tv_sec)*1000000L+(end.tv_nsec-begin.tv_nsec)/1000L;
        if(elapsed<100000L)usleep((unsigned)(100000L-elapsed));
    }
    curl_easy_cleanup(curl);return NULL;
}
static void publish_jpeg_frame(const uint8_t *data,size_t len){
    if(len<4||data[0]!=0xff||data[1]!=0xd8||data[len-2]!=0xff||data[len-1]!=0xd9)return;
    uint8_t *copy=malloc(len);if(!copy)return;memcpy(copy,data,len);
    pthread_mutex_lock(&g_jm);free(g_jpeg);g_jpeg=copy;g_jpeg_len=len;g_jpeg_seq++;
    pthread_cond_broadcast(&g_jc);pthread_mutex_unlock(&g_jm);atomic_fetch_add(&g_jpeg_frames,1);
}
static void *jpeg_ffmpeg_thread(void *unused){
    (void)unused;unsigned failed=0;
    while(g_running){
        if(!atomic_load(&g_camera_visualization)){usleep(200000);continue;}
        int fds[2];if(pipe(fds)){sleep(1);if(++failed>=3)return snapshot_thread(NULL);continue;}
        pid_t pid=fork();
        if(pid==0){prctl(PR_SET_PDEATHSIG,SIGTERM);dup2(fds[1],STDOUT_FILENO);close(fds[0]);close(fds[1]);
            int nullfd=open("/dev/null",O_WRONLY);if(nullfd>=0){dup2(nullfd,STDERR_FILENO);close(nullfd);}
            const char *program="/userdata/xtapp/ffmpeg";
            execl(program,"ffmpeg","-hide_banner","-loglevel","error","-rtsp_transport","tcp","-i",RTSP_URL,
                "-map","0:v:0","-an","-vf","fps=15,scale=960:-2","-c:v","mjpeg","-q:v","4","-threads","4",
                "-f","image2pipe","pipe:1",(char*)NULL);_exit(127);}
        close(fds[1]);if(pid<0){close(fds[0]);sleep(1);if(++failed>=3)return snapshot_thread(NULL);continue;}
        atomic_store(&g_camera_jpeg_pid,pid);JpegBuffer frame={0};int in_frame=0,prev=0;unsigned frames=0;
        uint8_t chunk[32768];
        while(g_running&&atomic_load(&g_camera_visualization)){
            struct pollfd pfd={fds[0],POLLIN,0};int ready=poll(&pfd,1,4000);if(ready<=0||!(pfd.revents&POLLIN))break;
            ssize_t n=read(fds[0],chunk,sizeof(chunk));if(n<=0)break;
            for(ssize_t i=0;i<n;i++){uint8_t c=chunk[i];
                if(!in_frame){if(prev==0xff&&c==0xd8){frame.len=0;in_frame=1;uint8_t soi[2]={0xff,0xd8};if(jpeg_write((char*)soi,1,2,&frame)!=2)in_frame=0;}prev=c;continue;}
                if(jpeg_write((char*)&c,1,1,&frame)!=1){in_frame=0;frame.len=0;prev=c;continue;}
                if(prev==0xff&&c==0xd9){publish_jpeg_frame(frame.data,frame.len);frames++;frame.len=0;in_frame=0;}
                prev=c;
            }
        }
        free(frame.data);close(fds[0]);kill(pid,SIGTERM);
        for(int i=0;i<10&&waitpid(pid,NULL,WNOHANG)==0;i++)usleep(100000);
        if(waitpid(pid,NULL,WNOHANG)==0){kill(pid,SIGKILL);waitpid(pid,NULL,0);}atomic_store(&g_camera_jpeg_pid,0);
        atomic_fetch_add(&g_jpeg_errors,1);failed=frames>=10?0:failed+1;
        if(failed>=3)return snapshot_thread(NULL);
        if(g_running&&atomic_load(&g_camera_visualization))sleep(1);
    }
    return NULL;
}
#endif
static void serve_jpeg(int fd,const char *request){
    if(!atomic_load(&g_camera_visualization)){const char*h="HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";send_all(fd,h,strlen(h));return;}
    uint64_t after=0;const char *p=strstr(request,"after=");if(p)after=strtoull(p+6,NULL,10);
    pthread_mutex_lock(&g_jm);
    if(after&&after==g_jpeg_seq){struct timespec ts;clock_gettime(CLOCK_REALTIME,&ts);ts.tv_sec++;
        while(g_running&&after==g_jpeg_seq&&pthread_cond_timedwait(&g_jc,&g_jm,&ts)==0){} }
    uint64_t seq=g_jpeg_seq;size_t len=g_jpeg_len;uint8_t *copy=NULL;
    if(len){copy=malloc(len);if(copy)memcpy(copy,g_jpeg,len);}pthread_mutex_unlock(&g_jm);
    if(!copy||seq==after){free(copy);const char *h="HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";send_all(fd,h,strlen(h));return;}
    char h[300];int n=snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Expose-Headers: X-Frame-Id\r\nCache-Control: no-store\r\nX-Frame-Id: %llu\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",(unsigned long long)seq,len);
    if(n>0&&n<(int)sizeof(h)&&!send_all(fd,h,(size_t)n))send_all(fd,copy,len);free(copy);
}

/* Minimal bounds-checked protobuf pointcloud decoder. */
typedef struct{const uint8_t*d;size_t n,pos;int err;}PB;
static uint64_t varint(PB*r){uint64_t v=0;unsigned sh=0;while(r->pos<r->n&&sh<64){uint8_t b=r->d[r->pos++];v|=(uint64_t)(b&0x7f)<<sh;if(!(b&0x80))return v;sh+=7;}r->err=1;return 0;}
static int64_t zig(uint64_t v){return(int64_t)(v>>1)^-(int64_t)(v&1);}
static int skip_field(PB*r,unsigned w){if(w==0){(void)varint(r);return r->err?-1:0;}if(w==1){if(r->n-r->pos<8)return-1;r->pos+=8;return 0;}if(w==5){if(r->n-r->pos<4)return-1;r->pos+=4;return 0;}if(w==2){uint64_t n=varint(r);if(r->err||n>r->n-r->pos)return-1;r->pos+=(size_t)n;return 0;}return-1;}
static pthread_mutex_t g_gm=PTHREAD_MUTEX_INITIALIZER;
static uint8_t g_grid[GRID_W*GRID_H];static uint32_t g_grid_seq=0;static int g_grid_have=0;
typedef struct {float x,y,z;uint32_t intensity;} CloudPoint;
_Static_assert(sizeof(CloudPoint)==POINT_STRIDE,"point stride");
static CloudPoint g_cloud[MAX_POINTS];static uint32_t g_cloud_count=0,g_cloud_seq=0;
static void pc_render(const uint8_t*data,size_t len){
    if(len>4&&data[0]==0x0a&&(data[1]&0x80)){
        PB outer={data,len,0,0};uint64_t tag=varint(&outer),n=varint(&outer);
        if(!outer.err&&(tag>>3)==1&&(tag&7)==2&&n>2&&n<=len-outer.pos&&data[outer.pos]==0x0a){data+=outer.pos;len=(size_t)n;}
    }
    PB r={data,len,0,0};uint8_t grid[GRID_W*GRID_H]={0};CloudPoint cloud[MAX_POINTS];uint32_t cloud_count=0;int64_t scaler=1000;int have=0;
    while(r.pos<r.n&&!r.err){uint64_t tag=varint(&r);unsigned f=(unsigned)(tag>>3),w=tag&7;if(r.err)break;
        if(f==1&&w==2){uint64_t n=varint(&r);if(r.err||n>r.n-r.pos)break;PB h={r.d+r.pos,(size_t)n,0,0};r.pos+=(size_t)n;
            while(h.pos<h.n&&!h.err){uint64_t t=varint(&h);unsigned hf=t>>3,hw=t&7;if(hw==0){uint64_t v=varint(&h);if(hf==3)scaler=zig(v);}else if(skip_field(&h,hw))h.err=1;}}
        else if(f==2&&w==2){uint64_t n=varint(&r);if(r.err||n>r.n-r.pos)break;PB p={r.d+r.pos,(size_t)n,0,0};r.pos+=(size_t)n;int64_t px=0,py=0,pz=0;unsigned xy=0;uint32_t intensity=0;
            while(p.pos<p.n&&!p.err){uint64_t t=varint(&p);unsigned pf=t>>3,pw=t&7;if(pw==0){uint64_t v=varint(&p);if(pf==1){px=zig(v);xy|=1;}else if(pf==2){py=zig(v);xy|=2;}else if(pf==3)pz=zig(v);else if(pf==4)intensity=(uint32_t)v;}else if(skip_field(&p,pw))p.err=1;}
            if(!p.err&&xy==3&&scaler){float x=(float)px/scaler,y=(float)py/scaler,z=(float)pz/scaler;
                if(cloud_count<MAX_POINTS)cloud[cloud_count++]=(CloudPoint){x,y,z,intensity&255u};
                int gx=(int)((x-GRID_X_MIN)/(GRID_X_MAX-GRID_X_MIN)*GRID_W),gy=(int)((y-GRID_Y_MIN)/(GRID_Y_MAX-GRID_Y_MIN)*GRID_H);if(gx>=0&&gx<GRID_W&&gy>=0&&gy<GRID_H){uint8_t v=intensity>255?255:(uint8_t)intensity;if(v>grid[gy*GRID_W+gx])grid[gy*GRID_W+gx]=v;have=1;}}}
        else if(skip_field(&r,w))break;
    }
    if(cloud_count){pthread_mutex_lock(&g_gm);memcpy(g_cloud,cloud,cloud_count*sizeof(CloudPoint));g_cloud_count=cloud_count;g_cloud_seq++;
        if(have){memcpy(g_grid,grid,sizeof(grid));g_grid_seq++;g_grid_have=1;}
        pthread_mutex_unlock(&g_gm);atomic_fetch_add(&g_render_ok,1);}
}
static int seq_newer(uint32_t a,uint32_t b){return(int32_t)(a-b)>0;}
static void*udp_thread(void*unused){
    (void)unused;int fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0)return NULL;int on=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));struct sockaddr_in sa={0};sa.sin_family=AF_INET;sa.sin_port=htons(UDP_PORT);sa.sin_addr.s_addr=htonl(INADDR_ANY);if(bind(fd,(struct sockaddr*)&sa,sizeof(sa))<0){close(fd);return NULL;}struct timeval tv={1,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
    uint8_t pkt[65536],*acc=NULL,*seen=NULL;uint32_t cur=0,total=0,cnt=0,got=0;int active=0;
    while(g_running){ssize_t n=recvfrom(fd,pkt,sizeof(pkt),0,NULL,NULL);if(!atomic_load(&g_radar_visualization)){free(acc);free(seen);acc=seen=NULL;active=0;continue;}if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))continue;if(n<24||memcmp(pkt,"XTPC",4))continue;uint32_t seq,tl,idx,cc,cl;memcpy(&seq,pkt+4,4);memcpy(&tl,pkt+8,4);memcpy(&idx,pkt+12,4);memcpy(&cc,pkt+16,4);memcpy(&cl,pkt+20,4);atomic_fetch_add(&g_udp_pkts,1);
        if(!tl||tl>MAX_PC_FRAME||!cc||cc>4096||idx>=cc||cl!=(uint32_t)n-24||cl>PC_CHUNK)continue;size_t off=(size_t)idx*PC_CHUNK;if(off>=tl||cl>tl-off)continue;
        if(!active||seq_newer(seq,cur)){free(acc);free(seen);acc=calloc(1,tl);seen=calloc(cc,1);if(!acc||!seen){free(acc);free(seen);acc=seen=NULL;active=0;continue;}cur=seq;total=tl;cnt=cc;got=0;active=1;}
        if(!active||seq!=cur||tl!=total||cc!=cnt||seen[idx])continue;memcpy(acc+off,pkt+24,cl);seen[idx]=1;got++;if(got==cnt){atomic_fetch_add(&g_udp_frames,1);pc_render(acc,total);free(acc);free(seen);acc=seen=NULL;active=0;}
    }free(acc);free(seen);close(fd);return NULL;
}
static void serve_radar(int fd){
    if(!atomic_load(&g_radar_visualization)){const char*h="HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";send_all(fd,h,strlen(h));return;}
    uint8_t body[28+GRID_W*GRID_H];uint32_t seq=0;int have;pthread_mutex_lock(&g_gm);have=g_grid_have;if(have){memcpy(body+28,g_grid,sizeof(g_grid));seq=g_grid_seq;}pthread_mutex_unlock(&g_gm);
    if(!have){const char*b="XTGRNODATA";char h[256];int n=snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",strlen(b));send_all(fd,h,n);send_all(fd,b,strlen(b));return;}
    memcpy(body,"XTGR",4);body[4]=GRID_W;body[5]=GRID_H;memcpy(body+6,&seq,4);body[10]=body[11]=0;float ranges[4]={GRID_X_MIN,GRID_X_MAX,GRID_Y_MIN,GRID_Y_MAX};memcpy(body+12,ranges,sizeof(ranges));char h[256];int n=snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",sizeof(body));send_all(fd,h,n);send_all(fd,body,sizeof(body));
}
static void serve_cloud(int fd){
    if(!atomic_load(&g_radar_visualization)){const char*h="HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";send_all(fd,h,strlen(h));return;}
    uint32_t count,seq;CloudPoint *copy=NULL;
    pthread_mutex_lock(&g_gm);count=g_cloud_count;seq=g_cloud_seq;
    if(count){copy=malloc(count*sizeof(CloudPoint));if(copy)memcpy(copy,g_cloud,count*sizeof(CloudPoint));}
    pthread_mutex_unlock(&g_gm);
    if(!copy){const char *h="HTTP/1.1 503 Service Unavailable\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";send_all(fd,h,strlen(h));return;}
    size_t len=count*sizeof(CloudPoint);char h[300];int n=snprintf(h,sizeof(h),
        "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Expose-Headers: X-Point-Count, X-Point-Stride, X-Cloud-Seq\r\nCache-Control: no-store\r\nX-Point-Count: %u\r\nX-Point-Stride: %u\r\nX-Cloud-Seq: %u\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        count,POINT_STRIDE,seq,len);
    if(n>0&&n<(int)sizeof(h)&&!send_all(fd,h,(size_t)n))send_all(fd,copy,len);free(copy);
}
static void serve_health(int fd){
    char b[420],h[256];unsigned gs,cs,cn;pthread_mutex_lock(&g_gm);gs=g_grid_seq;cs=g_cloud_seq;cn=g_cloud_count;pthread_mutex_unlock(&g_gm);int bn=snprintf(b,sizeof(b),"ok udp_pkts=%llu udp_frames=%llu render=%llu grid_seq=%u cloud_seq=%u cloud_points=%u jpeg_frames=%llu jpeg_errors=%llu camera_fragments=%llu ffmpeg_starts=%llu clients=%u camera_visualization=%d radar_visualization=%d\n",(unsigned long long)atomic_load(&g_udp_pkts),(unsigned long long)atomic_load(&g_udp_frames),(unsigned long long)atomic_load(&g_render_ok),gs,cs,cn,(unsigned long long)atomic_load(&g_jpeg_frames),(unsigned long long)atomic_load(&g_jpeg_errors),(unsigned long long)atomic_load(&g_cam_fragments),(unsigned long long)atomic_load(&g_ffmpeg_starts),atomic_load(&g_clients),atomic_load(&g_camera_visualization),atomic_load(&g_radar_visualization));int hn=snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",bn);send_all(fd,h,hn);send_all(fd,b,bn);
}
static pthread_mutex_t g_system_mutex=PTHREAD_MUTEX_INITIALIZER;
static unsigned long long g_previous_cpu_total=0,g_previous_cpu_idle=0;
static void serve_system(int fd){
    char cpu_text[32]="null",temperature_text[32]="null";
    unsigned long long user=0,nice=0,system=0,idle=0,iowait=0,irq=0,softirq=0,steal=0;
    FILE*f=fopen("/proc/stat","r");
    if(f){char label[16];if(fscanf(f,"%15s %llu %llu %llu %llu %llu %llu %llu %llu",label,&user,&nice,&system,&idle,&iowait,&irq,&softirq,&steal)==9&&!strcmp(label,"cpu")){
        unsigned long long total=user+nice+system+idle+iowait+irq+softirq+steal,idle_all=idle+iowait;
        pthread_mutex_lock(&g_system_mutex);
        if(g_previous_cpu_total&&total>g_previous_cpu_total&&idle_all>=g_previous_cpu_idle){
            double pct=100.0*(double)((total-g_previous_cpu_total)-(idle_all-g_previous_cpu_idle))/(double)(total-g_previous_cpu_total);
            if(pct<0)pct=0;if(pct>100)pct=100;snprintf(cpu_text,sizeof(cpu_text),"%.1f",pct);
        }
        g_previous_cpu_total=total;g_previous_cpu_idle=idle_all;
        pthread_mutex_unlock(&g_system_mutex);
    }fclose(f);}
    unsigned long long memory_total=0,memory_available=0;
    f=fopen("/proc/meminfo","r");
    if(f){char line[128];while(fgets(line,sizeof(line),f)){
        if(sscanf(line,"MemTotal: %llu kB",&memory_total)==1)continue;
        if(sscanf(line,"MemAvailable: %llu kB",&memory_available)==1)continue;
    }fclose(f);}
    double hottest=-1000;
    for(int zone=0;zone<16;zone++){char path[96];snprintf(path,sizeof(path),"/sys/class/thermal/thermal_zone%d/temp",zone);f=fopen(path,"r");if(!f)continue;double value=0;int valid=fscanf(f,"%lf",&value)==1;fclose(f);if(!valid)continue;if(value>1000)value/=1000.0;if(value>-20&&value<150&&value>hottest)hottest=value;}
    if(hottest>-1000)snprintf(temperature_text,sizeof(temperature_text),"%.1f",hottest);
    double memory_pct=memory_total?100.0*(double)(memory_total-memory_available)/(double)memory_total:0;
    char body[180],header[256];int n=snprintf(body,sizeof(body),"{\"cpu_usage_pct\":%s,\"temperature_c\":%s,\"memory_used_pct\":%.1f}\n",cpu_text,temperature_text,memory_pct);
    int h=snprintf(header,sizeof(header),"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",n);
    send_all(fd,header,(size_t)h);send_all(fd,body,(size_t)n);
}
static int requested_switch(const char *request,const char *key,int current){
    char token[32];snprintf(token,sizeof(token),"%s=",key);const char*p=strstr(request,token);if(!p)return current;p+=strlen(token);
    return (*p=='1'||!strncmp(p,"true",4)||!strncmp(p,"on",2))?1:0;
}
static void serve_visualization_control(int fd,const char *request,int update){
    if(update){
        int camera=requested_switch(request,"camera",atomic_load(&g_camera_visualization));
        int radar=requested_switch(request,"radar",atomic_load(&g_radar_visualization));
        int previous_camera=atomic_load(&g_camera_visualization), previous_radar=atomic_load(&g_radar_visualization);
        if(save_visualization_config(camera,radar)!=0){const char*h="HTTP/1.1 500 Internal Server Error\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";send_all(fd,h,strlen(h));return;}
        atomic_store(&g_camera_visualization,camera);atomic_store(&g_radar_visualization,radar);
        if(previous_camera&&!camera){int p=atomic_load(&g_camera_copy_pid);if(p>0)kill(p,SIGTERM);p=atomic_load(&g_camera_jpeg_pid);if(p>0)kill(p,SIGTERM);camera_reset();pthread_mutex_lock(&g_jm);free(g_jpeg);g_jpeg=NULL;g_jpeg_len=0;pthread_cond_broadcast(&g_jc);pthread_mutex_unlock(&g_jm);}
        if(previous_radar&&!radar){pthread_mutex_lock(&g_gm);g_cloud_count=0;g_grid_have=0;memset(g_grid,0,sizeof(g_grid));pthread_mutex_unlock(&g_gm);}
    }
    char body[96];int n=snprintf(body,sizeof(body),"camera=%d radar=%d\n",atomic_load(&g_camera_visualization),atomic_load(&g_radar_visualization));
    char header[320];int h=snprintf(header,sizeof(header),"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",n);
    send_all(fd,header,(size_t)h);send_all(fd,body,(size_t)n);
}
static void*conn_thread(void*arg){
    int fd=(int)(intptr_t)arg;int no_delay=1;setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&no_delay,sizeof(no_delay));struct timeval tv={5,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));char req[1024];ssize_t n=recv(fd,req,sizeof(req)-1,0);
    if(n>0){req[n]=0;if(!strncmp(req,"GET /camera",11))serve_camera(fd);else if(!strncmp(req,"GET /latest.jpg",15))serve_jpeg(fd,req);else if(!strncmp(req,"GET /pointcloud.bin",19))serve_cloud(fd);else if(!strncmp(req,"GET /radar",10))serve_radar(fd);else if(!strncmp(req,"GET /health",11))serve_health(fd);else if(!strncmp(req,"GET /system",11))serve_system(fd);else if(!strncmp(req,"GET /control",12))serve_visualization_control(fd,req,0);else if(!strncmp(req,"POST /control",13))serve_visualization_control(fd,req,1);else{const char*r="HTTP/1.1 404 Not Found\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";send_all(fd,r,strlen(r));}}close(fd);atomic_fetch_sub(&g_clients,1);return NULL;
}
static void on_signal(int sig){(void)sig;g_running=0;if(g_listen_fd>=0)close(g_listen_fd);}
int main(void){
    signal(SIGPIPE,SIG_IGN);signal(SIGTERM,on_signal);signal(SIGINT,on_signal);
#ifndef VIZD_NO_CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
    load_visualization_config();
    pthread_t cam,udp;if(pthread_create(&cam,NULL,ffmpeg_thread,NULL)||pthread_create(&udp,NULL,udp_thread,NULL))return 1;
    int s=socket(AF_INET,SOCK_STREAM,0);if(s<0)return 1;g_listen_fd=s;int on=1;setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));struct sockaddr_in sa={0};sa.sin_family=AF_INET;sa.sin_port=htons(VIZD_PORT);sa.sin_addr.s_addr=htonl(INADDR_ANY);if(bind(s,(struct sockaddr*)&sa,sizeof(sa))||listen(s,MAX_CONN))return 1;
    while(g_running){int fd=accept(s,NULL,NULL);if(fd<0){if(errno==EINTR)continue;usleep(100000);continue;}unsigned old=atomic_load(&g_clients);while(old<MAX_CONN&&!atomic_compare_exchange_weak(&g_clients,&old,old+1)){}if(old>=MAX_CONN){const char*r="HTTP/1.1 503 Busy\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";send_all(fd,r,strlen(r));close(fd);continue;}pthread_t t;if(pthread_create(&t,NULL,conn_thread,(void*)(intptr_t)fd)){close(fd);atomic_fetch_sub(&g_clients,1);}else pthread_detach(t);}
    close(s);return 0;
}
