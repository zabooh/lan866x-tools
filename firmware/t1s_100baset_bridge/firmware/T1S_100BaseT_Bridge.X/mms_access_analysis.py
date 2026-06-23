#!/usr/bin/env python3
"""
LAN8651 MMS Access Pattern Analysis

Testet systematisch verschiedene MMS-Bereiche um festzustellen:
- MMS0-MMS4: Direkt via SPI zugreifbar?
- MMS5-MMS10: Nur indirekt zugreifbar?

Testet repräsentative Register aus jedem MMS-Bereich
"""

import argparse
import json
import re
import serial
import sys
import time
from datetime import datetime

class MMSAccessAnalyzer:
    """Analysiert Direct vs Indirect Access Pattern für verschiedene MMS-Bereiche"""
    
    def __init__(self, port: str = 'COM8', baudrate: int = 115200):
        self.port = port
        self.baudrate = baudrate
        self.serial_conn = None
        
        # MMD Indirect Access Register (in MMS4)
        self.MMD_ADDR_DATA = 0x000400D8
        self.MMD_CONTROL = 0x000400DA
        
        # Test Register für jeden MMS-Bereich
        self.mms_test_registers = {
            # MMS0: Open Alliance Standard + PHY Clause 22
            0: [
                (0x00000000, 'OA_ID'),
                (0x00000004, 'CHIP_ID'), 
                (0x00000008, 'OA_STATUS0'),
                (0x0000000C, 'OA_STATUS1'),
                (0x00000010, 'OA_BUFSTS'),
            ],
            
            # MMS1: MAC Registers
            1: [
                (0x00010000, 'MAC_NCR'),      # Network Control Register
                (0x00010004, 'MAC_NCFGR'),    # Network Configuration
                (0x00010008, 'MAC_NSR'),      # Network Status
                (0x0001000C, 'MAC_TSR'),      # Transmit Status
                (0x00010010, 'MAC_RBQP'),     # Receive Buffer Queue
            ],
            
            # MMS2: PHY PCS Registers  
            2: [
                (0x00020000, 'PCS_CTRL'),     # PCS Control
                (0x00020004, 'PCS_STATUS'),   # PCS Status
                (0x00020008, 'PCS_ID1'),      # PCS ID 1
                (0x0002000C, 'PCS_ID2'),      # PCS ID 2
            ],
            
            # MMS3: PHY PMA/PMD Registers
            3: [
                (0x00030000, 'PMD_CTRL'),     # PMD Control
                (0x00030004, 'PMD_STATUS'),   # PMD Status  
                (0x00030008, 'PMD_ID1'),      # PMD ID 1
                (0x0003000C, 'PMD_ID2'),      # PMD ID 2
            ],
            
            # MMS4: PHY Vendor Specific
            4: [
                (0x00040000, 'VENDOR_CTRL'),
                (0x00040004, 'VENDOR_STATUS'),
                (0x000400D8, 'MMD_ADDR_DATA'), # Known working
                (0x000400DA, 'MMD_CONTROL'),   # Known working
            ],
            
            # MMS10: Miscellaneous
            10: [
                (0x000A0000, 'MISC_CTRL'),
                (0x000A0004, 'MISC_STATUS'),
                (0x000A0008, 'MISC_CONFIG'),
            ]
        }
        
        self.results = {}
        
    def connect(self) -> bool:
        """Verbindung herstellen"""
        try:
            self.serial_conn = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                timeout=2.0
            )
            time.sleep(0.3)
            print(f"✅ Verbunden mit {self.port} @ {self.baudrate}")
            return True
        except Exception as e:
            print(f"❌ Verbindung fehlgeschlagen: {e}")
            return False
            
    def disconnect(self):
        """Verbindung schließen"""
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.close()
            print("🔌 Verbindung geschlossen")
    
    def send_cmd_with_timeout(self, cmd: str, timeout: float = 2.0) -> str:
        """Kommando mit Timeout senden"""
        if not self.serial_conn or not self.serial_conn.is_open:
            return ""
        
        try:
            self.serial_conn.reset_input_buffer()
            self.serial_conn.write((cmd + '\r\n').encode())
            self.serial_conn.flush()
            
            lines = []
            end_time = time.time() + timeout
            
            while time.time() < end_time:
                if self.serial_conn.in_waiting > 0:
                    line = self.serial_conn.readline().decode('ascii', errors='ignore').strip()
                    if line:
                        lines.append(line)
                        # Callback received
                        if 'Value=' in line or '- OK' in line:
                            break
                else:
                    time.sleep(0.01)
            
            return '\n'.join(lines)
            
        except Exception as e:
            return f"ERROR: {e}"
    
    def direct_read(self, address: int) -> tuple:
        """
        Direct SPI read test
        Returns: (value, success, consistent)
        """
        values = []
        
        # Read 3 times for consistency check
        for i in range(3):
            response = self.send_cmd_with_timeout(f"lan_read 0x{address:08X}")
            
            value = None
            for line in response.split('\n'):
                if 'Value=' in line:
                    match = re.search(r'Value=0x([0-9A-Fa-f]+)', line)
                    if match:
                        value = int(match.group(1), 16)
                        break
            
            values.append(value)
            time.sleep(0.05)  # Small delay between reads
        
        # Analyze results
        valid_values = [v for v in values if v is not None]
        
        if not valid_values:
            return (None, False, False)
        
        success = len(valid_values) > 0
        consistent = len(set(valid_values)) == 1  # All values are the same
        representative_value = valid_values[0]
        
        return (representative_value, success, consistent)
    
    def indirect_read_basic(self, addr: int) -> int:
        """Basic MMD indirect read"""
        try:
            # Write address
            response1 = self.send_cmd_with_timeout(f"lan_write 0x{self.MMD_ADDR_DATA:08X} 0x{addr:04X}")
            if "- OK" not in response1:
                return None
            
            time.sleep(0.05)
            
            # Trigger read
            response2 = self.send_cmd_with_timeout(f"lan_write 0x{self.MMD_CONTROL:08X} 0x0002")
            if "- OK" not in response2:
                return None
            
            time.sleep(0.1)
            
            # Read result
            response3 = self.send_cmd_with_timeout(f"lan_read 0x{self.MMD_ADDR_DATA:08X}")
            
            for line in response3.split('\n'):
                if 'Value=' in line:
                    match = re.search(r'Value=0x([0-9A-Fa-f]+)', line)
                    if match:
                        return int(match.group(1), 16)
            
            return None
            
        except Exception as e:
            return None
    
    def test_mms_range(self, mms: int) -> dict:
        """Test einen MMS-Bereich"""
        print(f"\n📡 MMS{mms} Test")
        print("="*60)
        
        mms_results = {
            'mms': mms,
            'registers': {},
            'direct_success_rate': 0,
            'direct_consistency_rate': 0,
            'total_registers': 0
        }
        
        if mms not in self.mms_test_registers:
            print(f"❌ Keine Test-Register für MMS{mms} definiert")
            return mms_results
        
        test_registers = self.mms_test_registers[mms]
        mms_results['total_registers'] = len(test_registers)
        
        direct_successes = 0
        direct_consistent = 0
        
        for addr, name in test_registers:
            print(f"\n📋 {name} (0x{addr:08X}):")
            
            # Direct Access Test
            direct_value, direct_ok, direct_cons = self.direct_read(addr)
            
            if direct_ok:
                direct_successes += 1
                status = "✅ SUCCESS"
                if direct_value == 0x00008000:
                    status += " (Error State)"
                elif direct_value == 0x00000000:
                    status += " (Zero/Uninitialized)"
                else:
                    status += f" (0x{direct_value:08X})"
            else:
                status = "❌ FAILED"
            
            if direct_cons:
                direct_consistent += 1
                status += " 🔒 Consistent"
            elif direct_ok:
                status += " ⚠️  Inconsistent"
                
            print(f"  📖 Direct: {status}")
            
            # Indirect Access Test (nur für niedrige Adressen)
            indirect_value = None
            if mms <= 4:  # Test indirect nur für MMS 0-4
                # Test verschiedene Encodings für indirect access
                test_addrs = [
                    addr & 0xFFFF,  # 16-bit
                    addr & 0xFF,    # 8-bit offset
                    (addr >> 16) & 0xFFFF,  # MMS only
                ]
                
                for test_addr in test_addrs:
                    indirect_value = self.indirect_read_basic(test_addr)
                    if indirect_value is not None:
                        print(f"  🔄 Indirect(0x{test_addr:04X}): 0x{indirect_value:04X}")
                        break
            
            # Store results
            mms_results['registers'][name] = {
                'address': f"0x{addr:08X}",
                'direct_value': f"0x{direct_value:08X}" if direct_value is not None else None,
                'direct_success': direct_ok,
                'direct_consistent': direct_cons,
                'indirect_value': f"0x{indirect_value:04X}" if indirect_value is not None else None,
                'error_state': direct_value == 0x00008000 if direct_value is not None else None
            }
        
        # Calculate success rates
        mms_results['direct_success_rate'] = (direct_successes / len(test_registers)) * 100
        mms_results['direct_consistency_rate'] = (direct_consistent / len(test_registers)) * 100
        
        print(f"\n📊 MMS{mms} Zusammenfassung:")
        print(f"  ✅ Direct Success: {direct_successes}/{len(test_registers)} ({mms_results['direct_success_rate']:.1f}%)")
        print(f"  🔒 Direct Consistent: {direct_consistent}/{len(test_registers)} ({mms_results['direct_consistency_rate']:.1f}%)")
        
        return mms_results
    
    def analyze_all_mms(self) -> dict:
        """Analysiere alle MMS-Bereiche"""
        print("="*80)
        print("🔍 LAN8651 MMS Access Pattern Analysis")
        print("="*80)
        print(f"🕒 {datetime.now().strftime('%H:%M:%S')}")
        print(f"📡 Testing Hypothesis: MMS0-4=Direct, MMS5-10=Indirect")
        
        analysis_results = {
            'timestamp': datetime.now().isoformat(),
            'hypothesis': 'MMS0-4=Direct, MMS5-10=Indirect',
            'device_info': {
                'port': self.port,
                'baudrate': self.baudrate
            },
            'mms_results': {},
            'summary': {}
        }
        
        # Test alle definierten MMS-Bereiche
        for mms in sorted(self.mms_test_registers.keys()):
            mms_result = self.test_mms_range(mms)
            analysis_results['mms_results'][f'MMS{mms}'] = mms_result
        
        return analysis_results
    
    def print_final_analysis(self, results: dict):
        """Finale Analyse ausgeben"""
        print("\n" + "="*80)
        print("📊 FINALE MMS ACCESS PATTERN ANALYSE")
        print("="*80)
        
        print("\n🔍 Hypothese: MMS0-4 = Direct, MMS5-10 = Indirect")
        print("-"*60)
        
        mms_low = []  # MMS 0-4
        mms_high = []  # MMS 5-10
        
        for mms_key, mms_data in results['mms_results'].items():
            mms_num = mms_data['mms']
            success_rate = mms_data['direct_success_rate']
            consistency_rate = mms_data['direct_consistency_rate']
            
            if mms_num <= 4:
                mms_low.append((mms_num, success_rate, consistency_rate))
            else:
                mms_high.append((mms_num, success_rate, consistency_rate))
        
        print("\n📈 MMS 0-4 (sollten direkt zugreifbar sein):")
        for mms, success, consistency in sorted(mms_low):
            status = "✅ HIGH" if success >= 60 else "⚠️  MEDIUM" if success >= 20 else "❌ LOW"
            print(f"  MMS{mms}: {success:5.1f}% Success, {consistency:5.1f}% Consistent - {status}")
        
        if mms_high:
            print("\n📉 MMS 5-10 (sollten nur indirekt zugreifbar sein):")
            for mms, success, consistency in sorted(mms_high):
                status = "✅ HIGH" if success >= 60 else "⚠️  MEDIUM" if success >= 20 else "❌ LOW"
                print(f"  MMS{mms}: {success:5.1f}% Success, {consistency:5.1f}% Consistent - {status}")
        
        # Hypothese bewerten
        avg_low = sum(s for _, s, _ in mms_low) / len(mms_low) if mms_low else 0
        avg_high = sum(s for _, s, _ in mms_high) / len(mms_high) if mms_high else 0
        
        print(f"\n🎯 Hypothese Bewertung:")
        print(f"  📊 MMS 0-4 Average Success: {avg_low:.1f}%")
        print(f"  📊 MMS 5-10 Average Success: {avg_high:.1f}%")
        
        if avg_low > avg_high + 20:
            print("  ✅ Hypothese BESTÄTIGT - MMS0-4 deutlich besser zugreifbar")
        elif abs(avg_low - avg_high) < 20:
            print("  ⚠️  Hypothese UNKLAR - Ähnliche Erfolgsraten")
        else:
            print("  ❌ Hypothese WIDERLEGT - Kein klares Pattern")


def main():
    parser = argparse.ArgumentParser(description="LAN8651 MMS Access Pattern Analyzer")
    parser.add_argument('--device', default='COM8', help='Serial port')
    parser.add_argument('--export', help='JSON export file')
    
    args = parser.parse_args()
    
    analyzer = None
    try:
        analyzer = MMSAccessAnalyzer(args.device)
        
        if not analyzer.connect():
            return 1
        
        # Führe Analyse durch
        results = analyzer.analyze_all_mms()
        
        # Finale Analyse
        analyzer.print_final_analysis(results)
        
        # Export
        if args.export:
            with open(args.export, 'w') as f:
                json.dump(results, f, indent=2)
            print(f"\n💾 Ergebnisse exportiert: {args.export}")
        else:
            timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
            filename = f"mms_access_analysis_{timestamp}.json"
            with open(filename, 'w') as f:
                json.dump(results, f, indent=2)
            print(f"\n💾 Ergebnisse exportiert: {filename}")
        
        return 0
        
    except Exception as e:
        print(f"❌ Fehler: {e}")
        return 1
    finally:
        if analyzer:
            analyzer.disconnect()

if __name__ == '__main__':
    sys.exit(main())