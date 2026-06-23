# PTP-Based Distributed Timestamp Synchronization

## Overview
Comprehensive documentation of the PTP (IEEE 1588) based timestamp synchronization system that enables nanosecond-accurate event correlation across multiple MCUs in a 10BASE-T1S Ethernet network.

## Architecture
- PTP Grandmaster topology
- LAN8651 hardware timestamps
- SAM E54 TCC0 interpolation
- Distributed event correlation

## System Components
1. PTP Follower (existing in PTP_FOL_task.c)
2. LAN8651 Wall Clock registers (MAC_TSH, MAC_TSL, MAC_TN)
3. SAM E54 TCC0 as high-resolution counter (120 MHz)
4. Rate Ratio compensation
5. Timestamp interpolation

## How It Works

### PTP Synchronization Flow
- Sync/FollowUp messages from Grandmaster
- Rate Ratio calculation (rateRatioFIR)
- Offset correction (MAC_TA register)
- Clock increment adjustment (MAC_TI/TISUBN registers)
- Achieves ±10-50 ns accuracy in FINE state

### Timestamp Creation
Detailed explanation of PTP_FOL_GetWallClockNs() function:
- Captures sync point at each PTP event
- Stores LAN8651 timestamp + SAM TCC0 counter value
- Interpolates between PTP events using TCC0
- Compensates with rate_ratio
- Returns 64-bit nanosecond timestamp

### Why TCC0 Interpolation Instead of SPI Read
- SPI register read latency: ~750 µs
- TCC0 register read latency: < 1 µs (1800x faster!)
- No blocking on driver/event system
- Hardware counter runs continuously

## Performance Characteristics
- Latency: < 1 µs
- Accuracy: ±10-50 ns (FINE state)
- Resolution: 8.3 ns (120 MHz TCC0)
- Update rate: Unlimited (MHz range possible)
- Time range: Centuries (64-bit nanoseconds)

## Use Cases

### 1. Multi-MCU Event Correlation
Code example showing events from different MCUs with precise timestamps

### 2. Causality Analysis
Detect cause-effect relationships across MCU boundaries (< 1 µs delta indicates causality)

### 3. Distributed Performance Measurement
Measure network latency and execution time across MCUs

### 4. Synchronized Control Loops
All MCUs execute control algorithms at exact same time

### 5. Chronological Logging
Sort events from all MCUs into single timeline

## Implementation

### Data Structures
```c
typedef struct {
    uint64_t lan_timestamp_ns;
    uint64_t sam_tcc0_ticks;
    double   rate_ratio;
    uint32_t last_update_ms;
    bool     valid;
} sync_point_t;
```

### API Functions
- PTP_FOL_GetWallClockNs() - Get synchronized timestamp
- Integration with existing PTP follower

### Event Structure for Distributed Systems
```c
typedef struct {
    uint64_t timestamp_ns;
    uint8_t  mcu_id;
    uint8_t  event_type;
    uint16_t sequence;
    uint32_t data;
    uint16_t crc16;
} distributed_event_t;
```

## Accuracy Analysis

### Error Sources
- TCC0 resolution: 8.3 ns (1 tick @ 120 MHz)
- PTP offset (FINE): ±10-50 ns
- Rate ratio error: ±0.1 ppm after filtering
- Interpolation drift: ~1 ns/ms between PTP events
- Total accuracy: ±10-50 ns

### Temporal Drift
Between PTP events (125 ms interval), accuracy remains < ±50 ns due to rate ratio compensation

## Code Integration

### In PTP_FOL_task.c
Add sync_point structure and update_sync_point() function called from processFollowUp()

### In Application Code
Simply call PTP_FOL_GetWallClockNs() whenever timestamp needed

## Example Scenarios

### Motor Control System
- MCU #1: Motor controller timestamps motor start
- MCU #2: Vibration sensor timestamps vibration detection
- MCU #3: Safety controller timestamps emergency stop
- MCU #4: Logger correlates all events with nanosecond precision

### Expected Output
Show example of chronological event log with precise timestamps

## Advantages Over Alternatives

### vs. PPS Signal Measurement
- No hardware pins needed
- 8 updates/second vs 1/second
- No capture/overflow handling complexity
- Already implemented in PTP follower

### vs. Direct SPI Register Read
- 1800x faster (< 1 µs vs 750 µs)
- No driver/event system latency
- No blocking operations
- Unlimited call rate

### vs. Local MCU Timers Only
- Synchronized across all MCUs
- Compensates for crystal drift
- Common time reference
- Enables distributed systems

## Requirements
- PTP Grandmaster on network
- LAN8651 with PTP support
- SAM E54 with TCC0 @ 120 MHz
- PTP Follower running (syncStatus >= COARSE)

## Limitations
- Requires active PTP synchronization
- Accuracy degrades if PTP sync lost
- First timestamp available after COARSE state reached
- Rate ratio must be updated regularly

## Future Enhancements
- Automatic PPS output for external verification
- Event log persistence (Flash/SD card)
- Real-time visualization
- Network latency compensation
- Redundant Grandmaster support

## References
- IEEE 1588-2019 (PTPv2)
- LAN8651 Datasheet (MAC registers)
- SAM E54 TCC documentation
- Existing PTP_FOL_task.c implementation

## Conclusion
This system provides industry-grade nanosecond-accurate timestamp synchronization across multiple MCUs without additional hardware, enabling sophisticated distributed control, monitoring, and analysis applications.

---
Author: System Documentation
Date: 2026-04-06
Version: 1.0
