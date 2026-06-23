#!/usr/bin/env python3
"""
LAN8651 SPI Register Discovery Tool

Systematische Analyse aller Register-Bereiche um herauszufinden,
welche Register tatsächlich über SPI direkt erreichbar sind.

Dieses Tool testet:
- Alle MMS Bereiche (Memory Map Selector) 0-15
- Verschiedene Offset-Bereiche in jedem MMS
- Read/Write-Fähigkeiten für jeden Register
- Konsistenz der Antworten

Author: T1S Development Team  
Date: März 2026
Hardware: LAN8650/8651 über SPI-Interface
"""

import argparse
import json
import re
import serial
import sys
import time
from datetime import datetime
from typing import Dict, List, Optional, Tuple, Set

class RegisterMap:
    """Register-Map Definitionen für LAN8651"""
    
    # Bekannte MMS Bereiche (Memory Map Selector)
    MMS_RANGES = {
        0: "Open Alliance Standard + PHY Clause 22",
        1: "MAC Registers (32-bit)",
        2: "PHY PCS Registers (16-bit)", 
        3: "PHY PMA/PMD Registers (16-bit)",
        4: "PHY Vendor Specific (16-bit)",
        10: "Miscellaneous (16-bit)"
    }
    
    # Bekannte funktionierende Register aus unseren Tools
    KNOWN_WORKING = {
        0x00000000: "OA_ID",
        0x00000004: "CHIP_ID", 
        0x00000008: "OA_STATUS0",
        0x00000009: "OA_STATUS1",
        0x0000FF01: "PHY_BASIC_STATUS",
        0x0000FF03: "PHY_ID2",
        0x0004CA01: "PLCA_CTRL0",
        0x0004CA02: "PLCA_CTRL1", 
        0x0004CA03: "PLCA_STS",
        0x00040081: "PMD_TEMPERATURE",
        0x00040082: "PMD_VOLTAGE", 
        0x00040083: "PMD_SQI",
        0x00040084: "PMD_LINK_QUALITY",
        0x00040087: "CDCTL0",
        0x000400B4: "AN1760_PARAM1",
        0x000400F4: "AN1760_PARAM2",
    }

class SPIRegisterDiscovery:
    """SPI Register Discovery Tool"""
    
    def __init__(self, port: str = 'COM8', baudrate: int = 115200):
        self.port = port
        self.baudrate = baudrate
        self.serial_conn = None
        
        # Ergebnisse
        self.accessible_registers = {}
        self.error_registers = {}
        self.readonly_registers = set()
        self.writable_registers = set()
        self.consistent_registers = set()
        
        # Test-Parameter
        self.test_timeout = 1.0
        self.retry_count = 2
        
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
            time.sleep(0.2)
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
    
    def send_command(self, command: str) -> str:
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
                else:
                    time.sleep(0.01)
            
            # Zusätzlich warten auf Register-Callbacks
            if command.startswith('lan_'):
                additional_wait = 0.4
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
    
    def read_register(self, address: int) -> Optional[int]:
        """Register lesen mit Fehlerbehandlung"""
        for attempt in range(self.retry_count):
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
                
                # Retry bei Fehlschlag
                if attempt < self.retry_count - 1:
                    time.sleep(0.1)
                    
            except Exception as e:
                if attempt < self.retry_count - 1:
                    time.sleep(0.1)
                    continue
                break
        
        return None
    
    def write_register(self, address: int, value: int) -> bool:
        """Register schreiben mit Verification"""
        try:
            command = f"lan_write 0x{address:08X} 0x{value:04X}"
            response = self.send_command(command)
            
            # Check für erfolgreichen Write
            if "- OK" in response or "successful" in response.lower():
                return True
            
            return False
            
        except Exception as e:
            return False
    
    def test_register_read_capability(self, address: int) -> Dict:
        """Test Read-Fähigkeit eines Registers"""
        result = {
            'address': f"0x{address:08X}",
            'readable': False,
            'value': None,
            'consistent': False,
            'error_state': False
        }
        
        print(f"  📖 0x{address:08X} ", end="", flush=True)
        
        # Mehrere Reads für Konsistenz-Test
        values = []
        for i in range(3):
            value = self.read_register(address)
            if value is not None:
                values.append(value)
            time.sleep(0.05)
        
        if len(values) > 0:
            result['readable'] = True
            result['value'] = values[0]
            
            # Konsistenz prüfen
            if len(set(values)) == 1:
                result['consistent'] = True
                print(f"✅ 0x{values[0]:08X} (konsistent)")
            else:
                print(f"⚠️ 0x{values[0]:08X} (inkonsistent: {[hex(v) for v in values]})")
            
            # Error-State prüfen
            if values[0] == 0x00008000:
                result['error_state'] = True
                print(f"    ⚠️ Error State erkannt")
                
        else:
            print("❌ Nicht lesbar")
        
        return result
    
    def test_register_write_capability(self, address: int, original_value: int) -> Dict:
        """Test Write-Fähigkeit eines Registers"""
        result = {
            'writable': False,
            'verified': False,
            'original_value': original_value,
            'test_values': []
        }
        
        print(f"    ✏️  Write-Test ", end="", flush=True)
        
        # Test-Werte für Write-Test
        test_values = [0x5555, 0xAAAA, 0x0000, 0xFFFF]
        
        for test_val in test_values:
            # Schreiben
            if not self.write_register(address, test_val):
                continue
            
            time.sleep(0.1)
            
            # Zurücklesen
            read_val = self.read_register(address)
            if read_val == test_val:
                result['writable'] = True
                result['verified'] = True
                result['test_values'].append(test_val)
                break
            elif read_val is not None:
                # Register nimmt Writes entgegen, aber Wert ändert sich nicht
                result['test_values'].append((test_val, read_val))
        
        # Original-Wert wiederherstellen
        if original_value is not None:
            self.write_register(address, original_value)
        
        if result['writable']:
            print("✅ Beschreibbar")
        elif len(result['test_values']) > 0:
            print("⚠️ Hardware-managed")
        else:
            print("❌ Read-only")
        
        return result
    
    def scan_mms_range(self, mms: int, start_offset: int = 0, end_offset: int = 0xFF, step: int = 1) -> Dict:
        """Scan eines MMS-Bereichs"""
        print(f"\n🔍 Scanning MMS {mms} ({RegisterMap.MMS_RANGES.get(mms, 'Unknown')})")
        print(f"   📍 Offset-Bereich: 0x{start_offset:04X} - 0x{end_offset:04X} (Schritt: {step})")
        
        results = {
            'mms': mms, 
            'description': RegisterMap.MMS_RANGES.get(mms, 'Unknown'),
            'accessible_registers': {},
            'total_tested': 0,
            'readable_count': 0,
            'writable_count': 0
        }
        
        for offset in range(start_offset, end_offset + 1, step):
            address = (mms << 16) | offset
            results['total_tested'] += 1
            
            # Read-Test
            read_result = self.test_register_read_capability(address)
            
            if read_result['readable']:
                results['readable_count'] += 1
                results['accessible_registers'][address] = read_result
                
                # Write-Test nur bei lesbaren Registern
                if not read_result['error_state']:
                    write_result = self.test_register_write_capability(address, read_result['value'])
                    read_result['write_capability'] = write_result
                    
                    if write_result['writable']:
                        results['writable_count'] += 1
        
        print(f"   📊 Ergebnis: {results['readable_count']}/{results['total_tested']} lesbar, {results['writable_count']} beschreibbar")
        
        return results
    
    def test_known_registers(self) -> Dict:
        """Test alle bekannten Register aus unseren Tools"""
        print(f"\n🎯 Test bekannter Register aus funktionierenden Tools")
        print(f"   📋 {len(RegisterMap.KNOWN_WORKING)} bekannte Adressen")
        
        results = {}
        working_count = 0
        
        for address, name in RegisterMap.KNOWN_WORKING.items():
            print(f"\n  🔍 {name} (0x{address:08X})")
            
            read_result = self.test_register_read_capability(address)
            if read_result['readable'] and not read_result['error_state']:
                working_count += 1
                
                # Write-Test
                write_result = self.test_register_write_capability(address, read_result['value'])
                read_result['write_capability'] = write_result
            
            results[address] = {
                'name': name,
                'result': read_result
            }
        
        print(f"\n   📊 Bekannte Register: {working_count}/{len(RegisterMap.KNOWN_WORKING)} funktionieren")
        return results
    
    def comprehensive_scan(self) -> Dict:
        """Vollständiger Register-Scan"""
        print("="*80)
        print("LAN8651 SPI Register Discovery - Vollständige Analyse")
        print("="*80)
        
        all_results = {
            'timestamp': datetime.now().isoformat(),
            'device_info': {},
            'known_registers': {},
            'mms_scans': {},
            'summary': {}
        }
        
        # 1. Bekannte Register testen
        all_results['known_registers'] = self.test_known_registers()
        
        # 2. Systematischer MMS-Scan
        print(f"\n🗺️  Systematischer MMS-Scan")
        
        # MMS 0: Open Alliance + PHY (wichtigste Register)
        all_results['mms_scans'][0] = self.scan_mms_range(0, 0x0000, 0x0020, 1)
        
        # MMS 0: PHY Clause 22 Bereich (wichtige PHY Register)
        clause22_result = self.scan_mms_range(0, 0xFF00, 0xFF10, 1)
        all_results['mms_scans']['0_clause22'] = clause22_result
        
        # MMS 1: MAC Registers (sample)
        all_results['mms_scans'][1] = self.scan_mms_range(1, 0x0000, 0x0020, 1)
        
        # MMS 4: PHY Vendor Specific (hier sind die meisten AN1760 Register)  
        all_results['mms_scans'][4] = self.scan_mms_range(4, 0x0070, 0x00C0, 1)  # Wichtiger Bereich
        all_results['mms_scans']['4_plca'] = self.scan_mms_range(4, 0xCA00, 0xCA10, 1)  # PLCA Bereich
        
        # 3. Zusammenfassung erstellen
        total_tested = sum(scan['total_tested'] for scan in all_results['mms_scans'].values())
        total_readable = sum(scan['readable_count'] for scan in all_results['mms_scans'].values()) 
        total_writable = sum(scan['writable_count'] for scan in all_results['mms_scans'].values())
        
        all_results['summary'] = {
            'total_registers_tested': total_tested,
            'readable_registers': total_readable,
            'writable_registers': total_writable,
            'known_working': sum(1 for r in all_results['known_registers'].values() 
                               if r['result']['readable'] and not r['result'].get('error_state', False))
        }
        
        return all_results
    
    def export_results(self, results: Dict, filename: Optional[str] = None) -> str:
        """Ergebnisse exportieren"""
        if filename is None:
            timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
            filename = f"lan8651_register_discovery_{timestamp}.json"
        
        with open(filename, 'w') as f:
            json.dump(results, f, indent=2)
        
        return filename
    
    def print_summary(self, results: Dict):
        """Ergebnis-Zusammenfassung ausgeben"""
        print("\n" + "="*80)
        print("📊 REGISTER DISCOVERY - ZUSAMMENFASSUNG")
        print("="*80)
        
        summary = results['summary']
        print(f"📋 Getestete Register: {summary['total_registers_tested']}")
        print(f"📖 Lesbare Register: {summary['readable_registers']}")  
        print(f"✏️  Beschreibbare Register: {summary['writable_registers']}")
        print(f"✅ Bekannte Register (funktionierend): {summary['known_working']}")
        
        print(f"\n🎯 Funktionierende Register im Detail:")
        
        # Bekannte Register
        working_known = 0
        for addr, info in results['known_registers'].items():
            if info['result']['readable'] and not info['result'].get('error_state', False):
                working_known += 1
                value = info['result']['value']
                write_cap = info['result'].get('write_capability', {})
                writable = "✏️" if write_cap.get('writable', False) else "📖"
                print(f"  {writable} {info['name']}: 0x{addr:08X} = 0x{value:08X}")
        
        # Neue gefundene Register
        print(f"\n🔍 Neu entdeckte Register:")
        new_found = 0
        for mms_key, scan in results['mms_scans'].items():
            for addr, reg_info in scan['accessible_registers'].items():
                if addr not in RegisterMap.KNOWN_WORKING and not reg_info.get('error_state', False):
                    new_found += 1
                    value = reg_info['value'] 
                    write_cap = reg_info.get('write_capability', {})
                    writable = "✏️" if write_cap.get('writable', False) else "📖"
                    print(f"  {writable} MMS{mms_key}: 0x{addr:08X} = 0x{value:08X}")
        
        if new_found == 0:
            print("  (Keine neuen Register gefunden)")
        
        print(f"\n💾 Vollständige Ergebnisse gespeichert")


def main():
    parser = argparse.ArgumentParser(
        description="LAN8651 SPI Register Discovery - Systematische Register-Analyse",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Beispiele:
  # Vollständige Register-Analyse
  python spi_register_discovery.py --device COM8 full-scan

  # Nur bekannte Register testen  
  python spi_register_discovery.py --device COM8 known-only

  # Spezifischen MMS-Bereich scannen
  python spi_register_discovery.py --device COM8 mms-scan --mms 4 --start 0x70 --end 0xC0
        """
    )
    
    parser.add_argument('--device', default='COM8', help='Serial port (default: COM8)')
    parser.add_argument('--baudrate', type=int, default=115200, help='Baud rate')
    parser.add_argument('--timeout', type=float, default=1.0, help='Command timeout')
    parser.add_argument('--export', metavar='FILE', help='Export results to file')
    
    subparsers = parser.add_subparsers(dest='command', help='Scan mode')
    
    # Full scan
    subparsers.add_parser('full-scan', help='Complete register discovery scan')
    
    # Known registers only
    subparsers.add_parser('known-only', help='Test only known working registers')
    
    # MMS scan
    mms_parser = subparsers.add_parser('mms-scan', help='Scan specific MMS range')
    mms_parser.add_argument('--mms', type=int, required=True, help='MMS number (0-15)')
    mms_parser.add_argument('--start', type=lambda x: int(x,0), default=0, help='Start offset (hex)')
    mms_parser.add_argument('--end', type=lambda x: int(x,0), default=0xFF, help='End offset (hex)')
    
    args = parser.parse_args()
    
    if not args.command:
        parser.error("Command required")
    
    # Discovery Tool erstellen
    discovery = SPIRegisterDiscovery(args.device, args.baudrate)
    discovery.test_timeout = args.timeout
    
    try:
        # Verbinden
        if not discovery.connect():
            return 1
        
        results = None
        
        if args.command == 'full-scan':
            results = discovery.comprehensive_scan()
            
        elif args.command == 'known-only':
            print("🎯 Test bekannter Register")
            results = {
                'timestamp': datetime.now().isoformat(),
                'known_registers': discovery.test_known_registers(),
                'mms_scans': {},
                'summary': {}
            }
            
        elif args.command == 'mms-scan':
            print(f"🔍 MMS {args.mms} Scan")
            scan_result = discovery.scan_mms_range(args.mms, args.start, args.end)
            results = {
                'timestamp': datetime.now().isoformat(),
                'known_registers': {},
                'mms_scans': {args.mms: scan_result},
                'summary': {
                    'total_registers_tested': scan_result['total_tested'],
                    'readable_registers': scan_result['readable_count'],
                    'writable_registers': scan_result['writable_count'],
                    'known_working': 0
                }
            }
        
        if results:
            # Zusammenfassung anzeigen
            discovery.print_summary(results)
            
            # Export
            if args.export:
                filename = discovery.export_results(results, args.export)
            else:
                filename = discovery.export_results(results)
            
            print(f"📁 Datei: {filename}")
        
        return 0
        
    finally:
        discovery.disconnect()

if __name__ == '__main__':
    sys.exit(main())