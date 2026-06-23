#!/usr/bin/env python3
"""
LAN8651 MAC Address Reader - Corrected Version
===============================================
Liest und zeigt die MAC-Adresse des LAN8651 Controllers an.

Basierend auf der Hardware-Entdeckung:
- MAC_SAB2 (0x00010024) enthält erste 4 Bytes (Little Endian)
- MAC_SAT2 (0x00010025) enthält letzte 2 Bytes (Little Endian)
- Komplette MAC: 00:04:25:01:02:03
"""

import serial
import time
import re
from typing import Optional, Tuple


class LAN8651MacReader:
    def __init__(self, port: str = "COM8", baudrate: int = 115200):
        self.port = port
        self.baudrate = baudrate
        self.ser = None
        
    def connect(self) -> bool:
        """Verbindung zur seriellen Schnittstelle."""
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=2)
            time.sleep(0.5)
            return self.test_communication()
        except Exception as e:
            print(f"❌ Verbindung fehlgeschlagen: {e}")
            return False
    
    def close(self):
        """Verbindung schließen."""
        if self.ser:
            self.ser.close()
    
    def test_communication(self) -> bool:
        """Test ob lan_read funktioniert."""
        try:
            self.ser.write(b'lan_read 0x00000000\r\n')
            time.sleep(0.5)
            response = self.ser.read_all().decode('utf-8', errors='ignore')
            return 'Value=' in response
        except:
            return False
    
    def read_register(self, address: int) -> Optional[int]:
        """Register lesen via lan_read."""
        try:
            cmd = f"lan_read 0x{address:08X}\r\n"
            self.ser.write(cmd.encode())
            time.sleep(0.5)
            
            response = self.ser.read_all().decode('utf-8', errors='ignore')
            
            # Wert extrahieren: "Value=0xXXXXXXXX"
            value_match = re.search(r'Value=0x([0-9a-fA-F]+)', response)
            if value_match:
                return int(value_match.group(1), 16)
            
            return None
            
        except Exception:
            return None
    
    def read_mac_address(self) -> Tuple[Optional[str], bool]:
        """MAC-Adresse aus den korrekten Registern lesen."""
        print("🔍 Lese MAC-Adresse aus LAN8651 Registern...")
        
        # MAC Register lesen (Hardware-verifizierte Adressen)
        sab2_addr = 0x00010024  # MAC_SAB2 - erste 4 Bytes
        sat2_addr = 0x00010025  # MAC_SAT2 - letzte 2 Bytes
        
        print(f"📍 Lese MAC_SAB2 (0x{sab2_addr:08X})...")
        sab2_value = self.read_register(sab2_addr)
        
        print(f"📍 Lese MAC_SAT2 (0x{sat2_addr:08X})...")
        sat2_value = self.read_register(sat2_addr)
        
        if sab2_value is None or sat2_value is None:
            print("❌ Fehler beim Lesen der MAC-Register")
            return None, False
        
        print(f"📊 Raw Register-Werte:")
        print(f"   MAC_SAB2: 0x{sab2_value:08X}")
        print(f"   MAC_SAT2: 0x{sat2_value:08X}")
        
        # Bytes extrahieren (Little Endian)
        sab2_bytes = [
            sab2_value & 0xFF,           # Byte 0
            (sab2_value >> 8) & 0xFF,    # Byte 1  
            (sab2_value >> 16) & 0xFF,   # Byte 2
            (sab2_value >> 24) & 0xFF    # Byte 3
        ]
        
        sat2_bytes = [
            sat2_value & 0xFF,           # Byte 4
            (sat2_value >> 8) & 0xFF     # Byte 5
        ]
        
        # MAC-Adresse zusammensetzen
        mac_bytes = sab2_bytes + sat2_bytes
        mac_address = ":".join(f"{byte:02X}" for byte in mac_bytes)
        
        print(f"🧮 Byte-Extraktion (Little Endian):")
        print(f"   SAB2 Bytes: {':'.join(f'{b:02X}' for b in sab2_bytes)} (erste 4)")
        print(f"   SAT2 Bytes: {':'.join(f'{b:02X}' for b in sat2_bytes)} (letzte 2)")
        
        return mac_address, True
    
    def display_mac_info(self, mac_address: str):
        """MAC-Adresse formatiert anzeigen."""
        print("\n" + "="*50)
        print("📡 LAN8651 MAC-ADRESSE")
        print("="*50)
        print(f"🎯 MAC-Adresse: {mac_address}")
        
        # Zusätzliche Informationen
        mac_bytes = mac_address.split(":")
        
        # OUI (Organizationally Unique Identifier) - erste 3 Bytes
        oui = ":".join(mac_bytes[:3])
        device_id = ":".join(mac_bytes[3:])
        
        print(f"📋 Details:")
        print(f"   OUI (Hersteller):  {oui}")
        print(f"   Device ID:         {device_id}")
        
        # OUI Lookup (bekannte Microchip OUIs)
        microchip_ouis = {
            "00:04:25": "Microchip Technology Inc."
        }
        
        if oui in microchip_ouis:
            print(f"   Hersteller:        {microchip_ouis[oui]} ✅")
        else:
            print(f"   Hersteller:        Unbekannt")
        
        # Adress-Typ prüfen
        first_byte = int(mac_bytes[0], 16)
        is_unicast = (first_byte & 0x01) == 0
        is_locally_administered = (first_byte & 0x02) != 0
        
        print(f"   Adress-Typ:        {'Unicast' if is_unicast else 'Multicast'}")
        print(f"   Verwaltung:        {'Lokal' if is_locally_administered else 'Global'}")
        
        print("="*50)
    
    def run(self):
        """Hauptprogramm."""
        print("🚀 LAN8651 MAC Address Reader (Corrected)")
        print("=" * 45)
        
        if not self.connect():
            return
        
        try:
            mac_address, success = self.read_mac_address()
            
            if success and mac_address:
                self.display_mac_info(mac_address)
                
                # Vergleich mit erwartetem Wert
                expected_mac = "00:04:25:01:02:03"
                if mac_address.upper() == expected_mac.upper():
                    print("✅ MAC-Adresse stimmt mit erwartetem Wert überein!")
                else:
                    print(f"⚠️  MAC-Adresse weicht ab. Erwartet: {expected_mac}")
                    
            else:
                print("❌ Konnte MAC-Adresse nicht lesen")
        
        except KeyboardInterrupt:
            print("\n⏹️  Abgebrochen durch Benutzer")
        
        finally:
            self.close()


if __name__ == "__main__":
    print("LAN8651 MAC Address Reader - Corrected Version")
    print("=" * 50)
    print("Liest MAC-Adresse aus MAC_SAB2/MAC_SAT2 Registern")
    print("Verwendet Little Endian Byte-Interpretation")
    print("Basierend auf Hardware-Verifikation!")
    
    reader = LAN8651MacReader()
    reader.run()