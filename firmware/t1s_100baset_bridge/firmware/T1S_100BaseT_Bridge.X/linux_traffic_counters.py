#!/usr/bin/env python3
"""
Linux PHY Traffic Counter Check via COM9
"""
import serial
import time

def check_linux_traffic_counters():
    print("🔍 Linux PHY Traffic Counter Check")
    print("=" * 40)
    
    try:
        ser = serial.Serial('COM9', 115200, timeout=3)
        print("✅ Connected to Linux shell")
        
        # Clear any existing output
        ser.write(b"\n")
        time.sleep(1)
        ser.read_all()
        
        # Test different Linux traffic counter commands
        commands = [
            ("ip -s link show", "Interface Statistics"),
            ("ethtool -S eth0", "T1S PHY Statistics"),  
            ("ethtool -S eth1", "Ethernet PHY Statistics"),
            ("cat /proc/net/dev", "Network Device Stats"),
            ("ifconfig", "Interface Configuration + Counters"),
        ]
        
        for cmd, desc in commands:
            print(f"\n{'='*50}")
            print(f"📊 {desc}")
            print(f"Command: {cmd}")
            print(f"{'='*50}")
            
            # Send command
            ser.write(f"{cmd}\n".encode())
            time.sleep(2)  # Wait for output
            
            # Read response
            response = ser.read_all().decode('utf-8', errors='ignore')
            
            # Filter out command echo
            lines = response.split('\n')
            filtered_lines = []
            skip_echo = True
            
            for line in lines:
                if cmd in line and skip_echo:
                    skip_echo = False
                    continue
                if not skip_echo and line.strip():
                    filtered_lines.append(line)
            
            if filtered_lines:
                output = '\n'.join(filtered_lines[:20])  # Limit output
                print(output)
                if len(filtered_lines) > 20:
                    print(f"... ({len(filtered_lines)-20} more lines)")
            else:
                print("❌ No output or command failed")
            
            # Clear serial buffer
            time.sleep(0.5)
            ser.read_all()
        
        print(f"\n{'='*50}")
        print("🎯 SPECIFIC T1S COUNTERS")
        print(f"{'='*50}")
        
        # Look for T1S-specific counters
        t1s_commands = [
            "ethtool -S eth0 | grep -i 'rx\\|tx\\|frame\\|packet'",
            "ip -s -d link show eth0",
        ]
        
        for cmd in t1s_commands:
            print(f"\n📊 Command: {cmd}")
            ser.write(f"{cmd}\n".encode())
            time.sleep(1)
            response = ser.read_all().decode('utf-8', errors='ignore')
            
            # Clean response
            lines = response.split('\n')
            for line in lines:
                if line.strip() and cmd not in line and '#' not in line:
                    print(f"   {line}")
        
        ser.close()
        print("\n✅ Linux counter check complete")
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    check_linux_traffic_counters()