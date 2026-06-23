#!/usr/bin/env python3
"""
Correct SQI Register Discovery
Find the right SQI registers for LAN8651
"""

import serial
import time
import re

def find_sqi_registers():
    print("🔍 SQI Register Discovery für LAN8651")
    print("=" * 40)
    
    try:
        conn = serial.Serial('COM8', 115200, timeout=3)
        print("✅ Verbindung zu COM8")
        
        # Bekannte SQI-relevante Register aus verschiedenen Quellen
        sqi_candidates = {
            # MMS 4 (PHY Vendor Specific) - Wahrscheinlichste Location
            'SQI_PHY_VENDOR_1': 0x00040083,  # Original attempt
            'SQI_PHY_VENDOR_2': 0x00040090,  # Alternative location
            'SQI_PHY_VENDOR_3': 0x00040091,  # SQI status register
            'SQI_PHY_VENDOR_4': 0x00040092,  # SQI measurement
            'PHY_EXTENDED_STATUS': 0x0004008F,  # Extended status often has SQI
            
            # MMS 3 (PHY PMA/PMD) - Alternative
            'PMD_EXTENDED_1': 0x00030015,  # Extended PMD register
            'PMD_EXTENDED_2': 0x00030020,  # PMD status register
            'PMD_EXTENDED_3': 0x00030021,  # PMD measurement register
            
            # MMS 2 (PHY PCS) - Weniger wahrscheinlich aber möglich  
            'PCS_STATUS_1': 0x00020008,
            'PCS_STATUS_2': 0x00020020,
        }
        
        print("\n📊 REGISTER SCAN:")
        print("=" * 40)
        
        for name, addr in sqi_candidates.items():
            conn.reset_input_buffer()
            command = f"lan_read 0x{addr:08X}\r\n"
            conn.write(command.encode('ascii'))
            time.sleep(0.5)
            
            response = ""
            timeout = time.time() + 2
            while time.time() < timeout:
                if conn.in_waiting:
                    response += conn.read_all().decode('ascii', errors='ignore')
                    break
                time.sleep(0.1)
            
            # Parse value
            value_match = re.search(r'Value=0x([0-9A-Fa-f]+)', response)
            if value_match:
                value = int(value_match.group(1), 16)
                print(f"🔍 {name:<20} (0x{addr:08X}): 0x{value:04X} = {value:5d}")
                
                # Analyze if this could be SQI
                if value > 0 and value <= 7:
                    print(f"   ⭐ POTENTIAL SQI: {value}/7")
                elif value > 0 and value < 100:
                    print(f"   📊 Possible quality metric: {value}")
                elif value > 1000:
                    print(f"   📈 Complex metric (maybe encoded)")
            else:
                print(f"❌ {name:<20} (0x{addr:08X}): No response")
        
        print("\n\n🎯 ADDITIONAL PHY STATUS CHECK:")
        print("=" * 40)
        
        # Check additional status registers that might contain SQI
        status_registers = {
            'PHY_SPECIAL_STATUS': 0x0004001F,  # Special status register
            'PHY_INT_SOURCE': 0x0004001D,     # Interrupt source (might have quality bits)
            'PHY_INT_MASK': 0x0004001E,       # Interrupt mask
            'SPECIAL_MODES': 0x00040012,      # Special modes register
        }
        
        for name, addr in status_registers.items():
            conn.reset_input_buffer()
            command = f"lan_read 0x{addr:08X}\r\n"
            conn.write(command.encode('ascii'))
            time.sleep(0.5)
            
            response = ""
            timeout = time.time() + 2
            while time.time() < timeout:
                if conn.in_waiting:
                    response += conn.read_all().decode('ascii', errors='ignore')
                    break
                time.sleep(0.1)
            
            value_match = re.search(r'Value=0x([0-9A-Fa-f]+)', response)
            if value_match:
                value = int(value_match.group(1), 16)
                print(f"📊 {name:<20} (0x{addr:08X}): 0x{value:04X} = {value:5d}")
                
                # Bit analysis for status registers
                if value > 0:
                    bits = []
                    for i in range(16):
                        if value & (1 << i):
                            bits.append(f"bit{i}")
                    if bits:
                        print(f"   🔍 Active bits: {', '.join(bits)}")
        
        print("\n\n💡 ANALYSE:")
        print("🔧 Hardware läuft mit 1.476 Mbps - SQI sollte gut sein")
        print("📊 Registerscan zeigt verfügbare Metriken")
        print("⚡ SQI könnte in speziellen Vendor-Registern versteckt sein")
        
        print("\n🎯 EMPFEHLUNG:")
        print("→ Register mit sinnvollen Werten (1-7, 0-100) sind SQI-Kandidaten")
        print("→ Komplexe Werte könnten codierte Qualitätsmetriken sein")
        print("→ Bei 1.5 Mbps Performance sollte SQI ≥ 5/7 sein")
        
        conn.close()
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    find_sqi_registers()