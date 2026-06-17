/*
 * dncpdisc.cpp  -  Aktive DNCP-Discovery (Dynamic Node Configuration Protocol).
 *
 * Agiert als temporaerer DNCP-Server: broadcastet eine (leere) Registry an
 * 224.0.0.1:65527; konfigurierte Clients, die sich darin nicht finden, senden
 * daraufhin ein Announce an 224.0.0.1:65526. Diese werden gesammelt und als
 * Knotenliste ausgegeben (MAC, Device-ID, IPv4/IPv6, State, PLCA-IDs).
 *
 * NUR LESEND: es werden KEINE PLCA-IDs/IPs zugewiesen, nichts persistiert
 * (kein Assign/StoreSettings/Activate). Quelle: AN1891 (DNCP-Spec V1.0.1).
 *
 *  ⚠  Sendet aktiv eine DNCP-Registry. Nur verwenden, wenn KEIN anderer
 *     DNCP-Server am Bus aktiv ist. EnumChannel = Default (11), damit der
 *     Enumeration-Channel der Knoten nicht veraendert wird.
 *
 * Aufruf:
 *   lan866x-dncpdisc                  3 Runden, dann auflisten
 *   lan866x-dncpdisc --channel 11 --rounds 5 --timeout 4
 */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

static const char *stateStr(uint8_t s){
    switch(s){case 0:return "Undefined";case 1:return "Unconfigured";case 2:return "Pre-Configured";
              case 3:return "Configured";default:return "?";}
}
static uint64_t be64(const uint8_t*p){uint64_t v=0;for(int i=0;i<8;i++)v=(v<<8)|p[i];return v;}

struct Node {
    uint8_t  mac[6];
    uint64_t devid;
    uint8_t  ipv6[16];
    uint32_t ipv4;
    uint8_t  persistency;
    uint8_t  state;
    uint8_t  burst;
    uint8_t  proto;
    std::vector<uint8_t> plca;
};
static std::vector<Node> g_nodes;

/* Endpoint-Typ aus der Vendor-Device-ID/heuristisch (best effort). */
static void printNode(const Node &n, int idx)
{
    printf("\n========================================================\n");
    printf("DNCP-Knoten #%d\n", idx);
    printf("========================================================\n");
    printf("  MAC-Adresse:      %02X:%02X:%02X:%02X:%02X:%02X\n",
           n.mac[0],n.mac[1],n.mac[2],n.mac[3],n.mac[4],n.mac[5]);
    printf("  Vendor Device-ID: 0x%016llX\n", (unsigned long long)n.devid);
    printf("  IPv4:             %u.%u.%u.%u\n",
           (n.ipv4>>24)&0xFF,(n.ipv4>>16)&0xFF,(n.ipv4>>8)&0xFF,n.ipv4&0xFF);
    printf("  IPv6:             ");
    bool allzero=true; for(int i=0;i<16;i++) if(n.ipv6[i]){allzero=false;break;}
    if (allzero) printf("(nicht gesetzt)\n");
    else { for(int i=0;i<16;i+=2) printf("%s%02X%02X", i?":":"", n.ipv6[i], n.ipv6[i+1]); printf("\n"); }
    printf("  State:            %s\n", stateStr(n.state));
    printf("  Persistency:      %s\n", n.persistency?"Persistent":"Non-Persistent");
    printf("  BurstFramesPerTO: %u\n", n.burst);
    printf("  Protokoll-Vers.:  %u\n", n.proto);
    printf("  PLCA Node-IDs:    %u Stueck:", (unsigned)n.plca.size());
    for (size_t i=0;i<n.plca.size();i++) printf(" %u", n.plca[i]);
    printf("\n");
}

static void addAnnounce(const uint8_t*b,int len)
{
    if (len < 56) return;
    if (((b[12]<<8)|b[13]) != 0x0200) return;  /* MessageId Announce */
    const uint8_t *m = b+18;              /* uint64 MacAddress -> lower 6 Bytes */
    for (auto &n : g_nodes) if (memcmp(n.mac,m,6)==0) return; /* dedup */
    Node n{};
    memcpy(n.mac,m,6);
    n.devid = be64(b+24);
    memcpy(n.ipv6, b+32, 16);
    n.ipv4 = (b[48]<<24)|(b[49]<<16)|(b[50]<<8)|b[51];
    n.persistency = b[52];
    n.state = b[53];
    n.burst = b[54];
    n.proto = b[10];
    uint8_t slots=b[55]; for(int j=0;j<slots && 56+j<len;j++) n.plca.push_back(b[56+j]);
    g_nodes.push_back(n);
    printNode(n, (int)g_nodes.size()-1);
}

/* Lokale IPv4-Interfaces ermitteln (fuer Multicast-Join/-Send). */
static std::vector<uint32_t> localIfs()
{
    std::vector<uint32_t> v;
    ULONG sz=0; GetAdaptersAddresses(AF_INET,0,nullptr,nullptr,&sz);
    std::vector<uint8_t> buf(sz);
    auto *aa=(IP_ADAPTER_ADDRESSES*)buf.data();
    if (GetAdaptersAddresses(AF_INET,0,nullptr,aa,&sz)==NO_ERROR)
        for (auto *p=aa;p;p=p->Next){
            if (p->OperStatus!=IfOperStatusUp) continue;
            for (auto *u=p->FirstUnicastAddress;u;u=u->Next){
                auto *sin=(sockaddr_in*)u->Address.lpSockaddr;
                uint32_t ip=ntohl(sin->sin_addr.s_addr);
                if ((ip>>24)==127) continue;   /* loopback ueberspringen */
                v.push_back(sin->sin_addr.s_addr);
            }
        }
    return v;
}

int main(int argc,char**argv)
{
    uint8_t channel=11; int rounds=3, timeoutS=4;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")){
            printf("lan866x-dncpdisc - aktive DNCP-Discovery (read-only)\n\n"
                   "AUFRUF:\n  lan866x-dncpdisc [--channel <0..255>] [--rounds <n>] [--timeout <s>]\n\n"
                   "Broadcastet eine leere DNCP-Registry (Channel default 11) und sammelt\n"
                   "die Announces der Knoten. Weist NICHTS zu, persistiert NICHTS.\n"
                   "Nur nutzen, wenn kein anderer DNCP-Server aktiv ist.\n");
            return 0;
        } else if(!strcmp(argv[i],"--channel")&&i+1<argc) channel=(uint8_t)atoi(argv[++i]);
        else if(!strcmp(argv[i],"--rounds")&&i+1<argc) rounds=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--timeout")&&i+1<argc) timeoutS=atoi(argv[++i]);
    }

    WSADATA w; if(WSAStartup(MAKEWORD(2,2),&w)){printf("WSAStartup failed\n");return 1;}

    /* RX-Socket: 65526 (Server-Port), Multicast 224.0.0.1 auf allen Interfaces joinen */
    SOCKET rx=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    int on=1; setsockopt(rx,SOL_SOCKET,SO_REUSEADDR,(char*)&on,sizeof(on));
    sockaddr_in ra{}; ra.sin_family=AF_INET; ra.sin_addr.s_addr=INADDR_ANY; ra.sin_port=htons(65526);
    if(bind(rx,(sockaddr*)&ra,sizeof(ra))==SOCKET_ERROR){printf("bind 65526 fehlgeschlagen (%d)\n",WSAGetLastError());return 1;}
    auto ifs=localIfs();
    for(uint32_t ifip:ifs){ ip_mreq mr{}; mr.imr_multiaddr.s_addr=inet_addr("224.0.0.1"); mr.imr_interface.s_addr=ifip;
        setsockopt(rx,IPPROTO_IP,IP_ADD_MEMBERSHIP,(char*)&mr,sizeof(mr)); }

    /* TX-Socket: Registry an 224.0.0.1:65527, TTL 1, je Interface senden */
    SOCKET tx=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    int ttl=1; setsockopt(tx,IPPROTO_IP,IP_MULTICAST_TTL,(char*)&ttl,sizeof(ttl));

    /* Registry (0x0100) REQUEST_NO_RESPONSE, leer */
    uint8_t reg[19]; memset(reg,0,sizeof(reg));
    uint16_t cnt=1;
    reg[2]=0;reg[3]=0; memset(reg+4,0xFF,6);   /* MacAddress = FF:FF:FF:FF:FF:FF (broadcast) */
    reg[10]=1;                                  /* ProtocolVersion */
    reg[11]=1;                                  /* REQUEST_NO_RESPONSE */
    reg[12]=0x01;reg[13]=0x00;                  /* MessageId 0x0100 */
    reg[16]=channel; reg[17]=0; reg[18]=0;      /* EnumChannel, NodeCount=0, NumOfEntries=0 */
    uint16_t length=sizeof(reg)-2; reg[0]=(uint8_t)(length>>8); reg[1]=(uint8_t)length;

    printf("Aktive DNCP-Discovery (Channel %u) - broadcaste Registry, sammle Announces ...\n", channel);
    sockaddr_in dst{}; dst.sin_family=AF_INET; dst.sin_addr.s_addr=inet_addr("224.0.0.1"); dst.sin_port=htons(65527);

    uint8_t buf[2048];
    for(int r=0;r<rounds;r++){
        reg[14]=(uint8_t)(cnt>>8); reg[15]=(uint8_t)cnt; cnt++;
        for(uint32_t ifip:ifs){ setsockopt(tx,IPPROTO_IP,IP_MULTICAST_IF,(char*)&ifip,sizeof(ifip));
            sendto(tx,(char*)reg,sizeof(reg),0,(sockaddr*)&dst,sizeof(dst)); }
        /* nach jedem Broadcast kurz auf Announces lauschen */
        for(int t=0;t<10;t++){
            fd_set fd; FD_ZERO(&fd); FD_SET(rx,&fd); timeval tv{0,100000};
            if(select(0,&fd,nullptr,nullptr,&tv)>0){
                sockaddr_in from{};int fl=sizeof(from);
                int n=recvfrom(rx,(char*)buf,sizeof(buf),0,(sockaddr*)&from,&fl);
                if(n>0) addAnnounce(buf,n);
            }
        }
    }
    /* Nachlauf-Fenster */
    for(int t=0;t<timeoutS*10;t++){
        fd_set fd; FD_ZERO(&fd); FD_SET(rx,&fd); timeval tv{0,100000};
        if(select(0,&fd,nullptr,nullptr,&tv)>0){
            sockaddr_in from{};int fl=sizeof(from);
            int n=recvfrom(rx,(char*)buf,sizeof(buf),0,(sockaddr*)&from,&fl);
            if(n>0) addAnnounce(buf,n);
        }
    }

    printf("\n%zu DNCP-Knoten gefunden.\n", g_nodes.size());
    if(g_nodes.empty())
        printf("Hinweis: DNCP muss an den Knoten aktiviert sein; sonst antwortet niemand.\n");
    closesocket(rx); closesocket(tx); WSACleanup();
    return 0;
}
