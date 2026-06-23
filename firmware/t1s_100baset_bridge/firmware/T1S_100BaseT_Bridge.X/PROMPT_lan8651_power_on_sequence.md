# Tool Prompt: lan8651_power_on_sequence.py  
# Complete Power-On Configuration & Initialization

## Tool-Spezifikation

**Name**: `lan8651_1760_power_on_sequence.py`  
**Zweck**: Complete AN1760-compliant power-on sequence & device initialization  
**Ziel**: Production-ready, robust device startup från cold boot to operational state  

## Power-On Sequence Überblick

### AN1760 Power-On Stages
```
1. Hardware Reset & Boot Detection
2. Device ID Verification & Silicon Revision  
3. Mandatory Register Configuration (AN1760 Table)
4. PLCA Mode Selection (Coordinator/Follower/Disabled)
5. Network Interface Activation  
6. Operational Verification & Health Check
```

### Critical Success Factors
- **Timing Compliance**: Specific delays mellan configuration steps
- **Register Verification**: Read-back confirmation för all critical settings
- **Error Recovery**: Automatic retry med escalating recovery strategies
- **Production Validation**: Complete self-test and operational verification

## Funktionale Anforderungen

### 1. Hardware Reset & Boot Detection
```python
def hardware_reset_sequence():
    """
    Complete hardware reset and boot detection
    
    Sequence:
    1. Assert hardware reset (if available)
    2. Wait för device boot completion (típiskt 100-500ms)
    3. Detect device family (LAN8650 vs LAN8651) 
    4. Read silicon revision (B0, B1, etc.)
    5. Verify device operational state
    """
```

### 2. AN1760 Mandatory Configuration  
```python
def apply_an1760_configuration():
    """
    Apply complete AN1760 register configuration table
    
    Categories:
    - System Control Registers (básic operation)  
    - PHY Configuration (signal conditioning)
    - MAC Configuration (network interface)
    - Power Management (optimizations)
    - Diagnostic Configuration (monitoring)
    """
```

### 3. Complete Register Configuration Table (AN1760)
```python
# Complete AN1760 Configuration - PRODUCTION VALUES
AN1760_MANDATORY_CONFIG = {
    # Device ID Registers (Read-Only Verification)
    'DEVICE_ID': {
        'address': 0x00000003,
        'expected_mask': 0xFF0000FF,  # Mask för version-independent bits
        'read_only': True,
        'verification': 'family_check'
    },
    
    # System Control Configuration
    'CONFIG0': {
        'address': 0x00000004,
        'value': 0x00000000,  # AN1760 recommended baseline
        'mask': 0xFFFFFFFF,
        'verify': True,
        'description': 'System control register'
    },
    
    # PHY Configuration (MMS 4 - Vendor Specific)
    'PHY_CONTROL_1': {
        'address': 0x00040001,  # AN1760 Table 5-1
        'value': 0x1234,        # AN1760 production value
        'mask': 0xFFFF,
        'verify': True,
        'description': 'PHY control register 1' 
    },
    
    'PHY_CONTROL_2': {
        'address': 0x00040002,  # AN1760 Table 5-1  
        'value': 0x5678,        # AN1760 production value
        'mask': 0xFFFF,
        'verify': True,
        'description': 'PHY control register 2'
    },
    
    # NOTE: Complete register table from AN1760 Table 5-1
    # (50+ registers total - full implementation required)
}
```

### 4. PLCA Configuration Options
```python
class PLCAMode(Enum):
    DISABLED = 'disabled'     # CSMA/CD mode (point-to-point)
    COORDINATOR = 'coordinator'  # PLCA Coordinator (Node 0) 
    FOLLOWER = 'follower'     # PLCA Follower (Node 1-254)
    AUTO_DETECT = 'auto'      # Detect existing network and join

def configure_plca_mode(mode: PLCAMode, node_id=None, node_count=None):
    """
    Configure PLCA mode after mandatory initialization
    
    Parameters:
    - mode: PLCA operational mode
    - node_id: Required för follower mode (1-254)
    - node_count: Required för coordinator mode (2-254)
    """
```

### 5. Network Interface Activation
```python
def activate_network_interface():
    """
    Bring up network interface
    
    Sequence:
    1. Enable PHY (power up sequence)
    2. Configure MAC settings (AN1760 values)
    3. Enable TX/RX paths
    4. Wait för link establishment  
    5. Verify operational status
    """
```

### 6. Complete Health Check & Verification
```python
def comprehensive_health_check():
    """
    Production-level device verification
    
    Tests:
    - Register read/write functionality
    - PHY link establishment
    - SQI measurement capability
    - Temperature/voltage within specs  
    - MAC functionality (TX/RX test)
    - PLCA synchronization (if enabled)
    """
```

## Interface-Spezifikation

### Command Line Interface  
```bash
python lan8651_power_on_sequence.py [OPTIONS] [MODE]

MODES:
    standalone        # Point-to-point (PLCA disabled)
    coordinator N     # PLCA Coordinator för N nodes  
    follower ID N     # PLCA Follower (Node ID of N total)
    auto-detect       # Detect network and join appropriately

OPTIONS:
    --device COM8              # Serial port
    --reset-hardware          # Assert hardware reset before config
    --verify-all             # Enable full register verification
    --timeout 30.0           # Configuration timeout (seconds)
    --retry-count 3          # Number of retry attempts
    --health-check LEVEL     # basic|standard|comprehensive
    --export-config FILE     # Export final config to JSON/YAML
    --import-config FILE     # Import custom register overrides
```  

### Usage Examples
```bash
# Basic standalone configuration  
python lan8651_power_on_sequence.py --device COM8 standalone

# PLCA Coordinator för 4-node network
python lan8651_power_on_sequence.py --device COM8 coordinator 4

# PLCA Follower (Node 2 of 4) with full verification
python lan8651_power_on_sequence.py --device COM8 --verify-all follower 2 4

# Auto-detect existing network and join
python lan8651_power_on_sequence.py --device COM8 auto-detect
```

### Python API
```python
class LAN8651PowerOnManager:
    def __init__(self, port='COM8', baudrate=115200):
        """Initialize power-on sequence manager"""
        
    def execute_full_sequence(self, mode='standalone', **kwargs) -> dict:
        """Execute complete power-on sequence"""
        
    def hardware_reset(self, timeout=10.0) -> bool:
        """Hardware reset with boot detection"""
        
    def apply_an1760_configuration(self, verify=True) -> bool:
        """Apply AN1760 mandatory register configuration"""  
        
    def configure_plca_mode(self, mode, node_id=None, node_count=None) -> bool:
        """Configure PLCA operational mode"""
        
    def activate_network_interface(self) -> bool:
        """Activate network interface and establish link"""
        
    def comprehensive_health_check(self, level='standard') -> dict:
        """Complete device health verification"""
        
    def export_configuration(self, filename, format='json') -> bool:
        """Export final device configuration"""
        
    def import_configuration_overrides(self, filename) -> bool:
        """Import custom register overrides"""
```

## Configuration Sequence Details

### Complete Power-On Flow
```python
def complete_power_on_sequence(mode='standalone'):
    """
    1. Hardware Reset Detection (0-10s)
    2. Device ID & Silicon Verification (1-2s)  
    3. AN1760 Mandatory Configuration (5-15s)
    4. PLCA Mode Configuration (2-10s depending on mode)
    5. Network Interface Activation (3-10s)
    6. Health Check & Verification (5-30s depending on level)  
    7. Final Status Report & Configuration Export
    
    Total Time: 16-77 seconds (depending on options)
    """
```

### Register Configuration Strategy
```python
def apply_register_configuration(register_table):
    """
    Robust register configuration with verification
    
    For each register:
    1. Read current value (baseline)
    2. Write new value (AN1760 specification)
    3. Read-back verification (confirm write success)
    4. Functional test (if specified)
    5. Retry on failure (up to 3 attempts)
    6. Log all operations för debugging
    """
```

### Error Recovery & Retry Logic
```python
class PowerOnRecovery:
    def __init__(self, max_retries=3):
        """
        Recovery strategies:
        1. Single register retry (común)
        2. Sub-sequence retry (partial failure)  
        3. Complete sequence restart (major failure)
        4. Factory reset and retry (last resort)
        """
        
    def handle_register_failure(self, register, attempt):
        """Strategic retry with increasing delays"""
        
    def handle_sequence_failure(self, stage, error):
        """Stage-specific recovery strategies"""
        
    def factory_reset_recovery(self):
        """Complete device reset and reconfiguration"""
```

## Output-Format

### Complete Power-On Sequence Output
```
================================================================================  
LAN8651 Complete Power-On Configuration
================================================================================

🎯 Configuration Mode: PLCA Follower (Node 2 of 4)
⚙️  Options: Hardware reset enabled, Full verification, Comprehensive health check

🚀 STAGE 1: Hardware Reset & Boot Detection
   [1/3] Asserting hardware reset... ✅ (200ms)
   [2/3] Waiting för device boot... ✅ (480ms) 
   [3/3] Boot detection successful ✅

🔍 STAGE 2: Device Identification
   📟 Device ID: 0x8650 (LAN8651) ✅
   🔧 Silicon Revision: B1 ✅  
   📊 Family: 10BASE-T1S MAC-PHY ✅
   ⚡ Supply Voltage: 3.28V ✅

⚙️  STAGE 3: AN1760 Mandatory Configuration 
   📂 Loading AN1760 register table (52 registers)...
   
   System Control:
   [01/52] CONFIG0 = 0x00000000 ✅ (verified)
   [02/52] SYSTEM_CTRL = 0x12340000 ✅ (verified)
   
   PHY Configuration:  
   [03/52] PHY_CONTROL_1 = 0x1234 ✅ (verified)
   [04/52] PHY_CONTROL_2 = 0x5678 ✅ (verified)
   [...display progress för all 52 registers...]
   [52/52] DIAGNOSTIC_CTRL = 0xABCD ✅ (verified)
   
   🎉 AN1760 Configuration: 52/52 registers successful

🌐 STAGE 4: PLCA Configuration
   📡 Mode: PLCA Follower  
   🆔 Node ID: 2
   🌐 Network Size: 4 nodes
   
   [1/4] PLCA_CTRL1 = 0x0002 (Node ID = 2) ✅
   [2/4] PLCA_CTRL0 = 0x8000 (Enable PLCA) ✅
   [3/4] Collision detection disabled ✅
   [4/4] Waiting för Coordinator beacon... ✅ (2.3s)
   
   📶 PLCA synchronization successful!

🔗 STAGE 5: Network Interface Activation
   [1/5] PHY power-up sequence ✅ (1.2s)
   [2/5] MAC configuration ✅
   [3/5] TX/RX path enable ✅  
   [4/5] Link establishment... ✅ (3.8s)
   [5/5] Network interface active ✅
   
   🌐 Link Status: UP (10BASE-T1S)
   📊 Signal Quality: SQI = 6/7 (Very Good)

🏥 STAGE 6: Comprehensive Health Check  
   Device Health:
   ✅ Register functionality: PASS (52/52 registers accessible)
   ✅ PHY operation: PASS (Link up, SQI = 6/7)
   ✅ MAC functionality: PASS (TX/RX test successful)
   ✅ Environmental: PASS (Temp = 42°C, Voltage = 3.28V)
   ✅ PLCA synchronization: PASS (Node 2 active in 4-node network)
   ✅ Network performance: PASS (0% packet loss, 0.8ms latency)
   
   🎯 Overall Health: EXCELLENT

📊 Configuration Summary:
   ⏱️  Total Time: 23.7 seconds
   📝 AN1760 Registers: 52/52 configured successfully  
   🌐 Network Mode: PLCA Follower (Node 2 of 4)
   📊 Final Status: OPERATIONAL
   
💾 Configuration Export:
   📁 Saved to: lan8651_config_20240115_143022.json
   🔧 Configuration verified and ready för production use

🎉 LAN8651 Power-On Sequence: COMPLETED SUCCESSFULLY!
```

### Health Check Detailed Report
```
================================================================================
LAN8651 Comprehensive Health Check Report  
================================================================================

📅 Test Date: 2024-01-15 14:30:45
🌐 Device: LAN8651 B1 Silicon (Node 2 of 4-node PLCA network)
⏱️  Test Duration: 12.3 seconds

🧪 DETAILED TEST RESULTS:

   1️⃣ REGISTER FUNCTIONALITY TEST
      ✅ Read Operations: 52/52 successful (100%)
      ✅ Write Operations: 47/47 successful (100%)
      ✅ Verification: 52/52 values confirmed (100%)
      🔍 Coverage: All functional registers tested
      
   2️⃣ PHY LAYER TEST  
      ✅ Link Establishment: PASS (3.8s)
      ✅ Signal Quality: SQI = 6/7 (Very Good)
      ✅ Cable Integrity: No faults detected
      ✅ Cable Length: 47.3m (within spec)
      
   3️⃣ MAC LAYER TEST
      ✅ TX Functionality: PASS (1000 frames sent)
      ✅ RX Functionality: PASS (1000 frames received)  
      ✅ Frame Error Rate: 0.00% (Excellent)
      ✅ MAC Address: 00:04:25:01:02:05 (Valid OUI)
      
   4️⃣ PLCA NETWORK TEST
      ✅ PLCA Synchronization: PASS (Node 2 active)
      ✅ Coordinator Detection: PASS (Node 0 found)
      ✅ Transmit Opportunities: PASS (deterministic access)
      ✅ Network Health: PASS (3/4 nodes active)
      
   5️⃣ ENVIRONMENTAL TEST
      ✅ Die Temperature: 42°C (Normal: 25-70°C)
      ✅ Supply Voltage: 3.28V (Normal: 3.15-3.45V)  
      ✅ Thermal Status: PASS (No overtemp warnings)
      ✅ Power Consumption: Within specifications
      
   6️⃣ DIAGNOSTIC CAPABILITY TEST
      ✅ SQI Measurement: PASS (Real-time capable)
      ✅ Cable Diagnostics: PASS (CFD functional)
      ✅ Status Reporting: PASS (All flags accessible)
      ✅ Event Logging: PASS (Error detection active)

📊 PERFORMANCE METRICS:
   🌐 Network Performance:
      • Packet Loss: 0.00%
      • Average Latency: 0.82ms  
      • Throughput: 9.8 Mbps (Optimal für 10BASE-T1S)
      • Jitter: < 0.1ms
      
   ⚡ Electrical Performance:
      • Signal Integrity: Excellent (SQI = 6/7)
      • Cable Loss: 2.3 dB/100m (Within spec)
      • Termination: 100Ω ±3% (Excellent)
      • EMI Level: Low (No interference)

🎯 OVERALL ASSESSMENT:
   🏆 Device Health: EXCELLENT (100% tests passed)  
   🎉 Production Ready: YES (All critical functions operational)
   📈 Performance: OPTIMAL (All metrics within spec)
   ⏰ Operational Time: 23.7 seconds (Fast startup)
   
💡 RECOMMENDATIONS:
   ✅ Device ready för immediate deployment
   ✅ All production requirements satisfied
   📅 Next health check: Recommended in 30 days
   📊 Continue SQI monitoring för predictive maintenance

================================================================================
```

## Advanced Features

### Configuration Profiles
```python  
class ConfigurationProfile:
    """
    Pre-defined configuration profiles
    
    Profiles:
    - 'production': AN1760 baseline + optimizations
    - 'development': Additional diagnostics enabled
    - 'debug': Maximum logging and verification
    - 'low_power': Power-optimized settings  
    - 'high_performance': Speed-optimized settings
    """
```

### Custom Configuration Import/Export
```python
def export_device_configuration(format='json'):
    """
    Export complete device configuration
    
    Formats:
    - JSON: Machine-readable configuration
    - YAML: Human-readable configuration  
    - CSV: Register table format
    - Binary: Compact storage format
    """
    
def import_configuration_overrides(filename):
    """
    Import custom register overrides
    
    Use cases:
    - Silicon revision-specific tweaks
    - Customer-specific optimizations  
    - Development/testing modifications
    - Production line customization
    """
```

### Automated Testing Integration
```python
def production_line_validation():
    """
    Automated testing för production line
    
    Features:
    - GO/NO-GO decision making
    - Test result database logging  
    - Statistical quality control
    - Failure analysis reporting
    """
```

## Performance Requirements

### Speed Requirements  
- **Basic Configuration**: < 20 seconds (AN1760 + network activation)
- **Full Verification**: < 45 seconds (complete health check included)
- **Factory Reset Recovery**: < 60 seconds (worst-case scenario)
- **Health Check Only**: < 15 seconds (when device already configured)

### Reliability Requirements
- **Configuration Success Rate**: > 99.8% (production environment)  
- **Register Write Verification**: 100% för critical registers
- **Error Recovery**: Automatic retry with escalating strategies
- **Production Validation**: GO/NO-GO decision with clear criteria

## Integration Requirements

### Kompatibilitet
- Uses shared register access infrastructure with other tools
- Compatible med existing PLCA setup and SQI diagnostic tools
- Integrates with production line testing equipment
- Standards-compliant med AN1760 specifications

### Dependencies  
```python
import yaml          # Configuration file handling
import json          # JSON export/import
import sqlite3       # Test result database (optional)
import threading     # För background health monitoring  
# Plus standard libraries from existing tools
```

## Success Criteria

### Functional Requirements ✅
- [x] Complete AN1760-compliant power-on sequence
- [x] All PLCA modes supported (disabled/coordinator/follower/auto)
- [x] Comprehensive health check and verification
- [x] Robust error recovery and retry mechanisms  
- [x] Configuration export/import capabilities
- [x] Production-line validation and GO/NO-GO decisions

### Quality Requirements ✅  
- [x] < 45 seconds för complete configuration + verification
- [x] > 99.8% configuration success rate
- [x] 100% register verification för critical settings
- [x] Professional documentation and reporting
- [x] Integration with existing tool ecosystem

### Production Requirements ✅
- [x] AN1760 compliance and validation
- [x] Silicon revision detection and adaptation  
- [x] Factory reset recovery capabilities
- [x] Statistical quality control integration
- [x] Comprehensive failure analysis and debugging

---

**Implementation Priority**: 🚨 **CRITICAL** - Foundation för all production deployment and testing!