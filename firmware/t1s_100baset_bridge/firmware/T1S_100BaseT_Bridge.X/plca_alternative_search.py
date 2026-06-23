#!/usr/bin/env python3
"""
Alternative PLCA Location Search
"""
import serial
import time

def search_plca_alternatives():
    print("🔍 Alternative PLCA Register Search")
    print("=" * 40)
    
    try:
        ser = serial.Serial('COM8', 115200, timeout=2)
        print("✅ Connected to firmware")
        
        ser.write(b"\n")
        time.sleep(1)
        ser.read_all()
        
        # Try different MMS locations for PLCA
        locations = [
            # Original MMS 4 addresses (0x0004xxxx)
            (0x0004CA0, "MMS4_PLCA_CTRL0"),
            (0x0004CA1, "MMS4_PLCA_CTRL1"),
            (0x0004CA2, "MMS4_PLCA_STATUS"),
            
            # Try MMS 3 (PMA/PMD) - sometimes PLCA is here
            (0x00030CA0, "MMS3_PLCA_CTRL0"),
            (0x00030CA1, "MMS3_PLCA_CTRL1"),
            (0x00030CA2, "MMS3_PLCA_STATUS"),
            
            # Try MMS 2 (PCS) 
            (0x00020CA0, "MMS2_PLCA_CTRL0"),
            (0x00020CA1, "MMS2_PLCA_CTRL1"),
            
            # Standard Clause 22 via MMS 0 indirect
            (0x0000FF1D, "CL22_PLCA_CTRL0"),  # Register 29
            (0x0000FF1E, "CL22_PLCA_CTRL1"),  # Register 30
            
            # Alternative standard addresses
            (0x0000001D, "PLCA_CTRL_ALT1"),
            (0x0000001E, "PLCA_CTRL_ALT2"),
        ]
        
        print("\n📊 Scanning for PLCA registers:")
        found_plca = False
        
        for addr, name in locations:
            ser.write(f"lan_read 0x{addr:08X}\n".encode())
            time.sleep(0.3)
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
                    found_plca = True
                elif addr % 0x10 == 0:  # Show some zeros for reference
                    print(f"❌ 0x{addr:08X} {name:20} = 0x{value:04X}")
            else:
                print(f"🚫 0x{addr:08X} {name:20} = READ FAILED")
        
        # Check some basic PHY management registers
        print(f"\n📊 Basic PHY registers:")
        basic_regs = [  
            (0x0000FF00, "BASIC_CTRL"),      # Clause 22 Reg 0
            (0x0000FF01, "BASIC_STATUS"),    # Clause 22 Reg 1  
            (0x0000FF02, "PHY_ID1"),         # Clause 22 Reg 2
            (0x0000FF03, "PHY_ID2"),         # Clause 22 Reg 3
        ]
        
        for addr, name in basic_regs:
            ser.write(f"lan_read 0x{addr:08X}\n".encode())
            time.sleep(0.3) 
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
                print(f"🔥 0x{addr:08X} {name:15} = 0x{value:04X} ({value})")
        
        if not found_plca:
            print(f"\n❌ NO ACTIVE PLCA REGISTERS FOUND!")
            print(f"   Possible explanations:")
            print(f"   1. PLCA runs in hardware without software access")
            print(f"   2. Point-to-Point mode (no PLCA needed)")
            print(f"   3. PLCA registers at unknown addresses")
            print(f"   4. PLCA managed by different interface")
        
        ser.close()
        print("\n✅ PLCA search complete")
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    search_plca_alternatives()