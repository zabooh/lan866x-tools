#!/usr/bin/env python3
"""
LAN8651 Complete MMS 1 Scanner - MAC Address Mystery Solver
===========================================================
Scannt ALLE Register in MMS 1 systematisch nach MAC-Adress-Bytes

Ziel: Finden der fehlenden "00:04" Bytes aus der MAC-Adresse 00:04:25:01:02:03

Bisherige Erkenntnisse:
- MAC_SAT1 = 0x00000000 (leer)
- MAC_SAB1 = 0x03020125 (enthält 03:02:01:25)
- MAC_SAT2 = 0x00000302 (enthält 03:02)
- MAC_SAB2 = 0x01250400 (enthält 01:25:04:00)

Fehlend: 00:04 - Wo sind diese Bytes?
"""

import serial
import time
import struct
from typing import Dict, Tuple, Optional, List
from dataclasses import dataclass


class MMS1CompleteScan:
    def __init__(self, port: str = "COM8", baudrate: int = 115200):
        self.port = port
        self.baudrate = baudrate
        self.ser = None
        self.target_mac = "00:04:25:01:02:03"
        self.target_bytes = [0x00, 0x04, 0x25, 0x01, 0x02, 0x03]
        
    def connect(self) -> bool:
        """Verbindung herstellen."""
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=2)
            time.sleep(0.5)
            return self.test_communication()
        except Exception as e:
            print(f"❌ Verbindung fehlgeschlagen: {e}")
            return False

    def close(self):
        if self.ser:
            self.ser.close()

    def test_communication(self) -> bool:
        """Test ob lan_read funktioniert."""
        try:
            self.ser.write(b'lan_read 0x00000000\r\n')
            time.sleep(0.5)
            response = self.ser.read_all().decode('utf-8', errors='ignore')
            return 'LAN865X Read:' in response or 'Value=' in response
        except:
            return False

    def read_register(self, address: int) -> Tuple[Optional[int], bool]:
        """Register lesen."""
        try:
            cmd = f"lan_read 0x{address:08X}\r\n"
            self.ser.write(cmd.encode())
            time.sleep(0.5)
            
            response = self.ser.read_all().decode('utf-8', errors='ignore')
            
            import re
            value_match = re.search(r'Value=0x([0-9a-fA-F]+)', response)
            if value_match:
                value = int(value_match.group(1), 16)
                return value, True
            
            return None, False
            
        except Exception:
            return None, False

    def scan_mms1_complete(self) -> Dict[int, int]:
        """Kompletter MMS 1 Scan - alle möglichen Register."""
        print("\n" + "="*80)
        print("🔍 KOMPLETTER MMS 1 SCAN - Suche nach MAC-Bytes")
        print("="*80)
        
        results = {}
        tested_count = 0
        success_count = 0
        
        # MMS 1 = 0x0001XXXX - Scannen von 0x00010000 bis 0x000103FF (1024 Register)
        print("⏳ Scanne MMS 1 Register (0x00010000 - 0x000103FF)...")
        
        for offset in range(0x0000, 0x0400, 4):  # 32-bit Register, alle 4 Bytes
            address = 0x00010000 + offset
            value, success = self.read_register(address)
            tested_count += 1
            
            if success:
                results[address] = value
                success_count += 1
                
                # Sofort anzeigen wenn interessanter Wert
                if value != 0 or (address % 0x20 == 0):  # Alle 32 Register oder bei non-zero
                    print(f"  📍 0x{address:08X}: 0x{value:08X}")
            
            # Progress alle 64 Register
            if tested_count % 64 == 0:
                progress = (tested_count / 256) * 100
                print(f"     Progress: {tested_count} Register ({success_count} erfolgreich, {progress:.1f}%)")
        
        print(f"\n✅ MMS 1 Scan komplett: {success_count}/{tested_count} Register gefunden")
        return results

    def analyze_mac_bytes_in_registers(self, registers: Dict[int, int]):
        """Analysiere alle Register nach MAC-Bytes."""
        print("\n" + "="*80)
        print("🧬 MAC-BYTES ANALYSE IN ALLEN REGISTERN")
        print("="*80)
        
        target_patterns = [
            (0x00, 0x04, "00:04"),
            (0x04, 0x25, "04:25"),
            (0x25, 0x01, "25:01"),
            (0x01, 0x02, "01:02"),
            (0x02, 0x03, "02:03"),
        ]
        
        found_patterns = []
        
        print(f"🎯 Suche nach MAC-Byte-Mustern aus: {self.target_mac}")
        
        for addr, value in registers.items():
            if value == 0:
                continue
                
            # Bytes extrahieren (Big Endian und Little Endian)
            bytes_be = [
                (value >> 24) & 0xFF,
                (value >> 16) & 0xFF, 
                (value >> 8) & 0xFF,
                value & 0xFF
            ]
            
            bytes_le = [
                value & 0xFF,
                (value >> 8) & 0xFF,
                (value >> 16) & 0xFF,
                (value >> 24) & 0xFF
            ]
            
            # Nach MAC-Bytes suchen
            for byte1, byte2, pattern_name in target_patterns:
                # Big Endian Check
                for i in range(len(bytes_be)-1):
                    if bytes_be[i] == byte1 and bytes_be[i+1] == byte2:
                        found_patterns.append({
                            'address': addr,
                            'value': value,
                            'pattern': pattern_name,
                            'endian': 'BE',
                            'position': i,
                            'bytes': bytes_be
                        })
                
                # Little Endian Check  
                for i in range(len(bytes_le)-1):
                    if bytes_le[i] == byte1 and bytes_le[i+1] == byte2:
                        found_patterns.append({
                            'address': addr,
                            'value': value,
                            'pattern': pattern_name,
                            'endian': 'LE',
                            'position': i,
                            'bytes': bytes_le
                        })
        
        # Gefundene Pattern anzeigen
        if found_patterns:
            print(f"\n🎉 Gefundene MAC-Byte-Muster:")
            for pattern in found_patterns:
                bytes_str = ":".join(f"{b:02X}" for b in pattern['bytes'])
                print(f"   ✅ {pattern['pattern']:>5} in 0x{pattern['address']:08X} = 0x{pattern['value']:08X}")
                print(f"      {pattern['endian']} Bytes: {bytes_str} (Position {pattern['position']})")
        else:
            print("❌ Keine MAC-Byte-Muster gefunden!")

    def search_missing_bytes(self, registers: Dict[int, int]):
        """Suche speziell nach den fehlenden 00:04 Bytes."""
        print("\n" + "="*80)
        print("🕵️ SUCHE NACH FEHLENDEN 00:04 BYTES")
        print("="*80)
        
        missing_found = []
        
        for addr, value in registers.items():
            if value == 0:
                continue
                
            # Verschiedene Byte-Interpretationen
            interpretations = [
                ("32-bit BE", [(value >> 24) & 0xFF, (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF]),
                ("32-bit LE", [value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF, (value >> 24) & 0xFF]),
                ("16-bit BE High", [(value >> 24) & 0xFF, (value >> 16) & 0xFF]),
                ("16-bit BE Low", [(value >> 8) & 0xFF, value & 0xFF]),
                ("16-bit LE High", [(value >> 16) & 0xFF, (value >> 24) & 0xFF]),
                ("16-bit LE Low", [value & 0xFF, (value >> 8) & 0xFF]),
            ]
            
            for interp_name, bytes_list in interpretations:
                # Suche nach 0x00, 0x04 Pattern
                if len(bytes_list) >= 2:
                    for i in range(len(bytes_list)-1):
                        if bytes_list[i] == 0x00 and bytes_list[i+1] == 0x04:
                            missing_found.append({
                                'address': addr,
                                'value': value,
                                'interpretation': interp_name,
                                'position': i,
                                'bytes': bytes_list
                            })
        
        if missing_found:
            print("🎯 FEHLENDE 00:04 BYTES GEFUNDEN:")
            for found in missing_found:
                bytes_str = ":".join(f"{b:02X}" for b in found['bytes'])
                print(f"   🔍 0x{found['address']:08X} = 0x{found['value']:08X}")
                print(f"      {found['interpretation']}: {bytes_str} (00:04 bei Position {found['position']})")
        else:
            print("❌ 00:04 Bytes nicht direkt gefunden - möglicherweise anderes Format")

    def reconstruct_mac_theories(self, registers: Dict[int, int]):
        """Versuche MAC-Adresse aus verschiedenen Register-Kombinationen zu rekonstruieren."""
        print("\n" + "="*80)
        print("🧩 MAC-REKONSTRUKTIONS-THEORIEN")
        print("="*80)
        
        # Bekannte MAC-relevante Register
        known_mac_regs = {
            0x00010022: registers.get(0x00010022, 0),  # MAC_SAB1
            0x00010023: registers.get(0x00010023, 0),  # MAC_SAT1  
            0x00010024: registers.get(0x00010024, 0),  # MAC_SAB2
            0x00010025: registers.get(0x00010025, 0),  # MAC_SAT2
        }
        
        print("📊 Bekannte MAC-Register:")
        for addr, value in known_mac_regs.items():
            print(f"   0x{addr:08X}: 0x{value:08X}")
        
        # Theorie 1: Split über mehrere Register
        print(f"\n🧮 Theorie 1: MAC split über SAT2+SAB2+SAB1")
        
        sat2 = known_mac_regs[0x00010025]  # 0x00000302
        sab2 = known_mac_regs[0x00010024]  # 0x01250400  
        sab1 = known_mac_regs[0x00010022]  # 0x03020125
        
        # Bytes extrahieren
        sat2_bytes = [sat2 & 0xFF, (sat2 >> 8) & 0xFF]  # LE: [02, 03]
        sab2_bytes = [sab2 & 0xFF, (sab2 >> 8) & 0xFF, (sab2 >> 16) & 0xFF, (sab2 >> 24) & 0xFF]  # LE: [00, 04, 25, 01]
        sab1_bytes = [sab1 & 0xFF, (sab1 >> 8) & 0xFF, (sab1 >> 16) & 0xFF, (sab1 >> 24) & 0xFF]  # LE: [25, 01, 02, 03]
        
        # Verschiedene Kombinationen testen
        combinations = [
            (f"SAT2(LE) + SAB2(LE)[:2]", sat2_bytes[::-1] + sab2_bytes[1:3]),  # [03,02] + [04,25] = 03:02:04:25
            (f"SAB2(LE)[1:3] + SAB1(LE)[1:3]", sab2_bytes[1:3] + sab1_bytes[1:3]),  # [04,25] + [01,02] = 04:25:01:02
            (f"Komplett: SAB2[1:3] + SAB1[1:3] + SAT2[0:2]", sab2_bytes[1:3] + sab1_bytes[1:3] + sat2_bytes[:2]),  # 04:25:01:02:02:03
        ]
        
        target = self.target_mac.replace(":", "").upper()
        
        for theory_name, byte_combo in combinations:
            if len(byte_combo) >= 4:
                mac_attempt = ":".join(f"{b:02X}" for b in byte_combo)
                match = "✅" if mac_attempt.replace(":", "").upper() == target else "❌"
                print(f"   {match} {theory_name}: {mac_attempt}")

    def run_complete_scan(self):
        """Hauptprogramm."""
        print("🚀 LAN8651 Complete MMS 1 Scanner")
        print("=" * 50)
        print(f"🎯 Ziel: MAC-Adresse {self.target_mac} vollständig finden")
        
        if not self.connect():
            return
        
        try:
            # 1. Kompletter MMS 1 Scan
            registers = self.scan_mms1_complete()
            
            # 2. MAC-Bytes Analyse
            self.analyze_mac_bytes_in_registers(registers)
            
            # 3. Suche nach 00:04
            self.search_missing_bytes(registers)
            
            # 4. Rekonstruktions-Theorien
            self.reconstruct_mac_theories(registers)
            
        finally:
            self.close()


if __name__ == "__main__":
    print("LAN8651 Complete MMS 1 Scanner - MAC Address Mystery Solver")
    print("=" * 60)
    print("Scannt alle Register in MMS 1 nach MAC-Adress-Bytes")
    print("Ziel: Vollständige Rekonstruktion von 00:04:25:01:02:03")
    
    scanner = MMS1CompleteScan()
    scanner.run_complete_scan()