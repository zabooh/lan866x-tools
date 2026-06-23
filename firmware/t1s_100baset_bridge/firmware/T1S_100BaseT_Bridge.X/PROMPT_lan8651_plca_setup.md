# Tool Prompt: lan8651_plca_setup.py  
# PLCA Coordinator/Follower Setup

## Tool-Spezifikation

**Name**: `lan8651_1760_plca_setup.py`  
**Zweck**: Physical Layer Collision Avoidance (PLCA) Setup für Multi-Node 10BASE-T1S Networks  
**Ziel**: Deterministic, kollisionsfreie Multi-Drop Ethernet Netzwerke  

## PLCA-Technologie Überblick

### PLCA vs. CSMA/CD
- **PLCA**: Deterministic access, collision-free, optimal für >2 nodes
- **CSMA/CD**: Classic Ethernet mit collision detection, für point-to-point

### Network Topology
```
    Coordinator (Node 0)
    |
    |-- Follower (Node 1) 
    |-- Follower (Node 2)
    |-- Follower (Node 3)
    |-- ... (bis zu 254 Nodes möglich)
```

## Funktionale Anforderungen

### 1. PLCA Coordinator Setup (Node ID = 0)
```python
def setup_plca_coordinator(node_count: int):
    """
    Konfiguriere Device als PLCA Coordinator
    
    Parameters:
    - node_count: Gesamtanzahl Nodes im Network (1-254)
    
    Configuration:
    - PLCA_CTRL1: (node_count << 8) für Coordinator
    - PLCA_CTRL0: 0x8000 (PLCA Enable + Reset)
    - CDCTL0: Collision Detection meist disabled
    """
```

### 2. PLCA Follower Setup (Node ID ≠ 0) 
```python
def setup_plca_follower(node_id: int, node_count: int):
    """
    Konfiguriere Device als PLCA Follower
    
    Parameters:
    - node_id: Eindeutige Node-ID (1-254)
    - node_count: Gesamtanzahl Nodes im Network
    
    Configuration:
    - PLCA_CTRL1: node_id (einfache Node-ID)
    - PLCA_CTRL0: 0x8000 (PLCA Enable + Reset)
    - Wait für Coordinator-Beacons
    """
```

### 3. PLCA Register Mapping (AN1760 + Hardware-verified)
```python
PLCA_REGISTERS = {
    # Hardware-verified aus unserem Projekt
    'PLCA_CTRL0': 0x0004CA01,    # Enable/Disable + Reset
    'PLCA_CTRL1': 0x0004CA02,    # Node ID + Node Count Setup  
    'PLCA_STS':   0x0004CA03,    # PLCA Status Register
    'PLCA_TOTMR': 0x0004CA04,    # Transmit Opportunity Timer
    'PLCA_BURST': 0x0004CA05,    # Burst Mode Timer
    
    # Collision Detection Control (AN1760)
    'CDCTL0': 0x00040087,        # Collision Detection Control (estimated)
}
```

### 4. PLCA Status & Diagnostics
```python
def get_plca_status() -> dict:
    """
    Read current PLCA status
    
    Returns:
    {
        'plca_enabled': bool,
        'node_id': int,  
        'node_count': int,
        'coordinator': bool,
        'status': 'active|inactive|error',
        'transmit_opportunities': int,
        'beacon_detected': bool
    }
    """
```

### 5. Collision Detection Management
```python
def configure_collision_detection(mode: str):
    """
    Configure collision detection behavior
    
    Modes:
    - 'plca_disabled': Enable collision detection (CSMA/CD mode)  
    - 'plca_enabled': Disable collision detection (PLCA mode)
    - 'hybrid': Smart switching basierend auf PLCA status
    """
```

## Interface-Spezifikation

### Command Line Interface
```bash
python lan8651_plca_setup.py [OPTIONS] COMMAND

COMMANDS:
    coordinator --nodes N        # Setup als PLCA Coordinator für N nodes
    follower --id X --nodes N   # Setup als Follower (Node ID X von N)  
    status                      # Show current PLCA status
    disable                     # Disable PLCA (return to CSMA/CD)
    scan                       # Scan network für active PLCA nodes

OPTIONS:
    --device COM8              # Serial port
    --verify                  # Enable configuration verification
    --collision-detect MODE   # auto|enable|disable collision detection
    --timeout 5.0            # Configuration timeout
    --wait-beacon 10.0       # Wait time für Coordinator beacon (followers)
```

### Usage Examples
```bash
# Setup Coordinator für 4-Node network
python lan8651_plca_setup.py --device COM8 coordinator --nodes 4

# Setup Node 2 in 4-Node network
python lan8651_plca_setup.py --device COM8 follower --id 2 --nodes 4

# Check current PLCA status
python lan8651_plca_setup.py --device COM8 status

# Network scan
python lan8651_plca_setup.py --device COM8 scan --timeout 30
```

### Python API
```python
class PLCAManager:
    def __init__(self, port='COM8', baudrate=115200):
        """Initialize PLCA Manager"""
        
    def setup_coordinator(self, node_count: int, verify=True) -> bool:
        """Configure device als PLCA Coordinator"""
        
    def setup_follower(self, node_id: int, node_count: int, verify=True) -> bool:
        """Configure device als PLCA Follower"""
        
    def get_plca_status(self) -> dict:
        """Get current PLCA status"""
        
    def disable_plca(self) -> bool:
        """Disable PLCA (return to CSMA/CD)"""
        
    def scan_network(self, timeout=30.0) -> list:
        """Scan für active PLCA nodes"""
        
    def configure_collision_detection(self, mode: str) -> bool:
        """Configure collision detection behavior"""
        
    def wait_for_beacon(self, timeout=10.0) -> bool:
        """Wait für Coordinator beacon (für followers)"""
```

## Configuration Process 

### Coordinator Setup Sequence
```python
def coordinator_setup_sequence(node_count):
    """
    1. Device verification
    2. Disable PLCA (reset state)
    3. Configure PLCA_CTRL1 = (node_count << 8)
    4. Enable PLCA via PLCA_CTRL0 = 0x8000
    5. Configure collision detection (usually disable)
    6. Verify PLCA status
    7. Start transmit opportunity management
    """
```

### Follower Setup Sequence  
```python
def follower_setup_sequence(node_id, node_count):
    """
    1. Device verification
    2. Disable PLCA (reset state) 
    3. Configure PLCA_CTRL1 = node_id
    4. Enable PLCA via PLCA_CTRL0 = 0x8000
    5. Configure collision detection (usually disable)
    6. Wait für Coordinator beacon (timeout: 10s)
    7. Verify PLCA synchronization
    8. Report ready status
    """
```

## Output-Format

### Coordinator Setup Output
```
================================================================================
PLCA Coordinator Setup - Node Count: 4
================================================================================

🎯 Network Configuration:
   📡 Role: PLCA Coordinator (Master)
   🌐 Nodes: 4 total (1 Coordinator + 3 Followers)  
   🔄 Transmit Opportunities: Managed by this node

🔧 Device Configuration:
   [1/5] PLCA_CTRL0 = 0x0000 (Reset PLCA) ✅
   [2/5] PLCA_CTRL1 = 0x0400 (Node Count = 4) ✅
   [3/5] PLCA_CTRL0 = 0x8000 (Enable PLCA) ✅
   [4/5] CDCTL0 = 0x0000 (Disable Collision Detection) ✅
   [5/5] Configuration verification ✅

📊 PLCA Status:
   ✅ PLCA enabled and active
   ✅ Coordinator role confirmed
   ✅ Broadcasting transmit opportunities
   ✅ Network ready für followers

⏱️ Configuration completed in 3.2 seconds
🎉 PLCA Coordinator ready - Followers can now join!
```

### Follower Setup Output
```
================================================================================
PLCA Follower Setup - Node ID: 2 of 4
================================================================================

🎯 Network Configuration:
   📡 Role: PLCA Follower (Node 2)
   🌐 Network: 4 nodes total
   📶 Coordinator: Waiting für beacon...

🔧 Device Configuration:  
   [1/5] PLCA_CTRL0 = 0x0000 (Reset PLCA) ✅
   [2/5] PLCA_CTRL1 = 0x0002 (Node ID = 2) ✅
   [3/5] PLCA_CTRL0 = 0x8000 (Enable PLCA) ✅
   [4/5] CDCTL0 = 0x0000 (Disable Collision Detection) ✅
   [5/5] Waiting für Coordinator beacon...

📶 Beacon Detection:
   ⏳ Listening für Coordinator... 
   ✅ Beacon detected from Coordinator!
   ✅ PLCA synchronization successful
   ✅ Node ready für scheduled transmission

⏱️ Configuration completed in 7.8 seconds (incl. beacon wait)
🎉 PLCA Follower ready - Node 2 active in 4-node network!
```

### Network Scan Output
```
================================================================================  
PLCA Network Scan - Discovering Active Nodes
================================================================================

🔍 Scanning 10BASE-T1S network...

📊 Discovered Nodes:
   Node 0: ✅ PLCA Coordinator (MAC: 00:04:25:01:02:03)
   Node 1: ✅ PLCA Follower    (MAC: 00:04:25:01:02:04)  
   Node 2: ✅ PLCA Follower    (MAC: 00:04:25:01:02:05)
   Node 3: ❌ Not responding   (Timeout)

📈 Network Health:
   🌐 Active Nodes: 3 of 4 configured
   📶 Coordinator: Active and managing
   ⚡ Network Performance: Good
   ⚠️  Warning: Node 3 not responding

⏱️ Scan completed in 15.3 seconds
```

## Error Handling & Recovery

### Fehler-Szenarien
1. **No Coordinator Found**: Follower setup ohne active Coordinator
2. **Node ID Conflicts**: Mehrere Nodes mit gleicher ID
3. **Beacon Timeout**: Follower kann nicht synchronize mit Coordinator  
4. **PLCA Hardware Error**: Register-Zugriff fehlerhaft

### Recovery Strategies
```python
class PLCARecovery:
    def handle_no_coordinator(self):
        """
        - Promote einen Follower zum Coordinator
        - Oder manual configuration required
        """
        
    def handle_node_id_conflict(self, conflicting_id):  
        """
        - Automatic ID reassignment
        - Network topology rebuild
        """
        
    def handle_beacon_timeout(self):
        """
        - Extended wait period
        - Fallback zu CSMA/CD mode
        - Manual coordinator check
        """
```

## Performance & Diagnostics

### Network Performance Metrics
```python
def get_plca_performance() -> dict:
    return {
        'transmit_opportunities': count,
        'successful_transmissions': count, 
        'collision_avoidance_rate': percentage,
        'network_utilization': percentage,
        'beacon_interval': milliseconds,
        'node_response_times': [node_id: time_ms]
    }
```

### Real-time Monitoring
```python  
def monitor_plca_network(duration_seconds=60):
    """
    Real-time PLCA network monitoring
    - Transmit opportunity tracking
    - Node responsiveness
    - Performance metrics
    - Alert bei network issues
    """
```

## Integration Requirements

### Kompatibilität 
- Nutzt bestehende Serial Communication Infrastructure
- Compatible mit AN1760 Configuration Tool  
- Shared Registry für Network-wide Settings
- Consistent Logging & Error Reporting

### Dependencies
```python
import time
import threading  # Für background monitoring
import queue     # Für async communication
# Plus standard libraries from existing tools
```

## Success Criteria

### Funktionale Requirements ✅
- [x] Coordinator Setup (Node 0) mit variable node count
- [x] Follower Setup (Node 1-254) mit automatic beacon synchronization
- [x] Network Scanning & Discovery
- [x] PLCA Status Monitoring & Diagnostics  
- [x] Collision Detection Management
- [x] Error Recovery & Fallback Mechanisms

### Network Requirements ✅
- [x] Support für 2-254 Nodes per network
- [x] Deterministic transmission scheduling
- [x] <1% collision rate in proper PLCA setup
- [x] Coordinator failover handling
- [x] Network topology auto-discovery  

### Performance Requirements ✅
- [x] Coordinator setup: <5 seconds
- [x] Follower setup: <10 seconds (incl. beacon wait)
- [x] Network scan: <30 seconds för 8-node network  
- [x] Status query: <2 seconds
- [x] 99.9% erfolgreiche PLCA synchronization rate

---

**Implementation Priority**: 🚨 **HIGH** - Critical für Multi-Node 10BASE-T1S network deployment!