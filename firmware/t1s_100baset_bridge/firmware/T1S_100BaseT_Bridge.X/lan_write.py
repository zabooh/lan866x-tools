#!/usr/bin/env python3
"""
LAN865x Register Write Tool
Verwendung: python lan_write.py <address_hex> <value_hex>
Beispiel: python lan_write.py 0x00040000 0x12345678
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

def write_register(ser, reg_addr, value):
    """Schreibt Wert in Register über debugfs"""
    send_and_read(ser, f"echo '0x{reg_addr:08X} 0x{value:08X}' > /sys/kernel/debug/lan865x_eth0/register", 0.3)
    time.sleep(0.5)
    
    response = send_and_read(ser, "dmesg | tail -5 | grep 'Register write'", 0.2)
    
    # Prüfe auf erfolgreichen Write
    if f"0x{reg_addr:08X}" in response and f"0x{value:08X}" in response:
        return True
    else:
        return False

def main():
    if len(sys.argv) != 3:
        print("Usage: lan_write <address_hex> <value_hex>")
        print("Example: lan_write 0x00040000 0x12345678")
        sys.exit(1)
    
    try:
        # Parse Adresse und Wert
        addr_str = sys.argv[1]
        value_str = sys.argv[2]
        
        if addr_str.startswith('0x') or addr_str.startswith('0X'):
            reg_addr = int(addr_str, 16)
        else:
            reg_addr = int(addr_str, 16)
            
        if value_str.startswith('0x') or value_str.startswith('0X'):
            reg_value = int(value_str, 16)
        else:
            reg_value = int(value_str, 16)
        
        # COM9 verbinden
        ser = serial.Serial('COM9', 115200, timeout=2)
        time.sleep(0.5)
        
        # Register schreiben
        success = write_register(ser, reg_addr, reg_value)
        
        if success:
            print(f"Write OK: 0x{reg_addr:08X} = 0x{reg_value:08X}")
        else:
            print(f"Error: Could not write 0x{reg_value:08X} to register 0x{reg_addr:08X}")
            sys.exit(1)
        
        ser.close()
        
    except ValueError as e:
        print(f"Error: Invalid format - {e}")
        print("Use hex format like: lan_write 0x00040000 0x12345678")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()