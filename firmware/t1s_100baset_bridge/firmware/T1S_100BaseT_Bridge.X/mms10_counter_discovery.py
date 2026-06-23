#!/usr/bin/env python3
"""
MMS 10 Counter Discovery - Die richtigen Zähler finden!
"""
import serial
import time

def scan_mms10_counters():
    print("🔍 MMS 10 Counter Discovery")
    print("=" * 40)
    
    try:
        ser = serial.Serial('COM8', 115200, timeout=2)
        print("✅ Connected to firmware")
        
        print("\n🔥 MMS 10 COUNTER SCAN:")
        
        # Scan MMS 10 range for counters
        for offset in range(0x00, 0x40, 0x04):  # Scan first 64 bytes, 32-bit aligned
            addr = 0x000A0000 + offset
            
            ser.write(f"lan_read 0x{addr:08X}\n".encode())
            time.sleep(0.5)
            response = ser.read_all().decode('utf-8', errors='ignore')
            
            value = None
            for line in response.split('\n'):
                if 'Value=' in line:
                    try:
                        value_part = line.split('Value=')[1]
                        value = int(value_part.split()[0], 16)
                        break
                    except:
                        pass
            
            if value is not None:
                if value > 0:
                    print(f"🔥 0x{addr:08X} (MMS10+0x{offset:02X}) = {value:10} (0x{value:08X}) ← ACTIVE!")
                elif offset % 0x10 == 0:  # Show every 4th zero for reference
                    print(f"❌ 0x{addr:08X} (MMS10+0x{offset:02X}) = {value:10}")
        
        print("\n📊 PLCA STATUS CHECK:")
        # Check PLCA related counters (might be in MMS 4)
        plca_addresses = [
            (0x00040CA0, "PLCA_CTRL0"),
            (0x00040CA1, "PLCA_CTRL1"), 
            (0x00040CA2, "PLCA_STATUS"),
            (0x00040CA5, "PLCA_BURST_CNT"),
        ]
        
        for addr, name in plca_addresses:
            ser.write(f"lan_read 0x{addr:08X}\n".encode())
            time.sleep(0.5)
            response = ser.read_all().decode('utf-8', errors='ignore')
            
            value = None
            for line in response.split('\n'):
                if 'Value=' in line:
                    try:
                        value_part = line.split('Value=')[1]
                        value = int(value_part.split()[0], 16)
                        break
                    except:
                        pass
            
            if value is not None:
                if value > 0:
                    print(f"🔥 0x{addr:08X} {name:20} = 0x{value:04X} ({value}) ← NON-ZERO!")
                else:
                    print(f"❌ 0x{addr:08X} {name:20} = 0x{value:04X} ({value})")
        
        print("\n🎯 STATUS REGISTER DECODE:")
        print("OA_STATUS0 = 0x51:")
        print("  Bit 0 (RESETC): {}".format("SET" if 0x51 & 0x01 else "CLEAR"))
        print("  Bit 4 (TXPE):   {}".format("SET" if 0x51 & 0x10 else "CLEAR")) 
        print("  Bit 6 (HDRE):   {}".format("SET" if 0x51 & 0x40 else "CLEAR"))
        
        ser.close()
        print("\n✅ Discovery complete")
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    scan_mms10_counters()