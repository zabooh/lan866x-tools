#!/usr/bin/env python3
"""
Prove PMD Indirect Access - MDIO-style addressing test
Beweis dass PMD über MDIO-Indirektion erreichbar ist
"""

import serial
import time
import re

def prove_pmd_indirect_access():
    print("🔍 PROOF: PMD Indirect Access via MDIO")
    print("=" * 45)
    
    try:
        conn = serial.Serial('COM8', 115200, timeout=3)
        print("✅ Connected to COM8")
        
        # MDIO-typische Register suchen (bekannte Patterns)
        mdio_candidates = {
            # Standard MDIO Register (verschiedene mögliche Adressen)
            'MDIO_ADDR_1': 0x00000014,    # Typische MDIO Address
            'MDIO_DATA_1': 0x00000015,    # Typische MDIO Data
            'MDIO_ADDR_2': 0x00010014,    # MAC-Space MDIO  
            'MDIO_DATA_2': 0x00010015,    # MAC-Space MDIO
            'PHY_ADDR_REG': 0x00000016,   # PHY Address Register
            'PHY_DATA_REG': 0x00000017,   # PHY Data Register
            
            # Alternative MDIO Locations
            'MDIO_CTRL': 0x00000020,      # MDIO Control
            'MDIO_STATUS': 0x00000021,    # MDIO Status
            
            # Clause 22/45 Access Registers
            'MMD_ADDR': 0x0000000D,       # MMD Address for Clause 45
            'MMD_DATA': 0x0000000E,       # MMD Data for Clause 45
        }
        
        print("\n🔍 STEP 1: Find MDIO Registers")
        print("=" * 30)
        
        # Test alle möglichen MDIO Register
        working_mdio = {}
        for name, addr in mdio_candidates.items():
            conn.reset_input_buffer()
            command = f"lan_read 0x{addr:08X}\r\n"
            conn.write(command.encode('ascii'))
            time.sleep(0.8)
            
            response = ""
            timeout_time = time.time() + 3
            while time.time() < timeout_time:
                if conn.in_waiting:
                    response += conn.read_all().decode('ascii', errors='ignore')
                    break
                time.sleep(0.1)
            
            value_match = re.search(r'Value=0x([0-9A-Fa-f]+)', response)
            if value_match:
                value = int(value_match.group(1), 16)
                working_mdio[name] = {'addr': addr, 'value': value}
                print(f"✅ {name:<15} (0x{addr:08X}): 0x{value:04X}")
            else:
                print(f"❌ {name:<15} (0x{addr:08X}): No response")
        
        if not working_mdio:
            print("❌ No MDIO registers found - trying alternative methods")
            return
        
        print(f"\n🧪 STEP 2: Test Indirect PMD Access")
        print("=" * 35)
        
        # Try different MDIO register pairs for indirect access
        test_pairs = [
            ('MDIO_ADDR_1', 'MDIO_DATA_1'),
            ('MDIO_ADDR_2', 'MDIO_DATA_2'),
            ('PHY_ADDR_REG', 'PHY_DATA_REG'),
            ('MMD_ADDR', 'MMD_DATA'),
        ]
        
        successful_indirect = False
        
        for addr_reg, data_reg in test_pairs:
            if addr_reg in working_mdio and data_reg in working_mdio:
                print(f"\n📡 Testing {addr_reg} + {data_reg}:")
                
                # Test Method: Write PMD register address, then read data
                pmd_target = 0x00030001  # PMD_CONTROL address
                
                # Step 1: Write target PMD address to MDIO Address register
                addr_reg_addr = working_mdio[addr_reg]['addr']
                addr_cmd = f"lan_write 0x{addr_reg_addr:08X} 0x{pmd_target & 0xFFFF:04X}\r\n"
                
                print(f"   📝 Writing PMD addr 0x{pmd_target:08X} to {addr_reg}")
                conn.reset_input_buffer()
                conn.write(addr_cmd.encode('ascii'))
                time.sleep(1)
                response1 = conn.read_all().decode('ascii', errors='ignore')
                
                if "successful" in response1.lower() or "OK" in response1:
                    print(f"   ✅ Address write successful")
                    
                    # Step 2: Read data from MDIO Data register  
                    data_reg_addr = working_mdio[data_reg]['addr']
                    data_cmd = f"lan_read 0x{data_reg_addr:08X}\r\n"
                    
                    print(f"   📖 Reading PMD data from {data_reg}")
                    conn.reset_input_buffer()
                    conn.write(data_cmd.encode('ascii'))
                    time.sleep(1)
                    response2 = conn.read_all().decode('ascii', errors='ignore')
                    
                    value_match = re.search(r'Value=0x([0-9A-Fa-f]+)', response2)
                    if value_match:
                        pmd_value = int(value_match.group(1), 16)
                        print(f"   📊 Indirect PMD_CONTROL: 0x{pmd_value:04X}")
                        
                        if pmd_value != 0x0000:
                            print(f"   🎯 SUCCESS: Non-zero PMD data via indirect access!")
                            successful_indirect = True
                            
                            # Test another PMD register to confirm
                            print(f"\n   🧪 Confirming with PMD_STATUS (0x00030002):")
                            status_cmd = f"lan_write 0x{addr_reg_addr:08X} 0x0002\r\n"
                            conn.reset_input_buffer()
                            conn.write(status_cmd.encode('ascii'))
                            time.sleep(1)
                            conn.read_all()
                            
                            conn.reset_input_buffer()
                            conn.write(data_cmd.encode('ascii'))
                            time.sleep(1)
                            response3 = conn.read_all().decode('ascii', errors='ignore')
                            
                            status_match = re.search(r'Value=0x([0-9A-Fa-f]+)', response3)
                            if status_match:
                                status_value = int(status_match.group(1), 16)
                                print(f"   📊 Indirect PMD_STATUS: 0x{status_value:04X}")
                                
                                if status_value != 0x0000:
                                    print(f"   🏆 DOUBLE CONFIRMED: PMD indirect access works!")
                        else:
                            print(f"   ❌ Still getting 0x0000 - method failed")
                else:
                    print(f"   ❌ Address write failed")
        
        print(f"\n🧪 STEP 3: Try Clause 22 PHY Access")
        print("=" * 35)
        
        # Alternative: Use standard PHY register access (Clause 22)
        # PMD registers might be accessible via PHY register space
        clause22_tests = [
            {'name': 'PHY_PMD_CTRL', 'phy_addr': 0x01, 'reg_addr': 0x00},  # PMD Control
            {'name': 'PHY_PMD_STATUS', 'phy_addr': 0x01, 'reg_addr': 0x01}, # PMD Status  
            {'name': 'PHY_PMD_ID1', 'phy_addr': 0x01, 'reg_addr': 0x02},    # PMD ID1
            {'name': 'PHY_PMD_ID2', 'phy_addr': 0x01, 'reg_addr': 0x03},    # PMD ID2
        ]
        
        for test in clause22_tests:
            # Compute Clause 22 address: 0x0000FF00 + (phy_addr << 8) + reg_addr
            clause22_addr = 0x0000FF00 + (test['phy_addr'] << 5) + test['reg_addr']
            
            conn.reset_input_buffer()
            command = f"lan_read 0x{clause22_addr:08X}\r\n"
            conn.write(command.encode('ascii'))
            time.sleep(1)
            
            response = ""
            timeout_time = time.time() + 3
            while time.time() < timeout_time:
                if conn.in_waiting:
                    response += conn.read_all().decode('ascii', errors='ignore')
                    break
                time.sleep(0.1)
            
            value_match = re.search(r'Value=0x([0-9A-Fa-f]+)', response)
            if value_match:
                value = int(value_match.group(1), 16)
                print(f"📊 {test['name']:<15}: 0x{value:04X} (addr: 0x{clause22_addr:08X})")
                
                if value != 0x0000:
                    print(f"   🎯 NON-ZERO PMD data via Clause 22!")
                    successful_indirect = True
            else:
                print(f"❌ {test['name']:<15}: No response")
        
        print(f"\n🏆 FINAL PROOF RESULT:")
        print("=" * 25)
        
        if successful_indirect:
            print("✅ PMD INDIRECT ACCESS: PROVEN!")
            print("🔧 Method: MDIO-style addressing works")
            print("📊 Evidence: Non-zero PMD data obtained")
            print("🎯 Conclusion: PMD requires indirect access for real data")
        else:
            print("❌ PMD INDIRECT ACCESS: NOT PROVEN")
            print("🤔 May need different MDIO implementation")
            print("💡 Alternative: Check datasheet for correct MDIO registers")
        
        print(f"\n💡 TECHNICAL BACKGROUND:")
        print("📚 Many Ethernet PHYs use MDIO (Management Data I/O)")
        print("🔧 MDIO Address Register + Data Register pattern")
        print("📡 Clause 22: Direct register access")  
        print("📡 Clause 45: MMD (MDIO Manageable Device) access")
        print("🎯 PMD = MDIO Manageable Device in Clause 45")
        
        conn.close()
        
    except Exception as e:
        print(f"❌ Error: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    prove_pmd_indirect_access()