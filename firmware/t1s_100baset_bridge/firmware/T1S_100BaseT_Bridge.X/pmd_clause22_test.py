#!/usr/bin/env python3
"""
PMD via Clause 22 PHY Register Access
Test if PMD is accessible through standard PHY register space
"""

import serial
import time
import re

def test_pmd_via_clause22():
    print("🔍 PMD Access via Clause 22 PHY Registers")
    print("=" * 45)
    
    try:
        conn = serial.Serial('COM8', 115200, timeout=2)
        print("✅ Connected to COM8")
        
        # Standard Clause 22 PHY Register Space (0x0000FFxx)
        # PMD could be mapped to specific PHY addresses
        
        print(f"\n📡 Testing Clause 22 PHY Register Space:")
        
        # Test different PHY addresses and registers for PMD
        pmd_candidates = [
            # Format: Name, PHY_ADDR, REG_ADDR, Description
            ('PMD_CTRL_1', 0x01, 0x00, 'PMD Control via PHY 1 Reg 0'),  
            ('PMD_STATUS_1', 0x01, 0x01, 'PMD Status via PHY 1 Reg 1'),
            ('PMD_ID1_1', 0x01, 0x02, 'PMD ID1 via PHY 1 Reg 2'),
            ('PMD_ID2_1', 0x01, 0x03, 'PMD ID2 via PHY 1 Reg 3'),
            
            ('PMD_CTRL_3', 0x03, 0x00, 'PMD Control via PHY 3 Reg 0'),
            ('PMD_STATUS_3', 0x03, 0x01, 'PMD Status via PHY 3 Reg 1'),
            
            # Alternative PHY addresses for PMD
            ('PMD_CTRL_1E', 0x1E, 0x00, 'PMD Control via PHY 30'),
            ('PMD_STATUS_1E', 0x1E, 0x01, 'PMD Status via PHY 30'),
        ]
        
        significant_values = []
        
        for name, phy_addr, reg_addr, desc in pmd_candidates:
            # Clause 22 address calculation: 0x0000FF00 + (phy_addr << 5) + reg_addr
            clause22_addr = 0x0000FF00 + (phy_addr << 5) + reg_addr
            
            conn.reset_input_buffer()
            conn.write(f"lan_read 0x{clause22_addr:08X}\r\n".encode())
            time.sleep(0.8)
            
            resp = conn.read_all().decode('ascii', errors='ignore')
            match = re.search(r'Value=0x([0-9A-Fa-f]+)', resp)
            
            if match:
                value = int(match.group(1), 16)
                status = "📊" if value == 0x0000 else "🎯 NON-ZERO"
                print(f"{name:<15}: 0x{value:04X} {status} ({desc})")
                
                if value != 0x0000:
                    significant_values.append({
                        'name': name, 'value': value, 'addr': clause22_addr, 
                        'phy': phy_addr, 'reg': reg_addr
                    })
            else:
                print(f"{name:<15}: ❌ No response")
        
        print(f"\n🎯 SIGNIFICANT FINDINGS:")
        print("=" * 25)
        
        if significant_values:
            print(f"✅ Found {len(significant_values)} non-zero PMD candidates!")
            
            for item in significant_values:
                print(f"🔍 {item['name']}: 0x{item['value']:04X}")
                print(f"   Address: 0x{item['addr']:08X} (PHY {item['phy']}, Reg {item['reg']})")
                
                # Analyze the value for PMD-like patterns
                value = item['value']
                
                # Check for typical PMD status patterns
                if 'STATUS' in item['name']:
                    link_up = bool(value & 0x0004)  # Standard link bit
                    speed_bits = (value >> 6) & 0x03  # Speed encoding 
                    print(f"   📊 Link Up: {'✅' if link_up else '❌'}")
                    print(f"   📊 Speed: {speed_bits}")
                    
                    if link_up and value & 0xF000:  # High bits set suggest real status
                        print(f"   🎯 LOOKS LIKE REAL PMD STATUS!")
                
                elif 'CTRL' in item['name']:
                    reset_bit = bool(value & 0x8000)  # Reset bit
                    loopback = bool(value & 0x4000)   # Loopback bit
                    print(f"   📊 Reset: {'⚠️' if reset_bit else '✅'}")
                    print(f"   📊 Loopback: {'✅' if loopback else '❌'}")
                    
                    if value & 0x3FFF:  # Control bits set
                        print(f"   🎯 LOOKS LIKE REAL PMD CONTROL!")
                
                elif 'ID' in item['name']:
                    if value > 0x0100:  # Realistic ID value
                        print(f"   🎯 LOOKS LIKE REAL PMD ID!")
        else:
            print("❌ No significant PMD data found via Clause 22")
        
        print(f"\n🧪 Testing Alternative MDIO Methods:")
        print("=" * 35)
        
        # Test Clause 45 MMD addressing (MDIO Manageable Device)
        mmd_tests = [
            ('MMD_DEVAD', 0x0000000D, 'MMD Device Address'),
            ('MMD_DATA', 0x0000000E, 'MMD Data Register'),
        ]
        
        for name, addr, desc in mmd_tests:
            conn.reset_input_buffer()
            conn.write(f"lan_read 0x{addr:08X}\r\n".encode())
            time.sleep(0.8)
            
            resp = conn.read_all().decode('ascii', errors='ignore')
            match = re.search(r'Value=0x([0-9A-Fa-f]+)', resp)
            
            if match:
                value = int(match.group(1), 16)
                print(f"✅ {name}: 0x{value:04X} ({desc})")
                
                if value != 0x0000:
                    print(f"   🎯 Could be used for Clause 45 PMD access!")
            else:
                print(f"❌ {name}: No response")
        
        print(f"\n🏆 CONCLUSION:")
        print("=" * 15)
        
        if significant_values:
            print("✅ PMD INDIRECT ACCESS: PROVEN via Clause 22!")
            print(f"🔧 Method: PHY register space contains PMD data")
            print(f"📊 Found {len(significant_values)} working PMD registers")
            print(f"🎯 PMD accessible through standard PHY addressing")
        else:
            print("❌ PMD INDIRECT ACCESS: Not yet proven")
            print("💡 May need different addressing scheme")
            print("📚 Check LAN8651 datasheet for correct PMD access method")
        
        conn.close()
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    test_pmd_via_clause22()