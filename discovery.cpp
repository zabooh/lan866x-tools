/*
 * discovery.cpp  -  Endpoint-/Service-Discovery (Windows, sofort lauffaehig).
 *
 * Listet alle ueber den T1S-USB-Adapter erreichbaren LAN866x-Endpoints und gibt
 * pro Endpoint die vollstaendigen Status-Infos aus (wie der Remote Configurator):
 *   - GetStatus       (0x1002): Uptime, Chip, App-/Bootloader-Namen & -Versionen,
 *                               COMO/Service/Keys-Version, Startup-/Reset-Info
 *   - GetNetworkStatus(0x1600): MAC, IPv4/IPv6, Endpoint-/OASPI-Status,
 *                               Arbitration, PLCA Node Id
 *
 * Nutzt den fertigen C++-Stack (libepmicrochip). Discovery laeuft ueber SOME/IP-SD.
 */
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <thread>
#include <chrono>
#include "lan866x_client.hpp"

using namespace microchip::rcp;

extern "C" { uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 }; }

/* laengenbehaftetes Byte-Feld -> C-String */
static const char *S(const uint8_t *buf, uint16_t len)
{
    static char tmp[65];
    uint16_t n = (len < 64u) ? len : 64u;
    memcpy(tmp, buf, n); tmp[n] = '\0';
    return tmp;
}

static void printUptime(uint64_t ns)
{
    uint64_t s = ns / 1000000000ull;
    uint32_t ms = (uint32_t)((ns / 1000000ull) % 1000ull);
    uint32_t h = (uint32_t)(s / 3600ull); s %= 3600ull;
    uint32_t m = (uint32_t)(s / 60ull);   uint32_t sec = (uint32_t)(s % 60ull);
    printf("%uh %um %u.%03us", h, m, sec, ms);
}

/* Endpoint-Typ aus dem Chip-Identifier ableiten (z. B. "LAN8661B"). */
static const char *epType(const char *chip)
{
    if (strncmp(chip, "LAN8660", 7) == 0) return "Control Endpoint";
    if (strncmp(chip, "LAN8661", 7) == 0) return "Lighting Endpoint (LED/Display)";
    if (strncmp(chip, "LAN8662", 7) == 0) return "Audio Endpoint";
    return "unbekannt";
}

static const char *arbStr(uint8_t a)
{ return a==0?"CSMA/CD":a==1?"PLCA":a==2?"PLCA no fallback":"?"; }
static const char *linkStr(uint8_t s)
{ return s==1?"Link-Up":s==2?"Link-Down":"?"; }
static const char *oaspiStr(uint8_t s)
{ return s==0?"Disabled":s==1?"Link-Up":s==2?"Link-Down":"?"; }

static void printStartup(uint64_t si)
{
    static const char *reasons[] = {
        "Power-On-Reset","Under-voltage VDDC Reset","Under-voltage VDDA Reset",
        "BG Error Reset","External Reset","Watchdog Reset","Over-temperature Reset",
        "Software Reset","Lock-up Reset"
    };
    for (int b = 0; b <= 8; ++b)
        if (si & (1ull << b)) printf("        ..%s\n", reasons[b]);
    printf("        ..Security Mode: %u\n", (unsigned)((si >> 9) & 0x3ull));
}

static void usage(const char *prog)
{
    printf(
        "%s - LAN866x Endpoint-/Service-Discovery\n\n"
        "Listet alle ueber den T1S-USB-Adapter erreichbaren Endpoints und gibt pro\n"
        "Endpoint Typ, RCP-Service (0xFF10) sowie GetStatus/GetNetworkStatus aus.\n\n"
        "AUFRUF:\n"
        "  %s [--help]\n\n"
        "OPTIONEN:\n"
        "  -h, --help   diese Hilfe anzeigen\n\n"
        "Das Tool nimmt keine weiteren Argumente; die Endpoints werden per\n"
        "SOME/IP Service Discovery (Multicast 224.0.0.1) automatisch gefunden.\n"
        "Voraussetzung: T1S-USB-Adapter mit IP 192.168.0.x, Firewall frei.\n",
        prog, prog);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i],"--help") || !strcmp(argv[i],"-h")) { usage("lan866x-discovery"); return 0; }

    uint8_t maj=0,min=0,bug=0;
    LAN866XClientFactory::GetVersion(maj,min,bug);
    printf("libLAN866x %u.%u.%u  -  Endpoint Discovery\n", maj,min,bug);
    printf("Suche erreichbare Endpoints (5 s) ...\n");

    for (int i = 0; i < 50; ++i) {
        (void)LAN866XClientFactory::GetAllClients();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto clients = LAN866XClientFactory::GetAllClients();
    printf("\nDevices available = %zu\n", clients.size());

    int idx = 0;
    for (auto *c : clients) {
        uint8_t *ip=nullptr, ipLen=0; uint16_t port=0;
        c->GetIpAddressAndPort(&ip,&ipLen,&port);
        printf("\n========================================================\n");
        printf("Endpoint #%d  -  %u.%u.%u.%u:%u  (SOME/IP Instance 0x%04X, available=%d)\n",
               idx++, ip?ip[0]:0, ip?ip[1]:0, ip?ip[2]:0, ip?ip[3]:0, port,
               c->GetSomeIpInstaneId(), (int)c->IsNodeAvailable());
        printf("========================================================\n");

        /* ---- GetStatus ---- */
        GetStatusReply_t st; memset(&st,0,sizeof(st));
        if (c->GetStatus(&st) == RT_OK) {
            printf("  Uptime:             "); printUptime(st.UpTime); printf("\n");
            printf("  Application:        %s\n", S(st.ActiveApplication, st.ActiveApplicationLength));
            printf("  Chip Identifier:    %s\n", S(st.ChipIdentifier, st.ChipIdentifierLength));
            printf("  Endpoint Type:      %s\n", epType(S(st.ChipIdentifier, st.ChipIdentifierLength)));
            printf("  Main Name:          %s\n", S(st.MainConfigurationVersion, st.MainConfigurationVersionLength));
            printf("  Bootloader Name:    %s\n", S(st.BootConfigurationVersion, st.BootConfigurationVersionLength));
            printf("  Main Version:       %s\n", S(st.MainApplicationVersion, st.MainApplicationVersionLength));
            printf("  Root Version:       %s\n", S(st.RootApplicationVersion, st.RootApplicationVersionLength));
            printf("  Bootloader Version: %s\n", S(st.BootApplicationVersion, st.BootApplicationVersionLength));
            printf("  COMO Version:       V%u.%u.%u\n",
                   (st.ComoVersion>>16)&0xFF, (st.ComoVersion>>8)&0xFF, st.ComoVersion&0xFF);
            printf("  Service Version:    V%u.%u\n",
                   (st.ServiceVersion>>16)&0xFF, (st.ServiceVersion>>8)&0xFF);
            printf("  Keys Version:       %s\n", S(st.KeysVersion, st.KeysVersionLength));
            printf("  StartupInformation:\n"); printStartup(st.StartupInformation);
        } else {
            printf("  GetStatus fehlgeschlagen\n");
        }

        /* ---- GetNetworkStatus ---- */
        GetNetworkStatusReply_t ns; memset(&ns,0,sizeof(ns));
        if (c->GetNetworkStatus(&ns) == RT_OK) {
            uint64_t m = ns.EndpointAddress;
            uint32_t v4 = ns.EndpointIpV4Address;
            printf("  MAC:                %02X:%02X:%02X:%02X:%02X:%02X\n",
                   (unsigned)((m>>40)&0xFF),(unsigned)((m>>32)&0xFF),(unsigned)((m>>24)&0xFF),
                   (unsigned)((m>>16)&0xFF),(unsigned)((m>>8)&0xFF),(unsigned)(m&0xFF));
            printf("  IPv4:               %u.%u.%u.%u\n",
                   (v4>>24)&0xFF,(v4>>16)&0xFF,(v4>>8)&0xFF,v4&0xFF);
            printf("  IPv6:               %04X:%04X:%04X:%04X:%04X:%04X:%04X:%04X\n",
                   (unsigned)((ns.EndpointIpV6AddressHi>>48)&0xFFFF),(unsigned)((ns.EndpointIpV6AddressHi>>32)&0xFFFF),
                   (unsigned)((ns.EndpointIpV6AddressHi>>16)&0xFFFF),(unsigned)(ns.EndpointIpV6AddressHi&0xFFFF),
                   (unsigned)((ns.EndpointIpV6AddressLo>>48)&0xFFFF),(unsigned)((ns.EndpointIpV6AddressLo>>32)&0xFFFF),
                   (unsigned)((ns.EndpointIpV6AddressLo>>16)&0xFFFF),(unsigned)(ns.EndpointIpV6AddressLo&0xFFFF));
            printf("  Endpoint Status:    %s\n", linkStr(ns.EndpointStatus));
            printf("  OASPI Status:       %s\n", oaspiStr(ns.OaspiStatus));
            printf("  Arbitration:        %s\n", arbStr(ns.ArbitrationMode));
            printf("  PLCA Node Id:       %u\n", (unsigned)ns.PLCANodeId);
        } else {
            printf("  GetNetworkStatus fehlgeschlagen\n");
        }
    }

    if (clients.empty())
        printf("\nKeine Endpoints. Pruefe: Treiber, NIC-IP (192.168.0.x), Bus-Termination, Firewall.\n");
    return 0;
}
