#!/usr/bin/env python3
"""
LAN8651 Cable Diagnostic Tool
Comprehensive cable testing and diagnosis for 10BASE-T1S Single Pair Ethernet

Features:
- Signal quality assessment 
- Single Pair Ethernet specific tests
- Link status monitoring
- Basic cable connectivity tests

Hardware: LAN8650/1 10BASE-T1S MAC-PHY
Protocol: TC6 over SPI
"""

import re
import serial
import time
import math
from datetime import datetime

def send_robust_command(ser, command, timeout=3.5, complete_pattern=None):
    """Send command with robust prompt synchronization.
    complete_pattern: if given, the response must contain this string before
    an ending '>' prompt is considered final.
    """
    ser.reset_input_buffer()
    ser.write(f'{command}\r\n'.encode())
    
    start_time = time.time()
    response = ""
    
    while time.time() - start_time < timeout:
        if ser.in_waiting > 0:
            chunk = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
            response += chunk
            
            if complete_pattern:
                if complete_pattern in response and response.rstrip().endswith('>'):
                    return response
            else:
                if response.rstrip().endswith('>'):
                    return response
                
        time.sleep(0.01)
    
    print(f"⚠️ Command timeout: {command}")
    return response

def read_register(ser, address):
    """Read LAN8651 register with error handling"""
    try:
        result = send_robust_command(ser, f"lan_read 0x{address:08X}", complete_pattern='Value=')
        if result:
            m = re.search(r'LAN865X Read:.*Value=(0x[0-9a-fA-F]+)', result)
            if m:
                return int(m.group(1), 16)
        return None
    except Exception as e:
        print(f"❌ Register read error (0x{address:08X}): {e}")
        return None

def write_register(ser, address, value):
    """Write LAN8651 register with error handling"""
    try:
        result = send_robust_command(ser, f"lan_write 0x{address:08X} {value}")
        return result and "OK" in result
    except Exception as e:
        print(f"❌ Register write error (0x{address:08X}): {e}")
        return False

class CableDiagnostic:
    def __init__(self, port='COM8', baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        
        # Cable Diagnostic Register Map
        # Address format: (MMS << 16) | offset  (upper 16 bit = MMS, lower 16 bit = offset)
        self.registers = {
            # Basic PHY Status — MMS 0, Clause 22 indirect (offset 0xFF00+)
            'PHY_BASIC_STATUS':  0x0000FF01,   # PHY Basic Status Register
            'PHY_BASIC_CONTROL': 0x0000FF00,   # PHY Basic Control Register

            # OA Status — MMS 0
            'OA_STATUS0': 0x00000008,           # Open Alliance Status 0 (link up etc.)
            'OA_STATUS1': 0x00000009,           # Open Alliance Status 1

            # PLCA Control/Status — MMS 4 Vendor-Specific (0x0004xxxx)
            'PLCA_CONTROL_0': 0x0004CA01,       # PLCA Enable (bit15), Node-ID (bits7:0)
            'PLCA_CONTROL_1': 0x0004CA02,       # Node-Count (bits7:0)
            'PLCA_STATUS':    0x0004CA03,       # PLCA_STATUS (bit15 = PCST: Receive Opportunity)
            'PLCA_BURST':     0x0004CA05,       # Burst-Count (bits7:0), Burst-Timer (bits15:8)
        }

    def test_basic_connectivity(self, ser):
        """Test basic PHY connectivity and link status via PHY Basic Status (MMS 0, Clause 22)"""
        print("🔗 Basic Connectivity Test")
        print("-" * 50)

        status = read_register(ser, self.registers['PHY_BASIC_STATUS'])
        if status is not None:
            link_up = bool(status & 0x04)       # Bit 2 = Link Status
            remote_fault = bool(status & 0x10)  # Bit 4 = Remote Fault
            print(f"📋 PHY_BASIC_STATUS: 0x{status:04X}")
            print(f"📡 Link Status:  {'🟢 UP' if link_up else '🔴 DOWN'}")
            print(f"⚠️ Remote Fault: {'❌ DETECTED' if remote_fault else '✅ None'}")
            return link_up
        else:
            print("❌ Failed to read PHY_BASIC_STATUS (0x0000FF01)")
            return False

    def test_oa_status(self, ser):
        """Check Open Alliance status registers for error flags (MMS 0)"""
        print("\n📊 Open Alliance Status Check")
        print("-" * 50)

        sts0 = read_register(ser, self.registers['OA_STATUS0'])
        if sts0 is not None:
            print(f"📋 OA_STATUS0: 0x{sts0:08X}")
            errors = []
            if sts0 & 0x001: errors.append("TXPE (TX Protocol Error)")
            if sts0 & 0x002: errors.append("TXBOE (TX Buffer Overflow)")
            if sts0 & 0x004: errors.append("TXBUE (TX Buffer Underrun)")
            if sts0 & 0x008: errors.append("RXBOE (RX Buffer Overflow)")
            if sts0 & 0x010: errors.append("LOFE (Loss of Frame)")
            if sts0 & 0x020: errors.append("HDRE (Header Error)")
            if sts0 & 0x800: errors.append("TXFCSE (TX FCS Error)")
            if sts0 & 0x040: print("  ℹ️  RESETC: Reset Complete")
            if sts0 & 0x080: print("  ℹ️  PHYINT: PHY Interrupt pending")
            for e in errors:
                print(f"  ⚠️  {e}")
            if not errors:
                print("  ✅ No errors")
        else:
            print("❌ Failed to read OA_STATUS0")

        sts1 = read_register(ser, self.registers['OA_STATUS1'])
        if sts1 is not None:
            print(f"📋 OA_STATUS1: 0x{sts1:08X}")
            errors1 = []
            if sts1 & 0x00000001: errors1.append("RXNER (RX FIFO Error)")
            if sts1 & 0x00000002: errors1.append("TXNER (TX FIFO Error)")
            if sts1 & 0x00020000: errors1.append("FSMSTER (State Machine Error)")
            if sts1 & 0x00040000: errors1.append("ECC Error")
            if sts1 & 0x00080000: errors1.append("UV18 (1.8 V Undervoltage)")
            if sts1 & 0x00100000: errors1.append("BUSER (Bus Error)")
            for e in errors1:
                print(f"  ❌ {e}")
            if not errors1:
                print("  ✅ No errors")

    def test_plca_status(self, ser):
        """Check PLCA configuration and status via MMS 4 Vendor-Specific registers (0x0004CAxx)"""
        print("\n🔄 PLCA Status")
        print("-" * 50)

        ctrl0 = read_register(ser, self.registers['PLCA_CONTROL_0'])
        ctrl1 = read_register(ser, self.registers['PLCA_CONTROL_1'])
        status = read_register(ser, self.registers['PLCA_STATUS'])
        burst = read_register(ser, self.registers['PLCA_BURST'])

        if ctrl0 is not None:
            plca_enabled = bool(ctrl0 & 0x8000)
            print(f"📋 PLCA_CONTROL_0 (0x0004CA01): 0x{ctrl0:08X}")
            print(f"  PLCA Enable: {'🟢 YES' if plca_enabled else '🔴 NO'}")
        else:
            print("❌ Failed to read PLCA_CONTROL_0")

        if ctrl1 is not None:
            # TC6 spec: PLCA_CONTROL_1[15:8] = NODE_CNT, [7:0] = NODE_ID
            node_count = (ctrl1 >> 8) & 0xFF
            node_id    = ctrl1 & 0xFF
            print(f"📋 PLCA_CONTROL_1 (0x0004CA02): 0x{ctrl1:08X}")
            print(f"  Node-ID: {node_id},  Node-Count: {node_count}")

        if status is not None:
            pcst = bool(status & 0x8000)   # Bit 15: Receive Opportunity available
            print(f"📋 PLCA_STATUS    (0x0004CA03): 0x{status:08X}")
            print(f"  PCST (Receive Opportunity): {'🟢 Active' if pcst else '🔴 Inactive'}")
        else:
            print("❌ Failed to read PLCA_STATUS")

        if burst is not None:
            burst_timer = burst & 0xFF           # BTMR in bits[7:0]
            burst_count = (burst >> 8) & 0xFF    # BCNT in bits[15:8]
            print(f"📋 PLCA_BURST     (0x0004CA05): 0x{burst:08X}")
            print(f"  Burst-Timer: {burst_timer},  Burst-Count: {burst_count}")

        return status is not None and bool(status & 0x8000)

    def generate_report(self, test_results):
        """Generate comprehensive diagnostic report"""
        print("\n" + "="*80)
        print("📋 CABLE DIAGNOSTIC SUMMARY REPORT")
        print("="*80)
        
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        print(f"🕐 Test Time: {timestamp}")
        print(f"🔧 Device: LAN8651 10BASE-T1S")
        print(f"📡 Interface: {self.port}")
        
        print(f"\n🏆 OVERALL ASSESSMENT:")
        if test_results.get('link_up', False):
            print("✅ CABLE STATUS: LINK UP")
        elif test_results.get('link_up') is None:
            print("⚠️ CABLE STATUS: PHY register read failed")
        else:
            print("❌ CABLE STATUS: LINK DOWN")
        if test_results.get('plca_active') is not None:
            print(f"  PLCA Receive Opportunity: {'🟢 Active' if test_results['plca_active'] else '🔴 Inactive'}")

def main():
    """Main cable diagnostic routine"""
    print("="*80)
    print("🔍 LAN8651 Cable Diagnostic Tool v1.0")
    print("Basic cable testing and diagnostics for 10BASE-T1S Single Pair Ethernet")
    print("="*80)
    
    diagnostic = CableDiagnostic()
    test_results = {}
    
    try:
        with serial.Serial(diagnostic.port, diagnostic.baudrate, timeout=1) as ser:
            print(f"✅ Connected to {diagnostic.port}")
            time.sleep(1)
            
            # Basic PHY connectivity
            test_results['link_up'] = diagnostic.test_basic_connectivity(ser)

            # Open Alliance status / error flags
            diagnostic.test_oa_status(ser)

            # PLCA status (MMS 4 Vendor-Specific)
            test_results['plca_active'] = diagnostic.test_plca_status(ser)

            # Summary report
            diagnostic.generate_report(test_results)
            
    except serial.SerialException as e:
        print(f"❌ Serial connection failed: {e}")
    except KeyboardInterrupt:
        print("\n⏹️ Test interrupted by user")
    except Exception as e:
        print(f"❌ Unexpected error: {e}")

if __name__ == "__main__":
    main()