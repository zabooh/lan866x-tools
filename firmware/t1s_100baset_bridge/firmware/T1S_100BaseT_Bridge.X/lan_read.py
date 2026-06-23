#!/usr/bin/env python3
"""
LAN865x Register Read Tool
Verwendung: python lan_read.py <address_hex>
Beispiel: python lan_read.py 0x00040000
"""

import serial
import time
import re
import sys

def send_and_read(ser, command, wait=0.3):
    """Sendet Befehl und liest Antwort"""
    ser.write((command + "\r\n").encode())
    time.sleep(wait)
    response = ""
    while ser.in_waiting > 0:
        response += ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
        time.sleep(0.1)
    return response

def read_register(ser, reg_addr):
    """Liest Register über debugfs"""
    send_and_read(ser, f"echo '0x{reg_addr:08X}' > /sys/kernel/debug/lan865x_eth0/register", 0.3)
    time.sleep(0.3)
    
    response = send_and_read(ser, "dmesg | tail -5 | grep 'Register read'", 0.2)
    
    # Finde letzten Read für unsere Adresse
    lines = response.split('\n')
    for line in reversed(lines):
        match = re.search(rf'Register read: 0x{reg_addr:08X} = 0x([0-9A-Fa-f]+)', line)
        if match:
            value = int(match.group(1), 16)
            return value
    
    return None

def main():
    if len(sys.argv) != 2:
        print("Usage: lan_read <address_hex>")
        print("Example: lan_read 0x00040000")
        sys.exit(1)
    
    try:
        # Parse Adresse
        addr_str = sys.argv[1]
        if addr_str.startswith('0x') or addr_str.startswith('0X'):
            reg_addr = int(addr_str, 16)
        else:
            reg_addr = int(addr_str, 16)
        
        # COM9 verbinden
        ser = serial.Serial('COM9', 115200, timeout=2)
        time.sleep(0.5)
        
        # Register lesen
        value = read_register(ser, reg_addr)
        
        if value is not None:
            print(f"0x{reg_addr:08X} = 0x{value:08X} ({value})")
        else:
            print(f"Error: Could not read register 0x{reg_addr:08X}")
            sys.exit(1)
        
        ser.close()
        
    except ValueError:
        print(f"Error: Invalid address format '{sys.argv[1]}'")
        print("Use hex format like 0x00040000")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()