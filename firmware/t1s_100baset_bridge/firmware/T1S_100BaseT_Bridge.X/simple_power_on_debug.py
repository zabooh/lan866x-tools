#!/usr/bin/env python3
"""
Simple Power-On Sequence Debug Tool

Test basic hardware communication and build up from working patterns.
This will help us understand what register addresses actually work.
"""

import serial
import time
import re

class SimplePowerOnDebug:
    def __init__(self, port='COM8'):
        self.port = port
        self.serial_conn = None
    
    def connect(self):
        try:
            self.serial_conn = serial.Serial(port=self.port, baudrate=115200, timeout=3)
            time.sleep(0.2)
            print(f"✅ Connected to {self.port}")
            return True
        except Exception as e:
            print(f"❌ Connection failed: {e}")
            return False
    
    def send_command(self, cmd, wait_time=0.5):
        if not self.serial_conn:
            return ""
        
        try:
            self.serial_conn.reset_input_buffer()
            self.serial_conn.write((cmd + '\r\n').encode())
            self.serial_conn.flush()
            
            # Collect response
            response_lines = []
            end_time = time.time() + wait_time
            
            while time.time() < end_time:
                if self.serial_conn.in_waiting > 0:
                    line = self.serial_conn.readline().decode('ascii', errors='ignore').strip()
                    if line:
                        response_lines.append(line)
                else:
                    time.sleep(0.01)
            
            return '\n'.join(response_lines)
        except Exception as e:
            print(f"Command error: {e}")
            return ""
    
    def lan_read(self, address):
        cmd = f"lan_read 0x{address:08X}"
        response = self.send_command(cmd, 0.8)
        print(f"📖 lan_read 0x{address:08X}")
        print(f"   Response: {response}")
        
        # Try to parse value
        for line in response.split('\n'):
            if 'Value=' in line:
                match = re.search(r'Value=0x([0-9A-Fa-f]+)', line)
                if match:
                    value = int(match.group(1), 16)
                    print(f"   ✅ Parsed value: 0x{value:08X}")
                    return value
        
        print("   ❌ Could not parse value")
        return None
    
    def lan_write(self, address, value):
        cmd = f"lan_write 0x{address:08X} 0x{value:04X}"
        response = self.send_command(cmd, 0.8)
        print(f"✏️  lan_write 0x{address:08X} 0x{value:04X}")
        print(f"   Response: {response}")
        
        if "- OK" in response or "successful" in response.lower():
            print("   ✅ Write successful")
            return True
        else:
            print("   ❌ Write failed")
            return False
    
    def test_basic_registers(self):
        print("\n🔍 Testing Basic Register Communication")
        print("="*50)
        
        # Test common register addresses from our working tools
        test_registers = {
            'OA_ID': 0x00000000,
            'CHIP_ID': 0x00000004,  
            'OA_STATUS0': 0x00000008,
            'PHY_BASIC_STATUS': 0x0000FF01,
            'PHY_ID2': 0x0000FF03,
            'PLCA_CTRL0': 0x0004CA01,
            'PLCA_CTRL1': 0x0004CA02,
            'PMD_SQI': 0x00040083,
            'TEMPERATURE': 0x00040081,
            'VOLTAGE': 0x00040082,
        }
        
        working_reads = []
        
        for name, address in test_registers.items():
            print(f"\n--- Testing {name} ---")
            value = self.lan_read(address)
            if value is not None:
                working_reads.append((name, address, value))
        
        print(f"\n📊 Summary: {len(working_reads)}/{len(test_registers)} registers readable")
        for name, addr, val in working_reads:
            print(f"   ✅ {name}: 0x{addr:08X} = 0x{val:08X}")
        
        return working_reads
    
    def test_simple_write(self, address, value):
        print(f"\n✏️  Testing Write to 0x{address:08X}")
        print("="*50)
        
        # Read original value
        original = self.lan_read(address)
        if original is None:
            print("❌ Cannot read register - skip write test")
            return False
        
        # Write new value
        success = self.lan_write(address, value)
        if not success:
            return False
        
        # Read back
        readback = self.lan_read(address)
        if readback == value:
            print(f"✅ Write verified: 0x{readback:08X}")
            return True
        else:
            print(f"❌ Write verification failed: expected 0x{value:04X}, got 0x{readback:08X}")
            return False
    
    def run_power_on_debug(self):
        print("LAN8651 Power-On Sequence Debug Tool")
        print("="*50)
        
        if not self.connect():
            return
        
        try:
            # Test basic register reads
            working_regs = self.test_basic_registers()
            
            # Test a simple write on PLCA register (usually writable)
            if any(name == 'PLCA_CTRL0' for name, _, _ in working_regs):
                print(f"\n🧪 Testing Register Write Capability")
                self.test_simple_write(0x0004CA01, 0x0000)  # PLCA disable
                
        finally:
            if self.serial_conn:
                self.serial_conn.close()
                print("\n✅ Debug completed")

if __name__ == '__main__':
    debug = SimplePowerOnDebug()
    debug.run_power_on_debug()