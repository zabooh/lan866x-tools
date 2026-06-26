/*******************************************************************************
  MPLAB Harmony Application Source File

  Company:
    Microchip Technology Inc.

  File Name:
    app.c

  Summary:
    This file contains the source code for the MPLAB Harmony application.

  Description:
    This file contains the source code for the MPLAB Harmony application.  It
    implements the logic of the application's state machine and it may call
    API routines of other MPLAB Harmony modules in the system, such as drivers,
    system services, and middleware.  However, it does not call any of the
    system interfaces (such as the "Initialize" and "Tasks" functions) of any of
    the modules in the system or make any assumptions about when those functions
    are called.  That is the responsibility of the configuration-specific system
    files.
 *******************************************************************************/

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************

#include "app.h"
#include <string.h>
#include <stdlib.h>           /* malloc/free - C-heap largest-free-block probe */
#include "config/default/system/console/sys_console.h"
#include "config/default/library/tcpip/tcpip.h"
#define TCPIP_THIS_MODULE_ID    TCPIP_MODULE_MANAGER
#include "config/default/library/tcpip/src/tcpip_packet.h"
#include "config/default/library/tcpip/telnet.h"
#include "config/default/system/time/sys_time.h"
#include "config/default/driver/gmac/drv_gmac.h"
#include "config/default/driver/lan865x/drv_lan865x.h"
#include "system/command/sys_command.h"
#include "tcpip_manager_control.h"
#include "lan866x_cli.h"
#include "env.h"


// *****************************************************************************
// *****************************************************************************
// Section: Global Data Definitions
// *****************************************************************************
// *****************************************************************************

// *****************************************************************************
/* Application Data

  Summary:
    Holds application data

  Description:
    This structure holds the application's data.

  Remarks:
    This structure should be initialized by the APP_Initialize function.

    Application strings and buffers are be defined outside this structure.
 */

APP_DATA appData;

// *****************************************************************************
// *****************************************************************************
// Section: Application Callback Functions
// *****************************************************************************
// *****************************************************************************

/* TODO:  Add any necessary callback functions.
 */

// *****************************************************************************
// *****************************************************************************
// Section: Application Local Functions
// *****************************************************************************
// *****************************************************************************
bool pktEth0Handler(TCPIP_NET_HANDLE hNet, struct _tag_TCPIP_MAC_PACKET* rxPkt, uint16_t frameType, const void* hParam);
const void *MyEth0HandlerParam;

bool pktEth1Handler(TCPIP_NET_HANDLE hNet, struct _tag_TCPIP_MAC_PACKET* rxPkt, uint16_t frameType, const void* hParam);
const void *MyEth1HandlerParam;

void DumpMem(uint32_t addr, uint32_t count);
bool Command_Init(void);

uint32_t ipdump_mode = 0;
uint32_t mirror_mode = 0;     /* eth0 (T1S) -> eth1 (100BASE-T) port mirror for Wireshark */
uint32_t my_delay_time = 0;

/* --- NoIP raw Ethernet test (EtherType 0x88B5 = IEEE 802 Local Experimental) --- */
#define NOIP_ETHERTYPE  0x88B5u
static uint32_t noip_tx_cnt = 0u;
static uint32_t noip_rx_cnt = 0u;
SYS_TIME_HANDLE timerHandle;

/* =========================================================
 * Deferred Packet Logging
 * =========================================================
 * Packet handlers store metadata into a ring buffer instead
 * of calling SYS_CONSOLE_PRINT()/DumpMem() directly.
 * APP_Tasks() drains the buffer (max 10 entries per call). */

#define PKT_LOG_BUF_SIZE    64u   /* ring buffer capacity; must be a power of 2 */
/* Full-frame capture: frame stored in shared pool (up to PKT_LOG_MAX_FRAME_SIZE bytes each) */
#define PKT_LOG_MAX_FRAMES     16u    /* number of full-size frames bufferable in pool */
#define PKT_LOG_MAX_FRAME_SIZE 1518u  /* max bytes per frame (standard Ethernet MTU)  */

typedef enum {
    PKT_LOG_NOIP = 0,  /* NoIP (0x88B5) frame from eth0 */
    PKT_LOG_ETH0 = 2,  /* generic frame from eth0        */
    PKT_LOG_ETH1 = 3,  /* generic frame from eth1        */
} pkt_log_type_t;

typedef struct {
    uint64_t       timestamp;    /* SYS_TIME_Counter64Get()                    */
    uint32_t       pkt_counter;  /* per-handler packet counter                 */
    uint32_t       noip_seq;     /* NoIP sequence number                       */
    uint16_t       frame_type;   /* EtherType                                  */
    uint16_t       length;       /* actual frame length in bytes               */
    uint32_t       data_offset;  /* offset into frame_data_pool[]              */
    uint16_t       data_len;     /* bytes stored in pool (may be 0 if dropped) */
    uint8_t        iface;        /* 0 = eth0, 1 = eth1                         */
    uint8_t        truncated;    /* 1 if frame data was truncated to fit pool  */
    pkt_log_type_t log_type;     /* entry classification                       */
    uint8_t        mac_src[6];   /* source MAC (extracted separately)          */
} PKT_LOG_ENTRY;

typedef struct {
    PKT_LOG_ENTRY     entries[PKT_LOG_BUF_SIZE];
    volatile uint32_t write_idx;     /* updated only by packet handlers  */
    volatile uint32_t read_idx;      /* updated only by APP_Tasks        */
    volatile uint32_t overflow_cnt;
    volatile uint32_t total_logged;
} PKT_LOG_BUF;

static PKT_LOG_BUF pkt_log = {0};

/* Shared circular pool for storing complete frame bytes.
 * Holds up to PKT_LOG_MAX_FRAMES full-size Ethernet frames.
 * Aligned to 4 bytes for efficient ARM word-aligned access. */
#define FRAME_DATA_POOL_SIZE  ((uint32_t)PKT_LOG_MAX_FRAMES * (uint32_t)PKT_LOG_MAX_FRAME_SIZE)

typedef struct {
    uint8_t  pool[FRAME_DATA_POOL_SIZE]; /* circular frame data storage           */
    uint32_t write_offset;               /* next write position in pool (0-based) */
} FRAME_DATA_POOL;

static FRAME_DATA_POOL frame_data_pool __attribute__((aligned(4))) = {0};

/* Lock-free single-producer/single-consumer ring buffer write.
 * On ARM Cortex-M, 32-bit aligned stores are single-instruction atomic.
 * write_idx is committed last so the reader never observes a partial entry.
 * Newest entries are dropped when the buffer is full.
 *
 * frame_data/frame_len provide the complete frame bytes to copy into the
 * shared pool.  The pool write_offset is advanced after the copy.
 * Wraparound safety: if the frame does not fit at the current write_offset
 * the function attempts to wrap to offset 0.  It only wraps if no pending
 * log entry references data in [0, copy_len), otherwise the frame is
 * truncated to the remaining bytes at the end of the pool.
 */
static void PktLog_Write(PKT_LOG_ENTRY *entry,
                         const uint8_t *frame_data, uint16_t frame_len)
{
    uint32_t next = (pkt_log.write_idx + 1u) & (PKT_LOG_BUF_SIZE - 1u);
    if (next == pkt_log.read_idx) {
        pkt_log.overflow_cnt++;
        return; /* ring buffer full – drop newest entry */
    }

    /* Clamp captured length to the maximum supported frame size */
    uint16_t copy_len = (frame_len > (uint16_t)PKT_LOG_MAX_FRAME_SIZE)
                        ? (uint16_t)PKT_LOG_MAX_FRAME_SIZE : frame_len;

    uint32_t pool_offset    = frame_data_pool.write_offset;
    uint8_t  truncated_flag = 0u;

    if (frame_data != NULL && copy_len > 0u) {
        uint32_t remaining = FRAME_DATA_POOL_SIZE - frame_data_pool.write_offset;

        if ((uint32_t)copy_len > remaining) {
            /* Frame does not fit at the current write position.
             * Attempt to wrap to the beginning of the pool.
             * This is safe only when no pending entry holds data in [0, copy_len). */
            bool ring_empty = (pkt_log.read_idx == pkt_log.write_idx);
            bool wrap_safe  = ring_empty ||
                              (pkt_log.entries[pkt_log.read_idx].data_offset >= (uint32_t)copy_len);

            if (wrap_safe) {
                /* Wrap: restart from pool beginning */
                pool_offset = 0u;
            } else {
                /* Cannot wrap safely – truncate to whatever space remains */
                copy_len       = (uint16_t)remaining;
                truncated_flag = 1u;
            }
        }

        if (copy_len > 0u) {
            memcpy(&frame_data_pool.pool[pool_offset], frame_data, copy_len);
            /* Advance the pool write pointer; reset to 0 if we exactly filled the end */
            uint32_t new_offset = pool_offset + (uint32_t)copy_len;
            frame_data_pool.write_offset = (new_offset >= FRAME_DATA_POOL_SIZE) ? 0u : new_offset;
        }
    }

    /* Store pool reference and flags in the ring entry */
    entry->data_offset = pool_offset;
    entry->data_len    = copy_len;
    entry->truncated   = truncated_flag;

    pkt_log.entries[pkt_log.write_idx] = *entry;
    pkt_log.total_logged++;
    pkt_log.write_idx = next; /* commit – must be the last store */
}

/* Read one entry from the ring buffer; returns false if empty. */
static bool PktLog_Read(PKT_LOG_ENTRY *entry)
{
    if (pkt_log.read_idx == pkt_log.write_idx) {
        return false; /* buffer empty */
    }
    *entry = pkt_log.entries[pkt_log.read_idx];
    pkt_log.read_idx = (pkt_log.read_idx + 1u) & (PKT_LOG_BUF_SIZE - 1u);
    return true;
}

static void app_wait_ms(uint32_t ms)
{
    uint64_t start = SYS_TIME_Counter64Get();
    uint64_t ticks = ((uint64_t)SYS_TIME_FrequencyGet() * (uint64_t)ms) / 1000ULL;
    while ((SYS_TIME_Counter64Get() - start) < ticks) {
    }
}

// LAN865X Register access variables
volatile bool app_lan_reg_operation_complete = false;
volatile bool app_lan_reg_operation_success = false;
volatile uint32_t app_lan_reg_read_value = 0;

#define APP_LAN_TIMEOUT_MS  200u    /* Max wait for a LAN865x register callback (matches GM/FOL WAIT-state timeout) */

typedef enum {
    APP_LAN_IDLE,
    APP_LAN_WAIT_READ,
    APP_LAN_WAIT_WRITE
} app_lan_state_t;

static app_lan_state_t app_lan_state   = APP_LAN_IDLE;
static uint8_t         s_plca_node_id  = DRV_LAN865X_PLCA_NODE_ID_IDX0;
static uint32_t        app_lan_addr    = 0u;
static uint32_t        app_lan_value   = 0u;
static uint64_t        app_lan_expire_tick = 0u;    /* SYS_TIME tick at which the operation times out */
static bool            app_lan_op_initiated = false;

/* TODO:  Add any necessary local functions.
 */


void BRIDGE_TimerCallback(uintptr_t context) {
    if (my_delay_time)my_delay_time--;
}

// LAN865X Register callback for read operations
void lan_read_callback(void *reserved1, bool success, uint32_t addr, uint32_t value, void *pTag, void *reserved2) {
    app_lan_reg_operation_success = success;
    app_lan_reg_read_value = value;
    app_lan_reg_operation_complete = true;
}

// LAN865X Register callback for write operations
void lan_write_callback(void *reserved1, bool success, uint32_t addr, uint32_t value, void *pTag, void *reserved2) {
    app_lan_reg_operation_success = success;
    app_lan_reg_operation_complete = true;
}

// Help command for Test group
static void test_help(SYS_CMD_DEVICE_NODE* pCmdIO, int argc, char** argv) {
    SYS_CONSOLE_PRINT("Test group commands:\n\r");
    SYS_CONSOLE_PRINT("  help                         - Show this help\n\r");
    SYS_CONSOLE_PRINT("  timestamp                    - Show build timestamp\n\r");
    SYS_CONSOLE_PRINT("  ipdump <mode>                - Dump RX IP packets (0=off, 1=eth0, 2=eth1, 3=both)\n\r");
    SYS_CONSOLE_PRINT("  mirror [0|1]                 - Mirror eth0(T1S) RX to eth1 for Wireshark\n\r");
    SYS_CONSOLE_PRINT("  stats                        - Show TX/RX software counters for eth0 and eth1\n\r");
    SYS_CONSOLE_PRINT("  lan_read  <addr>             - Read  LAN865X register (hex address)\n\r");
    SYS_CONSOLE_PRINT("  lan_write <addr> <value>     - Write LAN865X register (hex addr, hex value)\n\r");
    SYS_CONSOLE_PRINT("  dump <addr> <count>          - Dump memory (hex addr, count)\n\r");
    SYS_CONSOLE_PRINT("  plca_node [id]               - Get/set PLCA node ID (no arg = show current)\n\r");
    SYS_CONSOLE_PRINT("  noip_send <n> [gap_ms]       - Send N raw Ethernet frames (EtherType 0x88B5)\n\r");
    SYS_CONSOLE_PRINT("  noip_stat                    - Show NoIP TX/RX counters\n\r");
    SYS_CONSOLE_PRINT("  logclear                     - Clear deferred packet log buffer\n\r");
    SYS_CONSOLE_PRINT("  logstat                      - Show deferred log statistics\n\r");
    SYS_CONSOLE_PRINT("\n\rExample: Test plca_node       -> zeigt aktuelle Node-ID\n\r");
    SYS_CONSOLE_PRINT("Example: Test plca_node 0     -> setzt Node-ID auf 0 (GM/Coordinator)\n\r");
    SYS_CONSOLE_PRINT("Example: Test lan_read 0x0004CA02\n\r");
}

// stats command: print TX/RX software counters for both interfaces
static void cmd_stats(SYS_CMD_DEVICE_NODE* pCmdIO, int argc, char** argv) {
    TCPIP_MAC_RX_STATISTICS rxStats;
    TCPIP_MAC_TX_STATISTICS txStats;
    const char *ifNames[] = {"eth0", "eth1"};
    int i;
    for (i = 0; i < 2; i++) {
        TCPIP_NET_HANDLE netH = TCPIP_STACK_NetHandleGet(ifNames[i]);
        if (netH == NULL) {
            SYS_CONSOLE_PRINT("%s: not found\n\r", ifNames[i]);
            continue;
        }
        if (TCPIP_STACK_NetMACStatisticsGet(netH, &rxStats, &txStats)) {
            SYS_CONSOLE_PRINT("%s TX: ok=%d err=%d qFull=%d pend=%d\n\r",
                ifNames[i], txStats.nTxOkPackets, txStats.nTxErrorPackets,
                txStats.nTxQueueFull, txStats.nTxPendBuffers);
            SYS_CONSOLE_PRINT("%s RX: ok=%d err=%d nobufs=%d pend=%d\n\r",
                ifNames[i], rxStats.nRxOkPackets, rxStats.nRxErrorPackets,
                rxStats.nRxBuffNotAvailable, rxStats.nRxPendBuffers);
        } else {
            SYS_CONSOLE_PRINT("%s: stats not available\n\r", ifNames[i]);
        }
    }
}

// Timestamp command to show build info
static void show_timestamp(SYS_CMD_DEVICE_NODE* pCmdIO, int argc, char** argv) {
    SYS_CONSOLE_PRINT("======================================\n\r");
    SYS_CONSOLE_PRINT("T1S Packet Sniffer - Build Info\n\r");
    SYS_CONSOLE_PRINT("Build Timestamp: "__DATE__" "__TIME__"\n\r");
    SYS_CONSOLE_PRINT("======================================\n\r");
}

bool TelnetAuthenticationHandler(const char* user, const char* password, const TCPIP_TELNET_CONN_INFO* pInfo, const void* hParam) {

    if ((strcmp(user, "admin") == 0) && (strcmp(password, "password") == 0)) {
        SYS_CONSOLE_PRINT("Telnet Access Authenticated\n\r");
        return true;
    } else {
        SYS_CONSOLE_PRINT("Telnet Access Declined\n\r");
        return false;
    }
}

const void* TelnetHandlerParam;

// *****************************************************************************
// *****************************************************************************
// Section: Application Initialization and State Machine Functions
// *****************************************************************************
// *****************************************************************************

/*******************************************************************************
  Function:
    void APP_Initialize ( void )

  Remarks:
    See prototype in app.h.
 */

void APP_Initialize(void) {
    /* Place the App state machine in its initial state. */
    appData.state = APP_STATE_INIT;

    TCPIP_TELNET_AuthenticationRegister(TelnetAuthenticationHandler, &TelnetHandlerParam);

    timerHandle = SYS_TIME_TimerCreate(0, SYS_TIME_MSToCount(1000), &BRIDGE_TimerCallback, (uintptr_t) NULL, SYS_TIME_PERIODIC);
    SYS_TIME_TimerStart(timerHandle);

    Command_Init();
    LAN866X_CLI_Init();   /* register the "lan866x" SOME/IP client commands */
    GPIO_CLI_Init();      /* gpio, gpioevents, ledtoggle, ledpwm */
    I2C_CLI_Init();       /* i2cscan, i2cid, proxmon, lan8680, proxled */
    SPI_CLI_Init();       /* spi, spiid, thumbmon, adc, pwm */
    SYS_CLI_Init();       /* servicetest, boot, uart, video */
    DNCP_CLI_Init();      /* dncpmon, dncpdisc */
    NTP_Init();           /* software NTP time sync service (UDP 30491) + "ntp" CLI */
    /* ENV_Init() runs earlier, in SYS_Initialize before TCPIP_STACK_Init (initialization.c),
     * so the persistent MAC can be applied before the stack binds it. */
    /* TODO: Initialize your application's state machine and other
     * parameters.
     */
}

/******************************************************************************
  Function:
    void APP_Tasks ( void )

  Remarks:
    See prototype in app.h.
 */

void APP_Tasks(void) {

    /* Check the application's current state. */
    switch (appData.state) {
            /* Application's initial state. */
        case APP_STATE_INIT:
        {
            bool appInitialized = true;

            my_delay_time = 5;
            if (appInitialized) {

                appData.state = APP_STATE_WAIT;
            }
            break;
        }

        case APP_STATE_WAIT:
            if (my_delay_time == 0) {
                appData.state = APP_STATE_SERVICE_TASKS;
            }
            break;

        case APP_STATE_SERVICE_TASKS:
        {
            SYS_CONSOLE_PRINT("======================================\n\r ");
            SYS_CONSOLE_PRINT("T1S Packet Sniffer\n\r ");
            SYS_CONSOLE_PRINT("Build Timestamp: "__DATE__" "__TIME__"\n\r" );
            TCPIP_NET_HANDLE eth0_net_hd = TCPIP_STACK_IndexToNet(0);
            TCPIP_STACK_PacketHandlerRegister(eth0_net_hd, pktEth0Handler, MyEth0HandlerParam);
            TCPIP_NET_HANDLE eth1_net_hd = TCPIP_STACK_IndexToNet(1);
            TCPIP_STACK_PacketHandlerRegister(eth1_net_hd, pktEth1Handler, MyEth1HandlerParam);
            env_apply();   /* push the persisted network config into the stack (once, stack is up) */
            appData.state = APP_STATE_IDLE;
            break;
        }

            /* TODO: implement your application state machine.*/
        case APP_STATE_IDLE:
        {
            static uint64_t ticks_per_ms  = 0u;
            if (ticks_per_ms == 0u) {
                ticks_per_ms = (uint64_t)SYS_TIME_FrequencyGet() / 1000ULL;
            }
            uint64_t current_tick = SYS_TIME_Counter64Get();

            /* === LAN866x SOME/IP client: drive SD + replies once per tick === */
            LAN866X_CLI_Task();

            /* === Software NTP time-sync service (UDP 30491) === */
            NTP_Task();

            /* === Manual LAN865x register access service (Test commands) === */
            switch (app_lan_state) {
                case APP_LAN_IDLE:
                    break;

                case APP_LAN_WAIT_READ:
                    if (!app_lan_reg_operation_complete) {
                        if (!app_lan_op_initiated) {
                            TCPIP_MAC_RES result = DRV_LAN865X_ReadRegister(0, app_lan_addr, true, lan_read_callback, NULL);
                            if (result != TCPIP_MAC_RES_OK) {
                                SYS_CONSOLE_PRINT("LAN865X Read failed to start: result=%d\n\r", result);
                                app_lan_state = APP_LAN_IDLE;
                            } else {
                                app_lan_expire_tick = current_tick + (uint64_t)APP_LAN_TIMEOUT_MS * ticks_per_ms;
                                app_lan_op_initiated = true;
                            }
                        } else {
                            if ((int64_t)(current_tick - app_lan_expire_tick) >= 0) {
                                SYS_CONSOLE_PRINT("LAN865X Read timeout for addr=0x%08X\n\r", (unsigned int)app_lan_addr);
                                app_lan_state = APP_LAN_IDLE;
                                app_lan_op_initiated = false;
                            }
                        }
                    } else {
                        if (app_lan_reg_operation_success) {
                            SYS_CONSOLE_PRINT("LAN865X Read OK: Addr=0x%08X Value=0x%08X\n\r",
                                              (unsigned int)app_lan_addr, (unsigned int)app_lan_reg_read_value);
                        } else {
                            SYS_CONSOLE_PRINT("LAN865X Read failed for addr=0x%08X\n\r", (unsigned int)app_lan_addr);
                        }
                        app_lan_state = APP_LAN_IDLE;
                        app_lan_op_initiated = false;
                    }
                    break;

                case APP_LAN_WAIT_WRITE:
                    if (!app_lan_reg_operation_complete) {
                        if (!app_lan_op_initiated) {
                            TCPIP_MAC_RES result = DRV_LAN865X_WriteRegister(0, app_lan_addr, app_lan_value, true, lan_write_callback, NULL);
                            if (result != TCPIP_MAC_RES_OK) {
                                SYS_CONSOLE_PRINT("LAN865X Write failed to start: result=%d\n\r", result);
                                app_lan_state = APP_LAN_IDLE;
                            } else {
                                app_lan_expire_tick = current_tick + (uint64_t)APP_LAN_TIMEOUT_MS * ticks_per_ms;
                                app_lan_op_initiated = true;
                            }
                        } else {
                            if ((int64_t)(current_tick - app_lan_expire_tick) >= 0) {
                                SYS_CONSOLE_PRINT("LAN865X Write timeout for addr=0x%08X\n\r", (unsigned int)app_lan_addr);
                                app_lan_state = APP_LAN_IDLE;
                                app_lan_op_initiated = false;
                            }
                        }
                    } else {
                        if (app_lan_reg_operation_success) {
                            SYS_CONSOLE_PRINT("LAN865X Write OK: Addr=0x%08X Value=0x%08X\n\r",
                                              (unsigned int)app_lan_addr, (unsigned int)app_lan_value);
                        } else {
                            SYS_CONSOLE_PRINT("LAN865X Write failed for addr=0x%08X\n\r", (unsigned int)app_lan_addr);
                        }
                        app_lan_state = APP_LAN_IDLE;
                        app_lan_op_initiated = false;
                    }
                    break;

                default:
                    break;
            }

            /* === Deferred packet log output (max 10 entries per APP_Tasks iteration) === */
            if (ticks_per_ms > 0u) {
                PKT_LOG_ENTRY log_e;
                uint32_t max_print = 10u;
                while (max_print-- > 0u && PktLog_Read(&log_e)) {
                    uint64_t ts_ms = log_e.timestamp / ticks_per_ms;
                    switch (log_e.log_type) {
                        case PKT_LOG_NOIP:
                            SYS_CONSOLE_PRINT("[NoIP-RX] #%u seq=%u from %02X:%02X:%02X:%02X:%02X:%02X len=%d ts=%llu ms\r\n",
                                (unsigned)log_e.pkt_counter, (unsigned)log_e.noip_seq,
                                log_e.mac_src[0], log_e.mac_src[1], log_e.mac_src[2],
                                log_e.mac_src[3], log_e.mac_src[4], log_e.mac_src[5],
                                (int)log_e.length, (unsigned long long)ts_ms);
                            if (log_e.data_len > 0u) {
                                DumpMem((uint32_t)&frame_data_pool.pool[log_e.data_offset], log_e.data_len);
                            }
                            break;
                        case PKT_LOG_ETH0:
                            SYS_CONSOLE_PRINT("E0:%u len=%u ts=%llu ms%s\r\n",
                                (unsigned)log_e.pkt_counter, (unsigned)log_e.length,
                                (unsigned long long)ts_ms,
                                log_e.truncated ? " [TRUNC]" : "");
                            if (log_e.data_len > 0u) {
                                DumpMem((uint32_t)&frame_data_pool.pool[log_e.data_offset], log_e.data_len);
                            }
                            break;
                        case PKT_LOG_ETH1:
                            SYS_CONSOLE_PRINT("E1:%u len=%u ts=%llu ms%s\r\n",
                                (unsigned)log_e.pkt_counter, (unsigned)log_e.length,
                                (unsigned long long)ts_ms,
                                log_e.truncated ? " [TRUNC]" : "");
                            if (log_e.data_len > 0u) {
                                DumpMem((uint32_t)&frame_data_pool.pool[log_e.data_offset], log_e.data_len);
                            }
                            break;
                        default:
                            break;
                    }
                }
            }
            break;
        }

            /* The default state should never be executed. */
        default:
        {
            /* TODO: Handle error in application's state machine. */
            break;
        }
    }
}

/* --- eth0 (T1S) <-> eth1 (100BASE-T) port mirror (SPAN) for Wireshark -------
 * When mirror_mode is on, the bridge<->bus conversation is cloned onto eth1 so
 * a PC on eth1 can capture the T1S traffic in Wireshark. BOTH directions are
 * mirrored, but each is filtered by the bridge's OWN eth0 MAC so the capture is
 * duplicate-free:
 *   - RX (bus -> bridge): only frames addressed TO the bridge (dst == eth0 MAC)
 *                         - i.e. the endpoint's replies to the firmware.
 *   - TX (bridge -> bus): only frames the bridge ITSELF originates (src == eth0
 *                         MAC) - the firmware's own ping/ARP/SOME/IP.
 * Frames the MAC bridge merely FORWARDS between the PC and the bus keep their
 * original src/dst MAC and are already carried to/from eth1 natively; mirroring
 * them would duplicate them at the PC. Broadcast/multicast received on eth0 is
 * likewise left to the bridge (the PC sees it natively) - only the bridge's own
 * outgoing broadcast/multicast (src == eth0 MAC) is added by the TX path. */
static void mirror_pkt_ack(TCPIP_MAC_PACKET *pkt, const void *param)
{
    (void)param;
    TCPIP_PKT_PacketFree(pkt);
}

/* eth0 (T1S) interface MAC - the filter reference for both mirror directions. */
static const uint8_t *eth0_own_mac(void)
{
    TCPIP_NET_HANDLE eth0 = TCPIP_STACK_IndexToNet(0);
    return (eth0 != NULL) ? TCPIP_STACK_NetAddressMac(eth0) : NULL;
}

/* Clone a complete Ethernet frame onto eth1 for the PC-side capture. The caller
 * has already applied the own-MAC filter. Single-segment copy (bridge/stack
 * frames are single-segment); empty/oversize frames are dropped. */
static void mirror_ethpkt_to_eth1(const uint8_t *frame, uint16_t flen)
{
    TCPIP_MAC_PACKET *pTx;
    TCPIP_NET_HANDLE  eth1;

    if (frame == NULL || flen == 0u || flen > 1518u) return;
    pTx = TCPIP_PKT_PacketAlloc(sizeof(TCPIP_MAC_PACKET), flen, 0);   /* flags=0: same as the MAC bridge's own fwd alloc */
    if (pTx == NULL) return;                         /* packet pool busy: drop the mirror copy */

    pTx->pMacLayer = pTx->pDSeg->segLoad;
    memcpy(pTx->pMacLayer, frame, flen);             /* full Ethernet frame (header + payload) */
    pTx->pDSeg->segLen = flen;
    pTx->pNetLayer = pTx->pMacLayer + sizeof(TCPIP_MAC_ETHERNET_HEADER);
    pTx->ackFunc   = mirror_pkt_ack;                 /* freed by the MAC driver after TX */
    pTx->ackParam  = NULL;

    eth1 = TCPIP_STACK_IndexToNet(1);
    if (eth1 != NULL) {
        (void)DRV_GMAC_PacketTx(((TCPIP_NET_IF*)eth1)->hIfMac, pTx);
    } else {
        TCPIP_PKT_PacketFree(pTx);
    }
}

/* RX mirror: a frame just arrived on eth0. Mirror it only if it is addressed to
 * the bridge itself (dst MAC == eth0 MAC). PC-bound unicast and broadcast/
 * multicast are forwarded to eth1 by the MAC bridge already - mirroring them
 * would duplicate them at the PC. */
static void mirror_eth0_rx_to_eth1(struct _tag_TCPIP_MAC_PACKET *rxPkt)
{
    const uint8_t *frame = rxPkt->pMacLayer;
    const uint8_t *mac   = eth0_own_mac();
    if (mac == NULL || frame == NULL) return;
    if (memcmp(frame, mac, 6) != 0) return;          /* dst MAC != eth0 -> not for us, skip */
    mirror_ethpkt_to_eth1(frame, rxPkt->pDSeg->segLen);
}

/* TX mirror: called from DRV_LAN865X_PacketTx (the single eth0 egress point) for
 * every frame about to leave on eth0. Mirror it only if the bridge ITSELF
 * originated it (src MAC == eth0 MAC) - the firmware's own ping/ARP/SOME/IP.
 * Frames forwarded from eth1 keep their original (PC) src MAC and are skipped;
 * the PC already has them. Non-static: the LAN865x driver calls it via an
 * extern declaration. The driver transmits from pDSeg->segLoad. */
void mirror_eth0_tx_hook(struct _tag_TCPIP_MAC_PACKET *txPkt)
{
    const uint8_t *frame;
    const uint8_t *mac;
    if (txPkt == NULL || txPkt->pDSeg == NULL) return;
    frame = txPkt->pDSeg->segLoad;
    if (frame == NULL) return;
    ntp_tap_eth0(1u, frame, txPkt->pDSeg->segLen);   /* eth0 egress timestamp (forwarded + originated) */
    if (!mirror_mode) return;
    mac = eth0_own_mac();
    if (mac == NULL) return;
    if (memcmp(frame + 6, mac, 6) != 0) return;      /* src MAC != eth0 -> forwarded, skip mirror */
    mirror_ethpkt_to_eth1(frame, txPkt->pDSeg->segLen);
}

bool pktEth0Handler(TCPIP_NET_HANDLE hNet, struct _tag_TCPIP_MAC_PACKET* rxPkt, uint16_t frameType, const void* hParam) {
    static uint32_t packet_counter = 0;
    bool ret_val = false;

    packet_counter++;

    ntp_tap_eth0(0u, rxPkt->pMacLayer, rxPkt->pDSeg->segLen);   /* eth0 ingress timestamp (from bus) */

    if (mirror_mode) {
        mirror_eth0_rx_to_eth1(rxPkt);   /* clone endpoint->bridge frames to eth1 (dst-MAC filtered) */
    }

    /* NoIP raw test frame (EtherType 0x88B5): increment counter + print, free buffer */
    if (frameType == NOIP_ETHERTYPE) {
        noip_rx_cnt++;
        const uint8_t *p = rxPkt->pMacLayer;
        uint32_t seq = ((uint32_t)p[14] << 24) | ((uint32_t)p[15] << 16)
                     | ((uint32_t)p[16] <<  8) |  (uint32_t)p[17];
        PKT_LOG_ENTRY log_e = {0};
        log_e.timestamp   = SYS_TIME_Counter64Get();
        log_e.pkt_counter = noip_rx_cnt;
        log_e.noip_seq    = seq;
        log_e.frame_type  = frameType;
        log_e.length      = rxPkt->pDSeg->segLen;
        log_e.iface       = 0u;
        log_e.log_type    = PKT_LOG_NOIP;
        memcpy(log_e.mac_src, &p[6], 6u);
        PktLog_Write(&log_e, rxPkt->pMacLayer, rxPkt->pDSeg->segLen);
        TCPIP_PKT_PacketAcknowledge(rxPkt, TCPIP_MAC_PKT_ACK_RX_OK);
        return true;
    }

    if (ipdump_mode == 1 || ipdump_mode == 3) {
        PKT_LOG_ENTRY log_e = {0};
        log_e.timestamp   = SYS_TIME_Counter64Get();
        log_e.pkt_counter = packet_counter;
        log_e.frame_type  = frameType;
        log_e.length      = rxPkt->pDSeg->segLen;
        log_e.iface       = 0u;
        log_e.log_type    = PKT_LOG_ETH0;
        memcpy(log_e.mac_src, &rxPkt->pMacLayer[6], 6u);
        PktLog_Write(&log_e, rxPkt->pMacLayer, rxPkt->pDSeg->segLen);
    }

    /* eth0<->eth1 L2 bridging is done by the Harmony MAC bridge, not here.
     * Return false so the frame goes to normal stack/bridge processing. */
    return ret_val;
}

bool pktEth1Handler(TCPIP_NET_HANDLE hNet, struct _tag_TCPIP_MAC_PACKET* rxPkt, uint16_t frameType, const void* hParam) {
    static uint32_t packet_counter = 0;

    packet_counter++;

    if (ipdump_mode == 2 || ipdump_mode == 3) {
        PKT_LOG_ENTRY log_e = {0};
        log_e.timestamp   = SYS_TIME_Counter64Get();
        log_e.pkt_counter = packet_counter;
        log_e.frame_type  = frameType;
        log_e.length      = rxPkt->pDSeg->segLen;
        log_e.iface       = 1u;
        log_e.log_type    = PKT_LOG_ETH1;
        memcpy(log_e.mac_src, &rxPkt->pDSeg->segLoad[6], 6u);
        PktLog_Write(&log_e, rxPkt->pDSeg->segLoad, rxPkt->pDSeg->segLen);
    }
    return false;
}

void DumpMem(uint32_t addr, uint32_t count) {
    uint32_t ix, jx, kx;
    uint8_t *puc;
    char str[64];
    int flag = 0;

    puc = (uint8_t *) addr;

    jx = kx = 0;
    for (ix = 0; ix < count; ix++) {
        if ((ix % 16) == 0) {
            if (flag == 1) {
                str[16] = 0;
                kx = 0;
                SYS_CONSOLE_PRINT("   %s\n\r", str);
            }
            SYS_CONSOLE_PRINT("%08x: ", puc);
            flag = 1;
            jx = 0;
        }
        SYS_CONSOLE_PRINT(" %02x", *puc);
        kx++;
        if ((*puc > 31) && (*puc < 127))
            str[jx++] = *puc;
        else
            str[jx++] = '.';
        puc++;
    }
    for (; kx < 16; kx++) {
        SYS_CONSOLE_PRINT("   ");
    }
    str[jx] = 0;
    SYS_CONSOLE_PRINT("   %s", str);
    SYS_CONSOLE_PRINT("\n\r");
}

static void cmd_logclear(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv) {
    (void)pCmdIO; (void)argc; (void)argv;
    pkt_log.read_idx     = pkt_log.write_idx; /* drain pending entries */
    pkt_log.overflow_cnt = 0u;
    pkt_log.total_logged = 0u;
    frame_data_pool.write_offset = 0u;
    SYS_CONSOLE_PRINT("[LOG] ring buffer cleared\r\n");
}

static void cmd_logstat(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv) {
    (void)pCmdIO; (void)argc; (void)argv;
    uint32_t wi      = pkt_log.write_idx;  /* snapshot volatile index */
    uint32_t pending = (wi - pkt_log.read_idx) & (PKT_LOG_BUF_SIZE - 1u);
    SYS_CONSOLE_PRINT("[LOG] total=%u pending=%u overflows=%u bufsize=%u\r\n",
        (unsigned)pkt_log.total_logged, (unsigned)pending,
        (unsigned)pkt_log.overflow_cnt, (unsigned)PKT_LOG_BUF_SIZE);
    SYS_CONSOLE_PRINT("[LOG] pool_offset=%u pool_size=%u (%u frames x %u bytes)\r\n",
        (unsigned)frame_data_pool.write_offset,
        (unsigned)FRAME_DATA_POOL_SIZE,
        (unsigned)PKT_LOG_MAX_FRAMES,
        (unsigned)PKT_LOG_MAX_FRAME_SIZE);
}

static void my_dump(SYS_CMD_DEVICE_NODE* pCmdIO, int argc, char** argv) {
    //const void* cmdIoParam = pCmdIO->cmdIoParam;

    ipdump_mode = strtoul(argv[1], NULL, 16);
    if (ipdump_mode == 0) {
        SYS_CONSOLE_PRINT("IP Layer Dump de-activated\n\r");
    } else if (ipdump_mode == 1) {
        SYS_CONSOLE_PRINT("IP Layer Dump activated on eth0\n\r");
    } else if (ipdump_mode == 2) {
        SYS_CONSOLE_PRINT("IP Layer Dump activated on eth1\n\r");
    } else if (ipdump_mode == 3) {
        SYS_CONSOLE_PRINT("IP Layer Dump activated on eth0 and eth1\n\r");
    } else {
        SYS_CONSOLE_PRINT("Parameter out of range\n\r");
    }

}

/* mirror [0|1] - copy every eth0 (T1S) RX frame out eth1 so a PC on eth1 can
 * sniff the T1S bus with Wireshark (e.g. the endpoint's SOME/IP replies to a
 * firmware CLI command). No argument shows the current state. */
static void cmd_mirror(SYS_CMD_DEVICE_NODE* pCmdIO, int argc, char** argv) {
    if (argc >= 2) {
        mirror_mode = strtoul(argv[1], NULL, 0);
    }
    SYS_CONSOLE_PRINT("eth0(T1S)->eth1 mirror: %s\n\r", mirror_mode ? "ON" : "OFF");
    if (mirror_mode) {
        SYS_CONSOLE_PRINT("  Capture on the PC (eth1) in Wireshark to see the T1S bus traffic:\n\r");
        SYS_CONSOLE_PRINT("  RX (endpoint -> bridge: offers/replies/ARP) AND the bridge's own UDP TX.\n\r");
    }
}

// Memory dump command: dump <address> <count>
static void cmd_mem_dump(SYS_CMD_DEVICE_NODE* pCmdIO, int argc, char** argv) {
    if (argc != 3) {
        SYS_CONSOLE_PRINT("Usage: dump <address_hex> <count>\n\r");
        SYS_CONSOLE_PRINT("Example: dump 0x20000000 64\n\r");
        return;
    }

    uint32_t addr  = strtoul(argv[1], NULL, 0);
    uint32_t count = strtoul(argv[2], NULL, 0);

    if (count == 0) {
        SYS_CONSOLE_PRINT("Count must be > 0\n\r");
        return;
    }

    SYS_CONSOLE_PRINT("Memory dump: 0x%08X  %u bytes\n\r", (unsigned int)addr, (unsigned int)count);
    DumpMem(addr, count);
}

// LAN865X Register read command
static void lan_read(SYS_CMD_DEVICE_NODE* pCmdIO, int argc, char** argv) {
    if (argc != 2) {
        SYS_CONSOLE_PRINT("Usage: lan_read <address_hex>\n\r");
        SYS_CONSOLE_PRINT("Example: lan_read 0x00040000\n\r");
        return;
    }

    if (app_lan_state != APP_LAN_IDLE) {
        SYS_CONSOLE_PRINT("ERROR: Previous LAN operation still in progress\n\r");
        return;
    }

    app_lan_addr  = strtoul(argv[1], NULL, 0);
    app_lan_reg_operation_complete = false;
    app_lan_op_initiated = false;
    app_lan_state = APP_LAN_WAIT_READ;
}

// LAN865X Register write command  
static void lan_write(SYS_CMD_DEVICE_NODE* pCmdIO, int argc, char** argv) {
    if (argc != 3) {
        SYS_CONSOLE_PRINT("Usage: lan_write <address_hex> <value_hex>\n\r");
        SYS_CONSOLE_PRINT("Example: lan_write 0x00040000 0x12345678\n\r");
        return;
    }

    if (app_lan_state != APP_LAN_IDLE) {
        SYS_CONSOLE_PRINT("ERROR: Previous LAN operation still in progress\n\r");
        return;
    }

    app_lan_addr  = strtoul(argv[1], NULL, 0);
    app_lan_value = strtoul(argv[2], NULL, 0);
    app_lan_reg_operation_complete = false;
    app_lan_op_initiated = false;
    app_lan_state = APP_LAN_WAIT_WRITE;
}


/* Apply PLCA node id + node count to the LAN865x. Sets the driver node id and queues
 * the PLCA_CTRL1 register write via the app's LAN state machine. Shared by cmd_plca_node
 * and the persistent env layer. Skips if a LAN register op is already in progress. */
void APP_ApplyPlca(uint8_t node_id, uint8_t node_cnt) {
    if (app_lan_state != APP_LAN_IDLE) {
        SYS_CONSOLE_PRINT("[PLCA] LAN busy - apply skipped (retry when idle)\r\n");
        return;
    }
    s_plca_node_id = node_id;
    /* Update driver internal state so LOFE re-init uses the new node ID */
    DRV_LAN865X_SetPlcaNodeId(0u, node_id);
    /* Write PLCA_CTRL1 register: bits[15:8]=NODE_CNT, bits[7:0]=NODE_ID */
    app_lan_addr  = 0x0004CA02u;
    app_lan_value = ((uint32_t)node_cnt << 8u) | node_id;
    app_lan_reg_operation_complete = false;
    app_lan_op_initiated = false;
    app_lan_state = APP_LAN_WAIT_WRITE;
    SYS_CONSOLE_PRINT("[PLCA] node ID set to %u (NODE_CNT=%u, reg=0x%08lX)\r\n",
                      (unsigned)node_id, (unsigned)node_cnt, app_lan_value);
}

static void cmd_plca_node(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv) {
    if (argc < 2) {
        /* No parameter: show current node ID + the env-configured node count */
        SYS_CONSOLE_PRINT("[PLCA] current node ID: %u (NODE_CNT=%u)\r\n",
                          (unsigned)s_plca_node_id, (unsigned)env_plca_cnt());
        return;
    }
    /* Live override (not persisted - use 'setenv plca_id'/'saveenv' for that). The
     * node count comes from the persistent env config so the two stay consistent. */
    APP_ApplyPlca((uint8_t)strtoul(argv[1], NULL, 0), env_plca_cnt());
}

uint8_t frame[60];

/* noip_send <n> [gap_ms]  — send N raw Ethernet frames (EtherType 0x88B5) on eth0/T1S */
static void cmd_noip_send(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    uint32_t count = 5u;
    uint32_t gap_ms = 0u;
    if (argc >= 2) { count = (uint32_t)strtoul(argv[1], NULL, 10); }
    if (argc >= 3) { gap_ms = (uint32_t)strtoul(argv[2], NULL, 10); }
    if (count == 0u || count > 100u) {
        SYS_CONSOLE_PRINT("[NoIP] count must be 1..100\r\n");
        return;
    }
    if (gap_ms > 1000u) {
        SYS_CONSOLE_PRINT("[NoIP] gap_ms must be 0..1000\r\n");
        return;
    }

    SYS_CONSOLE_PRINT("[NoIP-TX] start count=%u gap_ms=%u\r\n", (unsigned)count, (unsigned)gap_ms);

    /* Get our MAC from the T1S interface (index 0 = eth0) */
    TCPIP_NET_HANDLE netH = TCPIP_STACK_IndexToNet(0);
    const uint8_t  *pMac  = TCPIP_STACK_NetAddressMac(netH);

    
    /* DST: Layer-2 broadcast */
    frame[0]=0xFFu; frame[1]=0xFFu; frame[2]=0xFFu;
    frame[3]=0xFFu; frame[4]=0xFFu; frame[5]=0xFFu;
    /* SRC: our MAC */
    if (pMac != NULL) { memcpy(&frame[6], pMac, 6u); }
    else              { memset(&frame[6], 0u,   6u); }
    /* EtherType 0x88B5 */
    frame[12] = (uint8_t)((NOIP_ETHERTYPE >> 8u) & 0xFFu);
    frame[13] = (uint8_t)( NOIP_ETHERTYPE        & 0xFFu);
    /* Payload: 4-byte sequence + 42-byte fill to reach 60-byte min frame */
    memset(&frame[14], 0xAAu, 46u);

    uint32_t i;
    for (i = 0u; i < count; i++) {
        noip_tx_cnt++;
        frame[14] = (uint8_t)((noip_tx_cnt >> 24u) & 0xFFu);
        frame[15] = (uint8_t)((noip_tx_cnt >> 16u) & 0xFFu);
        frame[16] = (uint8_t)((noip_tx_cnt >>  8u) & 0xFFu);
        frame[17] = (uint8_t)( noip_tx_cnt          & 0xFFu);
        if (!DRV_LAN865X_SendRawEthFrame(0u, frame, (uint16_t)sizeof(frame), 0x00u, NULL, NULL)) {
            SYS_CONSOLE_PRINT("[NoIP-TX] send failed at seq=%u\r\n", (unsigned)noip_tx_cnt);
            noip_tx_cnt--;
            break;
        }
        SYS_CONSOLE_PRINT("[NoIP-TX] sent seq=%u\r\n", (unsigned)noip_tx_cnt);
        if (gap_ms > 0u) {
            app_wait_ms(gap_ms);
        }
    }
}

static void cmd_noip_stat(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    SYS_CONSOLE_PRINT("[NoIP] TX=%u  RX=%u\r\n", (unsigned)noip_tx_cnt, (unsigned)noip_rx_cnt);
}

/* meminfo: free memory on BOTH heaps.
 *  - C-runtime heap: XC32 uses nano-malloc (no mallinfo, and the whole heap is
 *    sbrk'd up front with free blocks tracked internally), so we report the total
 *    reserved size (_eheap-_heap) and PROBE the largest allocatable block with a
 *    non-destructive malloc/free binary search - a real "largest free chunk".
 *  - TCP/IP stack heap: the DRAM pool where packets/sockets/the MAC bridge
 *    allocate (same figures as the built-in 'heapinfo'). */
extern char _heap;            /* linker: C-runtime heap start (absolute symbol)  */
extern char _eheap;           /* linker: C-runtime heap end (= _heap + heap size) */
static size_t cheap_largest_free(size_t cap) {
    size_t lo = 1u, hi = cap, best = 0u;
    while (lo <= hi) {
        size_t mid = lo + (hi - lo) / 2u;
        void *p = malloc(mid);
        if (p) { free(p); best = mid; lo = mid + 1u; }
        else   { if (mid == 0u) break; hi = mid - 1u; }
    }
    return best;
}
static void cmd_meminfo(SYS_CMD_DEVICE_NODE* pCmdIO, int argc, char** argv) {
    size_t total = (size_t)((uintptr_t)&_eheap - (uintptr_t)&_heap);  /* via uintptr_t: not UB pointer subtraction */
    size_t largest = cheap_largest_free(total);
    TCPIP_STACK_HEAP_HANDLE h;
    (void)pCmdIO; (void)argc; (void)argv;

    SYS_CONSOLE_PRINT("C-runtime heap: total=%u  largest free block=%u  (nano-malloc; no exact free count)\r\n",
        (unsigned)total, (unsigned)largest);

    h = TCPIP_STACK_HeapHandleGet(TCPIP_STACK_HEAP_TYPE_INTERNAL_HEAP, 0);
    if (h != 0) {
        SYS_CONSOLE_PRINT("TCP/IP heap:    size=%u  free=%u  maxblock=%u  highwater=%u\r\n",
            (unsigned)TCPIP_STACK_HEAP_Size(h), (unsigned)TCPIP_STACK_HEAP_FreeSize(h),
            (unsigned)TCPIP_STACK_HEAP_MaxSize(h), (unsigned)TCPIP_STACK_HEAP_HighWatermark(h));
    } else {
        SYS_CONSOLE_PRINT("TCP/IP heap:    (no handle)\r\n");
    }
}

const SYS_CMD_DESCRIPTOR msd_cmd_tbl[] = {
    {"help", (SYS_CMD_FNC) test_help, ": show Test group commands"},
    {"timestamp", (SYS_CMD_FNC) show_timestamp, ": show build timestamp"},
    {"ipdump", (SYS_CMD_FNC) my_dump, ": dump rx ip packets (0:off 1:eth0 2:eth1 3:both)"},
    {"mirror", (SYS_CMD_FNC) cmd_mirror, ": mirror eth0(T1S) RX to eth1 for Wireshark (mirror [0|1])"},
    {"stats", (SYS_CMD_FNC) cmd_stats, ": show TX/RX counters for eth0 and eth1"},
    {"meminfo", (SYS_CMD_FNC) cmd_meminfo, ": free memory on the C-runtime heap and the TCP/IP heap"},
    {"lan_read", (SYS_CMD_FNC) lan_read, ": read LAN865X register (lan_read <addr_hex>)"},
    {"lan_write", (SYS_CMD_FNC) lan_write, ": write LAN865X register (lan_write <addr_hex> <value_hex>)"},
    {"dump", (SYS_CMD_FNC) cmd_mem_dump, ": dump memory (dump <addr_hex> <count>)"},
    {"plca_node",    (SYS_CMD_FNC) cmd_plca_node,    ": get/set PLCA node ID (plca_node [id], no arg: show current)"},
    {"noip_send",    (SYS_CMD_FNC) cmd_noip_send,    ": send N raw Ethernet frames bypassing TCP stack (noip_send <n> [gap_ms])"},
    {"noip_stat",    (SYS_CMD_FNC) cmd_noip_stat,    ": show NoIP TX/RX counters"},
    {"logclear",     (SYS_CMD_FNC) cmd_logclear,     ": clear deferred packet log buffer"},
    {"logstat",      (SYS_CMD_FNC) cmd_logstat,      ": show deferred log statistics (total, pending, overflows)"},
};

bool Command_Init(void) {
    bool ret = true;  // Start with success

    if (!SYS_CMD_ADDGRP(msd_cmd_tbl, sizeof (msd_cmd_tbl) / sizeof (*msd_cmd_tbl), "Test", ": Test Commands")) {
        ret = false;  // If SYS_CMD_ADDGRP fails, return failure
    }
    return ret;
}


/*******************************************************************************
 End of File
 */
