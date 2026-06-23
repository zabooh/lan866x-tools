# Tool Prompt: lan8651_appnote_config.py
# Vollständige AN1760-Konfiguration

## Tool-Spezifikation

**Name**: `lan8651_1760_appnote_onfig.py`  
**Zweck**: Vollständige Implementation der Microchip AN1760 Configuration Application Note  
**Ziel**: Optimale LAN8650/1 Konfiguration für 10BASE-T1S Networks  

## Funktionale Anforderungen

### 1. Device Identification & Verification
- LAN8650/1 Hardware Detection via OA_ID Register (0x00000000)
- Silicon Revision Detection via PHY_ID2 Register (0x0000FF03)  
- Support für B0 (0001) und B1 (0010) Revisionen
- Fehlerbehandlung bei unsupportierten Hardware-Versionen

### 2. Berechnete Parameter (Critical!)
Implementierung der AN1760 Parameter-Berechnungs-Algorithmen:

```python
def calculate_cfgparam1():
    """
    Berechnet cfgparam1 basierend auf Device-spezifischen Werten
    - Read Value1 via indirect_read(0x04, 0x01F) 
    - Offset-Berechnung gemäß AN1760 Algorithmus
    - Return: Berechneter cfgparam1 Wert
    """

def calculate_cfgparam2(): 
    """
    Berechnet cfgparam2 basierend auf Device-spezifischen Werten
    - Read Value2 via indirect_read(0x08, 0x01F)
    - Offset-Berechnung gemäß AN1760 Algorithmus  
    - Return: Berechneter cfgparam2 Wert
    """

def indirect_read(addr, mask):
    """
    Proprietärer indirect read access (AN1760 Methode)
    - write_register(0x04, 0x00D8, addr)
    - write_register(0x04, 0x00DA, 0x02) 
    - return (read_register(0x04, 0x00D8) & mask)
    """
```

### 3. Table 1: Mandatory Configuration Registers
**Alle Register aus AN1760 Table 1 MÜSSEN in korrekter Reihenfolge konfiguriert werden:**

```python
MANDATORY_CONFIG = [
    # Basis Configuration 
    (0x00040000, 0x3F31, "Basis Config"),
    (0x000400E0, 0x00C0, "Performance"),
    
    # Berechnete Parameter  
    (0x000400B4, "cfgparam1", "Config Param 1 (calculated)"),
    (0x000400B6, "cfgparam2", "Config Param 2 (calculated)"),
    
    # Fixed Configuration Values
    (0x000400E9, 0x9E50, "Extended Config"),
    (0x000400F5, 0x1CF8, "Signal Tuning"),
    (0x000400F4, 0x0C00, "Interface"),
    (0x000400F8, 0xB900, "Hardware Tuning"),  
    (0x000400F9, 0x4E53, "Performance Reg"),
    (0x00040081, 0x0080, "Control Register"),
    
    # Additional AN1760 Registers
    (0x00040091, 0x9860, "Additional Config"),
    (0x00040077, 0x0028, "Extended Setup"),
    (0x00040043, 0x00FF, "Parameter Reg 1"),
    (0x00040044, 0xFFFF, "Parameter Reg 2"),
    (0x00040045, 0x0000, "Parameter Reg 3"),
    (0x00040053, 0x00FF, "Config Extension 1"),
    (0x00040054, 0xFFFF, "Config Extension 2"),
    (0x00040055, 0x0000, "Config Extension 3"),
    (0x00040040, 0x0002, "Final Config 1"),
    (0x00040050, 0x0002, "Final Config 2")
]
```

### 4. Verification & Rollback  
- Nach jedem Register-Write: Read-back Verification
- Bei Fehlern: Automatic Rollback oder Retry-Mechanism
- Comprehensive Error Reporting mit Register-Address Details

### 5. Performance & Safety Features
- Prompt-based Serial Communication (wie in bestehenden Tools)
- Timeout Protection (3s pro Register-Operation)
- Progress Reporting mit ETA
- Detailed Logging für Troubleshooting

## Interface-Spezifikation

### Command Line Interface
```bash
python lan8651_appnote_config.py [OPTIONS]

OPTIONS:
    --device COM8              # Serial port (default: COM8)
    --baudrate 115200         # Baud Rate (default: 115200) 
    --verify                  # Enable read-back verification
    --rollback               # Enable automatic rollback on errors
    --log-level INFO         # Logging verbosity
    --dry-run               # Show configuration ohne Write-Operations
    --force                 # Ignore hardware revision warnings
    --timeout 3.0           # Timeout per register operation (seconds)
```

### Python API
```python
class AN1760Configurator:
    def __init__(self, port='COM8', baudrate=115200):
        """Initialize AN1760 configurator"""
        
    def detect_device(self) -> dict:
        """Detect and verify LAN8650/1 hardware"""
        
    def calculate_parameters(self) -> dict: 
        """Calculate device-specific parameters"""
        
    def apply_mandatory_config(self, verify=True) -> bool:
        """Apply Table 1 mandatory configuration"""
        
    def verify_configuration(self) -> dict:
        """Verify AN1760 configuration compliance"""
        
    def rollback_configuration(self) -> bool:
        """Rollback to safe default configuration"""
        
    def get_configuration_status(self) -> dict:
        """Get current AN1760 compliance status"""
```

## Output-Format

### Console Output
```
================================================================================
LAN8650/1 AN1760 Configuration Tool v1.0
================================================================================

🔍 Device Detection:
   ✅ LAN8650/1 detected (OA_ID: 0x00000011)
   ✅ Silicon Revision: B1 (0010)
   ✅ Hardware compatible with AN1760

🧮 Parameter Calculation:
   ✅ cfgparam1: 0x1234 (calculated from device-specific values)
   ✅ cfgparam2: 0x5678 (calculated from device-specific values)

📋 Mandatory Configuration (Table 1):
   [1/20] 0x00040000 = 0x3F31 (Basis Config) ✅
   [2/20] 0x000400E0 = 0x00C0 (Performance) ✅  
   [3/20] 0x000400B4 = 0x1234 (Config Param 1) ✅
   [4/20] 0x000400B6 = 0x5678 (Config Param 2) ✅
   [...]
   Progress: ████████████████████ 100% Complete

✅ Verification:
   ✅ All 20 registers configured correctly
   ✅ Read-back verification successful
   ✅ AN1760 compliance: 100%

⏱️ Configuration completed in 15.3 seconds
🎯 LAN8650/1 ready for optimal 10BASE-T1S operation
```

### JSON Output Mode
```json
{
    "status": "success",
    "device": {
        "oa_id": "0x00000011",
        "silicon_revision": "B1_0010", 
        "compatible": true
    },
    "parameters": {
        "cfgparam1": "0x1234",
        "cfgparam2": "0x5678"
    },
    "configuration": {
        "registers_written": 20,
        "verification_passed": 20,
        "compliance_score": 100.0,
        "duration_seconds": 15.3
    },
    "errors": []
}
```

## Error Handling

### Fehler-Kategorien
1. **Hardware Errors**: Unsupported device, communication failure
2. **Configuration Errors**: Register write failures, verification failures  
3. **Parameter Errors**: Calculation failures, invalid values
4. **Timeout Errors**: Communication timeouts, device not responding

### Recovery Strategies  
1. **Retry Mechanism**: 3 retries per register mit exponential backoff
2. **Partial Recovery**: Continue mit warnings bei nicht-kritischen Fehlern
3. **Full Rollback**: Restore safe default configuration bei kritischen Fehlern
4. **Diagnostic Mode**: Extended logging für Failure-Analysis

## Integration Requirements

### Kompatibilität mit bestehenden Tools
- Verwendet selbe Serial Communication Library
- Kompatibel mit lan_read/lan_write Command Interface  
- Shared Configuration für COM Port Settings
- Consistent Error Reporting Format

### Dependencies
```python
# Required Libraries (bereits in unserem Projekt verfügbar)
import serial        # Für SPI/TC6 Communication
import time         # Für Timeout & Delays
import logging      # Für Debugging & Error Reporting  
import json         # Für structured Output
from tabulate import tabulate  # Für Table Display
```

## Success Criteria

### Funktionale Requirements ✅
- [x] Device Detection & Hardware Verification
- [x] Parameter Calculation gemäß AN1760 Algorithmus  
- [x] Complete Table 1 Implementation (20+ Registers)
- [x] Read-back Verification für alle Writes
- [x] Error Handling & Recovery Mechanisms

### Performance Requirements ✅  
- [x] Configuration Time: <30 seconds für complete setup
- [x] Success Rate: >95% unter normalen Bedingungen
- [x] Verification: 100% read-back verification accuracy
- [x] Communication: Robust serial communication mit timeout protection

### Usability Requirements ✅
- [x] CLI Interface für automation & scripting
- [x] Python API für programmatic access  
- [x] Progress Reporting für user feedback
- [x] Comprehensive logging für troubleshooting
- [x] JSON output für integration mit anderen tools

---

**Implementation Priority**: 🚨 **CRITICAL** - Dieses Tool ist fundamental für production-ready LAN8650/1 setup gemäß Microchip Spezifikation!