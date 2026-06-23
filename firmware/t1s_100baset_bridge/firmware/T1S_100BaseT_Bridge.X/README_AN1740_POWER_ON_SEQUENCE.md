# LAN8651 Complete Power-On Configuration & Initialization Tool

## Overview

The `lan8651_1760_power_on_sequence.py` tool provides a complete, production-ready power-on sequence for LAN8650/8651 10BASE-T1S MAC-PHY controllers. It implements the full AN1760-compliant initialization sequence with comprehensive PLCA configuration, network interface activation, and health verification.

## Features

- 🚀 **Complete 6-Stage Power-On Sequence**
  - Hardware reset detection and boot sequencing 
  - Device identification and silicon revision detection
  - AN1760 mandatory register configuration (12+ registers)
  - PLCA mode configuration (Standalone/Coordinator/Follower)
  - Network interface activation with link establishment
  - Comprehensive health check and verification

- 📊 **Multiple Configuration Modes**
  - **Standalone**: PLCA disabled with collision detection (traditional Ethernet)
  - **Coordinator**: PLCA enabled as network coordinator (Node 0)  
  - **Follower**: PLCA enabled as network follower (Node 1-254)
  - **Auto-detect**: Automatically detect existing network configuration

- 🔧 **Production-Ready Features**
  - Automatic hardware reset and error recovery
  - Register verification with read-back checking
  - Configuration export to JSON for production use
  - Comprehensive health checking (Basic/Standard/Comprehensive)
  - Professional logging and error reporting

## Hardware Requirements

- **Device**: LAN8650/8651 10BASE-T1S MAC-PHY Controller
- **Interface**: Serial communication (UART/USB-Serial)
- **Baudrate**: 115200 bps (configurable)
- **Cable**: Standard T1S twisted pair cable for network testing

## Installation

```bash
# Clone the repository
git clone <repository-url>

# Navigate to the firmware directory
cd t1s_100baset_bridge/firmware/T1S_100BaseT_Bridge.X

# Install Python dependencies
pip install -r requirements.txt
```

## Usage

### Basic Commands

```bash
# Basic standalone configuration (traditional Ethernet mode)
python lan8651_1760_power_on_sequence.py --device COM8 standalone

# PLCA Coordinator for 4-node network  
python lan8651_1760_power_on_sequence.py --device COM8 coordinator --nodes 4

# PLCA Follower (Node 2 of 8) with full verification
python lan8651_1760_power_on_sequence.py --device COM8 follower --id 2 --nodes 8 --verify-all

# Auto-detect existing network configuration
python lan8651_1760_power_on_sequence.py --device COM8 auto-detect
```

### Advanced Options

```bash
# Custom serial port and comprehensive health check
python lan8651_1760_power_on_sequence.py --device /dev/ttyUSB0 --health-check comprehensive standalone

# Export configuration to custom file
python lan8651_1760_power_on_sequence.py --device COM8 --export-config my_config.json coordinator --nodes 6

# Increase timeout for slow hardware
python lan8651_1760_power_on_sequence.py --device COM8 --timeout 5.0 follower --id 1 --nodes 4
```

## Configuration Modes

### 1. Standalone Mode
Traditional Ethernet mode with PLCA disabled.

```bash
python lan8651_1760_power_on_sequence.py --device COM8 standalone
```

**Configuration:**
- PLCA disabled (PLCA_CTRL0 = 0x0000)
- Collision detection enabled (CDCTL0 = 0x0001)
- Standard Ethernet CSMA/CD behavior
- Suitable for point-to-point links

### 2. Coordinator Mode
PLCA enabled as network coordinator (Node 0).

```bash
python lan8651_1760_power_on_sequence.py --device COM8 coordinator --nodes 4
```

**Configuration:**
- PLCA enabled as coordinator (PLCA_CTRL0 = 0x8000)
- Node count configured (PLCA_CTRL1 = node_count << 8)
- Beacon generation enabled
- Collision detection disabled
- Manages network timing and access

**Parameters:**
- `--nodes`: Total number of nodes in network (required, 2-255)

### 3. Follower Mode
PLCA enabled as network follower node.

```bash
python lan8651_1760_power_on_sequence.py --device COM8 follower --id 2 --nodes 4
```

**Configuration:**
- PLCA enabled as follower (PLCA_CTRL0 = 0x8000)
- Node ID configured (PLCA_CTRL1 = node_id)
- Waits for coordinator beacon
- Collision detection disabled
- Follows coordinator timing

**Parameters:**
- `--id`: Node ID (required, 1-254)
- `--nodes`: Total number of nodes in network (required, 2-255)

### 4. Auto-Detect Mode
Automatically detect and configure for existing network.

```bash
python lan8651_1760_power_on_sequence.py --device COM8 auto-detect
```

**Operation:**
- Scans for existing PLCA beacons
- Determines network size and available node IDs
- Automatically configures as follower with next available ID
- Falls back to standalone if no network detected

## Power-On Sequence Stages

### Stage 1: Hardware Reset & Boot Detection
- Verifies device communication
- Detects hardware error states (0x00008000 response)
- Attempts device reset using `reset` and `init` commands
- Waits for proper boot sequence completion

### Stage 2: Device Identification  
- Reads device ID registers (Chip ID, OA ID, PHY ID2)
- Identifies LAN8650 vs LAN8651 variants
- Determines silicon revision (A0, B0, B1, etc.)
- Validates device compatibility

### Stage 3: AN1760 Mandatory Configuration
Applies the complete AN1760 application note register configuration:

| Register | Address | Value | Description |
|----------|---------|-------|-------------|
| PARAMETER_1 | 0x000400B4 | 0x10F0 | Configuration Parameter 1 |
| PARAMETER_2 | 0x000400F4 | 0x0C00 | Configuration Parameter 2 |
| PARAMETER_3 | 0x000400F5 | 0x0280 | Configuration Parameter 3 |
| PARAMETER_4 | 0x000400F6 | 0x0600 | Configuration Parameter 4 |
| PARAMETER_5 | 0x000400F7 | 0x8200 | Configuration Parameter 5 |
| EMI_CTRL_1 | 0x00040077 | 0x0028 | EMI Control Register 1 |
| EMI_CTRL_2 | 0x00040078 | 0x001C | EMI Control Register 2 |
| RX_CFG_1 | 0x00040091 | 0x9660 | RX Configuration 1 |
| RX_CFG_2 | 0x00040081 | 0x00C0 | RX Configuration 2 |
| TX_CFG_1 | 0x00040075 | 0x0001 | TX Configuration 1 |
| TX_BOOST | 0x00040079 | 0x1C78 | TX Boost Configuration |
| BASELINE_CTL | 0x00040094 | 0x0038 | Baseline Control |

### Stage 4: PLCA Configuration
- Configures Physical Layer Collision Avoidance based on selected mode
- Sets node ID and network size for PLCA networks
- Configures collision detection appropriately
- Enables/disables PLCA operation

### Stage 5: Network Interface Activation  
- Executes PHY power-up sequence (1.2s)
- Configures MAC layer settings
- Enables TX/RX data paths
- Establishes physical link
- Monitors link status

### Stage 6: Health Check
Performs comprehensive device health verification:

- **Basic**: Register accessibility, link status
- **Standard**: + PLCA operation, environmental conditions  
- **Comprehensive**: + Signal quality, error counters, performance metrics

## Command Line Options

| Option | Default | Description |
|--------|---------|-------------|
| `--device` | COM8 | Serial port (COM8, /dev/ttyUSB0, etc.) |
| `--baudrate` | 115200 | Communication baud rate |
| `--timeout` | 3.0 | Command timeout in seconds |
| `--verify-all` | False | Enable full register read-back verification |
| `--health-check` | standard | Health check level (basic/standard/comprehensive) |
| `--export-config` | Auto | Export configuration to specified file |
| `--nodes` | - | Total network nodes (required for coordinator/follower) |
| `--id` | - | Node ID for follower mode (1-254) |

## Output and Reporting

### Console Output
The tool provides real-time progress reporting with color-coded status indicators:

```
================================================================================
LAN8651 Complete Power-On Configuration
================================================================================
🎯 Configuration Mode: COORDINATOR

🚀 STAGE 1: Hardware Reset & Boot Detection
   [1/5] Checking device communication... ✅
   [2/5] Checking device state... ✅
   [4/5] Waiting for device boot... ✅ (500ms)
   [5/5] Boot detection successful ✅

🔍 STAGE 2: Device Identification
   📟 Device ID: 0x00009226 (LAN8650/1) ✅
   🔧 Silicon Revision: Unknown (9226) ✅
   📊 Family: 10BASE-T1S MAC-PHY ✅
   ⚡ Supply Voltage: 3.30V ✅

⚙️ STAGE 3: AN1760 Mandatory Configuration
   📂 Loading AN1760 register table (12 registers)...
   [01/12] PARAMETER_1 = 0x10F0 ✅
   [02/12] PARAMETER_2 = 0x0C00 ✅
   ...
   🎉 AN1760 Configuration: 12/12 registers successful

🌐 STAGE 4: PLCA Configuration
   📡 Mode: PLCA Coordinator
   🌐 Network Size: 4 nodes
   [1/4] Resetting PLCA... ✅
   [2/4] PLCA_CTRL1 = 0x0400 (Node Count = 4) ✅
   [3/4] PLCA_CTRL0 = 0x8000 (Enable PLCA) ✅
   [4/4] Collision detection disabled ✅
   📡 PLCA Coordinator ready!

🔗 STAGE 5: Network Interface Activation
   [1/5] PHY power-up sequence ✅ (1.2s)
   [2/5] MAC configuration ✅
   [3/5] TX/RX path enable ✅
   [4/5] Link establishment... ✅ (0.5s)
   [5/5] Network interface active ✅

🏥 STAGE 6: Standard Health Check
   ✅ Register functionality: PASS (12/12 registers accessible)
   ✅ PHY operation: PASS (Link up)
   ✅ Environmental: PASS (Operating within specs)
   ✅ PLCA operation: PASS (PLCA operational)
   🎯 Overall Health: EXCELLENT

📊 Configuration Summary:
   ⏱️ Total Time: 3.0 seconds
   📝 AN1760 Registers: 12/12 configured successfully
   🌐 Network Mode: COORDINATOR
   📊 Final Status: OPERATIONAL

🎉 LAN8651 Power-On Sequence: COMPLETED SUCCESSFULLY!

💾 Configuration Export:
   📁 Saved to: lan8651_config_20260311_161823.json
   🔧 Configuration verified and ready for production use
```

### Configuration Export
The tool automatically exports the final configuration to JSON format:

```json
{
  "timestamp": "2026-03-11T16:18:23.123456",
  "device_info": {
    "chip_id": 37318,
    "oa_id": 37318, 
    "phy_id2": null,
    "silicon_revision": "Unknown (9226)"
  },
  "an1760_config": {
    "PARAMETER_1": {
      "address": "0x000400B4",
      "value": "0x10F0", 
      "description": "Configuration Parameter 1 - VERIFIED"
    },
    ...
  },
  "tool_version": "1.0"
}
```

## Error Handling

The tool includes comprehensive error handling for common scenarios:

### Device Error States
- **0x00008000 Response**: Device in error/uninitialized state
  - Automatically attempts reset sequence
  - Falls back to basic configuration if needed
  
### Communication Issues  
- **Timeout Errors**: Configurable timeouts with retry logic
- **Serial Port Issues**: Clear error messages with connection guidance
- **Command Failures**: Graceful degradation with partial success reporting

### Hardware Issues
- **Link Problems**: Continues configuration even without physical link
- **Register Access**: Identifies read-only vs hardware-managed registers
- **PLCA Conflicts**: Validates network configuration parameters

## Integration with Other Tools

The power-on sequence tool is designed to work alongside other LAN8651 development tools:

- **SQI Diagnostics**: Run after power-on for signal quality monitoring
- **PLCA Setup**: Complementary tool for interactive network configuration
- **Cable Testing**: Use after power-on for comprehensive cable diagnostics

```bash
# Complete workflow example
python lan8651_1760_power_on_sequence.py --device COM8 coordinator --nodes 4
python lan8651_1760_sqi_diagnostics.py --device COM8 monitor --time 30
python lan8651_1760_plca_setup.py --device COM8 scan-network
```

## Troubleshooting

### Common Issues

**1. All registers return 0x00008000**
```
Solution: Device in error state
- Tool automatically attempts reset sequence
- Check physical connections and power
- Verify correct serial port and baudrate
```

**2. Register writes fail**
```
Solution: Permission or timing issues
- Use --verify-all for detailed diagnostics
- Increase --timeout for slow hardware
- Check if device needs special initialization
```

**3. PLCA configuration fails**
```
Solution: Network conflict or invalid parameters
- Verify --nodes and --id parameters are valid
- Check for existing PLCA coordinator on network
- Use auto-detect mode to scan network first
```

**4. Health check failures**
```
Solution: Hardware or configuration issues
- Check physical cable connections
- Verify correct PLCA mode for network
- Use basic health check mode first
```

### Debug Mode

For detailed troubleshooting, use the companion debug tool:

```bash
python simple_power_on_debug.py
```

This provides register-level debugging information.

## Performance

- **Typical Execution Time**: 3-11 seconds depending on mode
- **AN1760 Configuration**: 12 registers in ~0.5 seconds  
- **PLCA Setup**: <1 second for all modes
- **Health Check**: 1-3 seconds depending on level

## Version History

- **v1.0**: Initial release with complete 6-stage power-on sequence
  - AN1760 mandatory register configuration
  - All PLCA modes (Standalone/Coordinator/Follower/Auto-detect)
  - Comprehensive health checking
  - Production-ready configuration export

## Support

For technical support or bug reports:
1. Check hardware connections and power supply
2. Verify correct serial port and baudrate
3. Run simple_power_on_debug.py for detailed diagnostics
4. Review exported configuration files for validation

## Related Tools

- [`lan8651_1760_sqi_diagnostics.py`](README_AN1740_SQI_DIAGNOSTICS.md) - Signal Quality Monitoring
- [`lan8651_1760_plca_setup.py`](README_AN1740_PLCA_SETUP.md) - Interactive PLCA Configuration  
- [`simple_power_on_debug.py`] - Register-level debugging

---

**Author**: T1S Development Team  
**Date**: March 2026  
**Hardware**: LAN8650/8651 10BASE-T1S MAC-PHY Controller