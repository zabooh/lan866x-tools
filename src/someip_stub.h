/*
 * someip_stub.h  -  Platform-neutral SOME/IP platform stub (single-thread).
 *
 * The stub implements the libsomeip SOMEIP_CB_* callbacks on top of the narrow
 * plat.h layer. It has NO threads: received datagrams are dispatched
 * synchronously and the SD/timer maintenance runs once per superloop tick from
 * someip_service(). Porting to a new target means writing plat_<target>.c only.
 */
#ifndef SOMEIP_STUB_H
#define SOMEIP_STUB_H

/* One superloop tick of the SOME/IP platform layer:
 *   1. plat_udp_poll()            - drain UDP RX, dispatch synchronously
 *   2. SOMEIP_Client_CheckTimers()- run the SD state machine
 *   3. network maintenance        - (throttled) interface enum + SD multicast join
 * The application/rcp pump calls this each iteration while waiting. */
void someip_service(void);

#endif /* SOMEIP_STUB_H */
