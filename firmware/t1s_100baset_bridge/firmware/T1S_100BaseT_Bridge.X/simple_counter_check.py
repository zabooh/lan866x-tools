#!/usr/bin/env python3
"""
Simple T1S Counter Check - Direct Terminal Style
"""

import serial
import time

def simple_counter_check():
    print("🔍 Simple T1S Counter Check")
    print("=" * 40)
    
    try:
        ser = serial.Serial('COM8', 115200, timeout=3)
        print("✅ Connected to COM8 (Firmware Interface)")
        
        # Test basic connectivity
        print("\n📡 Sending simple command...")
        ser.write(b"\n")
        time.sleep(1)
        
        response = ser.read_all().decode('utf-8', errors='ignore')
        print(f"Response: {repr(response)}")
        
        # Try a direct register read
        print("\n📊 Testing register read...")
        ser.write(b"lan_read 0x00080000\n")
        time.sleep(2)
        
        response = ser.read_all().decode('utf-8', errors='ignore')
        print(f"Register response: {repr(response)}")
        
        ser.close()
        print("✅ Test complete")
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    simple_counter_check()