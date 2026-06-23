#!/usr/bin/env python3
"""
LAN8651 MMD Indirect Access Test Tool

Tests verschiedene indirekte Zugriffsmethoden für LAN8651 Register:
- AN1760 Configuration Parameter (über MMD)  
- IEEE 802.3 Clause 45 MMD Register
- Verschiedene Address-Encoding-Varianten
- Hardware-spezifische Indirect Access Pattern

Basiert auf AN1740/AN1760 Applikation Notes mit der Methode:
  write_register(0x04, 0x00D8, addr)    // Set address
  write_register(0x04, 0x00DA, 0x02)    // Trigger read  
  return (read_register(0x04, 0x00D8) & mask)

Author: T1S Development Team
Date: März 2026
Hardware: LAN8650/8651 über SPI/TC6-Interface
"""

import argparse
import json
import re
import serial
import sys
import time
from datetime import datetime
from typing import Dict, List, Optional, Tuple

class MMDIndirectAccessTester:
    """MMD Indirect Access Test Tool für LAN8651"""
    
    def __init__(self, port: str = 'COM8', baudrate: int = 115200):
        self.port = port
        self.baudrate = baudrate
        self.serial_conn = None
        self.test_timeout = 2.0
        
        # MMD Indirect Access Register (MMS 4)
        self.MMD_ADDRESS_DATA = 0x000400D8    # MMD Address/Data Register
        self.MMD_CONTROL = 0x000400DA         # MMD Control Register
        self.MMD_READ_OP = 0x0002            # Read Operation Trigger
        
        # Test Results
        self.test_results = {}
        
    def connect(self) -> bool:
        """Verbindung zum Device herstellen"""
        try:
            self.serial_conn = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                timeout=self.test_timeout,
                bytesize=8,
                parity='N',
                stopbits=1
            )
            time.sleep(0.3)
            print(f"✅ Verbunden mit {self.port} bei {self.baudrate} baud")
            return True
        except Exception as e:
            print(f"❌ Verbindungsfehler: {e}")
            return False
    
    def disconnect(self):
        """Verbindung schließen"""
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.close()
            print("🔌 Verbindung getrennt")
    
    def send_command(self, command: str, expect_callback: bool = True) -> str:
        """Kommando senden und Antwort empfangen"""
        if not self.serial_conn or not self.serial_conn.is_open:
            return ""
        
        try:
            # Buffer leeren
            self.serial_conn.reset_input_buffer()
            
            # Kommando senden
            cmd_bytes = (command + '\r\n').encode('ascii')
            self.serial_conn.write(cmd_bytes)
            self.serial_conn.flush()
            
            # Antwort sammeln
            response_lines = []
            end_time = time.time() + self.test_timeout
            
            while time.time() < end_time:
                if self.serial_conn.in_waiting > 0:
                    line = self.serial_conn.readline().decode('ascii', errors='ignore').strip()
                    if line:
                        response_lines.append(line)
                        if not expect_callback and line.endswith('>'):
                            break
                else:
                    time.sleep(0.01)
            
            # Zusätzlich warten auf Register-Callbacks
            if expect_callback and command.startswith('lan_'):
                additional_wait = 0.5
                end_time = time.time() + additional_wait
                callback_received = False
                
                while time.time() < end_time and not callback_received:
                    if self.serial_conn.in_waiting > 0:
                        line = self.serial_conn.readline().decode('ascii', errors='ignore').strip()
                        if line:
                            response_lines.append(line)
                            if 'Value=' in line or '- OK' in line:
                                callback_received = True
                    else:
                        time.sleep(0.01)
            
            return '\n'.join(response_lines)
            
        except Exception as e:
            return f"ERROR: {e}"
    
    def lan_read(self, address: int) -> Optional[int]:
        """Register lesen mit Hardware-Callback-Parsing"""
        try:
            command = f"lan_read 0x{address:08X}"
            response = self.send_command(command)
            
            # Parse Hardware-Callback Format
            for line in response.split('\n'):
                line = line.strip()
                if 'LAN865X Read:' in line and 'Value=' in line:
                    value_match = re.search(r'Value=0x([0-9A-Fa-f]+)', line)
                    if value_match:
                        return int(value_match.group(1), 16)
            
            return None
            
        except Exception as e:
            print(f"❌ Read error: {e}")
            return None
    
    def lan_write(self, address: int, value: int) -> bool:
        """Register schreiben mit Erfolgs-Verifikation"""
        try:
            command = f"lan_write 0x{address:08X} 0x{value:04X}"
            response = self.send_command(command)
            
            # Check für erfolgreichen Write
            if "- OK" in response or "successful" in response.lower():
                return True
            
            return False
            
        except Exception as e:
            print(f"❌ Write error: {e}")
            return False
    
    def indirect_read_basic(self, addr: int, mask: int = 0xFFFF) -> Optional[int]:
        """
        Basic MMD Indirect Access (AN1760 Pattern)
        
        Args:
            addr: Address to read (8-bit for AN1760, 16-bit for MMD)
            mask: Bit mask to apply to result
        
        Returns:
            Masked value or None if failed
        """
        try:
            print(f"    📖 Indirect Read: addr=0x{addr:04X}, mask=0x{mask:04X}")
            
            # Step 1: Write address to MMD Address/Data register
            if not self.lan_write(self.MMD_ADDRESS_DATA, addr):
                print("    ❌ Failed to write address")
                return None
            
            time.sleep(0.05)  # Small delay between operations
            
            # Step 2: Trigger read operation
            if not self.lan_write(self.MMD_CONTROL, self.MMD_READ_OP):
                print("    ❌ Failed to trigger read")
                return None
            
            time.sleep(0.1)  # Wait for read operation to complete
            
            # Step 3: Read result from Address/Data register
            result = self.lan_read(self.MMD_ADDRESS_DATA)
            if result is not None:
                masked_result = result & mask
                print(f"    ✅ Raw=0x{result:04X}, Masked=0x{masked_result:04X}")
                return masked_result
            else:
                print("    ❌ Failed to read result")
                return None
            
        except Exception as e:
            print(f"    ❌ Indirect read error: {e}")
            return None
    
    def indirect_read_mmd(self, mmd_device: int, mmd_register: int, mask: int = 0xFFFF) -> Optional[int]:
        """
        IEEE 802.3 Clause 45 MMD Access
        
        Args:
            mmd_device: MMD device number (1=PMD/PMA, 3=PCS, 7=AN, etc.)
            mmd_register: Register within MMD device
            mask: Bit mask to apply to result
        
        Returns:
            Masked value or None if failed
        """
        
        # Test verschiedene Encoding-Varianten für MMD
        encodings = {
            'dev<<11_reg': (mmd_device << 11) | mmd_register,
            'dev<<10_reg': (mmd_device << 10) | mmd_register,  
            'dev<<8_reg':  (mmd_device << 8)  | mmd_register,
            'dev<<5_reg':  (mmd_device << 5)  | mmd_register,
            'reg_only':    mmd_register,
            'dev_only':    mmd_device,
        }
        
        print(f"    🔍 MMD {mmd_device} Register 0x{mmd_register:04X}")
        
        results = {}
        for encoding_name, encoded_addr in encodings.items():
            print(f"      🧪 {encoding_name}: 0x{encoded_addr:04X} ", end="")
            
            result = self.indirect_read_basic(encoded_addr, mask)
            if result is not None:
                results[encoding_name] = result
                print(f"→ 0x{result:04X}")
            else:
                print("→ FAIL")
        
        return results if results else None
    
    def test_an1760_parameters(self) -> Dict:
        """Test AN1760 Device-Specific Parameter Access"""
        print("\n🧮 AN1760 Configuration Parameter Test")
        print("="*60)
        
        results = {}
        
        # AN1760 Parameter 1 (cfgparam1 calculation)
        print("📋 Parameter 1 (cfgparam1 base):")
        value1 = self.indirect_read_basic(0x04, 0x1F)  # addr=0x04, mask=0x1F
        if value1 is not None:
            cfgparam1 = 0x1000 + (value1 * 0x10)  # AN1760 algorithm
            results['param1'] = {
                'raw_value': value1,
                'calculated': cfgparam1,
                'success': True
            }
            print(f"  ✅ Raw Value: 0x{value1:02X}")
            print(f"  🧮 Calculated cfgparam1: 0x{cfgparam1:04X}")
        else:
            results['param1'] = {'success': False}
            print("  ❌ Failed to read parameter 1")
        
        # AN1760 Parameter 2 (cfgparam2 calculation)  
        print("\n📋 Parameter 2 (cfgparam2 base):")
        value2 = self.indirect_read_basic(0x08, 0x1F)  # addr=0x08, mask=0x1F
        if value2 is not None:
            cfgparam2 = 0x2000 + (value2 * 0x08)  # AN1760 algorithm
            results['param2'] = {
                'raw_value': value2,
                'calculated': cfgparam2,
                'success': True
            }
            print(f"  ✅ Raw Value: 0x{value2:02X}")
            print(f"  🧮 Calculated cfgparam2: 0x{cfgparam2:04X}")
        else:
            results['param2'] = {'success': False}
            print("  ❌ Failed to read parameter 2")
        
        return results
    
    def test_ieee_mmd_registers(self) -> Dict:
        """Test IEEE 802.3 Clause 45 MMD Registers"""
        print("\n🔌 IEEE 802.3 Clause 45 MMD Register Test")
        print("="*60)
        
        results = {}
        
        # MMD 1: PMD/PMA Registers
        print("📡 MMD 1 (PMD/PMA):")
        mmd1_results = {}
        
        print("  📋 PMD Control Register (0x0000):")
        mmd1_ctrl = self.indirect_read_mmd(1, 0x0000, 0xFFFF)
        if mmd1_ctrl:
            mmd1_results['control'] = mmd1_ctrl
            # Analyze PMD Control bits
            for encoding, value in mmd1_ctrl.items():
                if value & 0x8000:
                    print(f"    ⚠️  {encoding}: Soft Reset bit set (0x{value:04X})")
                else:
                    print(f"    ✅ {encoding}: Normal operation (0x{value:04X})")
        
        print("  📋 PMD Status Register (0x0001):")
        mmd1_status = self.indirect_read_mmd(1, 0x0001, 0xFFFF)
        if mmd1_status:
            mmd1_results['status'] = mmd1_status
            # Analyze PMD Status bits
            for encoding, value in mmd1_status.items():
                rx_link = "LINK UP" if (value & 0x0002) else "LINK DOWN"
                print(f"    📊 {encoding}: {rx_link} (0x{value:04X})")
        
        results['mmd1_pmd_pma'] = mmd1_results
        
        # MMD 3: PCS Registers
        print("\n📡 MMD 3 (PCS):")
        mmd3_results = {}
        
        print("  📋 PCS Control Register (0x0000):") 
        mmd3_ctrl = self.indirect_read_mmd(3, 0x0000, 0xFFFF)
        if mmd3_ctrl:
            mmd3_results['control'] = mmd3_ctrl
        
        print("  📋 PCS Status Register (0x0001):")
        mmd3_status = self.indirect_read_mmd(3, 0x0001, 0xFFFF)
        if mmd3_status:
            mmd3_results['status'] = mmd3_status
        
        results['mmd3_pcs'] = mmd3_results
        
        # MMD 7: Auto Negotiation
        print("\n📡 MMD 7 (Auto Negotiation):")
        mmd7_results = {}
        
        print("  📋 AN Control Register (0x0000):")
        mmd7_ctrl = self.indirect_read_mmd(7, 0x0000, 0xFFFF)
        if mmd7_ctrl:
            mmd7_results['control'] = mmd7_ctrl
            # Analyze AN Control bits
            for encoding, value in mmd7_ctrl.items():
                an_enable = "ENABLED" if (value & 0x1000) else "DISABLED"
                print(f"    ⚙️  {encoding}: AN {an_enable} (0x{value:04X})")
        
        print("  📋 AN Status Register (0x0001):")
        mmd7_status = self.indirect_read_mmd(7, 0x0001, 0xFFFF)
        if mmd7_status:
            mmd7_results['status'] = mmd7_status
            # Analyze AN Status bits
            for encoding, value in mmd7_status.items():
                an_able = "ABLE" if (value & 0x0001) else "NOT ABLE"
                print(f"    📊 {encoding}: Link Partner AN {an_able} (0x{value:04X})")
        
        results['mmd7_autoneg'] = mmd7_results
        
        return results
    
    def test_direct_comparison(self) -> Dict:
        """Vergleiche direkte vs. indirekte Zugriffe"""
        print("\n⚖️  Direkt vs. Indirekt Vergleichstest")
        print("="*60)
        
        results = {}
        
        # Test bekannte Register sowohl direkt als auch indirekt
        test_registers = {
            'OA_ID': 0x00000000,
            'CHIP_ID': 0x00000004,
            'OA_STATUS0': 0x00000008,
        }
        
        for reg_name, reg_addr in test_registers.items():
            print(f"\n📋 {reg_name} (0x{reg_addr:08X}):")
            
            # Direkter Zugriff
            direct_value = self.lan_read(reg_addr)
            print(f"  📖 Direkt: 0x{direct_value:08X}" if direct_value else "  ❌ Direkt: FAIL")
            
            # Indirekter Zugriff (verschiedene Methoden ausprobieren)
            indirect_values = {}
            
            # Method 1: Register-Adresse als 32-bit verwenden 
            addr_32bit = reg_addr & 0xFFFF
            indirect_32 = self.indirect_read_basic(addr_32bit)
            if indirect_32:
                indirect_values['addr_32bit'] = indirect_32
                print(f"  🔄 Indirekt (32-bit addr): 0x{indirect_32:04X}")
            
            # Method 2: Nur Offset verwenden
            offset = reg_addr & 0xFF
            indirect_offset = self.indirect_read_basic(offset) 
            if indirect_offset:
                indirect_values['offset_only'] = indirect_offset
                print(f"  🔄 Indirekt (offset): 0x{indirect_offset:04X}")
            
            results[reg_name] = {
                'direct': direct_value,
                'indirect': indirect_values,
                'address': reg_addr
            }
        
        return results
    
    def comprehensive_test(self) -> Dict:
        """Vollständiger MMD Indirect Access Test"""
        print("="*80)
        print("LAN8651 MMD Indirect Access Test - Vollständige Analyse")
        print("="*80)
        print(f"🕒 Test gestartet: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        print(f"🔌 Device: {self.port} @ {self.baudrate} baud")
        print(f"📡 MMD Registers: 0x{self.MMD_ADDRESS_DATA:08X}, 0x{self.MMD_CONTROL:08X}")
        
        all_results = {
            'timestamp': datetime.now().isoformat(),
            'device_info': {
                'port': self.port,
                'baudrate': self.baudrate,
                'mmd_address_data': f"0x{self.MMD_ADDRESS_DATA:08X}",
                'mmd_control': f"0x{self.MMD_CONTROL:08X}"
            },
            'tests': {}
        }
        
        # Test 1: AN1760 Parameter
        all_results['tests']['an1760_parameters'] = self.test_an1760_parameters()
        
        # Test 2: IEEE MMD Register
        all_results['tests']['ieee_mmd_registers'] = self.test_ieee_mmd_registers()
        
        # Test 3: Direkt vs. Indirekt Vergleich
        all_results['tests']['direct_vs_indirect'] = self.test_direct_comparison()
        
        return all_results
    
    def export_results(self, results: Dict, filename: Optional[str] = None) -> str:
        """Test-Ergebnisse exportieren"""
        if filename is None:
            timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
            filename = f"lan8651_mmd_indirect_test_{timestamp}.json"
        
        with open(filename, 'w') as f:
            json.dump(results, f, indent=2)
        
        return filename
    
    def print_summary(self, results: Dict):
        """Test-Zusammenfassung ausgeben"""
        print("\n" + "="*80)
        print("📊 MMD INDIRECT ACCESS TEST - ZUSAMMENFASSUNG")
        print("="*80)
        
        # AN1760 Parameter Summary
        an1760 = results['tests'].get('an1760_parameters', {})
        print("🧮 AN1760 Configuration Parameter:")
        param1_success = an1760.get('param1', {}).get('success', False)
        param2_success = an1760.get('param2', {}).get('success', False)
        print(f"  📋 Parameter 1: {'✅ SUCCESS' if param1_success else '❌ FAILED'}")
        print(f"  📋 Parameter 2: {'✅ SUCCESS' if param2_success else '❌ FAILED'}")
        
        # IEEE MMD Register Summary
        ieee_mmd = results['tests'].get('ieee_mmd_registers', {})
        print("\n🔌 IEEE 802.3 MMD Register:")
        mmd1_count = len(ieee_mmd.get('mmd1_pmd_pma', {}))
        mmd3_count = len(ieee_mmd.get('mmd3_pcs', {}))
        mmd7_count = len(ieee_mmd.get('mmd7_autoneg', {}))
        print(f"  📡 MMD 1 (PMD/PMA): {mmd1_count} register accessible")
        print(f"  📡 MMD 3 (PCS): {mmd3_count} register accessible")  
        print(f"  📡 MMD 7 (AutoNeg): {mmd7_count} register accessible")
        
        # Direkt vs Indirekt Summary
        direct_vs_indirect = results['tests'].get('direct_vs_indirect', {})
        print("\n⚖️  Direkt vs. Indirekt Vergleich:")
        for reg_name, reg_data in direct_vs_indirect.items():
            direct_ok = reg_data.get('direct') is not None
            indirect_count = len(reg_data.get('indirect', {}))
            print(f"  📋 {reg_name}: Direkt {'✅' if direct_ok else '❌'}, Indirekt {indirect_count} methods")
        
        print(f"\n💾 Vollständige Ergebnisse gespeichert")


def main():
    parser = argparse.ArgumentParser(
        description="LAN8651 MMD Indirect Access Test Tool",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Beispiele:
  # Vollständiger MMD Test
  python mmd_indirect_test.py --device COM8

  # Nur AN1760 Parameter testen
  python mmd_indirect_test.py --device COM8 --test an1760

  # IEEE MMD Register testen  
  python mmd_indirect_test.py --device COM8 --test ieee-mmd

  # Direkt vs. Indirekt Vergleich
  python mmd_indirect_test.py --device COM8 --test comparison
        """
    )
    
    parser.add_argument('--device', default='COM8', help='Serial port (default: COM8)')
    parser.add_argument('--baudrate', type=int, default=115200, help='Baud rate')
    parser.add_argument('--timeout', type=float, default=2.0, help='Command timeout')
    parser.add_argument('--export', metavar='FILE', help='Export results to file')
    parser.add_argument('--test', choices=['an1760', 'ieee-mmd', 'comparison', 'full'],
                       default='full', help='Test type to run')
    
    args = parser.parse_args()
    
    # Test Tool erstellen
    tester = MMDIndirectAccessTester(args.device, args.baudrate)
    tester.test_timeout = args.timeout
    
    try:
        # Verbinden
        if not tester.connect():
            return 1
        
        # Test ausführen basierend auf --test Parameter
        if args.test == 'full':
            results = tester.comprehensive_test()
        else:
            # Einzelne Tests
            results = {
                'timestamp': datetime.now().isoformat(),
                'device_info': {
                    'port': args.device,
                    'baudrate': args.baudrate
                },
                'tests': {}
            }
            
            if args.test == 'an1760':
                print("🧮 AN1760 Parameter Test")
                results['tests']['an1760_parameters'] = tester.test_an1760_parameters()
            
            elif args.test == 'ieee-mmd':
                print("🔌 IEEE MMD Register Test")
                results['tests']['ieee_mmd_registers'] = tester.test_ieee_mmd_registers()
                
            elif args.test == 'comparison':
                print("⚖️  Direkt vs. Indirekt Vergleich")
                results['tests']['direct_vs_indirect'] = tester.test_direct_comparison()
        
        if results:
            # Zusammenfassung anzeigen
            tester.print_summary(results)
            
            # Export
            if args.export:
                filename = tester.export_results(results, args.export)
            else:
                filename = tester.export_results(results)
            
            print(f"📁 Datei: {filename}")
        
        return 0
        
    finally:
        tester.disconnect()

if __name__ == '__main__':
    sys.exit(main())