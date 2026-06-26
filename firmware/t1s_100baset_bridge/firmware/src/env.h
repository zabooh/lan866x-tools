/*
 * env.h - persistent network "environment" for the bridge firmware.
 *
 * A versioned, CRC-protected network config (per-interface IP/mask/gateway/DNS)
 * stored in the Emulated EEPROM. Compiled defaults come from configuration.h; on
 * first boot (blank EEPROM) they are seeded. CLI: showenv/setenv/saveenv/readenv/resetenv.
 */
#ifndef ENV_H
#define ENV_H

/* Load the config from the Emulated EEPROM (or seed it from the compiled defaults
 * on first boot) and register the "env" CLI command group. Call once from
 * APP_Initialize, after EMU_EEPROM_Initialize(). */
void ENV_Init(void);

/* Push the current config into the TCP/IP stack (TCPIP_STACK_NetAddressSet/...) and
 * the LAN865x PLCA (via APP_ApplyPlca). Call once the stack is up (APP_STATE_SERVICE_TASKS). */
void env_apply(void);

/* Current PLCA node id / node count from the env (eth0 / LAN865x). */
#include <stdint.h>
uint8_t env_plca_id(void);
uint8_t env_plca_cnt(void);

/* Format the env MAC for interface 0/1 as "XX:XX:XX:XX:XX:XX" into buf (>= 18 bytes).
 * Call after ENV_Init(), before TCPIP_STACK_Init(), to fill the stack's MAC strings. */
void env_mac_str(int iface, char *buf);

#endif /* ENV_H */
