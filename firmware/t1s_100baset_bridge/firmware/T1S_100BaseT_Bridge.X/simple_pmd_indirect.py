#!/usr/bin/env python3
"""
Simple PMD Indirect Access Test - Focused approach
"""

import serial
import time
import re

def simple_pmd_indirect_test():
    print("🔍 Simple PMD Indirect Access Test")
    print("=" * 40)
    
    try:
        conn = serial.Serial('COM8', 115200, timeout=2)
        print("✅ Connected to COM8")
        
        # Test known working MDIO-style registers first
        print(f"\n📡 Testing Standard MDIO Registers:")
        
        mdio_regs = {
            'MDIO_ADDR': 0x00000014,
            'MDIO_DATA': 0x00000015,
        }
        
        for name, addr in mdio_regs.items():
            conn.reset_input_buffer()
            conn.write(f"lan_read 0x{addr:08X}\r\n".encode())
            time.sleep(1)
            resp = conn.read_all().decode('ascii', errors='ignore')
            
            match = re.search(r'Value=0x([0-9A-Fa-f]+)', resp)
            if match:
                value = int(match.group(1), 16)
                print(f"✅ {name}: 0x{value:04X} (addr: 0x{addr:08X})")
            else:
                print(f"❌ {name}: No response")
        
        print(f"\n🧪 Testing Indirect PMD Access:")
        print("Method: Write PMD address to MDIO_ADDR, read from MDIO_DATA")
        
        # Try to access PMD_CONTROL (0x00030001) indirectly
        pmd_control_addr = 0x00030001
        
        # Step 1: Write PMD register address to MDIO address register
        print(f"📝 Step 1: Write PMD addr 0x{pmd_control_addr:08X} to MDIO_ADDR")
        conn.reset_input_buffer()
        conn.write(f"lan_write 0x00000014 0x{pmd_control_addr & 0xFFFF:04X}\r\n".encode())
        time.sleep(1)
        write_resp = conn.read_all().decode('ascii', errors='ignore')
        
        if "successful" in write_resp.lower() or "- OK" in write_resp:
            print("✅ Address write successful")
            
            # Step 2: Read data from MDIO data register
            print(f"📖 Step 2: Read PMD data from MDIO_DATA")
            conn.reset_input_buffer()
            conn.write(f"lan_read 0x00000015\r\n".encode())
            time.sleep(1)
            read_resp = conn.read_all().decode('ascii', errors='ignore')
            
            match = re.search(r'Value=0x([0-9A-Fa-f]+)', read_resp)
            if match:
                pmd_value = int(match.group(1), 16)
                print(f"📊 Indirect PMD_CONTROL: 0x{pmd_value:04X}")
                
                if pmd_value != 0x0000:
                    print(f"🎯 SUCCESS: Non-zero PMD data obtained!")
                    print(f"✅ PMD indirect access PROVEN!")
                else:
                    print(f"❌ Still getting 0x0000")
            else:
                print(f"❌ No data response")
        else:
            print(f"❌ Address write failed: {write_resp}")
        
        conn.close()
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    simple_pmd_indirect_test()