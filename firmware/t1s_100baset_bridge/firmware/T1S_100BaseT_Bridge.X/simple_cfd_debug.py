#!/usr/bin/env python3
"""
Cable Fault Diagnostics Debug Tool
Debug CFD register reading and functionality
"""

import serial
import time
import re

def simple_cfd_test(port='COM8'):
    """Simple test to debug CFD functionality"""
    
    try:
        # Connect
        conn = serial.Serial(port, 115200, timeout=2.0)
        print(f"✅ Connected to {port}")
        
        # Test CFD-related registers
        registers = {
            'PMD_CONTROL': 0x00030001,       # CFD Enable/Start
            'PMD_STATUS': 0x00030002,        # CFD Results
            'PMD_LINK_QUALITY': 0x00040084,  # Alternative quality info
        }
        
        results = {}
        
        print(f"\n🔍 Reading CFD registers BEFORE enabling CFD:")
        for name, addr in registers.items():
            conn.reset_input_buffer()
            command = f"lan_read 0x{addr:08X}\r\n"
            conn.write(command.encode('ascii'))
            conn.flush()
            
            start_time = time.time()
            while time.time() - start_time < 2.0:
                if conn.in_waiting > 0:
                    line = conn.readline().decode('ascii', errors='ignore').strip()
                    if 'Value=' in line:
                        value_match = re.search(r'Value=0x([0-9A-Fa-f]+)', line)
                        if value_match:
                            value = int(value_match.group(1), 16)
                            results[f"{name}_BEFORE"] = value
                            print(f"  {name}: 0x{value:04X}")
                            break
                else:
                    time.sleep(0.01)
        
        # Try to enable CFD
        print(f"\n🔧 Attempting to enable CFD...")
        conn.reset_input_buffer()
        
        # Write CFD enable bit to PMD_CONTROL 
        command = f"lan_write 0x00030001 0x8000\r\n"  # CFD_EN bit
        conn.write(command.encode('ascii'))
        conn.flush()
        time.sleep(0.5)
        
        # Read confirmation
        while conn.in_waiting > 0:
            line = conn.readline().decode('ascii', errors='ignore').strip()
            if line:
                print(f"  📝 Write response: '{line}'")
        
        print(f"\n🔍 Reading CFD registers AFTER enabling CFD:")
        for name, addr in registers.items():
            conn.reset_input_buffer()
            command = f"lan_read 0x{addr:08X}\r\n"
            conn.write(command.encode('ascii'))
            conn.flush()
            
            start_time = time.time()
            while time.time() - start_time < 2.0:
                if conn.in_waiting > 0:
                    line = conn.readline().decode('ascii', errors='ignore').strip()
                    if 'Value=' in line:
                        value_match = re.search(r'Value=0x([0-9A-Fa-f]+)', line)
                        if value_match:
                            value = int(value_match.group(1), 16)
                            results[f"{name}_AFTER"] = value
                            print(f"  {name}: 0x{value:04X}")
                            break
                else:
                    time.sleep(0.01)
        
        # Analysis
        print(f"\n📊 CFD Analysis:")
        pmd_control_before = results.get('PMD_CONTROL_BEFORE', 0)
        pmd_control_after = results.get('PMD_CONTROL_AFTER', 0)
        pmd_status_after = results.get('PMD_STATUS_AFTER', 0)
        
        print(f"  🔧 PMD_CONTROL: 0x{pmd_control_before:04X} → 0x{pmd_control_after:04X}")
        print(f"  📊 PMD_STATUS: 0x{pmd_status_after:04X}")
        
        # Parse PMD_STATUS bits
        if pmd_status_after:
            cfd_done = bool(pmd_status_after & 0x8000)  # Bit 15
            fault_bits = (pmd_status_after >> 12) & 0x07  # Bits 14:12
            link_ok = bool(pmd_status_after & 0x0800)  # Bit 11
            
            print(f"    📍 CFD Done: {cfd_done}")
            print(f"    ⚠️ Fault Bits: {fault_bits}")
            print(f"    🔗 Link OK: {link_ok}")
        
        # Check PMD_LINK_QUALITY for alternative metrics
        link_quality = results.get('PMD_LINK_QUALITY_AFTER', 0)
        if link_quality:
            print(f"  📈 Link Quality: 0x{link_quality:04X}")
            # Try to extract meaningful info
            quality_level = link_quality & 0xFF
            error_count = (link_quality >> 8) & 0xFF
            print(f"    📊 Quality Level: {quality_level}")
            print(f"    ❌ Error Count: {error_count}")
        
        conn.close()
        print(f"\n✅ CFD test completed")
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == '__main__':
    simple_cfd_test()