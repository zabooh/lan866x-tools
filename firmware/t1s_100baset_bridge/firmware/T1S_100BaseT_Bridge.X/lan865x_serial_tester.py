#!/usr/bin/env python3
"""
LAN865x Register Access Tester via Serial Console
Windows COM Port Test Script

Usage: python lan865x_serial_tester.py
Connects to COM9 and tests lan_read/lan_write tools on target system.
"""

import serial
import time
import sys
import re
from datetime import datetime

class LAN865xSerialTester:
    def __init__(self, port="COM9", baudrate=115200, timeout=5):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.ser = None
        self.test_results = []
        
    def connect(self):
        """Establish serial connection"""
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                timeout=self.timeout,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE
            )
            print(f"✅ Connected to {self.port} @ {self.baudrate} baud")
            return True
        except Exception as e:
            print(f"❌ Failed to connect to {self.port}: {e}")
            return False
    
    def send_command(self, command, wait_time=2.0):
        """Send command and collect response"""
        if not self.ser:
            return "ERROR: Not connected"
        
        try:
            # Clear input buffer
            self.ser.read_all()
            time.sleep(0.1)
            
            # Send command
            self.ser.write((command + "\n").encode())
            self.ser.flush()
            
            # Wait and collect response
            time.sleep(wait_time)
            response = self.ser.read_all().decode('utf-8', errors='ignore')
            
            return response.strip()
        except Exception as e:
            return f"ERROR: {e}"
    
    def wait_for_prompt(self, timeout=10):
        """Wait for Linux prompt"""
        start_time = time.time()
        buffer = ""
        
        while time.time() - start_time < timeout:
            if self.ser.in_waiting:
                data = self.ser.read_all().decode('utf-8', errors='ignore')
                buffer += data
                if any(prompt in buffer for prompt in ['# ', '$ ', ':~ ', 'root@']):
                    return True
            time.sleep(0.1)
        return False
    
    def test_basic_system(self):
        """Test basic system functionality"""
        print("\n🔍 === BASIC SYSTEM TESTS ===")
        
        tests = [
            ("Kernel Version", "uname -a"),
            ("LAN Tools Available", "ls -la /usr/bin/lan_*"),
            ("ioctl Device Available", "ls -la /dev/lan865x_*"),
            ("debugfs Available", "ls -la /sys/kernel/debug/lan865x_* 2>/dev/null || echo 'debugfs not mounted'"),
            ("Ethernet Interface", "ip link show eth0 2>/dev/null || ifconfig eth0 2>/dev/null || echo 'Interface not found'")
        ]
        
        for test_name, command in tests:
            print(f"\n🧪 {test_name}")
            print(f"Command: {command}")
            response = self.send_command(command)
            print(f"Response: {response}")
            self.test_results.append((test_name, command, response))
    
    def test_register_access(self):
        """Test register read/write operations"""
        print("\n🎯 === REGISTER ACCESS TESTS ===")
        
        # Register read tests  
        read_tests = [
            ("MAC Network Control", "lan_read 0x00010000"),
            ("MAC Network Config", "lan_read 0x00010001"), 
            ("MAC Address Low", "lan_read 0x00010022"),
            ("MAC Address High", "lan_read 0x00010023"),
            ("Invalid Register", "lan_read 0xFFFFFFFF")
        ]
        
        for test_name, command in read_tests:
            print(f"\n📖 {test_name}")
            print(f"Command: {command}")
            response = self.send_command(command, wait_time=1.0)
            print(f"Response: {response}")
            self.test_results.append((test_name, command, response))
        
        # Register write/read test
        print(f"\n✍️ Register Write/Read Test")
        
        # Read initial value
        print("Step 1: Read initial MAC_NET_CTL")  
        initial = self.send_command("lan_read 0x00010000", wait_time=1.0)
        print(f"Initial: {initial}")
        
        # Write TX Enable (bit 3)
        print("Step 2: Set TX Enable (0x00000008)")
        write_resp = self.send_command("lan_write 0x00010000 0x00000008", wait_time=1.0) 
        print(f"Write: {write_resp}")
        
        # Read back
        print("Step 3: Read back value")
        readback = self.send_command("lan_read 0x00010000", wait_time=1.0)
        print(f"Readback: {readback}")
        
        # Write TX+RX Enable (bits 3+2)  
        print("Step 4: Set TX+RX Enable (0x0000000C)")
        write_resp2 = self.send_command("lan_write 0x00010000 0x0000000C", wait_time=1.0)
        print(f"Write: {write_resp2}")
        
        # Final read
        print("Step 5: Final read")
        final = self.send_command("lan_read 0x00010000", wait_time=1.0) 
        print(f"Final: {final}")
        
        self.test_results.extend([
            ("Initial MAC_NET_CTL Read", "lan_read 0x00010000", initial),
            ("TX Enable Write", "lan_write 0x00010000 0x00000008", write_resp),
            ("TX Enable Readback", "lan_read 0x00010000", readback),
            ("TX+RX Enable Write", "lan_write 0x00010000 0x0000000C", write_resp2),
            ("Final Read", "lan_read 0x00010000", final)
        ])
    
    def test_error_cases(self):
        """Test error handling and help"""
        print("\n❌ === ERROR HANDLING TESTS ===")
        
        error_tests = [
            ("Help - lan_read", "lan_read"),
            ("Help - lan_write", "lan_write"),  
            ("Invalid Arguments", "lan_read invalid_address"),
            ("Write without value", "lan_write 0x00010000")
        ]
        
        for test_name, command in error_tests:
            print(f"\n🔍 {test_name}")
            print(f"Command: {command}")
            response = self.send_command(command, wait_time=1.5)
            print(f"Response: {response}")
            self.test_results.append((test_name, command, response))
    
    def test_performance(self):
        """Basic performance test"""
        print("\n⚡ === PERFORMANCE TEST ===")
        
        print("Testing 5 consecutive reads...")
        start_time = time.time()
        
        for i in range(5):
            response = self.send_command("lan_read 0x00010000", wait_time=0.2)
            print(f"Read {i+1}: {response}")
        
        elapsed = time.time() - start_time
        avg_time = elapsed / 5
        print(f"\nTotal time: {elapsed:.3f}s, Average per read: {avg_time:.3f}s")
        
        if avg_time < 0.5:
            print("✅ FAST: ioctl interface likely working")
        else:
            print("⚠️ SLOW: might be using debugfs fallback") 
            
        self.test_results.append(("Performance Test", "5x lan_read", f"Avg: {avg_time:.3f}s/read"))
    
    def analyze_results(self):
        """Analyze test results and provide summary"""
        print("\n📊 === TEST RESULTS ANALYSIS ===")
        
        # Check if tools are available
        tools_found = any("lan_read" in result[2] and "lan_write" in result[2] 
                         for result in self.test_results if "LAN Tools" in result[0])
        
        # Check if ioctl device exists  
        ioctl_device = any("/dev/lan865x_" in result[2] 
                          for result in self.test_results if "ioctl Device" in result[0])
        
        # Check if register reads work
        register_works = any("0x" in result[2] and "=" in result[2] 
                            for result in self.test_results if "MAC Network Control" in result[0])
        
        # Check write consistency
        write_works = False
        for result in self.test_results:
            if "TX Enable Readback" in result[0] and "0x00000008" in result[2]:
                write_works = True
                break
        
        print(f"🔧 LAN Tools Available: {'✅ YES' if tools_found else '❌ NO'}")  
        print(f"🎯 ioctl Device Found: {'✅ YES' if ioctl_device else '❌ NO'}")
        print(f"📖 Register Reads Work: {'✅ YES' if register_works else '❌ NO'}")
        print(f"✍️ Register Writes Work: {'✅ YES' if write_works else '❌ NO'}")
        
        if tools_found and ioctl_device and register_works and write_works:
            print("\n🎉 SUCCESS: LAN865x ioctl interface is working perfectly!")
        elif tools_found and register_works:
            print("\n✅ PARTIAL: Tools work, possibly via debugfs fallback")
        else:
            print("\n❌ FAILED: LAN865x tools not working properly")
    
    def generate_report(self):
        """Generate detailed test report"""
        print("\n📋 === DETAILED TEST REPORT ===")
        print(f"Test Date: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        print(f"Port: {self.port} @ {self.baudrate} baud\n")
        
        for test_name, command, response in self.test_results:
            print(f"Test: {test_name}")
            print(f"Command: {command}")
            print(f"Response: {response}")
            print("-" * 60)
    
    def run_all_tests(self):
        """Run complete test suite"""
        print("🚀 LAN865x Register Access Test Suite")
        print("=" * 50)
        
        if not self.connect():
            return False
        
        try:
            # Wait for prompt
            print("⏳ Waiting for system prompt...")
            if not self.wait_for_prompt():
                print("⚠️ No prompt detected, continuing anyway...")
            
            # Send initial enter to get fresh prompt
            self.send_command("", wait_time=0.5)
            
            # Run test suites
            self.test_basic_system()
            self.test_register_access() 
            self.test_error_cases()
            self.test_performance()
            
            # Analysis
            self.analyze_results()
            self.generate_report()
            
            return True
            
        finally:
            if self.ser:
                self.ser.close()
                print(f"\n🔌 Disconnected from {self.port}")


def main():
    """Main function"""
    print("LAN865x Serial Tester for Windows")
    print("Make sure your target system is connected to COM9")
    print("Press Ctrl+C to abort\n")
    
    # Check if custom COM port specified
    if len(sys.argv) > 1:
        port = sys.argv[1] 
    else:
        port = "COM9"
    
    try:
        tester = LAN865xSerialTester(port=port)
        success = tester.run_all_tests()
        
        if success:
            print("\n✅ Test suite completed successfully!")
        else:
            print("\n❌ Test suite failed!")
            
    except KeyboardInterrupt:
        print("\n⚠️ Test aborted by user")
    except Exception as e:
        print(f"\n❌ Unexpected error: {e}")


if __name__ == "__main__":
    main()