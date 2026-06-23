#!/usr/bin/env python3
"""
LAN8651 Simple MMD Test - Vereinfachter Test für indirekte Register-Zugriffe

Testet die wichtigsten MMD Indirect Access Fälle:
1. AN1760 Configuration Parameter
2. Direkt vs. Indirekt Vergleich
3. IEEE MMD Beispiele (einzeln)
"""

import argparse
import re
import serial  
import sys
import time
from datetime import datetime

class SimpleMMDTester:
    """Vereinfachter MMD Test für LAN8651"""
    
    def __init__(self, port: str = 'COM8', baudrate: int = 115200):
        self.port = port
        self.baudrate = baudrate  
        self.serial_conn = serial.Serial(port, baudrate, timeout=2.0)
        time.sleep(0.3)
        print(f"✅ Verbunden mit {self.port} @ {self.baudrate}")
        
        # MMD Registers
        self.MMD_ADDR_DATA = 0x000400D8
        self.MMD_CONTROL = 0x000400DA
        
    def send_cmd(self, cmd: str) -> str:
        """Kommando senden und alle Zeilen sammeln"""
        self.serial_conn.reset_input_buffer()
        self.serial_conn.write((cmd + '\r\n').encode())
        self.serial_conn.flush()
        
        # Sammle alle Zeilen mit Timeout
        lines = []
        end_time = time.time() + 1.5
        
        while time.time() < end_time:
            if self.serial_conn.in_waiting > 0:
                line = self.serial_conn.readline().decode('ascii', errors='ignore').strip()
                if line:
                    lines.append(line)
            else:
                time.sleep(0.01)
        
        return '\n'.join(lines)
    
    def lan_read(self, addr: int) -> int:
        """Register lesen mit Wert-Parsing"""
        response = self.send_cmd(f"lan_read 0x{addr:08X}")
        
        for line in response.split('\n'):
            if 'Value=' in line:
                match = re.search(r'Value=0x([0-9A-Fa-f]+)', line)
                if match:
                    return int(match.group(1), 16)
        
        print(f"❌ Read failed: {addr:08X}")
        return None
        
    def lan_write(self, addr: int, value: int) -> bool:
        """Register schreiben"""
        response = self.send_cmd(f"lan_write 0x{addr:08X} 0x{value:04X}")
        return "- OK" in response
    
    def indirect_read(self, addr: int, mask: int = 0xFFFF) -> int:
        """MMD Indirect Access"""
        print(f"  📖 Indirect: addr=0x{addr:04X}, mask=0x{mask:04X}")
        
        # Step 1: Write address
        if not self.lan_write(self.MMD_ADDR_DATA, addr):
            return None
        time.sleep(0.05)
        
        # Step 2: Trigger read
        if not self.lan_write(self.MMD_CONTROL, 0x0002):
            return None
        time.sleep(0.1)
        
        # Step 3: Read result
        result = self.lan_read(self.MMD_ADDR_DATA)
        if result is not None:
            masked = result & mask
            print(f"  ✅ Raw=0x{result:04X} → Masked=0x{masked:04X}")
            return masked
        
        return None
    
    def test_an1760_params(self):
        """Test AN1760 Configuration Parameter"""
        print("\n🧮 AN1760 Configuration Parameter Test")
        print("="*50)
        
        # Parameter 1
        print("📋 Parameter 1 (cfgparam1):")
        param1 = self.indirect_read(0x04, 0x1F)
        if param1 is not None:
            cfgparam1 = 0x1000 + (param1 * 0x10)
            print(f"  ✅ Raw: 0x{param1:02X} → cfgparam1: 0x{cfgparam1:04X}")
        else:
            print("  ❌ Failed")
            
        # Parameter 2
        print("\n📋 Parameter 2 (cfgparam2):")
        param2 = self.indirect_read(0x08, 0x1F)
        if param2 is not None:
            cfgparam2 = 0x2000 + (param2 * 0x08)
            print(f"  ✅ Raw: 0x{param2:02X} → cfgparam2: 0x{cfgparam2:04X}")
        else:
            print("  ❌ Failed")
    
    def test_comparison(self):
        """Direkt vs. Indirekt Vergleich"""
        print("\n⚖️  Direkt vs. Indirekt Vergleichstest")
        print("="*50)
        
        test_registers = {
            'OA_ID': 0x00000000,
            'CHIP_ID': 0x00000004,
            'OA_STATUS0': 0x00000008,
        }
        
        for name, addr in test_registers.items():
            print(f"\n📋 {name} (0x{addr:08X}):")
            
            # Direkt
            direct = self.lan_read(addr)
            print(f"  📖 Direkt: 0x{direct:08X}" if direct else "  ❌ Direkt: FAIL")
            
            # Indirekt (verschiedene Encodings)
            if direct:
                # Method 1: 16-bit address
                addr16 = addr & 0xFFFF
                indirect1 = self.indirect_read(addr16)
                if indirect1:
                    print(f"  🔄 Indirekt(16-bit): 0x{indirect1:04X} {'✅ MATCH' if (indirect1 & 0xFFFF) == (direct & 0xFFFF) else '⚠️  DIFF'}")
                
                # Method 2: 8-bit offset
                offset = addr & 0xFF
                indirect2 = self.indirect_read(offset)
                if indirect2:
                    print(f"  🔄 Indirekt(offset): 0x{indirect2:04X} {'✅ MATCH' if (indirect2 & 0xFF) == (direct & 0xFF) else '⚠️  DIFF'}")
    
    def test_ieee_mmd_sample(self):
        """IEEE MMD Beispiele (vereinfacht)"""  
        print("\n🔌 IEEE MMD Beispiele")
        print("="*50)
        
        # PMD Control mit verschiedenen Encodings
        print("📋 PMD Control Register:")
        encodings = [
            ('dev<<11_reg', (1 << 11) | 0),  # Device 1, Reg 0 
            ('reg_only', 0),
            ('dev_only', 1),
        ]
        
        for name, addr in encodings:
            result = self.indirect_read(addr)
            if result is not None:
                print(f"  ✅ {name}: 0x{result:04X}")
            else:
                print(f"  ❌ {name}: FAIL")
    
    def close(self):
        """Verbindung schließen"""
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.close()
            print("🔌 Verbindung geschlossen")


def main():
    parser = argparse.ArgumentParser(description="Simple LAN8651 MMD Test")
    parser.add_argument('--device', default='COM8', help='Serial device')
    parser.add_argument('--test', choices=['an1760', 'comparison', 'ieee', 'all'], 
                       default='all', help='Test type')
    
    args = parser.parse_args()
    
    tester = None
    try:
        tester = SimpleMMDTester(args.device)
        
        print(f"\n🧪 LAN8651 MMD Indirect Access Test")
        print(f"🕒 {datetime.now().strftime('%H:%M:%S')}")
        print(f"📡 MMD Registers: 0x{tester.MMD_ADDR_DATA:08X}, 0x{tester.MMD_CONTROL:08X}")
        
        if args.test in ['an1760', 'all']:
            tester.test_an1760_params()
            
        if args.test in ['comparison', 'all']:
            tester.test_comparison()
            
        if args.test in ['ieee', 'all']:
            tester.test_ieee_mmd_sample()
        
        print(f"\n🎉 Test abgeschlossen!")
        
    except Exception as e:
        print(f"❌ Fehler: {e}")
        return 1
    finally:
        if tester:
            tester.close()
    
    return 0

if __name__ == '__main__':
    sys.exit(main())