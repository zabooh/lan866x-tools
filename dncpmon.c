/*
 * dncpmon.c  -  Passive DNCP monitor (Dynamic Node Configuration Protocol).
 *               Pure C, standalone (Winsock only, not SOME/IP).
 *
 * Listens on UDP 65526/65527 and decodes DNCP packets (Announce/Registry/...).
 * Passive only; DNCP must be active on the bus. For active enumeration see
 * lan866x-dncpdisc.
 *
 * Usage:
 *   lan866x-dncpmon                 listen forever (Ctrl+C to stop)
 *   lan866x-dncpmon --timeout 30    stop after 30 s without packets
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>

static uint16_t be16(const uint8_t *p){ return (uint16_t)((p[0]<<8)|p[1]); }
static uint64_t be64(const uint8_t *p){ uint64_t v=0; int i; for(i=0;i<8;i++) v=(v<<8)|p[i]; return v; }

static const char *typeStr(uint8_t t){
    switch(t){case 0:return "REQUEST";case 1:return "REQUEST_NO_RESPONSE";case 2:return "RESPONSE";
              case 3:return "ERROR";case 4:return "NOTIFICATION";default:return "?";}
}
static const char *idStr(uint16_t id){
    switch(id){case 0x100:return "Registry";case 0x200:return "Announce";case 0x300:return "StartTDMeasurement";
               case 0x301:return "GetTDMeasurementResult";case 0x400:return "StoreSettings";case 0x401:return "Activate";
               default:return "?";}
}
static const char *stateStr(uint8_t s){
    switch(s){case 0:return "Undefined";case 1:return "Unconfigured";case 2:return "Pre-Configured";
              case 3:return "Configured";default:return "?";}
}
static void mac(const uint8_t *p){ printf("%02X:%02X:%02X:%02X:%02X:%02X",p[0],p[1],p[2],p[3],p[4],p[5]); }

static void decode(const uint8_t *b, int len, const char *src)
{
    uint8_t ver, type; uint16_t id, cnt; int j;
    if (len < 16) return;
    ver  = b[10];
    type = b[11];
    id   = be16(b+12);
    cnt  = be16(b+14);
    printf("\n[%s] %s / %s  (proto v%u, cnt %u, %d B)\n", src, idStr(id), typeStr(type), ver, cnt, len);
    printf("  Header MAC: "); mac(b+4); printf("\n");

    if (id == 0x200 && len >= 56) {                 /* Announce */
        uint8_t slots;
        printf("  Node MAC:   "); mac(b+18); printf("\n");
        printf("  Device-ID:  0x%016llX\n", (unsigned long long)be64(b+24));
        printf("  IPv4:       %u.%u.%u.%u\n", b[48],b[49],b[50],b[51]);
        printf("  Persistency:%s\n", b[52]?"Persistent":"Non-Persistent");
        printf("  State:      %s\n", stateStr(b[53]));
        slots = b[55];
        printf("  PLCA slots: %u  IDs:", slots);
        for (j=0;j<slots && 56+j<len;j++) printf(" %u", b[56+j]);
        printf("\n");
    } else if (id == 0x100 && len >= 19) {          /* Registry */
        int off = 19, n = b[18], i;
        printf("  EnumChannel:%u  NodeCount:%u  Entries:%u\n", b[16], b[17], b[18]);
        for (i=0;i<n;i++){
            uint8_t slots;
            if (off+40 > len) break;
            printf("  - Entry %d MAC ", i); mac(b+off+2);
            printf("  DevID 0x%016llX  IPv4 %u.%u.%u.%u  State %s\n",
                   (unsigned long long)be64(b+off+8), b[off+32],b[off+33],b[off+34],b[off+35],
                   stateStr(b[off+36]));
            slots = b[off+39];
            off += slots + 40;
        }
    }
}

int main(int argc, char **argv)
{
    int timeoutS = 0, i;
    const uint16_t ports[2] = {65526, 65527};
    SOCKET s[2];
    fd_set base;
    uint8_t buf[2048];
    WSADATA w;

    for (i=1;i<argc;i++){
        if (!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")){
            printf("lan866x-dncpmon - passive DNCP monitor (UDP 65526/65527, pure C)\n\n"
                   "USAGE:\n  lan866x-dncpmon [--timeout <sec>]\n\n"
                   "Decodes DNCP packets (Announce/Registry/...) on the bus.\n"
                   "Passive only; DNCP must be active. Ctrl+C to stop.\n");
            return 0;
        } else if (!strcmp(argv[i],"--timeout") && i+1<argc) timeoutS = atoi(argv[++i]);
    }

    if (WSAStartup(MAKEWORD(2,2),&w)!=0){ printf("WSAStartup failed\n"); return 1; }
    FD_ZERO(&base);
    for (i=0;i<2;i++){
        int on=1; struct sockaddr_in a;
        s[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        setsockopt(s[i],SOL_SOCKET,SO_REUSEADDR,(char*)&on,sizeof(on));
        setsockopt(s[i],SOL_SOCKET,SO_BROADCAST,(char*)&on,sizeof(on));
        memset(&a,0,sizeof(a)); a.sin_family=AF_INET; a.sin_addr.s_addr=INADDR_ANY; a.sin_port=htons(ports[i]);
        if (bind(s[i],(struct sockaddr*)&a,sizeof(a))==SOCKET_ERROR)
            printf("bind %u failed (port in use?). Error %d\n", ports[i], WSAGetLastError());
        FD_SET(s[i],&base);
    }
    printf("DNCP monitor running - listening on UDP 65526/65527 ");
    if (timeoutS) printf("(timeout %ds)\n", timeoutS); else printf("(Ctrl+C to stop)\n");

    for (;;){
        fd_set rd = base;
        struct timeval tv; tv.tv_sec = timeoutS?timeoutS:5; tv.tv_usec = 0;
        int r = select(0,&rd,NULL,NULL, timeoutS?&tv:NULL);
        if (r==0){ if(timeoutS){ printf("\nTimeout - no further DNCP packets.\n"); break;} continue; }
        for (i=0;i<2;i++) if (FD_ISSET(s[i],&rd)){
            struct sockaddr_in from; int fl=sizeof(from); int n;
            n = recvfrom(s[i],(char*)buf,sizeof(buf),0,(struct sockaddr*)&from,&fl);
            if (n>0){
                char ip[20]; char srctag[40];
                inet_ntop(AF_INET,&from.sin_addr,ip,sizeof(ip));
                snprintf(srctag,sizeof(srctag),"%s:%u",ip,ports[i]);
                decode(buf,n,srctag);
            }
        }
    }
    for (i=0;i<2;i++) closesocket(s[i]);
    WSACleanup();
    return 0;
}
