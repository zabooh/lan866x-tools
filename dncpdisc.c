/*
 * dncpdisc.c  -  Active DNCP discovery (Dynamic Node Configuration Protocol).
 *                Pure C, standalone (Winsock only, not SOME/IP).
 *
 * Acts as a temporary DNCP server: broadcasts an (empty) Registry to
 * 224.0.0.1:65527; configured clients that do not find themselves in it then
 * send an Announce to 224.0.0.1:65526. These are collected and printed.
 *
 * READ-ONLY: assigns no PLCA ids/IPs, persists nothing. Source: AN1891.
 *  !  Use only when NO other DNCP server is active on the bus.
 *
 * Usage:
 *   lan866x-dncpdisc                  3 rounds, then list
 *   lan866x-dncpdisc --channel 11 --rounds 5 --timeout 4
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#define MAX_NODES  64u
#define MAX_IFS    16u

static const char *stateStr(uint8_t s){
    switch(s){case 0:return "Undefined";case 1:return "Unconfigured";case 2:return "Pre-Configured";
              case 3:return "Configured";default:return "?";}
}
static uint64_t be64(const uint8_t*p){uint64_t v=0;int i;for(i=0;i<8;i++)v=(v<<8)|p[i];return v;}

typedef struct {
    uint8_t  mac[6];
    uint64_t devid;
    uint8_t  ipv6[16];
    uint32_t ipv4;
    uint8_t  persistency;
    uint8_t  state;
    uint8_t  burst;
    uint8_t  proto;
    uint8_t  plca[8];
    uint8_t  plcaCount;
} Node;
static Node g_nodes[MAX_NODES];
static int  g_nodeCount = 0;

static void printNode(const Node *n, int idx)
{
    int i; int allzero = 1;
    printf("\n========================================================\n");
    printf("DNCP node #%d\n", idx);
    printf("========================================================\n");
    printf("  MAC address:      %02X:%02X:%02X:%02X:%02X:%02X\n",
           n->mac[0],n->mac[1],n->mac[2],n->mac[3],n->mac[4],n->mac[5]);
    printf("  Vendor Device-ID: 0x%016llX\n", (unsigned long long)n->devid);
    printf("  IPv4:             %u.%u.%u.%u\n",
           (n->ipv4>>24)&0xFF,(n->ipv4>>16)&0xFF,(n->ipv4>>8)&0xFF,n->ipv4&0xFF);
    printf("  IPv6:             ");
    for(i=0;i<16;i++) if(n->ipv6[i]){allzero=0;break;}
    if (allzero) printf("(not set)\n");
    else { for(i=0;i<16;i+=2) printf("%s%02X%02X", i?":":"", n->ipv6[i], n->ipv6[i+1]); printf("\n"); }
    printf("  State:            %s\n", stateStr(n->state));
    printf("  Persistency:      %s\n", n->persistency?"Persistent":"Non-Persistent");
    printf("  BurstFramesPerTO: %u\n", n->burst);
    printf("  Protocol version: %u\n", n->proto);
    printf("  PLCA node ids:    %u total:", n->plcaCount);
    for (i=0;i<n->plcaCount;i++) printf(" %u", n->plca[i]);
    printf("\n");
}

static void addAnnounce(const uint8_t*b,int len)
{
    const uint8_t *m; Node *n; int j; uint8_t slots;
    if (len < 56) return;
    if (((b[12]<<8)|b[13]) != 0x0200) return;  /* MessageId Announce */
    m = b+18;                                  /* uint64 MacAddress -> lower 6 bytes */
    for (j = 0; j < g_nodeCount; j++) if (memcmp(g_nodes[j].mac,m,6)==0) return; /* dedup */
    if (g_nodeCount >= (int)MAX_NODES) return;
    n = &g_nodes[g_nodeCount];
    memset(n, 0, sizeof(*n));
    memcpy(n->mac,m,6);
    n->devid = be64(b+24);
    memcpy(n->ipv6, b+32, 16);
    n->ipv4 = (b[48]<<24)|(b[49]<<16)|(b[50]<<8)|b[51];
    n->persistency = b[52];
    n->state = b[53];
    n->burst = b[54];
    n->proto = b[10];
    slots = b[55];
    for(j=0;j<slots && (56+j)<len && n->plcaCount<8;j++) n->plca[n->plcaCount++]=b[56+j];
    g_nodeCount++;
    printNode(n, g_nodeCount-1);
}

/* Determine local IPv4 interfaces (network byte order). Returns count. */
static int localIfs(uint32_t *out, int maxOut)
{
    ULONG sz=0; IP_ADAPTER_ADDRESSES *aa, *p; IP_ADAPTER_UNICAST_ADDRESS *u;
    uint8_t *buf; int cnt=0;
    GetAdaptersAddresses(AF_INET,0,NULL,NULL,&sz);
    buf = (uint8_t*)malloc(sz);
    if (!buf) return 0;
    aa = (IP_ADAPTER_ADDRESSES*)buf;
    if (GetAdaptersAddresses(AF_INET,0,NULL,aa,&sz)==NO_ERROR) {
        for (p=aa; p && cnt<maxOut; p=p->Next) {
            if (p->OperStatus!=IfOperStatusUp) continue;
            for (u=p->FirstUnicastAddress; u && cnt<maxOut; u=u->Next) {
                struct sockaddr_in *sin = (struct sockaddr_in*)u->Address.lpSockaddr;
                uint32_t ip = ntohl(sin->sin_addr.s_addr);
                if ((ip>>24)==127) continue;   /* skip loopback */
                out[cnt++] = sin->sin_addr.s_addr;
            }
        }
    }
    free(buf);
    return cnt;
}

int main(int argc,char**argv)
{
    uint8_t channel=11; int rounds=3, timeoutS=4, i, r, t, nIf;
    uint32_t ifs[MAX_IFS];
    SOCKET rx, tx;
    int on=1, ttl=1;
    struct sockaddr_in ra, dst;
    uint8_t reg[19]; uint16_t cnt=1, length;
    uint8_t buf[2048];
    WSADATA w;

    for(i=1;i<argc;i++){
        if(!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")){
            printf("lan866x-dncpdisc - active DNCP discovery (read-only, pure C)\n\n"
                   "USAGE:\n  lan866x-dncpdisc [--channel <0..255>] [--rounds <n>] [--timeout <s>]\n\n"
                   "Broadcasts an empty DNCP Registry (channel default 11) and collects\n"
                   "the nodes' Announces. Assigns NOTHING, persists NOTHING.\n"
                   "Use only when no other DNCP server is active.\n");
            return 0;
        } else if(!strcmp(argv[i],"--channel")&&i+1<argc) channel=(uint8_t)atoi(argv[++i]);
        else if(!strcmp(argv[i],"--rounds")&&i+1<argc) rounds=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--timeout")&&i+1<argc) timeoutS=atoi(argv[++i]);
    }

    if(WSAStartup(MAKEWORD(2,2),&w)){printf("WSAStartup failed\n");return 1;}

    rx=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    setsockopt(rx,SOL_SOCKET,SO_REUSEADDR,(char*)&on,sizeof(on));
    memset(&ra,0,sizeof(ra)); ra.sin_family=AF_INET; ra.sin_addr.s_addr=INADDR_ANY; ra.sin_port=htons(65526);
    if(bind(rx,(struct sockaddr*)&ra,sizeof(ra))==SOCKET_ERROR){printf("bind 65526 failed (%d)\n",WSAGetLastError());return 1;}
    nIf = localIfs(ifs, MAX_IFS);
    for(i=0;i<nIf;i++){ struct ip_mreq mr; memset(&mr,0,sizeof(mr));
        mr.imr_multiaddr.s_addr=inet_addr("224.0.0.1"); mr.imr_interface.s_addr=ifs[i];
        setsockopt(rx,IPPROTO_IP,IP_ADD_MEMBERSHIP,(char*)&mr,sizeof(mr)); }

    tx=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    setsockopt(tx,IPPROTO_IP,IP_MULTICAST_TTL,(char*)&ttl,sizeof(ttl));

    memset(reg,0,sizeof(reg));
    memset(reg+4,0xFF,6);                       /* MacAddress = FF:FF:FF:FF:FF:FF */
    reg[10]=1;                                  /* ProtocolVersion */
    reg[11]=1;                                  /* REQUEST_NO_RESPONSE */
    reg[12]=0x01; reg[13]=0x00;                 /* MessageId 0x0100 (Registry) */
    reg[16]=channel; reg[17]=0; reg[18]=0;      /* EnumChannel, NodeCount=0, NumOfEntries=0 */
    length=sizeof(reg)-2; reg[0]=(uint8_t)(length>>8); reg[1]=(uint8_t)length;

    printf("Active DNCP discovery (channel %u) - broadcasting Registry, collecting Announces ...\n", channel);
    memset(&dst,0,sizeof(dst)); dst.sin_family=AF_INET; dst.sin_addr.s_addr=inet_addr("224.0.0.1"); dst.sin_port=htons(65527);

    for(r=0;r<rounds;r++){
        reg[14]=(uint8_t)(cnt>>8); reg[15]=(uint8_t)cnt; cnt++;
        for(i=0;i<nIf;i++){ setsockopt(tx,IPPROTO_IP,IP_MULTICAST_IF,(char*)&ifs[i],sizeof(ifs[i]));
            sendto(tx,(char*)reg,sizeof(reg),0,(struct sockaddr*)&dst,sizeof(dst)); }
        for(t=0;t<10;t++){
            fd_set fd; struct timeval tv; FD_ZERO(&fd); FD_SET(rx,&fd); tv.tv_sec=0; tv.tv_usec=100000;
            if(select(0,&fd,NULL,NULL,&tv)>0){
                struct sockaddr_in from; int fl=sizeof(from); int n;
                n=recvfrom(rx,(char*)buf,sizeof(buf),0,(struct sockaddr*)&from,&fl);
                if(n>0) addAnnounce(buf,n);
            }
        }
    }
    for(t=0;t<timeoutS*10;t++){
        fd_set fd; struct timeval tv; FD_ZERO(&fd); FD_SET(rx,&fd); tv.tv_sec=0; tv.tv_usec=100000;
        if(select(0,&fd,NULL,NULL,&tv)>0){
            struct sockaddr_in from; int fl=sizeof(from); int n;
            n=recvfrom(rx,(char*)buf,sizeof(buf),0,(struct sockaddr*)&from,&fl);
            if(n>0) addAnnounce(buf,n);
        }
    }

    printf("\n%d DNCP node(s) found.\n", g_nodeCount);
    if(g_nodeCount==0)
        printf("Note: DNCP must be enabled on the nodes; otherwise none will answer.\n");
    closesocket(rx); closesocket(tx); WSACleanup();
    return 0;
}
