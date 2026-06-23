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
#include "ptp_ts_ipc.h"
#include "PTP_FOL_task.h"
#include "ptp_gm_task.h"
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
uint32_t fwd_mode = 0;
uint32_t my_delay_time = 0;

/* --- NoIP raw Ethernet test (EtherType 0x88B5 = IEEE 802 Local Experimental) --- */
#define NOIP_ETHERTYPE  0x88B5u
static uint32_t noip_tx_cnt = 0u;
static uint32_t noip_rx_cnt = 0u;
SYS_TIME_HANDLE timerHandle;

/* Maximum expected PTP frame size on wire.
 * Sync/FollowUp messages are ≤ 76 bytes; Announce up to ~90 bytes.
 * 128 bytes gives comfortable headroom for all standard PTP message types. */
#define PTP_MAX_FRAME_SIZE  128u

/* Buffer for a single pending PTP frame received in pktEth0Handler */
typedef struct {
    uint8_t  data[PTP_MAX_FRAME_SIZE]; /* buffer for PTP frame payload             */
    uint16_t length;                   /* frame length in bytes                    */
    uint64_t rxTimestamp;              /* hardware RX timestamp from LAN865x       */
    bool     pending;                  /* true when a frame is waiting to be read  */
} PTP_FRAME_BUFFER;

static PTP_FRAME_BUFFER ptp_rx_buffer = {0};

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
    PKT_LOG_PTP  = 1,  /* PTP  (0x88F7) frame from eth0 */
    PKT_LOG_ETH0 = 2,  /* generic frame from eth0        */
    PKT_LOG_ETH1 = 3,  /* generic frame from eth1        */
} pkt_log_type_t;

typedef struct {
    uint64_t       timestamp;    /* SYS_TIME_Counter64Get()                    */
    uint32_t       pkt_counter;  /* per-handler packet counter                 */
    uint64_t       ptp_ts;       /* hardware PTP RX timestamp                  */
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

/* Track LAN865x driver ready state to detect reinit-complete while in GM mode */
static bool lan865x_prev_ready = false;

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
    SYS_CONSOLE_PRINT("  fwd [mode]                   - Set forwarding (0=off, 1=on)\n\r");
    SYS_CONSOLE_PRINT("  stats                        - Show TX/RX software counters for eth0 and eth1\n\r");
    SYS_CONSOLE_PRINT("  lan_read  <addr>             - Read  LAN865X register (hex address)\n\r");
    SYS_CONSOLE_PRINT("  lan_write <addr> <value>     - Write LAN865X register (hex addr, hex value)\n\r");
    SYS_CONSOLE_PRINT("  dump <addr> <count>          - Dump memory (hex addr, count)\n\r");
    SYS_CONSOLE_PRINT("  plca_node [id]               - Get/set PLCA node ID (no arg = show current)\n\r");
    SYS_CONSOLE_PRINT("  ptp_mode [off|follower|master] - Set PTP operating mode\n\r");
    SYS_CONSOLE_PRINT("  ptp_status                   - Show PTP mode, sync count, offset\n\r");
    SYS_CONSOLE_PRINT("  ptp_interval <ms>            - Set GM sync interval in ms (default 125)\n\r");
    SYS_CONSOLE_PRINT("  ptp_dst [multicast|broadcast]- Set PTP destination MAC\n\r");
    SYS_CONSOLE_PRINT("  ptp_offset                   - Show follower time offset [ns]\n\r");
    SYS_CONSOLE_PRINT("  ptp_reset                    - Reset PTP follower servo to UNINIT\n\r");
    SYS_CONSOLE_PRINT("  ptp_regs                     - Dump TX-Match registers (no SPI collision)\n\r");
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
            PTP_FOL_Init();
            appData.state = APP_STATE_IDLE;
            break;
        }

            /* TODO: implement your application state machine.*/
        case APP_STATE_IDLE:
        {
            static uint64_t last_gm_tick  = 0u;
            static uint64_t last_fol_tick = 0u;
            static uint64_t ticks_per_ms  = 0u;
            if (ticks_per_ms == 0u) {
                ticks_per_ms = (uint64_t)SYS_TIME_FrequencyGet() / 1000ULL;
            }
            uint64_t current_tick = SYS_TIME_Counter64Get();

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

            /* === GM Service: call PTP_GM_Service() every 1 ms === */
            if (PTP_FOL_GetMode() == PTP_MASTER) {
                if ((current_tick - last_gm_tick) >= ticks_per_ms) {
                    PTP_GM_Service();
                    last_gm_tick = current_tick;
                }
            }

            if (PTP_FOL_GetMode() == PTP_SLAVE) {
                if ((current_tick - last_fol_tick) >= ticks_per_ms) {
                    PTP_FOL_Service();
                    last_fol_tick = current_tick;
                }
            }

            /* === FOL Service: process a buffered PTP frame ===
             * ptp_rx_buffer.pending is set by pktEth0Handler() and cleared here.
             * On ARM Cortex-M, aligned bool writes are single-instruction atomic,
             * so no explicit critical-section is needed for this flag.
             * All standard PTP message types (Sync, FollowUp, Announce, Delay_Req)
             * are buffered here; stale frames of any type are overwritten by the
             * next arrival, which is acceptable because PTP frames arrive at a
             * maximum rate of once every 125 ms and APP_Tasks() runs far more
             * frequently than that. */
            if (PTP_FOL_GetMode() == PTP_SLAVE && ptp_rx_buffer.pending) {
                ptp_rx_buffer.pending = false;
                PTP_FOL_OnFrame(ptp_rx_buffer.data,
                                ptp_rx_buffer.length,
                                ptp_rx_buffer.rxTimestamp);
            }

            /* Re-run PTP_GM_Init() if the LAN865x driver recovers from a
             * reinit (triggered by TC6Error_SyncLost / LOFE) while in GM mode.
             * The reinit clears all TX-Match registers written by PTP_GM_Init(),
             * so they must be reprogrammed once the driver is READY again. */
            bool lan865x_ready = DRV_LAN865X_IsReady(0u);
            if (!lan865x_prev_ready && lan865x_ready &&
                (PTP_FOL_GetMode() == PTP_MASTER))
            {
                SYS_CONSOLE_PRINT("[PTP-GM] driver ready after reinit - re-applying TX-Match config\r\n");
                PTP_GM_Init();
            }
            lan865x_prev_ready = lan865x_ready;

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
                        case PKT_LOG_PTP:
                            SYS_CONSOLE_PRINT("E0:PTP[0x88F7] len=%u ts=%llu%s\r\n",
                                (unsigned)log_e.length,
                                (unsigned long long)log_e.ptp_ts,
                                log_e.truncated ? " [TRUNC]" : "");
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

bool pktEth0Handler(TCPIP_NET_HANDLE hNet, struct _tag_TCPIP_MAC_PACKET* rxPkt, uint16_t frameType, const void* hParam) {
    static uint32_t packet_counter = 0;
    bool ret_val = false;

    packet_counter++;

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

    /* PTP frame (EtherType 0x88F7): buffer for processing in APP_Tasks, do not forward to IP stack */
    if (frameType == 0x88F7u) {
        uint64_t rxTs = 0u;
        if (g_ptp_rx_ts.valid) {
            rxTs = g_ptp_rx_ts.rxTimestamp;
            g_ptp_rx_ts.valid = false;
        }
        if (ipdump_mode == 1u || ipdump_mode == 3u) {
            PKT_LOG_ENTRY log_e = {0};
            log_e.timestamp   = SYS_TIME_Counter64Get();
            log_e.pkt_counter = packet_counter;
            log_e.ptp_ts      = rxTs;
            log_e.frame_type  = frameType;
            log_e.length      = rxPkt->pDSeg->segLen;
            log_e.iface       = 0u;
            log_e.log_type    = PKT_LOG_PTP;
            memcpy(log_e.mac_src, &rxPkt->pMacLayer[6], 6u);
            PktLog_Write(&log_e, rxPkt->pMacLayer, rxPkt->pDSeg->segLen);
        }
        /* Store the frame for later processing in APP_Tasks().
         * If a frame is already pending, the older (stale) frame is overwritten
         * because PTP Sync frames that are not processed promptly become irrelevant. */
        uint16_t copyLen = rxPkt->pDSeg->segLen;
        if (copyLen > (uint16_t)sizeof(ptp_rx_buffer.data)) {
            copyLen = (uint16_t)sizeof(ptp_rx_buffer.data);
        }
        memcpy(ptp_rx_buffer.data, rxPkt->pMacLayer, copyLen);
        ptp_rx_buffer.length      = copyLen;
        ptp_rx_buffer.rxTimestamp = rxTs;
        ptp_rx_buffer.pending     = true;
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

    if ( fwd_mode == 1) {
        /* Raw IP Packet received from eth0 (T1S) is send to eth1 (100BaseT)*/
        TCPIP_NET_HANDLE NetHdl = TCPIP_STACK_IndexToNet(1);
        DRV_GMAC_PacketTx(((TCPIP_NET_IF*) NetHdl)->hIfMac, rxPkt);
        ret_val = true;
    }

    /* a return value of true, tell the TCP stack not to handle this packet */
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

static void my_fwd(SYS_CMD_DEVICE_NODE* pCmdIO, int argc, char** argv) {

    fwd_mode = strtoul(argv[1], NULL, 16);
    if (fwd_mode == 0) {
        SYS_CONSOLE_PRINT("Forward mode set to off\n\r");
    } else if (fwd_mode == 1) {
        SYS_CONSOLE_PRINT("Forward mode set to on\n\r");
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


static void cmd_plca_node(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv) {
    if (argc < 2) {
        /* No parameter: show current node ID */
        SYS_CONSOLE_PRINT("[PLCA] current node ID: %u (NODE_CNT=%u)\r\n",
                          (unsigned)s_plca_node_id,
                          (unsigned)DRV_LAN865X_PLCA_NODE_COUNT_IDX0);
        return;
    }
    if (app_lan_state != APP_LAN_IDLE) {
        SYS_CONSOLE_PRINT("ERROR: LAN operation still in progress\r\n");
        return;
    }
    uint8_t nodeId = (uint8_t)strtoul(argv[1], NULL, 0);
    s_plca_node_id = nodeId;
    /* Update driver internal state so LOFE re-init uses the new node ID */
    DRV_LAN865X_SetPlcaNodeId(0u, nodeId);
    /* Write PLCA_CTRL1 register: bits[15:8]=NODE_CNT, bits[7:0]=NODE_ID */
    app_lan_addr  = 0x0004CA02u;
    app_lan_value = ((uint32_t)DRV_LAN865X_PLCA_NODE_COUNT_IDX0 << 8u) | nodeId;
    app_lan_reg_operation_complete = false;
    app_lan_op_initiated = false;
    app_lan_state = APP_LAN_WAIT_WRITE;
    SYS_CONSOLE_PRINT("[PLCA] node ID set to %u (NODE_CNT=%u, reg=0x%08lX)\r\n",
                      (unsigned)nodeId,
                      (unsigned)DRV_LAN865X_PLCA_NODE_COUNT_IDX0,
                      app_lan_value);
}

static void cmd_ptp_mode(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv) {
    if (argc != 2) {
        SYS_CONSOLE_PRINT("Usage: ptp_mode [off|follower|master]\r\n");
        return;
    }
    if (strcmp(argv[1], "off") == 0) {
        PTP_GM_Deinit();
        PTP_FOL_SetMode(PTP_DISABLED);
        SYS_CONSOLE_PRINT("[PTP] disabled\r\n");
    } else if (strcmp(argv[1], "follower") == 0) {
        PTP_FOL_SetMode(PTP_SLAVE);
        SYS_CONSOLE_PRINT("[PTP] follower mode (PLCA node %u)\r\n", (unsigned)DRV_LAN865X_PLCA_NODE_ID_IDX0);
    } else if (strcmp(argv[1], "master") == 0) {
        PTP_GM_Init();
        PTP_FOL_SetMode(PTP_MASTER);
        SYS_CONSOLE_PRINT("[PTP] grandmaster mode (PLCA node 0)\r\n");
    } else {
        SYS_CONSOLE_PRINT("Unknown mode: %s\r\n", argv[1]);
    }
}

static void cmd_ptp_status(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv) {
    const char *modeStr[] = {"disabled", "master", "slave"};
    uint32_t cnt = 0u, state = 0u;
    PTP_GM_GetStatus(&cnt, &state);
    SYS_CONSOLE_PRINT("[PTP] mode=%s gmSyncs=%u gmState=%u\r\n",
                       modeStr[PTP_FOL_GetMode()],
                       (unsigned)cnt, (unsigned)state);
}

static void cmd_ptp_interval(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv) {
    if (argc != 2) {
        SYS_CONSOLE_PRINT("Usage: ptp_interval <ms>\r\n");
        return;
    }
    uint32_t ms = (uint32_t)strtoul(argv[1], NULL, 10);
    PTP_GM_SetSyncInterval(ms);
    SYS_CONSOLE_PRINT("[PTP-GM] sync interval set to %u ms\r\n", (unsigned)ms);
}

static void cmd_ptp_dst(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv) {
    if (argc != 2) {
        ptp_gm_dst_mode_t mode = PTP_GM_GetDstMode();
        SYS_CONSOLE_PRINT("Usage: ptp_dst [multicast|broadcast]\r\n");
        SYS_CONSOLE_PRINT("[PTP-GM] dst=%s\r\n",
                          (mode == PTP_GM_DST_BROADCAST) ? "broadcast" : "multicast");
        return;
    }

    if (strcmp(argv[1], "multicast") == 0 || strcmp(argv[1], "mc") == 0) {
        PTP_GM_SetDstMode(PTP_GM_DST_MULTICAST);
        SYS_CONSOLE_PRINT("[PTP-GM] dst=multicast (01:80:C2:00:00:0E)\r\n");
    } else if (strcmp(argv[1], "broadcast") == 0 || strcmp(argv[1], "bc") == 0) {
        PTP_GM_SetDstMode(PTP_GM_DST_BROADCAST);
        SYS_CONSOLE_PRINT("[PTP-GM] dst=broadcast (FF:FF:FF:FF:FF:FF)\r\n");
    } else {
        SYS_CONSOLE_PRINT("Unknown dst mode: %s\r\n", argv[1]);
    }
}

static void cmd_ptp_offset(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv) {
    int64_t  off     = 0;
    uint64_t off_abs = 0;
    PTP_FOL_GetOffset(&off, &off_abs);
    SYS_CONSOLE_PRINT("[PTP] offset=%lld ns  abs=%llu ns\r\n",
                      (long long)off, (unsigned long long)off_abs);
}

static void cmd_ptp_reset(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv) {
    PTP_FOL_Reset();
    SYS_CONSOLE_PRINT("[PTP] follower servo reset to UNINIT\r\n");
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

static void cmd_ptp_regs(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    (void)pCmdIO; (void)argc; (void)argv;
    /* Triggers a register dump from within PTP_GM_Service (GM_STATE_WAIT_PERIOD).
     * This avoids injecting TC6 control chunks during active PLCA traffic, which
     * would cause TC6Error_SyncLost (Loss of Framing Error). */
    PTP_GM_RequestRegDump();
    SYS_CONSOLE_PRINT("[PTP] reg dump requested — output follows after next WAIT_PERIOD\r\n");
}

const SYS_CMD_DESCRIPTOR msd_cmd_tbl[] = {
    {"help", (SYS_CMD_FNC) test_help, ": show Test group commands"},
    {"timestamp", (SYS_CMD_FNC) show_timestamp, ": show build timestamp"},
    {"ipdump", (SYS_CMD_FNC) my_dump, ": dump rx ip packets (0:off 1:eth0 2:eth1 3:both)"},
    {"fwd", (SYS_CMD_FNC) my_fwd, ": fwd (0:off 1:on default:on)"},
    {"stats", (SYS_CMD_FNC) cmd_stats, ": show TX/RX counters for eth0 and eth1"},
    {"lan_read", (SYS_CMD_FNC) lan_read, ": read LAN865X register (lan_read <addr_hex>)"},
    {"lan_write", (SYS_CMD_FNC) lan_write, ": write LAN865X register (lan_write <addr_hex> <value_hex>)"},
    {"dump", (SYS_CMD_FNC) cmd_mem_dump, ": dump memory (dump <addr_hex> <count>)"},
    {"plca_node",    (SYS_CMD_FNC) cmd_plca_node,    ": get/set PLCA node ID (plca_node [id], no arg: show current)"},
    {"ptp_mode",     (SYS_CMD_FNC) cmd_ptp_mode,     ": set PTP mode (off|follower|master)"},
    {"ptp_status",   (SYS_CMD_FNC) cmd_ptp_status,   ": show PTP mode, sync count, offset"},
    {"ptp_interval", (SYS_CMD_FNC) cmd_ptp_interval, ": set GM Sync interval in ms (default 125)"},
    {"ptp_dst",      (SYS_CMD_FNC) cmd_ptp_dst,      ": set PTP DST MAC (multicast|broadcast)"},
    {"ptp_offset",   (SYS_CMD_FNC) cmd_ptp_offset,   ": show follower time offset [ns]"},
    {"ptp_reset",    (SYS_CMD_FNC) cmd_ptp_reset,    ": reset PTP follower servo to UNINIT"},
    {"ptp_regs",    (SYS_CMD_FNC) cmd_ptp_regs,    ": dump TX-Match registers via GM state machine (no SPI collision)"},
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
