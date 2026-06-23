#!/usr/bin/env python3
"""
LAN8651 Cable Fault Diagnostics (CFD) Test Tool
Detects cable faults: GND short, VDD short, general PHY errors

Hardware: LAN8650/1 10BASE-T1S MAC-PHY
CFD Registers (MMS_3):
- PMD_CONTROL (0x00030001): Start CFD test
- PMD_STATUS (0x00030002): CFD results and fault information
"""

import serial
import time
import sys

def send_robust_command(ser, command, timeout=2.0):
    """Send command with robust prompt synchronization"""
    ser.reset_input_buffer()
    ser.write(f'{command}\r\n'.encode())
    
    start_time = time.time()
    response = ""
    
    while time.time() - start_time < timeout:
        if ser.in_waiting > 0:
            chunk = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
            response += chunk
            
            if ('LAN865X Read:' in response and '>' in response) or response.rstrip().endswith('>'):
                return response
                
        time.sleep(0.01)
    
    return response

def read_register(ser, address):
    """Read LAN8651 register with improved error handling"""
    try:
        command = f"lan_read 0x{address:08X}"
        ser.reset_input_buffer()
        ser.write(f'{command}\r\n'.encode())
        
        response = ""
        start_time = time.time()
        
        # Read response with longer timeout for CFD
        while time.time() - start_time < 3.0:
            if ser.in_waiting > 0:
                try:
                    chunk = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                    response += chunk
                    
                    # Look for complete response
                    if '=' in response and ('OK' in response or '>' in response):
                        break
                except:
                    pass
            time.sleep(0.05)
        
        # Parse only real read-result lines, e.g.:
        # "LAN865X Read: [0x00030001] = 0x00000002"
        for line in response.splitlines():
            if "LAN865X Read:" not in line or "=" not in line:
                continue

            value_str = line.rsplit("=", 1)[1].strip().split()[0]

            if value_str.startswith("0x") or value_str.startswith("0X"):
                try:
                    return int(value_str, 16)
                except ValueError:
                    print(f"   ⚠️ Could not parse value: {value_str}")
                    return None

            try:
                return int(value_str)
            except ValueError:
                print(f"   ⚠️ Could not parse value: {value_str}")
                return None
        
        return None
    except Exception as e:
        print(f"   ⚠️ Register read error: {e}")
        return None

def write_register(ser, address, value):
    """Write LAN8651 register with improved error handling"""
    try:
        command = f"lan_write 0x{address:08X} {value}"
        ser.reset_input_buffer()
        ser.write(f'{command}\r\n'.encode())
        
        response = ""
        start_time = time.time()
        
        # Read response
        while time.time() - start_time < 2.0:
            if ser.in_waiting > 0:
                try:
                    chunk = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                    response += chunk
                    
                    if 'OK' in response or '>' in response:
                        break
                except:
                    pass
            time.sleep(0.05)
        
        return 'OK' in response
    except Exception as e:
        print(f"   ⚠️ Register write error: {e}")
        return False

class CFDTest:
    """Cable Fault Diagnostics Test"""
    
    def __init__(self, port='COM8', baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        
        # CFD Register Addresses (MMS_3: PHY PMA/PMD)
        # Some stacks use Clause-22 offsets (CONTROL=0x0000, STATUS=0x0001),
        # others use +1 shifted addresses. We detect this at runtime.
        self.PMD_CONTROL = 0x00030001
        self.PMD_STATUS = 0x00030002
        self.PMD_CANDIDATES = [
            (0x00030001, 0x00030002),
            (0x00030000, 0x00030001),
        ]
        
        # Control Bits
        self.CFD_START_BIT = 0x0001     # Bit 0: Start CFD test
        self.CFD_ENABLE_BIT = 0x0002    # Bit 1: Enable CFD
        
        # Status Bits
        self.LINK_BIT = 0x0004          # Bit 2: Link status
        self.FAULT_DETECTED_BIT = 0x0008    # Bit 3: Fault detected
        self.FAULT_TYPE_MASK = 0x00F0   # Bits 7:4: Fault type
        self.FAULT_TYPE_SHIFT = 4
        self.CFD_DONE_BIT = 0x0100      # Bit 8: CFD test complete
        
        # Fault Type Mapping
        self.FAULT_TYPES = {
            0x0: "No Fault",
            0x1: "Cable Short to GND",
            0x2: "Cable Short to VDD",
            0x3: "Cable Short (GND or VDD)",
            0x4: "High Impedance Fault",
            0x5: "Low Impedance Fault",
            0x6: "Receiver Saturation",
            0x7: "Transmitter Saturation",
            0x8: "PHY Analog Error",
            0x9: "PHY Digital Error",
            0xA: "Cable Open Circuit",
            0xB: "Cable Mismatch",
            0xC: "Unknown Fault",
            0xF: "Test Failed"
        }

    def detect_pmd_addresses(self, ser):
        """Detect working PMD control/status address pair."""
        for control_addr, status_addr in self.PMD_CANDIDATES:
            status = read_register(ser, status_addr)
            if status is not None:
                self.PMD_CONTROL = control_addr
                self.PMD_STATUS = status_addr
                print(f"   ✅ Using PMD map: CTRL=0x{control_addr:08X}, STATUS=0x{status_addr:08X}")
                return True

        print("   ⚠️ No PMD map responded (timeouts on all variants)")
        print(f"   ⚠️ Keeping default map: CTRL=0x{self.PMD_CONTROL:08X}, STATUS=0x{self.PMD_STATUS:08X}")
        return False

    def run_cfd_test(self, ser):
        """Execute Cable Fault Diagnostics test"""
        print("=" * 80)

        print("\n🧭 Detecting PMD address map...")
        self.detect_pmd_addresses(ser)
        print("🔍 LAN8651 Cable Fault Diagnostics (CFD) Test")
        print("=" * 80)
        
        # Check initial link status
        print("\n📊 Pre-Test Status Check...")
        status_before = read_register(ser, self.PMD_STATUS)
        if status_before is None:
            print("⚠️ Could not read PMD_STATUS - proceeding with test anyway...")
            link_up = "Unknown"
        else:
            link_up = bool(status_before & self.LINK_BIT)
            print(f"   Link Status: {'🟢 UP' if link_up else '🔴 DOWN'}")
        
        # Start CFD Test
        print("\n🚀 Starting CFD Test...")
        cfd_control = self.CFD_START_BIT | self.CFD_ENABLE_BIT
        if not write_register(ser, self.PMD_CONTROL, cfd_control):
            print("⚠️ CFD test write returned error - proceeding anyway...")
        else:
            print("   ✅ CFD test initiated")
        
        # Wait for test completion
        print("\n⏳ Waiting for CFD test to complete...")
        cfd_done = False
        timeout_count = 0
        max_timeout = 100  # 10 seconds (100 * 100ms)
        
        while not cfd_done and timeout_count < max_timeout:
            time.sleep(0.1)
            timeout_count += 1
            
            status = read_register(ser, self.PMD_STATUS)
            if status is None:
                if timeout_count % 20 == 0:
                    print(f"   ⏳ Waiting... ({timeout_count/10:.1f}s)")
                continue
            
            cfd_done = bool(status & self.CFD_DONE_BIT)
            
            # Progress indication
            if timeout_count % 20 == 0:
                print(f"   Progress: {timeout_count/10:.1f}s elapsed...")
        
        if not cfd_done:
            print("⏰ CFD test timeout - using available status")
            print("   (Register may not support CFD or test is very slow)")
        else:
            print("   ✅ CFD test completed")
        
        # Read and interpret results
        print("\n📋 CFD Test Results:")
        print("-" * 80)
        
        status = read_register(ser, self.PMD_STATUS)
        if status is None:
            print("⚠️ Could not read final CFD status")
            print("\n🔧 Fallback: Checking Link Status Instead...")
            
            # Try checking link via PHY Basic Status
            link_status = read_register(ser, 0x0000FF01)  # PHY_BASIC_STATUS
            if link_status is not None:
                if link_status & 0x0004:
                    print("✅ Link is UP - Cable appears OK")
                    return True
                else:
                    print("❌ Link is DOWN - Cable may have issues")
                    return False
            return False
        
        fault_detected = bool(status & self.FAULT_DETECTED_BIT)
        fault_type_code = (status >> self.FAULT_TYPE_SHIFT) & 0x0F
        fault_type_desc = self.FAULT_TYPES.get(fault_type_code, "Unknown Fault Type")
        link_up = bool(status & self.LINK_BIT)
        
        print(f"   Link Status: {'🟢 UP' if link_up else '🔴 DOWN'}")
        print(f"   Fault Detected: {'❌ YES' if fault_detected else '✅ NO'}")
        print(f"   Fault Type Code: 0x{fault_type_code:X}")
        print(f"   Fault Type: {fault_type_desc}")
        
        # Interpretation and Recommendations
        print("\n🎯 Interpretation & Recommendations:")
        print("-" * 80)
        
        if not fault_detected:
            print("✅ CABLE OK - No faults detected")
            print("   The single-pair Ethernet cable appears to be in good condition.")
            print("   No short circuits to GND or VDD detected.")
            return True
        
        else:
            print("⚠️ CABLE FAULT DETECTED")
            
            # Classify fault
            if fault_type_code == 0x1:
                print("   ❌ SHORT CIRCUIT TO GROUND")
                print("      • Check for:")
                print("        - Damaged cable insulation")
                print("        - Connector corrosion")
                print("        - Twisted pair touching external GND")
                print("      • Solution: Replace cable or repair connection")
                
            elif fault_type_code == 0x2:
                print("   ❌ SHORT CIRCUIT TO VDD (+3.3V)")
                print("      • Check for:")
                print("        - Damaged insulation near power pins")
                print("        - Connector pin contact with VDD")
                print("        - Moisture/condensation on connector")
                print("      • Solution: Replace cable, check PCB layout")
                
            elif fault_type_code == 0x3:
                print("   ❌ SHORT CIRCUIT (GND or VDD)")
                print("      • Unspecified short circuit detected")
                print("      • Check physical cable condition")
                
            elif fault_type_code == 0xA:
                print("   ⚠️ OPEN CIRCUIT")
                print("      • Cable broken or connection missing")
                print("      • Check for:")
                print("        - Physical cable break")
                print("        - Loose connector")
                print("        - Missing termination")
                print("      • Solution: Verify physical connection")
                
            elif fault_type_code == 0x4:
                print("   ⚠️ HIGH IMPEDANCE FAULT")
                print("      • Poor connection or intermittent fault")
                print("      • Solution: Clean connectors, re-seat cable")
                
            elif fault_type_code == 0x6:
                print("   ⚠️ RECEIVER SATURATION")
                print("      • Signal level too high")
                print("      • Check cable shielding and interface circuitry")
                
            else:
                print(f"   ⚠️ FAULT TYPE: {fault_type_desc}")
                print("      • Refer to LAN8651 datasheet for details")
            
            return False

    def run_multiple_tests(self, ser, num_tests=3):
        """Run CFD test multiple times for reliability"""
        print("\n" + "=" * 80)
        print(f"🔄 Running {num_tests} CFD Tests for Confidence Check")
        print("=" * 80)
        
        results = []
        for i in range(num_tests):
            print(f"\n[Test {i+1}/{num_tests}]")
            result = self.run_cfd_test(ser)
            results.append(result)
            
            if i < num_tests - 1:
                print("\n⏳ Waiting 2 seconds before next test...")
                time.sleep(2)
        
        # Summary
        print("\n" + "=" * 80)
        print("📊 Test Summary")
        print("=" * 80)
        passed = sum(results)
        print(f"Passed: {passed}/{num_tests} tests")
        
        if passed == num_tests:
            print("✅ CABLE HEALTHY - All tests passed")
        elif passed > num_tests / 2:
            print("⚠️ CABLE QUESTIONABLE - Some tests failed")
        else:
            print("❌ CABLE FAULTY - Most tests failed")
        
        return results

def main():
    """Main CFD test routine"""
    try:
        cfd = CFDTest()
        
        with serial.Serial(cfd.port, cfd.baudrate, timeout=1) as ser:
            print(f"✅ Connected to {cfd.port}\n")
            time.sleep(1)
            
            # Run single test
            cfd.run_cfd_test(ser)
            
            print("\n" + "=" * 80)
            print("🎉 CFD Test Complete!")
            print("=" * 80)
    
    except serial.SerialException as e:
        print(f"❌ Serial connection failed: {e}")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\n⏹️ Test interrupted by user")
        sys.exit(0)
    except Exception as e:
        print(f"❌ Unexpected error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
