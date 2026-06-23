#!/usr/bin/env python3
"""
Live Counter Monitor - Die wahren Counter während Traffic
"""
import serial
import time
import subprocess
import threading

def monitor_real_counters():
    print("🔍 Live T1S Counter Monitor")
    print("=" * 40)
    
    try:
        ser = serial.Serial('COM8', 115200, timeout=2)
        print("✅ Connected to firmware")
        
        # Key counter addresses in MMS 10
        counters = [
            (0x000A0000, "MMS10_STATUS"),
            (0x000A0010, "MMS10_BIG_COUNTER"),  
            (0x000A0020, "MMS10_SMALL_COUNTER"),
            (0x000A0004, "MMS10_ALT1"),
            (0x000A0014, "MMS10_ALT2"),
            (0x000A0024, "MMS10_ALT3"),
        ]
        
        print("\n📊 BASELINE (No Traffic):")
        baseline = {}
        for addr, name in counters:
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
            
            baseline[addr] = value
            if value is not None:
                print(f"📊 0x{addr:08X} {name:20} = {value:8}")
        
        print("\n🚀 STARTING TRAFFIC TEST...")
        print("   Running optimal ping: -s 1400 -i 0.001 for 10 seconds")
        
        # Start ping in background
        def run_ping():
            try:
                subprocess.run([
                    'ping', '-s', '1400', '-i', '0.001', '-c', '5000', '192.168.0.200'
                ], capture_output=True, text=True, timeout=15)
            except:
                pass
        
        ping_thread = threading.Thread(target=run_ping)
        ping_thread.daemon = True
        ping_thread.start()
        
        # Monitor for 10 seconds
        for second in range(1, 11):
            time.sleep(1)
            print(f"\n⏱️  AFTER {second} SECONDS:")
            
            for addr, name in counters:
                ser.write(f"lan_read 0x{addr:08X}\n".encode())
                time.sleep(0.2)
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
                
                if value is not None and baseline[addr] is not None:
                    change = value - baseline[addr]
                    if change > 0:
                        print(f"🔥 0x{addr:08X} {name:20} = {value:8} (+{change:4}) ← CHANGED!")
                    elif value != baseline[addr]:
                        print(f"🔄 0x{addr:08X} {name:20} = {value:8} ({change:+4})")
                    elif addr == 0x000A0010:  # Always show the big counter
                        print(f"📊 0x{addr:08X} {name:20} = {value:8}")
        
        ser.close()
        print("\n✅ Live monitoring complete")
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    monitor_real_counters()