#!/usr/bin/env python3
"""
Simple SQI Debug Tool
Debug SQI register reading issues
"""

import serial
import time
import re

def simple_sqi_test(port='COM8'):
    """Simple test to debug SQI register reading"""
    
    try:
        # Connect
        conn = serial.Serial(port, 115200, timeout=2.0)
        print(f"✅ Connected to {port}")
        
        # Test SQI-related registers
        registers = {
            'PMD_SQI': 0x00040083,           # Main SQI register
            'PMD_LINK_QUALITY': 0x00040084,  # Extended quality
            'PMD_TEMPERATURE': 0x00040081,   # Temperature
            'PMD_VOLTAGE': 0x00040082,       # Voltage
            'PHY_BASIC_STATUS': 0x0000FF01,  # Link status
        }
        
        results = {}
        
        for name, addr in registers.items():
            print(f"\n🔍 Reading {name} (0x{addr:08X}):")
            
            # Clear buffer
            conn.reset_input_buffer()
            
            # Send command
            command = f"lan_read 0x{addr:08X}\r\n"
            conn.write(command.encode('ascii'))
            conn.flush()
            
            # Read response with timeout
            lines = []
            start_time = time.time()
            
            while time.time() - start_time < 3.0:  # 3 second timeout
                if conn.in_waiting > 0:
                    line = conn.readline().decode('ascii', errors='ignore').strip()
                    if line:
                        lines.append(line)
                        print(f"  📝 '{line}'")
                        
                        # Check if we got the value callback
                        if 'Value=' in line:
                            value_match = re.search(r'Value=0x([0-9A-Fa-f]+)', line)
                            if value_match:
                                value = int(value_match.group(1), 16)
                                results[name] = value
                                print(f"  ✅ Parsed value: 0x{value:04X}")
                                break
                else:
                    time.sleep(0.01)
            
            if name not in results:
                print(f"  ❌ No value callback received")
                results[name] = None
        
        # Analysis
        print(f"\n📊 SQI Analysis:")
        pmd_sqi = results.get('PMD_SQI', 0)
        
        if pmd_sqi is not None:
            # Parse SQI register according to AN1760
            sqi_value = pmd_sqi & 0x07     # Bits 2:0 = SQI value
            sqi_valid = bool(pmd_sqi & 0x08)  # Bit 3 = Valid flag
            
            print(f"  📊 Raw PMD_SQI: 0x{pmd_sqi:04X}")
            print(f"  📈 SQI Value: {sqi_value}/7")
            print(f"  ✅ Valid Flag: {sqi_valid}")
        
        # Temperature and voltage parsing
        temp_reg = results.get('PMD_TEMPERATURE')
        volt_reg = results.get('PMD_VOLTAGE')
        
        if temp_reg is not None:
            # Temperature conversion (placeholder - needs actual formula)
            temp_celsius = temp_reg * 0.125 - 40  # Example conversion
            print(f"  🌡️ Temperature: {temp_celsius:.1f}°C (raw: 0x{temp_reg:04X})")
        
        if volt_reg is not None:
            # Voltage conversion (placeholder - needs actual formula)  
            voltage = volt_reg * 3.3 / 65536  # Example conversion
            print(f"  ⚡ Voltage: {voltage:.2f}V (raw: 0x{volt_reg:04X})")
        
        # Link status
        link_reg = results.get('PHY_BASIC_STATUS')
        if link_reg is not None:
            link_up = bool(link_reg & 0x0004)  # Bit 2 = Link Status
            print(f"  🔗 Link Up: {link_up} (raw: 0x{link_reg:04X})")
        
        conn.close()
        print(f"\n✅ SQI test completed")
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == '__main__':
    simple_sqi_test()