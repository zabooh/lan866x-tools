#!/usr/bin/env python3
"""
Simple PLCA Debug Tool
Quick test to fix register reading issue
"""

import serial
import time
import re

def simple_plca_test(port='COM8'):
    """Simple test to debug register reading"""
    
    try:
        # Connect
        conn = serial.Serial(port, 115200, timeout=2.0)
        print(f"✅ Connected to {port}")
        
        # Test register reads
        addresses = {
            'PLCA_CTRL0': 0x0004CA01,
            'PLCA_CTRL1': 0x0004CA02,  
            'PLCA_STS': 0x0004CA03
        }
        
        results = {}
        
        for name, addr in addresses.items():
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
        print(f"\n📊 PLCA Analysis:")
        ctrl0 = results.get('PLCA_CTRL0', 0)
        ctrl1 = results.get('PLCA_CTRL1', 0)
        
        if ctrl0 and ctrl1:
            plca_enabled = bool(ctrl0 & 0x8000)
            node_id = ctrl1 & 0xFF
            node_count = (ctrl1 >> 8) & 0xFF
            
            print(f"  ⚡ PLCA Enabled: {plca_enabled}")
            print(f"  🏠 Node ID: {node_id}")  
            print(f"  🌐 Node Count: {node_count}")
            print(f"  📡 Role: {'Coordinator' if node_id == 0 else 'Follower'}")
        else:
            print(f"  ❌ Could not parse PLCA registers")
        
        conn.close()
        print(f"\n✅ Test completed")
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == '__main__':
    simple_plca_test()