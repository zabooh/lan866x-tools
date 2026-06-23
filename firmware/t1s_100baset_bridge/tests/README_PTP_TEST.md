# PTP Test Agent Documentation

## Overview
This document provides a comprehensive guide for the PTP (Precision Time Protocol) Test Agent, detailing its functionality, usage, and requirements for the T1S 100BaseT Bridge project.

## Background on PTP Implementation

### What is PTP?
PTP (Precision Time Protocol, IEEE 1588) is a protocol designed to synchronize clocks throughout a computer network with sub-microsecond precision. This implementation uses PTP to synchronize two ATSAME54P20A microcontrollers with LAN865x Ethernet controllers connected via 10BASE-T1S network.

### Architecture
This PTP implementation uses a **Grandmaster-Follower** architecture:
- **Grandmaster (GM)**: The master clock that provides the reference time
- **Follower (FOL)**: The slave device that synchronizes its clock to the Grandmaster

### Key Features
- **Hardware Timestamping**: Uses hardware timestamps from the LAN865x controller to minimize latency and improve accuracy
- **Offset Calculation**: Measures round-trip delay of synchronization messages to calculate time offset
- **State Machine**: The follower progresses through synchronization states: `UNINIT → MATCHFREQ → HARDSYNC → COARSE → FINE`
- **Sub-100ns Precision**: Target synchronization accuracy of ±100 nanoseconds

### Synchronization Process
1. **Frequency Matching (MATCHFREQ)**: Initial frequency alignment between clocks
2. **Hardware Sync (HARDSYNC)**: Hardware-level clock synchronization
3. **Coarse Adjustment (COARSE)**: Large offset corrections
4. **Fine Adjustment (FINE)**: Precise sub-nanosecond adjustments for optimal accuracy

## Hardware Requirements
- **2x ATSAME54P20A Development Boards** with LAN865x Ethernet controllers
- **10BASE-T1S Network Connection** between the two boards
- **2x Serial/USB Connections** for CLI access to both boards (default 115200 baud, 8N1)
- **Power Supply** for both boards

## Installation Instructions
1. Clone the repository: 
   ```bash
   git clone https://github.com/zabooh/t1s_100baset_bridge.git
   ```
2. Navigate into the project directory: 
   ```bash
   cd t1s_100baset_bridge
   ```
3. Checkout to the `vscode-migration` branch: 
   ```bash
   git checkout vscode-migration
   ```
4. Install Python dependencies: 
   ```bash
   pip install pyserial
   ```

## CLI Command Reference

The test agent uses the following CLI commands via serial connection:

### Network Configuration Commands
- **`setip eth0 <IP> <NETMASK>`** - Configure IP address and netmask
  - Example: `setip eth0 192.168.0.20 255.255.255.0`
  - Response: `IP address set to 192.168.0.20`

- **`ping <IP>`** - Test network connectivity
  - Example: `ping 192.168.0.30`
  - Response: `Reply from 192.168.0.30...`

### PTP Control Commands
- **`ptp_mode master`** - Start PTP in Grandmaster mode
  - Response: `[PTP] grandmaster mode`
  
- **`ptp_mode follower`** - Start PTP in Follower mode
  - Response: `[PTP] follower mode`
  
- **`ptp_mode off`** - Disable PTP
  - Response: `[PTP] disabled`

### PTP Monitoring Commands
- **`ptp_status`** - Query current PTP status
  - Response format: `[PTP] mode=<mode> gmSyncs=<count> gmState=<state>`
  - Example: `[PTP] mode=master gmSyncs=198 gmState=3`
  
- **`ptp_offset`** - Get current time offset (Follower only)
  - Response format: `[PTP] offset=+45 ns  abs=45 ns`
  - Shows the time difference between Follower and Grandmaster in nanoseconds

### System Commands
- **`reset`** - Reset the board
- **`--verbose`** - Enable verbose output (test agent parameter)
- **`--help`** - Display help information (test agent parameter)

## Usage Examples

### Basic Test Run
```bash
python tests/ptp_test_agent.py --gm-port COM10 --fol-port COM8
```

### Custom IP Addresses
```bash
python tests/ptp_test_agent.py --gm-port COM10 --fol-port COM8 --gm-ip 10.0.0.1 --fol-ip 10.0.0.2
```

### Verbose Logging
```bash
python tests/ptp_test_agent.py --gm-port COM10 --fol-port COM8 --verbose
```

### Custom Sample Count
```bash
python tests/ptp_test_agent.py --gm-port COM10 --fol-port COM8 --samples 50
```

### Resume from Specific Step
```bash
python tests/ptp_test_agent.py --gm-port COM10 --fol-port COM8 --from-step 3
```

### All CLI Parameters
- `--gm-port PORT` - Grandmaster COM port (default: COM10)
- `--fol-port PORT` - Follower COM port (default: COM8)
- `--gm-ip IP` - Grandmaster IP address (default: 192.168.0.20)
- `--fol-ip IP` - Follower IP address (default: 192.168.0.30)
- `--samples N` - Number of offset samples to collect (default: 20)
- `--convergence-timeout S` - Maximum wait time for FINE state in seconds (default: 30)
- `--from-step N` - Start from step N (1-5, default: 1)
- `--no-stop-ptp` - Don't disable PTP after test completion
- `--log-file FILE` - Custom log file path (default: auto-generated with timestamp)
- `--verbose` - Enable debug output

## Detailed Test Sequence Breakdown

The PTP test agent executes the following steps in sequence:

### Step 0: Reset (Always Executed)
- Sends `reset` command to both boards
- Waits 8 seconds for boards to reboot
- Clears any previous state

### Step 1: IP Configuration
- **GM Board**: Assigns IP address (default 192.168.0.20)
- **Follower Board**: Assigns IP address (default 192.168.0.30)
- **Verification**: Confirms both boards accepted the IP configuration
- **Failure Condition**: If either board doesn't confirm IP assignment

### Step 2: Network Connectivity
- **GM → Follower**: Pings Follower IP to verify bidirectional communication
- **Follower → GM**: Pings GM IP to verify bidirectional communication
- **Verification**: Both pings receive replies
- **Failure Condition**: If either ping fails

### Step 3: PTP Start
- **Follower First**: Starts Follower in `follower` mode (waits for Grandmaster)
- **0.5s Pause**: Brief delay to let Follower initialize
- **Grandmaster Second**: Starts GM in `master` mode
- **Verification**: Both boards confirm their PTP mode
- **Failure Condition**: If either board doesn't confirm mode change

### Step 4: Convergence to FINE State
- **Monitoring**: Watches Follower serial output for state transitions
- **Expected Progression**: 
  1. `UNINIT → MATCHFREQ` - Frequency matching begins
  2. `MATCHFREQ → HARDSYNC` - Hardware sync initiated
  3. `HARDSYNC → COARSE` - Coarse offset adjustment
  4. `COARSE → FINE` - Fine-tuned synchronization achieved
- **Timeout**: 30 seconds (configurable via `--convergence-timeout`)
- **Milestones Logged**: Timestamp of each state transition
- **Verification**: FINE state is reached
- **Failure Condition**: FINE state not reached within timeout

### Step 5: Offset Validation
- **Sample Collection**: Queries `ptp_offset` command N times (default 20)
- **Interval**: 0.2 seconds between samples
- **Parsing**: Extracts offset value in nanoseconds from each response
- **Statistics Calculated**:
  - Mean offset
  - Standard deviation
  - Minimum offset
  - Maximum offset
  - Percentage within ±100ns threshold
- **Verification**: ALL samples must be within ±100ns
- **Failure Condition**: Any sample exceeds ±100ns threshold

### Post-Test Actions
- **PTP Status Check**: Queries both boards for final status
- **Cleanup**: Sends `ptp_mode off` to both boards (unless `--no-stop-ptp` specified)
- **Report Generation**: Creates detailed test report with statistics

## Expected Results in PASS Case

### Successful Test Output

When the test passes, you should see:

```
============================================================
  PTP Functionality Test Report
============================================================
Date      : 2026-04-05 14:23:45
GM Port   : COM10 (192.168.0.20)
FOL Port  : COM8 (192.168.0.30)

[PASS] Step 0: Reset
       GM reset sent; FOL reset sent
[PASS] Step 1: IP Configuration
       GM IP ok (192.168.0.20); FOL IP ok (192.168.0.30)
[PASS] Step 2: Network Connectivity (GM→FOL, FOL→GM)
       GM →FOL ok; FOL→GM  ok
[PASS] Step 3: PTP Start
       FOL start ok; GM start ok
[PASS] Step 4: Convergence to FINE state
       FINE reached in 8.3s
[PASS] Step 5: Offset Validation
       mean=-12.5ns stdev=18.2ns min=-45ns max=+38ns within±100ns: 20/20 (100%)

  Offset Statistics:
    Samples : 20
    Mean    : -12.5 ns
    Stdev   : 18.2 ns
    Min     : -45 ns
    Max     : +38 ns
    Within ±100ns: 20/20 (100%)

Overall Result: PASS (6/6 tests passed)
```

### Key PASS Criteria

1. **IP Configuration**
   - Both boards confirm IP address assignment
   - No error messages

2. **Network Connectivity**
   - Both ping directions receive replies
   - Response time typically < 10ms

3. **PTP Start**
   - Follower confirms: `[PTP] follower mode`
   - Grandmaster confirms: `[PTP] grandmaster mode`

4. **Convergence**
   - FINE state reached within 30 seconds
   - Typical convergence time: 5-15 seconds
   - All intermediate states observed (MATCHFREQ, HARDSYNC, COARSE)

5. **Offset Validation**
   - **100% of samples within ±100ns** (mandatory)
   - Mean offset typically < ±50ns
   - Standard deviation typically < 30ns
   - Stable readings without large jumps

### Typical Performance Metrics

| Metric | Expected Value |
|--------|----------------|
| Convergence Time | 5-15 seconds |
| Mean Offset | ±50 ns or better |
| Standard Deviation | < 30 ns |
| Within Threshold | 100% (20/20 samples) |
| Min/Max Range | Within ±100 ns |

## Troubleshooting Guide

### Issue: No response from the PTP Grandmaster
**Symptoms**: Follower doesn't progress past UNINIT or MATCHFREQ state
**Solutions**:
- Check Ethernet cable connection between boards
- Verify Grandmaster started before Follower
- Ensure network interfaces are UP (`netinfo` command)
- Check if Grandmaster is actually running (`ptp_status` on GM)

### Issue: Inconsistent time results
**Symptoms**: Offset samples vary wildly or exceed ±100ns threshold
**Solutions**:
- Verify hardware timestamps are enabled in firmware configuration
- Check for electromagnetic interference near the boards
- Ensure stable power supply to both boards
- Verify Ethernet PHY link status
- Re-run test after reset

### Issue: IP configuration fails
**Symptoms**: Step 1 fails with no confirmation message
**Solutions**:
- Check serial connection (correct COM port, baud rate 115200)
- Verify boards are powered on and running firmware
- Try manual `setip` command to debug

### Issue: Ping fails but IPs are set
**Symptoms**: Step 2 fails with no ping reply
**Solutions**:
- Verify Ethernet cable is properly connected
- Check that both boards are on the same subnet
- Use `netinfo` command to verify interface status
- Check for LED activity on Ethernet PHY

### Issue: Convergence timeout
**Symptoms**: Step 4 fails, FINE state not reached
**Solutions**:
- Increase `--convergence-timeout` to 60 seconds
- Check Follower serial output for error messages
- Verify PTP packets are being exchanged (use `ptp_status`)
- Ensure clock sources are stable

### Issue: Serial communication errors
**Symptoms**: Garbled text, missing responses, or connection failures
**Solutions**:
- Verify COM port settings: 115200 baud, 8 data bits, no parity, 1 stop bit
- Close any other programs using the COM ports
- Check USB cable quality
- Try different USB ports

## Advanced Usage Patterns

### Continuous Monitoring
Run multiple test iterations to verify long-term stability:
```bash
for i in {1..10}; do
  echo "=== Test Run $i ==="
  python tests/ptp_test_agent.py --gm-port COM10 --fol-port COM8
  sleep 5
done
```

### Network Stress Testing
Combine with network load generators to test PTP performance under traffic:
```bash
# Terminal 1: Start PTP test
python tests/ptp_test_agent.py --gm-port COM10 --fol-port COM8 --no-stop-ptp

# Terminal 2: Generate network traffic (external tool)
# iperf, ping flood, etc.
```

### Custom Threshold Testing
Modify `OFFSET_THRESHOLD_NS` in the script to test different precision requirements:
```python
OFFSET_THRESHOLD_NS = 50  # Stricter: ±50ns
OFFSET_THRESHOLD_NS = 200 # Relaxed: ±200ns
```

### Debugging with Verbose Logs
Capture detailed protocol exchange for analysis:
```bash
python tests/ptp_test_agent.py --gm-port COM10 --fol-port COM8 --verbose --log-file debug.log
```

## Result Interpretation Guidelines

### Understanding Offset Values
- **Positive Offset (+50 ns)**: Follower clock is ahead of Grandmaster by 50ns
- **Negative Offset (-30 ns)**: Follower clock is behind Grandmaster by 30ns
- **Zero Offset (0 ns)**: Perfect synchronization (rare, typically within ±5ns)

### Statistical Significance
- **Mean Offset**: Indicates systematic bias; ideally close to zero
- **Standard Deviation**: Measures jitter/stability; lower is better
- **Min/Max Range**: Should be narrow; wide ranges indicate instability

### Network Quality Indicators
Monitor these metrics to assess network performance:
- **Convergence Time**: Faster indicates better network conditions
- **Offset Stability**: Consistent values indicate low jitter
- **State Transitions**: Smooth progression indicates proper operation

### When to Re-run Tests
Consider re-running if:
- Convergence takes > 20 seconds
- Offset standard deviation > 40ns
- Any samples exceed ±80ns (warning threshold)
- Intermittent failures occur

## Technical Details about Offset Calculation and Hardware Timestamping

### Offset Calculation Method
The PTP implementation uses the **Delay Request-Response Mechanism**:

1. **Sync Message (GM → Follower)**:
   - Grandmaster sends Sync message at time T1 (hardware timestamped)
   - Follower receives at time T2 (hardware timestamped)

2. **Follow-Up Message (GM → Follower)**:
   - GM sends precise T1 timestamp

3. **Delay Request (Follower → GM)**:
   - Follower sends Delay_Req at time T3 (hardware timestamped)
   - GM receives at time T4 (hardware timestamped)

4. **Delay Response (GM → Follower)**:
   - GM sends T4 back to Follower

5. **Offset Calculation**:
   ```
   Offset = ((T2 - T1) - (T4 - T3)) / 2
   ```

### Hardware Timestamping Benefits
- **Reduced Latency**: Timestamps captured in PHY hardware, not software
- **Improved Accuracy**: Eliminates CPU and OS scheduling jitter
- **Consistency**: Deterministic timestamp capture point
- **Sub-microsecond Precision**: Hardware clocks operate at nanosecond resolution

### LAN865x Timestamping Features
- Timestamps captured at the **MII interface**
- Hardware timestamp counter synchronized to system clock
- Support for both **ingress and egress** timestamps
- Nanosecond-resolution 64-bit timestamp counter

### Synchronization Algorithm
The Follower adjusts its clock using a **PI controller** (Proportional-Integral):
- **Proportional Term**: Corrects current offset
- **Integral Term**: Eliminates long-term drift
- **Frequency Adjustment**: Modifies clock frequency to match GM
- **Phase Adjustment**: Directly adjusts clock value for large offsets