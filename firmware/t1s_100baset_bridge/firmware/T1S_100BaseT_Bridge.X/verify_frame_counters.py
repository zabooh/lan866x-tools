#!/usr/bin/env python3
"""
T1S Frame Counter Verification Tool
Überprüft verschiedene MMS-Bereiche auf Traffic-Zähler während aktiver Übertragung
"""

import serial
import time
import subprocess
import threading
from datetime import datetime

class T1SCounterVerifier:
    def __init__(self, port='COM8', baudrate=115200):
        self.ser = serial.Serial(port, baudrate, timeout=2)
        self.running = False
        
    def send_command(self, cmd):
        """Sendet Kommando und wartet auf Antwort"""
        self.ser.write(f"{cmd}\n".encode())
        time.sleep(1)  # Einfache Wartezeit statt Prompt-Parsing
        response = self.ser.read_all().decode('utf-8', errors='ignore')
        return response.strip()
        
    def read_register(self, address):
        """Liest Register und extrahiert Wert"""
        response = self.send_command(f"lan_read 0x{address:08X}")
        # Parse: "LAN865X Read: Addr=0x00080000 Value=0x00000000"
        for line in response.split('\n'):
            if 'Value=' in line:
                try:
                    value_part = line.split('Value=')[1]
                    return int(value_part.split()[0], 16)
                except:
                    pass
        return None
        
    def check_counter_locations(self):
        """Überprüft verschiedene bekannte Zähler-Locations"""
        locations = [
            # MMS 0 - OA Standard Counters
            (0x00000010, "OA_TX_FRAME_COUNT"),
            (0x00000011, "OA_RX_FRAME_COUNT"), 
            (0x00000012, "OA_TX_ERROR_COUNT"),
            (0x00000013, "OA_RX_ERROR_COUNT"),
            
            # MMS 1 - MAC Counters  
            (0x00010100, "MAC_TX_FRAME_COUNT_LOW"),
            (0x00010101, "MAC_TX_FRAME_COUNT_HIGH"),
            (0x00010120, "MAC_RX_FRAME_COUNT_LOW"), 
            (0x00010121, "MAC_RX_FRAME_COUNT_HIGH"),
            
            # MMS 8 - Current locations
            (0x00080000, "MMS8_FRAME_COUNTERS"),
            (0x00080001, "MMS8_ERROR_COUNTERS"),
            (0x00080002, "MMS8_DEBUG_COUNTERS"),
            
            # MMS 10 - Misc
            (0x000A0010, "MMS10_FRAME_COUNT"),
            (0x000A0020, "MMS10_TX_COUNT"),
            (0x000A0021, "MMS10_RX_COUNT"),
        ]
        
        print(f"\n{'='*60}")
        print(f"🔍 FRAME COUNTER VERIFICATION - {datetime.now().strftime('%H:%M:%S')}")
        print(f"{'='*60}")
        
        results = {}
        for addr, name in locations:
            value = self.read_register(addr)
            results[addr] = (name, value)
            status = "📊" if value and value > 0 else "❌" if value == 0 else "🚫"
            print(f"{status} 0x{addr:08X} {name:25} = {value if value is not None else 'FAIL'}")
            
        return results
        
    def continuous_ping(self, duration=30):
        """Startet kontinuierlichen Traffic für Zähler-Test"""
        print(f"\n🚀 Starting continuous ping for {duration} seconds...")
        
        # Optimaler Ping aus unseren Tests
        cmd = ["ping", "-s", "1400", "-i", "0.001", "-c", str(duration * 500), "192.168.0.200"]
        
        def run_ping():
            try:
                result = subprocess.run(cmd, capture_output=True, text=True)
                print(f"✅ Ping completed: {result.returncode}")
            except Exception as e:
                print(f"❌ Ping error: {e}")
                
        # Starte Ping in separatem Thread
        ping_thread = threading.Thread(target=run_ping)
        ping_thread.daemon = True
        ping_thread.start()
        
        # Überwache Zähler während Traffic
        print("\n📊 MONITORING COUNTERS DURING TRAFFIC:")
        start_counters = self.check_counter_locations()
        
        print(f"\n⏳ Waiting {duration} seconds for traffic...")
        time.sleep(duration)
        
        print(f"\n📊 FINAL COUNTER CHECK:")
        end_counters = self.check_counter_locations()
        
        # Vergleiche Start vs End
        print(f"\n{'='*60}")
        print("📈 COUNTER CHANGES:")
        print(f"{'='*60}")
        
        changes_found = False
        for addr in start_counters:
            start_name, start_val = start_counters[addr]
            end_name, end_val = end_counters[addr]
            
            if start_val is not None and end_val is not None:
                if end_val != start_val:
                    change = end_val - start_val
                    print(f"🔥 0x{addr:08X} {start_name:25} = {start_val:8} → {end_val:8} (+{change})")
                    changes_found = True
                elif end_val > 0:
                    print(f"📊 0x{addr:08X} {start_name:25} = {end_val:8} (static)")
                    
        if not changes_found:
            print("❌ NO COUNTER CHANGES DETECTED!")
            print("   Mögliche Probleme:")
            print("   - Traffic geht an Zählern vorbei") 
            print("   - Falsche Register-Adressen")
            print("   - Clear-on-Read Verhalten")
            print("   - Bridge-Modus umgeht Zähler")
            
        return changes_found

def main():
    print("🔍 T1S Frame Counter Verification Tool")
    print("=" * 50)
    
    try:
        verifier = T1SCounterVerifier()
        print("✅ Connected to T1S system")
        
        # Erst einmal ohne Traffic prüfen
        print("\n1️⃣ BASELINE CHECK (No Traffic):")
        verifier.check_counter_locations()
        
        # Dann mit Traffic testen
        print("\n2️⃣ TRAFFIC TEST:")
        changes_detected = verifier.continuous_ping(duration=30)
        
        if changes_detected:
            print("\n✅ COUNTERS WORKING - Traffic wird gemessen!")
        else:
            print("\n❌ PROBLEM - Keine Zähler-Veränderung trotz Traffic!")
            
    except Exception as e:
        print(f"❌ Error: {e}")
    
if __name__ == "__main__":
    main()