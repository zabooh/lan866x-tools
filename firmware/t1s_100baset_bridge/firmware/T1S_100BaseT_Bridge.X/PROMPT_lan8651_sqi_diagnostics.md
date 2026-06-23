# Tool Prompt: lan8651_sqi_diagnostics.py  
# Signal Quality Index & Cable Diagnostics

## Tool-Spezifikation

**Name**: `lan8651_sqi_diagnostics.py`  
**Zweck**: Real-time Signal Quality Monitoring & Cable Fault Diagnostics  
**Ziel**: Predictive maintenance, network troubleshooting, installation validation  

## Signal Quality Index (SQI) Überblick

### SQI Technology
- **Signal Quality Index**: 0-7 scale (7 = excellent, 0 = unusable)
- **Real-time Measurement**: Continuous link quality assessment
- **Cable Length Estimation**: TDR-based distance measurement
- **Fault Classification**: Open/Short/Miswiring detection

### SQI vs. Traditional Metrics
```
Traditional: Link Up/Down (binary)
SQI:         Granular quality (0-7 scale) + fault location
```

## Funktionale Anforderungen

### 1. Real-time SQI Monitoring
```python
def monitor_sqi(duration: int = 60, interval: float = 1.0):
    """
    Continuous SQI monitoring with trend analysis
    
    Parameters:
    - duration: Monitoring time in seconds
    - interval: Sample interval (0.1-10.0 seconds)
    
    Metrics:
    - Current SQI value (0-7)
    - SQI trend analysis (improving/stable/degrading) 
    - Historical min/max/average
    - Quality alerts når SQI < threshold
    """
```

### 2. Comprehensive Cable Diagnostics
```python
def run_cable_diagnostics():
    """
    Complete cable system analysis
    
    Tests:
    - Cable length measurement (TDR)
    - Fault detection (Open/Short/Miswiring)
    - Signal integrity assessment
    - Termination resistance check
    - EMI/RF interference detection  
    """
```

### 3. SQI & Diagnostic Register Mapping
```python
# AN1760-Confirmed + Hardware-verified Registers
SQI_DIAGNOSTIC_REGISTERS = {
    # Signal Quality Index (AN1760)
    'PMD_SQI': 0x00040083,       # SQI value (0-7) + validity
    
    # Cable Diagnostics (Hardware-verified from existing tools)
    'PMD_CONTROL': 0x00030001,   # CFD Enable/Start bits
    'PMD_STATUS': 0x00030002,    # CFD Results (Done/Fault Type/Link)
    
    # Advanced Link Quality (AN1760)
    'PMD_LINK_QUALITY': 0x00040084,  # Extended quality metrics
    'PMD_EYE_DIAGRAM': 0x00040085,   # Eye diagram parameters
    
    # Environmental Conditions (AN1760) 
    'PMD_TEMPERATURE': 0x00040081,   # Die temperature
    'PMD_VOLTAGE': 0x00040082,       # Supply voltage monitoring
} 
```

### 4. Cable Fault Detection
```python
class CableFaultType(Enum):
    NO_FAULT = 0
    OPEN_CIRCUIT = 1
    SHORT_CIRCUIT = 2  
    MISWIRING = 3
    POOR_TERMINATION = 4
    EXCESSIVE_LOSS = 5
    EMI_INTERFERENCE = 6

def detect_cable_faults() -> list:
    """
    Systematic cable fault detection
    
    Returns:
    [
        {
            'fault_type': CableFaultType,
            'location_meters': float,
            'severity': 'low|medium|high|critical',  
            'recommended_action': str
        }
    ]
    """
```

### 5. Network Performance Correlation  
```python
def correlate_sqi_performance():
    """
    Correlate SQI with actual network performance
    
    Metrics:
    - Packet loss vs. SQI
    - Latency vs. SQI  
    - Throughput vs. SQI
    - Frame error rate vs. SQI
    """
```

## Interface-Spezifikation

### Command Line Interface
```bash
python lan8651_sqi_diagnostics.py [OPTIONS] COMMAND

COMMANDS:
    monitor --time 60 --interval 1.0    # Real-time SQI monitoring
    cable-test                          # Complete cable diagnostics
    quick-check                         # Fast SQI + basic cable test
    trend --duration 300               # Long-term SQI trend analysis
    report --type pdf|json|csv         # Generate diagnostic report
    
OPTIONS:
    --device COM8                  # Serial port
    --threshold 3                  # SQI alert threshold (0-7)
    --temperature                 # Include temperature monitoring
    --export PATH                 # Export data to file  
    --continuous                  # Run until interrupted
    --alarm-sqi 2                # Critical SQI alarm level
```

### Usage Examples
```bash
# Basic SQI monitoring für 5 minutes
python lan8651_sqi_diagnostics.py --device COM8 monitor --time 300

# Complete cable diagnostics with temperature
python lan8651_sqi_diagnostics.py --device COM8 --temperature cable-test

# Continuous monitoring with low threshold alerts
python lan8651_sqi_diagnostics.py --device COM8 --threshold 4 --continuous monitor

# Generate comprehensive report
python lan8651_sqi_diagnostics.py --device COM8 report --type pdf --export diagnostics_2024.pdf
```

### Python API
```python
class SQIDiagnostics:
    def __init__(self, port='COM8', baudrate=115200):
        """Initialize SQI Diagnostics Manager"""
        
    def get_current_sqi(self) -> dict:
        """Get current SQI value and status"""
        
    def monitor_sqi_continuous(self, callback=None, threshold=3) -> None:
        """Continuous SQI monitoring with callbacks"""
        
    def run_cable_diagnostics(self) -> dict:
        """Complete cable diagnostic suite"""
        
    def measure_cable_length(self) -> float:
        """TDR-based cable length measurement"""
        
    def detect_faults(self) -> list:
        """Comprehensive fault detection"""
        
    def get_environmental_data(self) -> dict:
        """Temperature, voltage, environmental conditions"""
        
    def generate_report(self, format='json', filename=None) -> str:
        """Generate comprehensive diagnostic report"""
```

## SQI Measurement Process

### SQI Reading Sequence  
```python
def get_sqi_measurement():
    """
    1. Read PMD_SQI register (0x00040083)
    2. Extract SQI value (bits 2-0)
    3. Check validity flag (bit 3)
    4. Calculate trend från previous measurements
    5. Apply quality thresholds
    6. Generate alerts if needed
    """
```

### Cable Diagnostic Sequence
```python
def cable_diagnostic_sequence():
    """
    1. Enable Cable Fault Diagnostics (PMD_CONTROL)
    2. Start CFD test (PMD_CONTROL CFD_START = 1)
    3. Wait för completion (PMD_STATUS CFD_DONE = 1)
    4. Read fault results (PMD_STATUS fault bits)
    5. Measure cable length (TDR calculation)
    6. Classify fault type and severity
    7. Generate recommendations
    """
```

## Output-Format

### Real-time SQI Monitoring
```
================================================================================
Real-time SQI Monitoring - Link Quality Assessment
================================================================================

🎯 Monitoring Configuration:
   📊 Duration: 300 seconds (5 minutes)
   ⏱️  Sample Interval: 1.0 seconds
   🚨 Alert Threshold: SQI < 4
   🌡️  Temperature: Enabled

📈 Live SQI Data:
   Current SQI: 6 ✅ (Excellent)
   Trend: ↗️  Improving (+0.3 över last 30s)
   Min/Max/Avg: 4/7/5.8
   
   🌡️  Temperature: 42°C ✅ (Normal)
   ⚡ Supply Voltage: 3.28V ✅ (Normal)

📊 Real-time Graph:
   SQI │7 ┤                                  ╭─────╮          
       │6 ┤                             ╭────╯     ╰───╮      
       │5 ┤                        ╭────╯              ╰──╮   
       │4 ┤━━━━━━━━━━━━━━━━━━━━━━━━▌ ╭─╯                   ╰─  
       │3 ┤                        │                         
       │  └┬────┬────┬────┬────┬───┴────┬────┬────┬────┬───
         0s   60s  120s  180s  240s   300s
          
⏱️  Elapsed: 247/300 seconds
🎉 Link Quality: EXCELLENT - No issues detected
```

### Cable Diagnostics Report
```
================================================================================
Comprehensive Cable Diagnostics Report
================================================================================

🔍 Test Summary:
   📅 Date: 2024-01-15 14:30:22
   🌐 Device: LAN8651 (B1 Silicon)
   📏 Cable Type: Single Pair Ethernet
   
🧪 Diagnostic Results:

   1️⃣ CABLE LENGTH MEASUREMENT
      📏 Measured Length: 47.3 meters
      📐 Accuracy: ±0.5m (TDR-based)
      ✅ Status: Within specifications

   2️⃣ SIGNAL QUALITY INDEX  
      📊 Current SQI: 6/7 (Very Good)
      📈 24h Average: 5.8/7
      📉 Minimum Recorded: 4/7
      ✅ Status: Consistently good quality

   3️⃣ CABLE FAULT DETECTION
      🔍 Test Method: Cable Fault Diagnostics (CFD)
      ⚡ Fault Status: NO FAULTS DETECTED ✅
      🎯 Termination: Proper (100Ω ±5%)
      ✅ Status: Cable integrity verified

   4️⃣ ENVIRONMENTAL CONDITIONS
      🌡️  Temperature: 42°C (Normal operating range)
      ⚡ Supply Voltage: 3.28V (Within spec: 3.15-3.45V)
      📊 EMI Level: Low (No interference detected)
      ✅ Status: Optimal operating conditions

📊 PERFORMANCE CORRELATION:
   📦 Packet Loss: 0.00% (Excellent)
   ⏱️  Latency: 0.8ms (Excellent)  
   📈 Throughput: 9.8 Mbps (Optimal для 10BASE-T1S)
   🎯 Frame Error Rate: <0.01%

💡 RECOMMENDATIONS:
   ✅ Cable system performing optimally
   ✅ No immediate maintenance required
   📅 Next inspection: Recommended in 6 months
   📊 Continue monitoring SQI trends

⏱️  Total diagnostic time: 28.3 seconds
🎉 Cable System Status: EXCELLENT
```

### Quick Check Output
```  
================================================================================
Quick Cable & SQI Check
================================================================================

🚀 Fast Assessment (< 10 seconds):

   📊 Current SQI: 5/7 ✅ Good Quality
   📏 Cable Length: ~47m ✅ Normal
   ⚡ Link Status: UP ✅ Active
   🔍 Major Faults: NONE ✅ Clear

🎯 Summary: Link operational and healthy
⏱️  Check completed in 3.7 seconds

💡 Next action: Full diagnostics recommended in 24h
```

## Advanced Features

### SQI Trend Analysis
```python
def sqi_trend_analysis(measurements: list):
    """
    Statistical analysis av SQI trends
    
    Features:
    - Moving averages (5min, 1h, 24h)
    - Degradation rate calculation  
    - Predictive maintenance alerts
    - Seasonal/environmental correlation
    """
```

### Automated Alerts & Notifications  
```python
class SQIAlertManager:
    def __init__(self):
        """
        Alert thresholds:
        - SQI ≤ 2: CRITICAL (immediate action)
        - SQI = 3: WARNING (monitor closely)
        - SQI = 4: INFO (minor degradation)
        - SQI ≥ 5: OK (normal operation)
        """
        
    def check_alert_conditions(self, sqi_data):
        """Generate alerts basierend auf SQI thresholds"""
        
    def send_notifications(self, alert_type, message):
        """Send notifications (log, email, webhook)"""
```

### Report Generation
```python
def generate_diagnostic_report(format='pdf'):
    """
    Comprehensive report generation
    
    Formats:
    - PDF: Professional report with graphs
    - JSON: Machine-readable data export
    - CSV: Time-series data für spreadsheets  
    - HTML: Interactive dashboard
    """
```

## Error Handling & Recovery

### Diagnostic Failures
1. **SQI Read Failure**: Register access timeout/error
2. **Cable Test Timeout**: CFD doesn't complete
3. **Invalid Measurements**: Out-of-range values  
4. **Environmental Sensor Errors**: Temperature/voltage issues

### Recovery Strategies
```python
class DiagnosticRecovery:
    def handle_sqi_read_failure(self):
        """
        - Retry with different timing
        - Check device connectivity  
        - Fallback to basic link status
        """
        
    def handle_cable_test_timeout(self):
        """
        - Extend timeout för long cables
        - Retry with modified parameters
        - Report partial results
        """
        
    def validate_measurements(self, data):
        """
        - Range checking (SQI 0-7, length 0-100m)
        - Consistency verification
        - Outlier detection and filtering
        """
```

## Performance Requirements

### Speed Requirements
- **Quick Check**: < 5 seconds (SQI + basic cable status)
- **Full Diagnostics**: < 30 seconds (complete cable test + environment)
- **Continuous Monitoring**: Real-time updates (1-10Hz sample rate)
- **Report Generation**: < 60 seconds för comprehensive PDF report

### Accuracy Requirements  
- **SQI Measurement**: ±0.5 SQI units consistency
- **Cable Length**: ±0.5m accuracy (TDR-based)
- **Fault Location**: ±1.0m accuracy för fault distance
- **Temperature**: ±2°C accuracy (environmental monitoring)

## Integration Requirements

### Kompatibilität
- Integrates with existing register access infrastructure
- Shares utilities with PLCA and Configuration tools
- Compatible med AN1760 specifications
- Consistent logging and reporting format

### Dependencies
```python
import matplotlib.pyplot as plt  # För graph generation
import numpy as np              # Statistical analysis
import fpdf                     # PDF report generation
import json                     # JSON export
import csv                      # CSV export
# Plus standard libraries from core project
```

## Success Criteria

### Functional Requirements ✅
- [x] Real-time SQI monitoring (0.1-10Hz sample rates)
- [x] Complete cable fault diagnostics (Open/Short/Miswiring)
- [x] Cable length measurement (±0.5m accuracy)
- [x] Environmental condition monitoring (temp/voltage)
- [x] Trend analysis and predictive maintenance alerts
- [x] Multi-format report generation (PDF/JSON/CSV/HTML)

### Quality Requirements ✅
- [x] <5 second quick checks
- [x] <30 second comprehensive diagnostics
- [x] 99.5% measurement consistency
- [x] Automated fault classification and recommendations
- [x] Professional report formatting

### Integration Requirements ✅
- [x] Seamless integration with PLCA setup tools
- [x] Compatible med existing serial communication
- [x] Consistent error handling and logging
- [x] Shared configuration and device management

---

**Implementation Priority**: 🔧 **MEDIUM-HIGH** - Essential för network troubleshooting & predictive maintenance!