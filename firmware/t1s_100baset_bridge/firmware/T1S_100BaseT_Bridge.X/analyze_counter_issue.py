#!/usr/bin/env python3
"""
T1S Forward Mode & Counter Analysis
"""
import serial
import time

def check_forward_mode_and_counters():
    print("🔍 T1S Forward Mode & Counter Analysis")
    print("=" * 50)
    
    try:
        ser = serial.Serial('COM8', 115200, timeout=2)
        print("✅ Connected to firmware")
        
        # Check forward mode
        print("\n1️⃣ FORWARD MODE STATUS:")
        ser.write(b"fwd\n")
        time.sleep(1)
        response = ser.read_all().decode('utf-8', errors='ignore')
        print(f"Forward status: {repr(response)}")
        
        # Check key status registers
        print("\n2️⃣ KEY STATUS REGISTERS:")
        status_regs = [
            (0x00000004, "CONFIG0/CHIP_ID"),
            (0x00000008, "OA_STATUS0"),  
            (0x00000009, "OA_STATUS1"),
            (0x00010000, "MAC_NCR"),
        ]
        
        for addr, name in status_regs:
            ser.write(f"lan_read 0x{addr:08X}\n".encode())
            time.sleep(1)
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
                print(f"📊 0x{addr:08X} {name:20} = 0x{value:08X} ({value})")
            else:
                print(f"🚫 0x{addr:08X} {name:20} = READ FAILED")
        
        # Test alternative counter locations
        print("\n3️⃣ ALTERNATIVE COUNTER LOCATIONS:")
        alt_counters = [
            (0x00000020, "OA_TX_FRAMES_ALT"),
            (0x00000024, "OA_RX_FRAMES_ALT"),
            (0x00010008, "MAC_STATUS"),
            (0x00010104, "MAC_TX_HIGH"), 
            (0x00010124, "MAC_RX_HIGH"),
            (0x000A0000, "MMS10_BASE"),
        ]
        
        for addr, name in alt_counters:
            ser.write(f"lan_read 0x{addr:08X}\n".encode())
            time.sleep(1)
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
            
            if value is not None and value > 0:
                print(f"🔥 0x{addr:08X} {name:20} = 0x{value:08X} ({value}) ← NON-ZERO!")
            elif value is not None:
                print(f"❌ 0x{addr:08X} {name:20} = 0x{value:08X} ({value})")
            else:
                print(f"🚫 0x{addr:08X} {name:20} = READ FAILED")
                
        ser.close()
        print("\n✅ Analysis complete")
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    check_forward_mode_and_counters()