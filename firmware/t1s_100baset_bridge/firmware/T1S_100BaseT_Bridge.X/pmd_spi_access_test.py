#!/usr/bin/env python3
"""
PMD SPI Direct Access Test
Testet ob PMD-Register direkt über SPI erreichbar sind oder indirekte Adressierung brauchen
"""

import serial
import time
import re

def test_pmd_spi_access():
    print("🔍 PMD SPI Direct Access Test")
    print("=" * 40)
    
    try:
        conn = serial.Serial('COM8', 115200, timeout=3)
        print("✅ Verbindung zu COM8")
        
        # Test verschiedene MMS-Bereiche für Vergleich
        test_registers = {
            # MMS 0: OpenAlliance Standard (sollte direkt funktionieren)  
            'OA_CONFIG0': 0x00000004,
            'OA_STATUS0': 0x00000008,
            
            # MMS 1: MAC Registers (sollte direkt funktionieren)
            'MAC_NCR': 0x00010000,
            
            # MMS 2: PCS Registers (PHY Coding)
            'PCS_CONTROL': 0x00020000,
            
            # MMS 3: PMD Registers (Physical Medium) - HIER TESTEN!
            'PMD_CONTROL': 0x00030001,
            'PMD_STATUS': 0x00030002,
            
            # MMS 4: PHY Vendor Specific (sollte direkt funktionieren - bereits getestet)
            'PHY_EXTENDED_STATUS': 0x0004008F,
            
            # Clause 22 Indirect (PHY über MDIO-ähnlich)
            'PHY_BASIC_CONTROL': 0x0000FF00,
            'PHY_BASIC_STATUS': 0x0000FF01,
        }
        
        print(f"\n📊 SPI ACCESSIBILITY TEST:")
        print(f"Testing different MMS areas for direct SPI access...")
        
        successful_reads = {}
        failed_reads = {}
        
        for name, addr in test_registers.items():
            print(f"\n🔍 Testing {name} (0x{addr:08X}):")
            
            # Clear buffer
            conn.reset_input_buffer()
            command = f"lan_read 0x{addr:08X}\r\n"
            conn.write(command.encode('ascii'))
            time.sleep(1)
            
            response = ""
            timeout_time = time.time() + 3
            while time.time() < timeout_time:
                if conn.in_waiting:
                    response += conn.read_all().decode('ascii', errors='ignore')
                    break
                time.sleep(0.1)
            
            # Check for successful read
            mms = (addr >> 16) & 0xF
            value_match = re.search(r'Value=0x([0-9A-Fa-f]+)', response)
            
            if value_match:
                value = int(value_match.group(1), 16)
                successful_reads[name] = {'addr': addr, 'value': value, 'mms': mms}
                print(f"   ✅ DIRECT ACCESS: 0x{value:04X} (MMS {mms})")
            else:
                failed_reads[name] = {'addr': addr, 'mms': mms}
                print(f"   ❌ ACCESS FAILED (MMS {mms})")
                print(f"      Response: {response.strip()[:100]}")
        
        print(f"\n\n📊 RESULTS SUMMARY:")
        print(f"=" * 40)
        
        # Analyze by MMS
        mms_results = {}
        for name, data in successful_reads.items():
            mms = data['mms']
            if mms not in mms_results:
                mms_results[mms] = {'successful': [], 'failed': []}
            mms_results[mms]['successful'].append(name)
        
        for name, data in failed_reads.items():
            mms = data['mms']
            if mms not in mms_results:
                mms_results[mms] = {'successful': [], 'failed': []}
            mms_results[mms]['failed'].append(name)
        
        for mms in sorted(mms_results.keys()):
            results = mms_results[mms]
            total = len(results['successful']) + len(results['failed'])
            success_rate = len(results['successful']) / total * 100 if total > 0 else 0
            
            print(f"\n🔧 MMS {mms}: {success_rate:.0f}% success rate")
            if results['successful']:
                print(f"   ✅ Direct: {', '.join(results['successful'])}")
            if results['failed']:
                print(f"   ❌ Failed: {', '.join(results['failed'])}")
        
        print(f"\n💡 PMD SPECIFIC ANALYSIS:")
        print(f"=" * 30)
        
        if 'PMD_CONTROL' in successful_reads and 'PMD_STATUS' in successful_reads:
            pmd_control = successful_reads['PMD_CONTROL']['value']
            pmd_status = successful_reads['PMD_STATUS']['value']
            
            print(f"🎯 PMD DIRECT SPI ACCESS: ✅ CONFIRMED")
            print(f"   📊 PMD_CONTROL: 0x{pmd_control:04X}")
            print(f"   📊 PMD_STATUS:  0x{pmd_status:04X}")
            print(f"   🔧 No indirect addressing needed!")
            print(f"   ⚡ PMD accessible via TC6 protocol over SPI")
            
        elif 'PMD_CONTROL' in failed_reads or 'PMD_STATUS' in failed_reads:
            print(f"❌ PMD INDIRECT ACCESS: May require MDIO-style addressing")
            print(f"🔧 Would need Clause 22/45 indirect access via MDD registers")
            
        print(f"\n🔬 TECHNICAL EXPLANATION:")
        print(f"=" * 30)
        print(f"📡 TC6 Protocol: Translates SPI → Internal register map")
        print(f"🔧 MMS (Memory Map Selector): Upper 16 bits select subsystem")
        print(f"📊 MMS 3 (PMD): Physical Medium Dependent registers")
        print(f"⚡ If PMD works direct: TC6 handles MMS→PMD translation internally")
        print(f"❌ If PMD needs indirect: Would require MDIO-style access")
        
        # Test a write to see if PMD is truly writable
        if 'PMD_CONTROL' in successful_reads:
            print(f"\n🧪 PMD WRITE TEST:")
            original_value = successful_reads['PMD_CONTROL']['value']
            print(f"   📊 Original PMD_CONTROL: 0x{original_value:04X}")
            
            # Try to set a harmless bit (assuming safe bits exist)
            print(f"   ⚠️ Note: Write test not performed to avoid disrupting active link")
            print(f"   💡 Read success confirms direct access capability")
        
        conn.close()
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    test_pmd_spi_access()