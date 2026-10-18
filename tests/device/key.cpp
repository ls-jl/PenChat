// Bench only. Ordinary evdev events; never grab an input device.
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
int main(int argc,char**argv){if(argc!=6)return 2;const char*path=argv[1];int code=atoi(argv[2]),count=atoi(argv[3]),hold=atoi(argv[4]),gap=atoi(argv[5]);if(strncmp(path,"/dev/input/by-path/",19)||(code!=102&&code!=116&&code!=134)||count<1||count>100||hold<10||hold>2000||gap<10||gap>3000)return 2;int fd=open(path,O_WRONLY|O_CLOEXEC);if(fd<0){perror("open input");return 1;}auto send=[&](int type,int c,int value){input_event e{};e.type=type;e.code=c;e.value=value;return write(fd,&e,sizeof(e))==sizeof(e);};for(int i=0;i<count;i++){if(!send(EV_KEY,code,1)||!send(EV_SYN,SYN_REPORT,0)){close(fd);return 1;}usleep(hold*1000);send(EV_KEY,code,0);send(EV_SYN,SYN_REPORT,0);usleep(gap*1000);}close(fd);printf("sent %d paired events code=%d\n",count,code);}
