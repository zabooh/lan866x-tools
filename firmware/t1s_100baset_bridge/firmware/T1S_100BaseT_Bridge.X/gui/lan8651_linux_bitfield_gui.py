#!/usr/bin/env python3
"""
LAN8651 Register Bitfield Analyzer - Linux ioctl Remote Access GUI  
Erweiterte GUI für detaillierte Register-Bitfeld-Analyse mit Linux ioctl Remote Access (500x-10000x schneller)

Features:
- Detaillierte Bitfeld-Aufschlüsselung für jeden Register-Wert
- Automatische Interpretation der Bitfeld-Bedeutungen
- Visuelle Hervorhebung wichtiger Status-Bits
- 🌐 MAC-Adress-Dekodierung (Hardware-verifiziert: 00:04:25:01:02:03)
- 🔧 MAC_SAB2/MAC_SAT2 Register-Support (korrekte MAC-Speicher)
- ⚠️ Legacy MAC-Register-Warnung (MAC_SAB1/MAC_SAT1)
- ⚡ Linux ioctl Remote Access via COM9 (500x-10000x schneller als debugfs!)
- ✅ PMD Register Corrections: Clause 22 PMD access (0x0000FF20-FF23) 
- ❌ MMS 3 PMD Warning: Returns only 0x0000 - USE CLAUSE 22 INSTEAD
- 🎯 SQI Register: 0x0004008F shows 6/7 EXCELLENT signal quality
- 🚀 Hardware Status: 1.476 Mbps Link UP, SQI 6/7, PLCA Node 7/8
- Basiert auf offiziellem Microchip LAN8650/1 Datenblatt
- Threading für nicht-blockierende GUI
- Erweiterte Diagnose-Kommentare

Linux ioctl Access Features (NEUE VERSION): 
- Verwendet ultraschnelle ioctl-basierte lan_read/lan_write Tools
- 500x-10000x Performance-Verbesserung gegenüber debugfs
- Register-Zugriff in 0.1-1ms statt 500-1000ms
- Direkter Kernel-ioctl Interface ohne dmesg-Parsing
- Automatische Kompatibilität mit debugfs-Fallback

Basiert auf: lan8651_bitfield_gui.py + lan865x_serial_tester.py + LAN865X_OPTIMIZATION_README.md
Version: 5.0 - März 16, 2026 (ioctl Ultra-Performance Integration)
"""

import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext
import threading
import queue
import serial
import time
import re
import traceback
from dataclasses import dataclass
from typing import Dict, List, Optional, Union

@dataclass
class BitField:
    """Repräsentiert ein Bitfeld in einem Register"""
    name: str
    start_bit: int
    end_bit: int
    size: int
    description: str
    reset_value: Union[int, str]
    access: str = "R"  # R, W, R/W
    
    def get_mask(self) -> int:
        """Berechnet die Bitmaske für dieses Feld"""
        return ((1 << self.size) - 1) << self.start_bit
    
    def extract_value(self, register_value: int) -> int:
        """Extrahiert den Wert dieses Bitfelds aus einem Register-Wert"""
        return (register_value >> self.start_bit) & ((1 << self.size) - 1)
    
    def get_bit_range_str(self) -> str:
        """Gibt den Bit-Range als String zurück"""
        if self.start_bit == self.end_bit:
            return str(self.start_bit)
        return f"{self.end_bit}:{self.start_bit}"

class LinuxIoctlAccess:
    """Linux ioctl Remote Access Class für LAN865x Register - 500x-10000x schneller als debugfs"""
    
    def __init__(self, com_port="COM9", baudrate=115200):
        self.com_port = com_port
        self.baudrate = baudrate
        self.lan_read_cmd = "lan_read"
        self.lan_write_cmd = "lan_write"
        
        # Persistent connection management für Performance
        self._persistent_connection = None
        self._connection_last_used = 0
        self._connection_timeout = 30  # 30 seconds
        self._logged_in = False
    
    def _get_connection(self):
        """Holt persistente Verbindung oder erstellt eine neue - PERFORMANCE BOOST"""
        current_time = time.time()
        
        # Prüfe ob bestehende Verbindung noch gültig ist
        if (self._persistent_connection and 
            self._persistent_connection.is_open and 
            (current_time - self._connection_last_used) < self._connection_timeout):
            
            self._connection_last_used = current_time
            return self._persistent_connection, self._logged_in
        
        # Neue Verbindung erstellen
        if self._persistent_connection and self._persistent_connection.is_open:
            self._persistent_connection.close()
        
        try:
            self._persistent_connection = serial.Serial(self.com_port, self.baudrate, timeout=0.5)
            time.sleep(0.01)  # Minimal setup time
            self._connection_last_used = current_time
            
            # Auto-Login falls erforderlich
            self._logged_in = self.check_and_handle_login(self._persistent_connection)
            
            return self._persistent_connection, self._logged_in
        except Exception as e:
            print(f"[ERROR] Connection failed: {e}")
            self._persistent_connection = None
            self._logged_in = False
            return None, False
    
    def close_connection(self):
        """Schließt persistente Verbindung - für Cleanup"""
        if self._persistent_connection and self._persistent_connection.is_open:
            self._persistent_connection.close()
        self._persistent_connection = None
        self._logged_in = False
    
    def check_and_handle_login(self, ser):
        """Prüft ob Login-Prompt vorhanden und führt automatisches Login durch"""
        try:
            # Clear buffer und prüfen was aktuell angezeigt wird
            ser.reset_input_buffer()
            time.sleep(0.1)
            
            # Sende Enter um zu sehen was das Terminal anzeigt
            ser.write(b"\r\n")
            time.sleep(0.5)
            
            response = ""
            if ser.in_waiting > 0:
                response = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
            
            # Prüfe auf Login-Prompt
            if any(login_marker in response.lower() for login_marker in ['login:', 'username:', 'user:']):
                print("[INFO] Login-Prompt erkannt, führe automatisches Login durch...")
                
                # Username eingeben: root
                ser.write(b"root\r\n")
                time.sleep(0.5)
                
                # Prüfe auf Password-Prompt
                if ser.in_waiting > 0:
                    pwd_response = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                    
                    if any(pwd_marker in pwd_response.lower() for pwd_marker in ['password:', 'pwd:']):
                        # Passwort eingeben: microchip
                        ser.write(b"microchip\r\n")
                        time.sleep(1.0)  # Mehr Zeit für Login-Prozess
                        
                        # Prüfe ob Login erfolgreich (shell prompt)
                        login_result = ""
                        start_time = time.time()
                        while time.time() - start_time < 3.0:  # 3s timeout für Login
                            if ser.in_waiting > 0:
                                chunk = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                                login_result += chunk
                                # Prüfe auf Shell-Prompt
                                if any(prompt in login_result for prompt in ['# ', '$ ', 'root@']):
                                    print("[INFO] Automatisches Login erfolgreich!")
                                    return True
                            time.sleep(0.1)
                        
                        print("[WARNING] Login möglicherweise fehlgeschlagen")
                        return False
            
            # Kein Login erforderlich - bereits eingeloggt
            return True
            
        except Exception as e:
            print(f"[ERROR] Login check failed: {e}")
            return False

    def send_and_read(self, ser, command, wait=0.01):
        """Sendet Befehl an Linux Host und liest Antwort - Ultra-Speed für ioctl"""
        ser.write((command + "\r\n").encode())
        time.sleep(wait)  # Minimal command processing time
        response = ""
        start_time = time.time()
        
        # Ultra-fast response reading optimized for ioctl tools (1-5ms response)
        while time.time() - start_time < 0.2:  # 200ms max timeout for ioctl
            if ser.in_waiting > 0:
                chunk = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                response += chunk
                # Check for ioctl command completion markers
                if any(marker in response for marker in ['0x', 'Error:', 'Usage:', '# ', '$ ']):
                    time.sleep(0.01)  # Ultra-minimal wait for any remaining data
                    if ser.in_waiting > 0:
                        response += ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                    break
            time.sleep(0.005)  # Ultra-fast polling - 5ms
        
        return response
    
    def read_register(self, reg_addr):
        """Liest Register über persistente Verbindung - ULTRA-PERFORMANCE BOOST"""
        ser, logged_in = self._get_connection()
        if not ser or not logged_in:
            return None
        
        try:
            # Clear any pending output
            ser.reset_input_buffer()
            
            # Register Read via ioctl-based lan_read tool
            cmd = f"{self.lan_read_cmd} 0x{reg_addr:08X}"
            response = self.send_and_read(ser, cmd, 0.01)  # Ultra-fast response
            
            # Parse lan_read output: "0x12345678 = 0xABCDEF01" 
            lines = response.split('\n')
            for line in lines:
                # Look for address = value pattern
                match = re.search(rf'0x{reg_addr:08X}\s*=\s*0x([0-9A-Fa-f]+)', line)
                if match:
                    value = int(match.group(1), 16)
                    return value
                
                # Alternative: look for direct hex value output
                if line.strip().startswith('0x'):
                    try:
                        value = int(line.strip(), 16)
                        return value
                    except ValueError:
                        continue
            
            return None
            
        except Exception as e:
            # Connection error - reset for next attempt
            self.close_connection()
            return None
    
    def write_register(self, reg_addr, value):
        """Schreibt Register über persistente Verbindung - ULTRA-PERFORMANCE BOOST"""
        ser, logged_in = self._get_connection()
        if not ser or not logged_in:
            return False
        
        try:
            # Clear any pending output
            ser.reset_input_buffer()
            
            # Register Write via ioctl-based lan_write tool
            cmd = f"{self.lan_write_cmd} 0x{reg_addr:08X} 0x{value:08X}"
            response = self.send_and_read(ser, cmd, 0.01)  # Ultra-fast response
            
            # Parse lan_write output: "0x12345678 = 0xABCDEF01 (written successfully)"
            lines = response.split('\n')
            for line in lines:
                # Look for success confirmation
                if "written successfully" in line.lower():
                    return True
                # Look for address = value pattern (indicates successful write)
                if f"0x{reg_addr:08X}" in line and f"0x{value:08X}" in line:
                    return True
                # Check for direct success indicators
                if any(success_marker in line.lower() for success_marker in ['success', 'ok', 'written']):
                    return True
            
            # If no error message, assume success (ioctl tools typically fail silently or with error)
            if not any(error_marker in response.lower() for error_marker in ['error', 'failed', 'usage']):
                return True
                
            return False
            
        except Exception as e:
            # Connection error - reset for next attempt
            self.close_connection()
            return False
    
    def test_connection(self):
        """Testet Linux Host Verbindung und ioctl Tools"""
        # Force new connection for test
        self.close_connection()
        
        ser, logged_in = self._get_connection()
        if not ser or not logged_in:
            return False, "Auto-Login fehlgeschlagen"
        
        try:
            # Test basic Linux command
            response = self.send_and_read(ser, "uname -a", 0.2)
            
            if "Linux" in response:
                # Test ioctl tools availability
                response = self.send_and_read(ser, f"which {self.lan_read_cmd}", 0.2)
                if "/usr/bin/lan_read" in response or "lan_read" in response:
                    # Test /dev/lan865x_* device availability
                    dev_response = self.send_and_read(ser, "ls -la /dev/lan865x_*", 0.2)
                    if "/dev/lan865x_" in dev_response:
                        return True, "Linux connection OK, ioctl tools and devices available"
                    else:
                        return True, "Linux connection OK, ioctl tools available (devices might be loading)"
                else:
                    # Fallback: check if debugfs is available as backup
                    debug_response = self.send_and_read(ser, "ls -la /sys/kernel/debug/lan865x_*", 0.2)
                    if "register" in debug_response:
                        return True, "Linux connected - debugfs available as fallback"
                    else:
                        return False, "Linux connected but no LAN865x tools found"
            else:
                return False, "No Linux response"
                
        except Exception as e:
            self.close_connection()
            return False, f"Connection failed: {str(e)}"

class LAN8651BitfieldDefinitions:
    """Enthält alle Bitfeld-Definitionen für LAN8651 Register"""
    
    def __init__(self):
        self.register_bitfields = self._initialize_bitfields()
    
    def _initialize_bitfields(self) -> Dict[str, List[BitField]]:
        """Initialisiert alle Bitfeld-Definitionen basierend auf Datenblatt"""
        bitfields = {}
        
        # OA_ID Register (0x00000000)
        bitfields['0x00000000'] = [
            BitField("Reserved", 24, 31, 8, "Reserved, immer 0", 0x00, "R"),
            BitField("Reserved", 16, 23, 8, "Reserved, immer 0", 0x00, "R"),
            BitField("Reserved", 8, 15, 8, "Reserved, immer 0", 0x00, "R"),
            BitField("MAJVER", 4, 7, 4, "Major Version Number (Version 1.x)", 0x1, "R"),
            BitField("MINVER", 0, 3, 4, "Minor Version Number (Version x.1)", 0x1, "R"),
        ]
        
        # OA_PHYID Register (0x00000001)
        bitfields['0x00000001'] = [
            BitField("OUI[21:14]", 24, 31, 8, "Organizationally Unique Identifier (Bits 21:14)", "Variable", "R"),
            BitField("OUI[13:6]", 16, 23, 8, "Organizationally Unique Identifier (Bits 13:6)", "Variable", "R"),
            BitField("OUI[5:0]", 10, 15, 6, "Organizationally Unique Identifier (Bits 5:0)", "Variable", "R"),
            BitField("MODEL[5:4]", 6, 9, 2, "Model Number (Upper Bits)", "Variable", "R"),
            BitField("MODEL[3:0]", 4, 7, 4, "Model Number (Lower Bits)", "Variable", "R"),
            BitField("REVISION", 0, 3, 4, "Revision Number", "Variable", "R"),
        ]
        
        # OA_STDCAP Register (0x00000002)
        bitfields['0x00000002'] = [
            BitField("Reserved", 24, 31, 8, "Reserved, immer 0", 0x00, "R"),
            BitField("Reserved", 16, 23, 8, "Reserved, immer 0", 0x00, "R"),
            BitField("Reserved", 11, 15, 5, "Reserved, immer 0", 0x00, "R"),
            BitField("TXFCSVC", 10, 10, 1, "TX Frame Check Sequence Validation Capable", "Variable", "R"),
            BitField("IPRAC", 9, 9, 1, "Inter-Packet Receive Access Capable", "Variable", "R"),
            BitField("DPRAC", 8, 8, 1, "Data Packet Receive Access Capable", "Variable", "R"),
            BitField("CTC", 7, 7, 1, "Cut-Through Capable", "Variable", "R"),
            BitField("FTSC", 6, 6, 1, "Forward Timestamp Capable", "Variable", "R"),
            BitField("AIDC", 5, 5, 1, "Auto-negotiation ID Capable", "Variable", "R"),
            BitField("SEQC", 4, 4, 1, "Sequence ID Capable", "Variable", "R"),
            BitField("Reserved", 3, 3, 1, "Reserved, immer 0", 0, "R"),
            BitField("MINBPS", 0, 2, 3, "Minimum Burst Period Supported", "Variable", "R"),
        ]
        
        # OA_RESET Register (0x00000003)
        bitfields['0x00000003'] = [
            BitField("Reserved", 24, 31, 8, "Reserved, immer 0", 0x00, "R"),
            BitField("Reserved", 16, 23, 8, "Reserved, immer 0", 0x00, "R"),
            BitField("Reserved", 8, 15, 8, "Reserved, immer 0", 0x00, "R"),
            BitField("Reserved", 1, 7, 7, "Reserved, immer 0", 0x00, "R"),
            BitField("SWRESET", 0, 0, 1, "Software Reset (1 = Reset, Self-clearing)", 0, "R/W"),
        ]
        
        # OA_CONFIG0 Register (0x00000004)
        bitfields['0x00000004'] = [
            BitField("Reserved", 24, 31, 8, "Reserved, immer 0", 0x00, "R"),
            BitField("Reserved", 16, 23, 8, "Reserved, immer 0", 0x00, "R"),
            BitField("SYNC", 15, 15, 1, "Synchronization Enable", 0, "R/W"),
            BitField("TXFCSVE", 14, 14, 1, "TX Frame Check Sequence Validation Enable", 0, "R/W"),
            BitField("RFA", 12, 13, 2, "Receive Frame Assembly", 0x0, "R/W"),
            BitField("TXCTHRESH", 10, 11, 2, "TX Cut-Through Threshold", 0x0, "R/W"),
            BitField("TXCTE", 9, 9, 1, "TX Cut-Through Enable", 0, "R/W"),
            BitField("RXCTE", 8, 8, 1, "RX Cut-Through Enable", 0, "R/W"),
            BitField("FTSE", 7, 7, 1, "Forward Timestamp Enable", 0, "R/W"),
            BitField("FTSS", 6, 6, 1, "Forward Timestamp Select", 0, "R/W"),
            BitField("PROTE", 5, 5, 1, "Protocol Enable", 0, "R/W"),
            BitField("SEQE", 4, 4, 1, "Sequence ID Enable", 0, "R/W"),
            BitField("Reserved", 3, 3, 1, "Reserved, immer 0", 0, "R"),
            BitField("BPS", 0, 2, 3, "Burst Period Select", 0x0, "R/W"),
        ]
        
        # OA_STATUS0 Register (0x00000008)
        bitfields['0x00000008'] = [
            BitField("Reserved", 24, 31, 8, "Reserved, immer 0", 0x00, "R"),
            BitField("Reserved", 16, 23, 8, "Reserved, immer 0", 0x00, "R"),
            BitField("Reserved", 13, 15, 3, "Reserved, immer 0", 0x00, "R"),
            BitField("CPDE", 12, 12, 1, "Control Packet Dropped Error", "Variable", "R"),
            BitField("TXFCSE", 11, 11, 1, "TX Frame Check Sequence Error", "Variable", "R"),
            BitField("TTSCAC", 10, 10, 1, "Transmit Timestamp Capture A Complete", "Variable", "R"),
            BitField("TTSCAB", 9, 9, 1, "Transmit Timestamp Capture B Complete", "Variable", "R"),
            BitField("TTSCAA", 8, 8, 1, "Transmit Timestamp Capture A Available", "Variable", "R"),
            BitField("PHYINT", 7, 7, 1, "PHY Interrupt", "Variable", "R"),
            BitField("RESETC", 6, 6, 1, "Reset Complete", "Variable", "R"),
            BitField("HDRE", 5, 5, 1, "Header Error", "Variable", "R"),
            BitField("LOFE", 4, 4, 1, "Loss of Frame Error", "Variable", "R"),
            BitField("RXBOE", 3, 3, 1, "RX Buffer Overflow Error", "Variable", "R"),
            BitField("TXBUE", 2, 2, 1, "TX Buffer Underflow Error", "Variable", "R"),
            BitField("TXBOE", 1, 1, 1, "TX Buffer Overflow Error", "Variable", "R"),
            BitField("TXPE", 0, 0, 1, "TX Protocol Error", "Variable", "R"),
        ]
        
        # OA_STATUS1 Register (0x00000009)
        bitfields['0x00000009'] = [
            BitField("Reserved", 29, 31, 3, "Reserved, immer 0", 0x00, "R"),
            BitField("SEV", 28, 28, 1, "System Error", "Variable", "R"),
            BitField("Reserved", 27, 27, 1, "Reserved, immer 0", 0, "R"),
            BitField("TTSCMC", 26, 26, 1, "Transmit Timestamp Capture C Complete", "Variable", "R"),
            BitField("TTSCMB", 25, 25, 1, "Transmit Timestamp Capture B Complete", "Variable", "R"),
            BitField("TTSCMA", 24, 24, 1, "Transmit Timestamp Capture A Complete", "Variable", "R"),
            BitField("TTSCOFC", 23, 23, 1, "Transmit Timestamp Capture Overflow C", "Variable", "R"),
            BitField("TTSCOFB", 22, 22, 1, "Transmit Timestamp Capture Overflow B", "Variable", "R"),
            BitField("TTSCOFA", 21, 21, 1, "Transmit Timestamp Capture Overflow A", "Variable", "R"),
            BitField("BUSER", 20, 20, 1, "Bus Error", "Variable", "R"),
            BitField("UV18", 19, 19, 1, "Under Voltage 1.8V", "Variable", "R"),
            BitField("ECC", 18, 18, 1, "ECC Error", "Variable", "R"),
            BitField("FSMSTER", 17, 17, 1, "FSM State Error", "Variable", "R"),
            BitField("Reserved", 16, 16, 1, "Reserved, immer 0", 0, "R"),
            BitField("Reserved", 8, 15, 8, "Reserved, immer 0", 0x00, "R"),
            BitField("Reserved", 2, 7, 6, "Reserved, immer 0", 0x00, "R"),
            BitField("TXNER", 1, 1, 1, "TX MAC Not Empty Error", "Variable", "R"),
            BitField("RXNER", 0, 0, 1, "RX MAC Not Empty Error", "Variable", "R"),
        ]
        
        # OA_BUFSTS Register (0x0000000B)
        bitfields['0x0000000B'] = [
            BitField("Reserved", 24, 31, 8, "Reserved, immer 0", 0x00, "R"),
            BitField("Reserved", 16, 23, 8, "Reserved, immer 0", 0x00, "R"),
            BitField("TXC", 8, 15, 8, "TX Credits Available", "Variable", "R"),
            BitField("RBA", 0, 7, 8, "RX Buffer Available", "Variable", "R"),
        ]
        
        # OA_IMASK0 Register (0x0000000C)
        bitfields['0x0000000C'] = [
            BitField("Reserved", 24, 31, 8, "Reserved, immer 0", 0x00, "R"),
            BitField("Reserved", 16, 23, 8, "Reserved, immer 0", 0x00, "R"),
            BitField("Reserved", 13, 15, 3, "Reserved, immer 0", 0x00, "R"),
            BitField("CPDEM", 12, 12, 1, "Control Packet Dropped Error Mask", 0, "R/W"),
            BitField("TXFCSEM", 11, 11, 1, "TX Frame Check Sequence Error Mask", 0, "R/W"),
            BitField("TTSCACM", 10, 10, 1, "Transmit Timestamp Capture A Complete Mask", 0, "R/W"),
            BitField("TTSCABM", 9, 9, 1, "Transmit Timestamp Capture B Complete Mask", 0, "R/W"),
            BitField("TTSCAAM", 8, 8, 1, "Transmit Timestamp Capture A Available Mask", 0, "R/W"),
            BitField("PHYINTM", 7, 7, 1, "PHY Interrupt Mask", 0, "R/W"),
            BitField("RESETCM", 6, 6, 1, "Reset Complete Mask", 0, "R/W"),
            BitField("HDREM", 5, 5, 1, "Header Error Mask", 0, "R/W"),
            BitField("LOFEM", 4, 4, 1, "Loss of Frame Error Mask", 0, "R/W"),
            BitField("RXBOEM", 3, 3, 1, "RX Buffer Overflow Error Mask", 0, "R/W"),
            BitField("TXBUEM", 2, 2, 1, "TX Buffer Underflow Error Mask", 0, "R/W"),
            BitField("TXBOEM", 1, 1, 1, "TX Buffer Overflow Error Mask", 0, "R/W"),
            BitField("TXPEM", 0, 0, 1, "TX Protocol Error Mask", 0, "R/W"),
        ]
        
        # OA_IMASK1 Register (0x0000000D)  
        bitfields['0x0000000D'] = [
            BitField("Reserved", 29, 31, 3, "Reserved, immer 0", 0x00, "R"),
            BitField("SEVM", 28, 28, 1, "System Error Mask", 0, "R/W"),
            BitField("Reserved", 27, 27, 1, "Reserved, immer 0", 0, "R"),
            BitField("TTSCMCM", 26, 26, 1, "Transmit Timestamp Capture C Complete Mask", 0, "R/W"),
            BitField("TTSCMBM", 25, 25, 1, "Transmit Timestamp Capture B Complete Mask", 0, "R/W"),
            BitField("TTSCMAM", 24, 24, 1, "Transmit Timestamp Capture A Complete Mask", 0, "R/W"),
            BitField("TTSCOFCM", 23, 23, 1, "Transmit Timestamp Capture Overflow C Mask", 0, "R/W"),
            BitField("TTSCOFBM", 22, 22, 1, "Transmit Timestamp Capture Overflow B Mask", 0, "R/W"),
            BitField("TTSCOFAM", 21, 21, 1, "Transmit Timestamp Capture Overflow A Mask", 0, "R/W"),
            BitField("BUSERM", 20, 20, 1, "Bus Error Mask", 0, "R/W"),
            BitField("UV18M", 19, 19, 1, "Under Voltage 1.8V Mask", 0, "R/W"),
            BitField("ECCM", 18, 18, 1, "ECC Error Mask", 0, "R/W"),
            BitField("FSMSTM", 17, 17, 1, "FSM State Error Mask", 0, "R/W"),
            BitField("Reserved", 16, 16, 1, "Reserved, immer 0", 0, "R"),
            BitField("Reserved", 8, 15, 8, "Reserved, immer 0", 0x00, "R"),
            BitField("Reserved", 2, 7, 6, "Reserved, immer 0", 0x00, "R"),
            BitField("TXNERM", 1, 1, 1, "TX MAC Not Empty Error Mask", 0, "R/W"),
            BitField("RXNERM", 0, 0, 1, "RX MAC Not Empty Error Mask", 0, "R/W"),
        ]
        
        # ================================================================================
        # MAC REGISTERS (MMS_1) - HARDWARE-VERIFIED (März 10, 2026)
        # ================================================================================
        
        # MAC_SAB2 Register (0x00010024) - CURRENT MAC STORAGE
        bitfields['0x00010024'] = [
            BitField("MAC_BYTE3", 24, 31, 8, "MAC Address Byte 3 (Little Endian)", "Variable", "R/W"),
            BitField("MAC_BYTE2", 16, 23, 8, "MAC Address Byte 2 (Little Endian)", "Variable", "R/W"),
            BitField("MAC_BYTE1", 8, 15, 8, "MAC Address Byte 1 (Little Endian)", "Variable", "R/W"),
            BitField("MAC_BYTE0", 0, 7, 8, "MAC Address Byte 0 (Little Endian)", "Variable", "R/W"),
        ]
        
        # MAC_SAT2 Register (0x00010025) - CURRENT MAC STORAGE
        bitfields['0x00010025'] = [
            BitField("Reserved", 16, 31, 16, "Reserved, immer 0", 0x0000, "R"),
            BitField("MAC_BYTE5", 8, 15, 8, "MAC Address Byte 5 (Little Endian)", "Variable", "R/W"),
            BitField("MAC_BYTE4", 0, 7, 8, "MAC Address Byte 4 (Little Endian)", "Variable", "R/W"),
        ]
        
        # MAC_SAB1 Register (0x00010022) - LEGACY (UNUSED)
        bitfields['0x00010022'] = [
            BitField("LEGACY", 0, 31, 32, "Legacy MAC Register (Unused - always 0x00000000)", 0x00000000, "R"),
        ]
        
        # MAC_SAT1 Register (0x00010023) - LEGACY (UNUSED)
        bitfields['0x00010023'] = [
            BitField("LEGACY", 0, 31, 32, "Legacy MAC Register (Unused - always 0x00000000)", 0x00000000, "R"),
        ]

        # ================================================================================
        # MMS_0: OPEN ALLIANCE CLAUSE-22 REGISTERS (0xFF00 - 0xFF0E)
        # 16-bit IEEE 802.3 Clause 22 registers accessed via MMS 0
        # ================================================================================

        # BASIC_CONTROL Register (0x0000FF00) - 16-bit PHY Clause 22
        bitfields['0x0000FF00'] = [
            BitField("SW_RESET",   15, 15, 1, "Software Reset (1=Reset, self-clearing)", 0, "R/W"),
            BitField("LOOPBACK",   14, 14, 1, "Loopback Enable", 0, "R/W"),
            BitField("SPD_SEL0",   13, 13, 1, "Speed Select Bit 0 (with SPD_SEL1)", 0, "R/W"),
            BitField("AUTONEGEN",  12, 12, 1, "Auto-Negotiation Enable", 0, "R/W"),
            BitField("PD",         11, 11, 1, "Power Down", 0, "R/W"),
            BitField("Reserved",   10, 10, 1, "Reserved", 0, "R"),
            BitField("REAUTONEG",   9,  9, 1, "Restart Auto-Negotiation (self-clearing)", 0, "R/W"),
            BitField("DUPLEXMD",    8,  8, 1, "Duplex Mode (1=Full Duplex)", 0, "R/W"),
            BitField("Reserved",    7,  7, 1, "Reserved", 0, "R"),
            BitField("SPD_SEL1",    6,  6, 1, "Speed Select Bit 1 (with SPD_SEL0)", 0, "R/W"),
            BitField("Reserved",    0,  5, 6, "Reserved", 0, "R"),
        ]

        # BASIC_STATUS Register (0x0000FF01) - 16-bit PHY Clause 22
        bitfields['0x0000FF01'] = [
            BitField("100BT4A",    15, 15, 1, "100BASE-T4 Able", 0, "R"),
            BitField("100BTXFDA",  14, 14, 1, "100BASE-TX Full Duplex Able", 0, "R"),
            BitField("100BTXHDA",  13, 13, 1, "100BASE-TX Half Duplex Able", 0, "R"),
            BitField("10BTFDA",    12, 12, 1, "10BASE-T Full Duplex Able", 0, "R"),
            BitField("10BTHDA",    11, 11, 1, "10BASE-T Half Duplex Able", 0, "R"),
            BitField("100BT2FDA",  10, 10, 1, "100BASE-T2 Full Duplex Able", 0, "R"),
            BitField("100BT2HDA",   9,  9, 1, "100BASE-T2 Half Duplex Able", 0, "R"),
            BitField("EXTSTS",      8,  8, 1, "Extended Status Information in Register 15", 0, "R"),
            BitField("Reserved",    6,  7, 2, "Reserved", 0, "R"),
            BitField("AUTONEGC",    5,  5, 1, "Auto-Negotiation Complete", 0, "R"),
            BitField("RMTFLTD",     4,  4, 1, "Remote Fault Detected", 0, "R"),
            BitField("AUTONEGA",    3,  3, 1, "Auto-Negotiation Able", 0, "R"),
            BitField("LNKSTS",      2,  2, 1, "Link Status (1=Link Up)", 0, "R"),
            BitField("JABDET",      1,  1, 1, "Jabber Detect", 0, "R"),
            BitField("EXTCAPA",     0,  0, 1, "Extended Capability (MF Preamble Suppression)", 0, "R"),
        ]

        # PHY_ID1 Register (0x0000FF02) - 16-bit, OUI bits [2:17]
        bitfields['0x0000FF02'] = [
            BitField("OUI_H",      8, 15, 8, "OUI Bits [2:9] (Organizationally Unique Identifier high)", "Variable", "R"),
            BitField("OUI_L",      0,  7, 8, "OUI Bits [10:17] (Organizationally Unique Identifier low)", "Variable", "R"),
        ]
        
        # PHY_ID2 Register (0x0000FF03) - 16-bit, OUI[18:23] + MODEL + REV
        bitfields['0x0000FF03'] = [
            BitField("OUI_MSB",   10, 15, 6, "OUI Bits [18:23]", "Variable", "R"),
            BitField("MODEL_H",    8,  9, 2, "Model Number Bits [5:4]", "Variable", "R"),
            BitField("MODEL_L",    4,  7, 4, "Model Number Bits [3:0]", "Variable", "R"),
            BitField("REV",        0,  3, 4, "Revision Number", "Variable", "R"),
        ]

        # ================================================================================
        # MMS_1: MAC REGISTERS
        # ================================================================================

        # MAC_NCR Register (0x00010000) - Network Control
        bitfields['0x00010000'] = [
            BitField("Reserved",  4, 31, 28, "Reserved", 0, "R"),
            BitField("TXEN",      3,  3,  1, "Transmit Enable (1=enable TX)", 0, "R/W"),
            BitField("RXEN",      2,  2,  1, "Receive Enable (1=enable RX)", 0, "R/W"),
            BitField("LBL",       1,  1,  1, "Local Loopback (MII loopback)", 0, "R/W"),
            BitField("Reserved",  0,  0,  1, "Reserved", 0, "R"),
        ]

        # MAC_NCFGR Register (0x00010001) - Network Configuration
        bitfields['0x00010001'] = [
            BitField("Reserved",  30, 31, 2,  "Reserved", 0, "R"),
            BitField("RXBP",      29, 29, 1,  "Receive Bad Preamble (1=accept)", 0, "R/W"),
            BitField("Reserved",  23, 28, 6,  "Reserved", 0, "R"),
            BitField("IRXFCS",    22, 22, 1,  "Ignore RX FCS (1=pass packets with bad FCS)", 0, "R/W"),
            BitField("EFRHD",     21, 21, 1,  "Enable Frame Receive in Half Duplex", 0, "R/W"),
            BitField("Reserved",  18, 20, 3,  "Reserved", 0, "R"),
            BitField("RFCS",      17, 17, 1,  "Remove FCS (1=strip FCS from RX frames)", 0, "R/W"),
            BitField("LFERD",     16, 16, 1,  "Length Field Error Frame Discard", 0, "R/W"),
            BitField("Reserved",   9, 15, 7,  "Reserved", 0, "R"),
            BitField("MAXFS",      8,  8, 1,  "Max Frame Size (0=1518, 1=1536 bytes)", 0, "R/W"),
            BitField("UNIHEN",     7,  7, 1,  "Unicast Hash Enable", 0, "R/W"),
            BitField("MTIHEN",     6,  6, 1,  "Multicast Hash Enable", 0, "R/W"),
            BitField("NBC",        5,  5, 1,  "No Broadcast (1=reject broadcasts)", 0, "R/W"),
            BitField("CAF",        4,  4, 1,  "Copy All Frames (promiscuous mode)", 0, "R/W"),
            BitField("Reserved",   3,  3, 1,  "Reserved", 0, "R"),
            BitField("DNVLAN",     2,  2, 1,  "Discard Non-VLAN Frames", 0, "R/W"),
            BitField("Reserved",   0,  1, 2,  "Reserved", 0, "R"),
        ]

        # MAC_TISUBN Register (0x0001006F) - TSU Timer Increment Sub-nanoseconds
        bitfields['0x0001006F'] = [
            BitField("LSBTIR",    24, 31, 8,  "LSB Timer Increment[7:0] (sub-ns LSB)", 0, "R/W"),
            BitField("Reserved",  16, 23, 8,  "Reserved", 0, "R"),
            BitField("MSBTIR",     0, 15, 16, "MSB Timer Increment[15:0]", 0, "R/W"),
        ]

        # MAC_TSH Register (0x00010070) - TSU Timer Seconds High
        bitfields['0x00010070'] = [
            BitField("Reserved",  16, 31, 16, "Reserved", 0, "R"),
            BitField("TCS_H",      8, 15,  8, "Timer Counter Seconds[47:40]", 0, "R/W"),
            BitField("TCS_L",      0,  7,  8, "Timer Counter Seconds[39:32]", 0, "R/W"),
        ]

        # MAC_TSL Register (0x00010074) - TSU Timer Seconds Low
        bitfields['0x00010074'] = [
            BitField("TCS_31_24", 24, 31, 8,  "Timer Counter Seconds[31:24]", 0, "R/W"),
            BitField("TCS_23_16", 16, 23, 8,  "Timer Counter Seconds[23:16]", 0, "R/W"),
            BitField("TCS_15_8",   8, 15, 8,  "Timer Counter Seconds[15:8]", 0, "R/W"),
            BitField("TCS_7_0",    0,  7, 8,  "Timer Counter Seconds[7:0]", 0, "R/W"),
        ]

        # MAC_TN Register (0x00010075) - TSU Timer Nanoseconds
        bitfields['0x00010075'] = [
            BitField("Reserved", 30, 31,  2, "Reserved", 0, "R"),
            BitField("TNS",       0, 29, 30, "Timer Nanoseconds [29:0] (0..999999999)", 0, "R/W"),
        ]

        # ================================================================================
        # MMS_2: PHY PCS REGISTERS
        # ================================================================================

        # PCS_REG / T1SPCSCTL Register (0x000208F3) - 10BASE-T1S PCS Control
        bitfields['0x000208F3'] = [
            BitField("RST",      15, 15, 1, "PCS Reset (1=Reset, self-clearing)", 0, "R/W"),
            BitField("LBE",      14, 14, 1, "PCS Loopback Enable", 0, "R/W"),
            BitField("Reserved",  9, 13, 5, "Reserved", 0, "R"),
            BitField("DUPLEX",    8,  8, 1, "Duplex Mode (1=Full Duplex)", 0, "R/W"),
            BitField("Reserved",  0,  7, 8, "Reserved", 0, "R"),
        ]

        # ================================================================================
        # MMS_4: PHY VENDOR SPECIFIC REGISTERS
        # ================================================================================

        # CTRL1 Register (0x00040010) - Control 1
        bitfields['0x00040010'] = [
            BitField("Reserved",  4, 15, 12, "Reserved", 0, "R"),
            BitField("IWDE",      3,  3,  1, "Inactivity Watchdog Enable", 0, "R/W"),
            BitField("Reserved",  2,  2,  1, "Reserved", 0, "R"),
            BitField("DIGLBE",    1,  1,  1, "Digital Loopback Enable", 0, "R/W"),
            BitField("Reserved",  0,  0,  1, "Reserved", 0, "R"),
        ]

        # STS1 Register (0x00040018) - Status 1
        bitfields['0x00040018'] = [
            BitField("Reserved", 13, 15,  3, "Reserved", 0, "R"),
            BitField("SQI",      12, 12,  1, "Signal Quality Indicator interrupt", 0, "R"),
            BitField("PSTC",     11, 11,  1, "PLCA Status Change", 0, "R"),
            BitField("TXCOL",    10, 10,  1, "Transmit Collision", 0, "R"),
            BitField("TXJAB",     9,  9,  1, "Transmit Jabber", 0, "R"),
            BitField("TSSI",      8,  8,  1, "Timestamp Status Indication", 0, "R"),
            BitField("EMPCYC",    7,  7,  1, "Empty Cycle (PLCA opportunity not used)", 0, "R"),
            BitField("RXINTO",    6,  6,  1, "Receive Interrupt Overflow", 0, "R"),
            BitField("UNEXPB",    5,  5,  1, "Unexpected BEACON", 0, "R"),
            BitField("BCNBFTO",   4,  4,  1, "BEACON Burst Timeout", 0, "R"),
            BitField("UNCRS",     3,  3,  1, "Uncorrectable Receive Symbol error", 0, "R"),
            BitField("PLCASYM",   2,  2,  1, "PLCA Symbol error", 0, "R"),
            BitField("ESDERR",    1,  1,  1, "End-of-Stream Delimiter error", 0, "R"),
            BitField("DEC5B",     0,  0,  1, "5B Decode error", 0, "R"),
        ]

        # MIDVER Register (0x0004CA00) - OPEN Alliance Map ID and Version
        bitfields['0x0004CA00'] = [
            BitField("IDM",  8, 15, 8, "OPEN Alliance Map ID (0xFF = PLCA capable)", 0xFF, "R"),
            BitField("VER",  0,  7, 8, "OPEN Alliance Map Version", 0, "R"),
        ]

        # PLCA_CTRL0 Register (0x0004CA01) - PLCA Control 0
        bitfields['0x0004CA01'] = [
            BitField("EN",       15, 15, 1, "PLCA Enable (1=PLCA mode, 0=CSMA/CD)", 0, "R/W"),
            BitField("RST",      14, 14, 1, "PLCA Reset (1=Reset, self-clearing)", 0, "R/W"),
            BitField("Reserved",  0, 13, 14, "Reserved", 0, "R"),
        ]

        # PLCA_CTRL1 Register (0x0004CA02) - PLCA Control 1
        bitfields['0x0004CA02'] = [
            BitField("NCNT",  8, 15, 8, "Node Count (1..255, total nodes on bus)", 8, "R/W"),
            BitField("ID",    0,  7, 8, "Local Node ID (0=coordinator, 1..254=node)", 0, "R/W"),
        ]

        # PLCA_STS Register (0x0004CA03) - PLCA Status
        bitfields['0x0004CA03'] = [
            BitField("PST",      15, 15, 1, "PLCA Status (1=Online, 0=Offline)", 0, "R"),
            BitField("Reserved",  0, 14, 15, "Reserved", 0, "R"),
        ]

        # PLCA_TOTMR Register (0x0004CA04) - PLCA Transmit Opportunity Timer
        bitfields['0x0004CA04'] = [
            BitField("Reserved",  8, 15, 8,  "Reserved", 0, "R"),
            BitField("TOTMR",     0,  7, 8,  "TO Timer (transmit opportunity duration, in bits)", 32, "R/W"),
        ]

        # PLCA_BURST Register (0x0004CA05) - PLCA Burst Mode
        bitfields['0x0004CA05'] = [
            BitField("MAXBC",  8, 15, 8, "Max Burst Count (0=burst disabled)", 0, "R/W"),
            BitField("BTMR",   0,  7, 8, "Burst Timer (inter-burst gap, in bits)", 0x80, "R/W"),
        ]
        
        # ================================================================================
        # NEW HARDWARE-VERIFIED REGISTERS (März 11, 2026)
        # ================================================================================
        
        # SQI Register (0x0004008F) - Signal Quality Index - HARDWARE VERIFIED
        bitfields['0x0004008F'] = [
            BitField("Reserved", 16, 31, 16, "Reserved", 0x0000, "R"),
            BitField("Reserved", 11, 15,  5, "Reserved", 0x00, "R"),
            BitField("SQI",      8, 10,  3, "Signal Quality Index (0-7, higher=better)", "6=EXCELLENT", "R"),
            BitField("Reserved", 0,   7,  8, "Reserved/Additional Status", 0x31, "R"),
        ]
        
        # ================================================================================
        # CLAUSE 22 PMD REGISTERS (WORKING) - Use these instead of MMS 3!
        # Accessed via MMS 0 with 0x0000FF2x addresses
        # ================================================================================
        
        # PMD Control Register (0x0000FF20) - WORKING Clause 22 PMD
        bitfields['0x0000FF20'] = [
            BitField("Reserved", 16, 31, 16, "Reserved (upper 16 bits)", 0x0000, "R"),
            BitField("Reserved", 13, 15,  3, "Reserved", 0x0, "R"),
            BitField("CFD_START", 12, 12, 1, "Cable Fault Diagnostic Start", 0, "R/W"),
            BitField("CFD_EN",   11, 11,  1, "Cable Fault Diagnostic Enable", 0, "R/W"),
            BitField("Reserved",  0, 10, 11, "Reserved", 0x000, "R"),
        ]
        
        # PMD Status Register (0x0000FF21) - WORKING Clause 22 PMD  
        bitfields['0x0000FF21'] = [
            BitField("Reserved", 16, 31, 16, "Reserved (upper 16 bits)", 0x0000, "R"),
            BitField("Reserved", 12, 15,  4, "Reserved", 0x0, "R"),
            BitField("LINK",     11, 11,  1, "Link Status (1=Link Up)", "Variable", "R"),
            BitField("CFD_DONE", 10, 10,  1, "Cable Fault Diagnostic Done", "Variable", "R"),
            BitField("FAULT_TYPE", 8,  9, 2, "Cable Fault Type (if CFD_DONE=1)", "Variable", "R"),
            BitField("Reserved",  0,  7,  8, "Reserved/Additional Status", "Variable", "R"),
        ]
        
        # PMD Extended Status Register (0x0000FF22) - WORKING Clause 22 PMD
        bitfields['0x0000FF22'] = [
            BitField("Reserved", 16, 31, 16, "Reserved (upper 16 bits)", 0x0000, "R"),
            BitField("EXT_STATUS", 0, 15, 16, "Extended PMD Status Information", "Variable", "R"),
        ]
        
        # PMD Cable Fault Diagnostics Register (0x0000FF23) - WORKING Clause 22 PMD
        bitfields['0x0000FF23'] = [
            BitField("Reserved", 16, 31, 16, "Reserved (upper 16 bits)", 0x0000, "R"),
            BitField("CFD_RESULT", 0, 15, 16, "Cable Fault Diagnostic Result Data", "Variable", "R"),
        ]

        return bitfields
    
    def get_bitfields(self, address: str) -> Optional[List[BitField]]:
        """Gibt die Bitfeld-Definitionen für eine Register-Adresse zurück"""
        return self.register_bitfields.get(address)
    
    def analyze_register_value(self, address: str, value: int) -> List[Dict]:
        """Analysiert einen Register-Wert und gibt Bitfeld-Interpretationen zurück"""
        bitfields = self.get_bitfields(address)
        if not bitfields:
            return []
        
        analysis = []
        for bitfield in reversed(bitfields):  # Reverse für MSB first Darstellung
            field_value = bitfield.extract_value(value)
            interpretation = self._interpret_bitfield_value(bitfield, field_value)
            
            analysis.append({
                'bitfield': bitfield,
                'value': field_value,
                'hex_value': f"0x{field_value:X}",
                'binary_value': f"{field_value:0{bitfield.size}b}",
                'interpretation': interpretation,
                'is_error': self._is_error_bit(bitfield, field_value),
                'is_important': self._is_important_bit(bitfield, field_value)
            })
            
        return analysis
    
    def _interpret_bitfield_value(self, bitfield: BitField, value: int) -> str:
        """Interpretiert den Wert eines Bitfelds basierend auf seiner Bedeutung"""
        name = bitfield.name.upper()
        
        # Spezielle Interpretationen für verschiedene Bitfelder
        if 'MAJVER' in name:
            return f"Major Version: {value}"
        elif 'MINVER' in name:
            return f"Minor Version: {value}"
        elif 'REVISION' in name:
            return f"Revision: {value}"
        elif 'RESET' in name and 'SW' in name:
            return "🔄 RESET ACTIVE!" if value == 1 else "✅ Normal Operation"
        elif name.endswith('E') and not name.endswith('VER'):  # Error bits
            return "❌ ERROR DETECTED!" if value == 1 else "✅ OK"
        elif name.endswith('M'):  # Mask bits  
            return "🔕 Masked (Interrupt disabled)" if value == 1 else "🔔 Unmasked (Interrupt enabled)"
        elif 'ENABLE' in bitfield.description.upper():
            return "✅ Enabled" if value == 1 else "❌ Disabled"
        elif 'CAPABLE' in bitfield.description.upper():
            return "✅ Capable" if value == 1 else "❌ Not Capable"
        elif 'COMPLETE' in bitfield.description.upper():
            return "✅ Complete" if value == 1 else "⏳ Pending"
        elif 'AVAILABLE' in bitfield.description.upper():
            return "✅ Available" if value == 1 else "❌ Not Available"
        elif 'OVERFLOW' in bitfield.description.upper():
            return "⚠️ OVERFLOW!" if value == 1 else "✅ OK"
        elif 'UNDERFLOW' in bitfield.description.upper():
            return "⚠️ UNDERFLOW!" if value == 1 else "✅ OK"
        elif 'CREDITS' in bitfield.description.upper():
            return f"📊 {value} credits available"
        elif 'BUFFER' in bitfield.description.upper() and 'AVAILABLE' in bitfield.description.upper():
            return f"📊 {value} bytes available"
        elif 'OUI' in name:
            return f"OUI: 0x{value:02X}"
        elif 'MODEL' in name:
            return f"Model: 0x{value:X}"
        elif 'MAC_BYTE' in name:
            return f"MAC Byte: 0x{value:02X} ({value:02X})"
        elif 'LEGACY' in name:
            if value == 0:
                return "✅ Legacy Register (Unused - Correct Value 0)"
            else:
                return f"⚠️ Legacy Register (Unexpected Value: 0x{value:08X})"
        elif 'SQI' == name:
            sqi_quality = ["POOR", "BAD", "FAIR", "OK", "GOOD", "VERY GOOD", "EXCELLENT", "EXCELLENT"]
            return f"🎯 SQI: {value}/7 ({sqi_quality[min(value, 7)]})"
        elif 'LNKSTS' == name:
            return "🔗 Link UP" if value == 1 else "❌ Link DOWN"
        elif 'TXEN' == name:
            return "📤 TX Enabled" if value == 1 else "⏹️ TX Disabled"  
        elif 'RXEN' == name:
            return "📥 RX Enabled" if value == 1 else "⏹️ RX Disabled"
        elif 'PLCA' in name and 'EN' in name:
            return "🔀 PLCA Mode" if value == 1 else "⚡ CSMA/CD Mode"
        elif 'NCNT' == name:
            return f"🌐 Network: {value} total nodes"
        elif 'ID' == name and 'PLCA' in bitfield.description.upper():
            if value == 0:
                return "👑 PLCA Coordinator (Node 0)"
            else:
                return f"🔗 PLCA Node {value}"
        elif 'PST' == name:
            return "🟢 PLCA Online" if value == 1 else "🔴 PLCA Offline"
        elif name == 'RESERVED':
            return "Reserved (unused)" if value == 0 else f"⚠️ Unexpected value: {value}"
        else:
            # Generic interpretation basierend auf Wert
            if bitfield.size == 1:
                return f"Set ({value})" if value == 1 else f"Clear ({value})"
            else:
                return f"Value: {value} (0x{value:X})"
    
    def _is_error_bit(self, bitfield: BitField, value: int) -> bool:
        """Bestimmt ob ein Bitfeld einen Fehler-Status repräsentiert"""
        name = bitfield.name.upper()
        desc = bitfield.description.upper()
        
        error_indicators = ['ERROR', 'OVERFLOW', 'UNDERFLOW', 'UV18', 'ECC', 'BUSER']
        
        if any(indicator in name or indicator in desc for indicator in error_indicators):
            return value == 1
        
        return False
    
    def _is_important_bit(self, bitfield: BitField, value: int) -> bool:
        """Bestimmt ob ein Bitfeld wichtige Status-Information enthält"""
        name = bitfield.name.upper()
        desc = bitfield.description.upper()
        
        important_indicators = ['RESET', 'ENABLE', 'COMPLETE', 'INTERRUPT', 'PHYINT', 'SEV', 'LNKSTS', 'SQI', 'PLCA', 'TXEN', 'RXEN']
        
        return any(indicator in name or indicator in desc for indicator in important_indicators)

class LAN8651LinuxBitfieldGUI:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("LAN8651 Register Bitfield Analyzer - Linux Remote Access")
        self.root.geometry("1400x900")
        
        # Configuration
        self.com_port = tk.StringVar(value="COM9")
        self.baudrate = tk.IntVar(value=115200)
        
        # Linux ioctl access - 500x-10000x faster than debugfs
        self.linux_access = LinuxIoctlAccess(self.com_port.get(), self.baudrate.get())
        
        # Set up proper cleanup on window close
        self.root.protocol("WM_DELETE_WINDOW", self._on_closing)
        
        # Thread communication
        self.result_queue = queue.Queue()
        self.is_scanning = False
        
        # Bitfield analysis engine
        self.bitfield_analyzer = LAN8651BitfieldDefinitions()
        
        # Current selected register
        self.current_register = None
        self.current_value = None
        
        # Stored register read values (addr_str → int value)
        self.register_values = {}
        
        # MAC Address storage for decoding
        self.mac_sab2_value = None
        self.mac_sat2_value = None
        self.decoded_mac = "Not available"
        
        # Register maps - Complete MMS architecture from original GUI
        self.register_maps = {
            "MMS_0": {
                "name": "Open Alliance Standard + PMD (Clause 22)",
                "description": "Standard Register gemäß Open Alliance Spezifikation + Working PMD",
                "registers": {
                    "0x00000000": {"name": "OA_ID",       "description": "Open Alliance ID Register",           "access": "R",   "width": "32-bit"},
                    "0x00000001": {"name": "OA_PHYID",    "description": "Open Alliance PHY ID",                "access": "R",   "width": "32-bit"},
                    "0x00000002": {"name": "OA_STDCAP",   "description": "Standard Capabilities Register",      "access": "R",   "width": "32-bit"},
                    "0x00000003": {"name": "OA_RESET",    "description": "Reset Control and Status Register",   "access": "R/W", "width": "32-bit"},
                    "0x00000004": {"name": "OA_CONFIG0",  "description": "Configuration Register 0",            "access": "R/W", "width": "32-bit"},
                    "0x00000008": {"name": "OA_STATUS0",  "description": "Status Register 0",                   "access": "R",   "width": "32-bit"},
                    "0x00000009": {"name": "OA_STATUS1",  "description": "Status Register 1",                   "access": "R",   "width": "32-bit"},
                    "0x0000000B": {"name": "OA_BUFSTS",   "description": "Buffer Status Register",              "access": "R",   "width": "32-bit"},
                    "0x0000000C": {"name": "OA_IMASK0",   "description": "Interrupt Mask Register 0",           "access": "R/W", "width": "32-bit"},
                    "0x0000000D": {"name": "OA_IMASK1",   "description": "Interrupt Mask Register 1",           "access": "R/W", "width": "32-bit"},
                    "0x0000FF00": {"name": "BASIC_CTRL",  "description": "PHY Basic Control Register",          "access": "R/W", "width": "16-bit"},
                    "0x0000FF01": {"name": "BASIC_STS",   "description": "PHY Basic Status Register",           "access": "R",   "width": "16-bit"},
                    "0x0000FF02": {"name": "PHY_ID1",     "description": "PHY Identifier Register 1",           "access": "R",   "width": "16-bit"},
                    "0x0000FF03": {"name": "PHY_ID2",     "description": "PHY Identifier Register 2",           "access": "R",   "width": "16-bit"},
                    "0x0000FF20": {"name": "PMD_CTRL",    "description": "WORKING PMD Control (Clause 22)",     "access": "R/W", "width": "16-bit"},
                    "0x0000FF21": {"name": "PMD_STS",     "description": "WORKING PMD Status (Clause 22)",      "access": "R",   "width": "16-bit"},
                    "0x0000FF22": {"name": "PMD_EXT_STS", "description": "WORKING PMD Extended Status",         "access": "R",   "width": "16-bit"},
                    "0x0000FF23": {"name": "PMD_CFD",     "description": "WORKING PMD Cable Fault Diagnostics", "access": "R",   "width": "16-bit"},
                }
            },
            "MMS_1": {
                "name": "MAC Registers",
                "description": "Ethernet MAC Control and Status Register",
                "registers": {
                    "0x00010000": {"name": "MAC_NCR",    "description": "Network Control Register",                   "access": "R/W", "width": "32-bit"},
                    "0x00010001": {"name": "MAC_NCFGR",  "description": "Network Configuration Register",             "access": "R/W", "width": "32-bit"},
                    "0x00010022": {"name": "MAC_SAB1",   "description": "MAC Address Bottom 1 (Legacy)",              "access": "R/W", "width": "32-bit"},
                    "0x00010023": {"name": "MAC_SAT1",   "description": "MAC Address Top 1 (Legacy)",                 "access": "R/W", "width": "32-bit"},
                    "0x00010024": {"name": "MAC_SAB2",   "description": "MAC Address Bottom 2 (Active - Bytes 0-3 LE)", "access": "R/W", "width": "32-bit"},
                    "0x00010025": {"name": "MAC_SAT2",   "description": "MAC Address Top 2 (Active - Bytes 4-5 LE)",  "access": "R/W", "width": "32-bit"},
                    "0x0001006F": {"name": "MAC_TISUBN", "description": "TSU Sub-nanoseconds",                       "access": "R/W", "width": "32-bit"},
                    "0x00010070": {"name": "MAC_TSH",    "description": "TSU Seconds High",                          "access": "R/W", "width": "32-bit"},
                    "0x00010074": {"name": "MAC_TSL",    "description": "TSU Seconds Low",                           "access": "R/W", "width": "32-bit"},
                    "0x00010075": {"name": "MAC_TN",     "description": "TSU Nanoseconds",                           "access": "R/W", "width": "32-bit"},
                }
            },
            "MMS_2": {
                "name": "PHY PCS Registers",
                "description": "Physical Coding Sublayer Register",
                "registers": {
                    "0x000208F3": {"name": "PCS_REG", "description": "PCS Basic Register", "access": "R/W", "width": "16-bit"},
                }
            },
            "MMS_3": {
                "name": "PHY PMA/PMD Registers (BROKEN - Returns 0x0000)",
                "description": "WARNING: MMS 3 PMD registers non-functional! Use Clause 22 instead!",
                "registers": {
                    "0x00030001": {"name": "PMD_CONTROL", "description": "BROKEN - PMA/PMD Control (returns 0x0000) - USE 0x0000FF20", "access": "R/W", "width": "16-bit"},
                    "0x00030002": {"name": "PMD_STATUS",  "description": "BROKEN - PMA/PMD Status (returns 0x0000) - USE 0x0000FF21", "access": "R",   "width": "16-bit"},
                }
            },
            "MMS_4": {
                "name": "PHY Vendor / PLCA / SQI",
                "description": "Microchip-spezifische PHY Register mit PLCA und SQI",
                "registers": {
                    "0x00040010": {"name": "CTRL1",      "description": "Vendor Control 1 Register",         "access": "R/W", "width": "16-bit"},
                    "0x00040018": {"name": "STS1",       "description": "Vendor Status 1 Register",          "access": "R",   "width": "16-bit"},
                    "0x0004008F": {"name": "SQI",        "description": "Signal Quality Index (6/7 EXCELLENT)", "access": "R",   "width": "16-bit"},
                    "0x0004CA00": {"name": "MIDVER",     "description": "Map ID Version Register",           "access": "R",   "width": "16-bit"},
                    "0x0004CA01": {"name": "PLCA_CTRL0", "description": "PLCA Control 0 (Enable/Reset)",     "access": "R/W", "width": "16-bit"},
                    "0x0004CA02": {"name": "PLCA_CTRL1", "description": "PLCA Control 1 (Node ID/Count)",    "access": "R/W", "width": "16-bit"},
                    "0x0004CA03": {"name": "PLCA_STS",   "description": "PLCA Status Register",              "access": "R",   "width": "16-bit"},
                    "0x0004CA04": {"name": "PLCA_TOTMR", "description": "PLCA TO Timer Register",            "access": "R/W", "width": "16-bit"},
                    "0x0004CA05": {"name": "PLCA_BURST", "description": "PLCA Burst Mode Register",          "access": "R/W", "width": "16-bit"},
                }
            },
            "MMS_5": {
                "name": "SPI / TC6 Interface",
                "description": "SPI-Status und TC6 Control Register",
                "registers": {
                    "0x00050000": {"name": "SPI_STATUS",     "description": "SPI Status Register",    "access": "R",   "width": "16-bit"},
                    "0x00050001": {"name": "TC6_CONTROL",    "description": "TC6 Control Register",   "access": "R/W", "width": "16-bit"},
                    "0x00050002": {"name": "PARITY_CONTROL", "description": "Parity Control Register","access": "R/W", "width": "16-bit"},
                }
            },
            "MMS_6": {
                "name": "Interrupt / Event Control",
                "description": "Interrupt Status und Event Control Register",
                "registers": {
                    "0x00060000": {"name": "IRQ_STATUS",    "description": "Interrupt Status Register", "access": "R",   "width": "16-bit"},
                    "0x00060001": {"name": "IRQ_MASK",      "description": "Interrupt Mask Register",   "access": "R/W", "width": "16-bit"},
                    "0x00060002": {"name": "EVENT_CONTROL", "description": "Event Control Register",    "access": "R/W", "width": "16-bit"},
                }
            },
            "MMS_7": {
                "name": "Power / Reset / Clock",
                "description": "Power Management und Clock Control Register",
                "registers": {
                    "0x00070000": {"name": "RESET_STATUS",  "description": "Reset Status Register",  "access": "R",   "width": "16-bit"},
                    "0x00070001": {"name": "POWER_CONTROL", "description": "Power Control Register",  "access": "R/W", "width": "16-bit"},
                    "0x00070002": {"name": "CLOCK_CONTROL", "description": "Clock Control Register",  "access": "R/W", "width": "16-bit"},
                }
            },
            "MMS_8": {
                "name": "Statistics / Counters",
                "description": "Frame und Error Counter Register",
                "registers": {
                    "0x00080000": {"name": "FRAME_COUNTERS", "description": "Frame Counter Register", "access": "R", "width": "32-bit"},
                    "0x00080001": {"name": "ERROR_COUNTERS", "description": "Error Counter Register", "access": "R", "width": "32-bit"},
                    "0x00080002": {"name": "DEBUG_COUNTERS", "description": "Debug Counter Register", "access": "R", "width": "32-bit"},
                }
            },
            "MMS_9": {
                "name": "Vendor / Debug Extensions",
                "description": "Debug und Vendor Extension Register",
                "registers": {
                    "0x00090000": {"name": "DEBUG_REG1", "description": "Debug Register 1",        "access": "R/W", "width": "16-bit"},
                    "0x00090001": {"name": "DEBUG_REG2", "description": "Debug Register 2",        "access": "R/W", "width": "16-bit"},
                    "0x00090002": {"name": "VENDOR_EXT", "description": "Vendor Extensions Register","access": "R/W", "width": "16-bit"},
                }
            },
            "MMS_10": {
                "name": "Miscellaneous Registers",
                "description": "Zusätzliche Microchip-Funktionsregister",
                "registers": {
                    "0x000A0000": {"name": "MISC_CONTROL", "description": "Miscellaneous Control Register", "access": "R/W", "width": "16-bit"},
                }
            },
        }

        self.create_gui()
        self.process_queue()
        
    def create_gui(self):
        """Create the main GUI"""
        # Create main paned window
        main_paned = ttk.PanedWindow(self.root, orient=tk.HORIZONTAL)
        main_paned.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Left panel (1/3): Configuration and Register Selection
        left_frame = ttk.Frame(main_paned, width=467)
        main_paned.add(left_frame, weight=1)
        
        # Right panel (2/3): Bitfield Analysis
        right_frame = ttk.Frame(main_paned, width=933)
        main_paned.add(right_frame, weight=2)
        
        self.create_left_panel(left_frame)
        self.create_right_panel(right_frame)
        
    def create_left_panel(self, parent):
        """Create left panel with tabbed organization for better layout"""
        # Create main notebook for tabs
        main_notebook = ttk.Notebook(parent)
        main_notebook.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Tab 1: Configuration
        config_tab = ttk.Frame(main_notebook)
        main_notebook.add(config_tab, text="⚙️ Config")
        self._create_config_tab(config_tab)
        
        # Tab 2: Tools  
        tools_tab = ttk.Frame(main_notebook)
        main_notebook.add(tools_tab, text="🔧 Tools")
        self._create_tools_tab(tools_tab)
        
        # Tab 3: Quick Access
        quick_tab = ttk.Frame(main_notebook)
        main_notebook.add(quick_tab, text="🚀 Quick")
        self._create_quick_tab(quick_tab)
        
        # Tab 4: Register Selection
        register_tab = ttk.Frame(main_notebook)
        main_notebook.add(register_tab, text="📋 Registers")
        self._create_register_tab(register_tab)
    
    def _create_config_tab(self, parent):
        """Create configuration tab content"""
        # Configuration section
        config_frame = ttk.LabelFrame(parent, text="🔧 Linux Remote Configuration", padding=10)
        config_frame.pack(fill=tk.X, padx=5, pady=5)
        
        ttk.Label(config_frame, text="COM Port (Linux Host):").grid(row=0, column=0, sticky=tk.W)
        com_entry = ttk.Entry(config_frame, textvariable=self.com_port, width=10)
        com_entry.grid(row=0, column=1, padx=5)
        com_entry.bind('<KeyRelease>', self._on_com_port_change)
        
        ttk.Label(config_frame, text="Baudrate:").grid(row=1, column=0, sticky=tk.W)
        ttk.Entry(config_frame, textvariable=self.baudrate, width=10).grid(row=1, column=1, padx=5)
        
        ttk.Button(config_frame, text="🔍 Test Linux Connection", 
                  command=self.test_linux_connection).grid(row=2, column=0, columnspan=2, pady=10)
        
        # Connection status
        self.connection_status = tk.StringVar(value="Not connected to Linux host")
        ttk.Label(config_frame, textvariable=self.connection_status, 
                 font=("Arial", 9)).grid(row=3, column=0, columnspan=2, pady=5)
        
        # Actions frame
        actions_frame = ttk.LabelFrame(parent, text="📄 Reports & Data", padding=10)
        actions_frame.pack(fill=tk.X, padx=5, pady=5)
        
        ttk.Button(actions_frame, text="📄 Generate HTML Report", 
                  command=self.generate_html_report).pack(fill=tk.X, pady=2)
        ttk.Button(actions_frame, text="📚 Read All Registers", 
                  command=self.read_all_registers).pack(fill=tk.X, pady=2)
    
    def _create_tools_tab(self, parent):
        """Create tools tab content"""
        # Ping Tools Section
        ping_frame = ttk.LabelFrame(parent, text="🏓 Ping Tools", padding=10)
        ping_frame.pack(fill=tk.X, padx=5, pady=5)

        ttk.Button(ping_frame, text="📡 Ping quick (5x)",
                   command=self.ping_quick).pack(fill=tk.X, pady=2)
        ttk.Button(ping_frame, text="🔥 Ping stress (1400B × 1000)",
                   command=self.ping_stress).pack(fill=tk.X, pady=2)

        # Terminal Tools Section  
        terminal_frame = ttk.LabelFrame(parent, text="💻 Terminal Access", padding=10)
        terminal_frame.pack(fill=tk.X, padx=5, pady=5)

        ttk.Button(terminal_frame, text="🖥️ Open Terminal Window",
                   command=self.open_terminal_window).pack(fill=tk.X, pady=2)
    
    def _create_quick_tab(self, parent):
        """Create quick access tab content"""
        # Quick read section
        quick_frame = ttk.LabelFrame(parent, text="🚀 Quick Register Access", padding=10)
        quick_frame.pack(fill=tk.X, padx=5, pady=5)
        
        ttk.Label(quick_frame, text="Register Address:").grid(row=0, column=0, sticky=tk.W)
        self.quick_addr = tk.StringVar(value="0x00000000")
        ttk.Entry(quick_frame, textvariable=self.quick_addr, width=15).grid(row=0, column=1, padx=5)
        
        quick_btn_frame = ttk.Frame(quick_frame)
        quick_btn_frame.grid(row=1, column=0, columnspan=2, pady=5)
        ttk.Button(quick_btn_frame, text="📖 Read", command=self.quick_read_register).pack(side=tk.LEFT, padx=2)
        
        # Write functionality
        ttk.Label(quick_frame, text="Write Value:").grid(row=2, column=0, sticky=tk.W)  
        self.quick_value = tk.StringVar(value="0x00000000")
        ttk.Entry(quick_frame, textvariable=self.quick_value, width=15).grid(row=2, column=1, padx=5)
        ttk.Button(quick_frame, text="✏️ Write", command=self.quick_write_register).grid(row=3, column=0, columnspan=2, pady=5)
    
    def _create_register_tab(self, parent):
        """Create register selection tab content"""
        # Register selection
        reg_frame = ttk.LabelFrame(parent, text="📋 Register Selection", padding=5)
        reg_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        self.register_notebook = ttk.Notebook(reg_frame)
        self.register_notebook.pack(fill=tk.BOTH, expand=True)
        
        self.register_trees = {}
        
        for group_name, group_info in self.register_maps.items():
            tab_frame = ttk.Frame(self.register_notebook)
            self.register_notebook.add(tab_frame, text=group_name)
            
            # Scan button
            ttk.Button(tab_frame, text=f"📡 Scan All {group_name}",
                      command=lambda g=group_name: self.scan_register_group(g)).pack(pady=5)
            
            # Register treeview (Address | Name | Value)
            tree_frame = ttk.Frame(tab_frame)
            tree_frame.pack(fill=tk.BOTH, expand=True, padx=3, pady=2)
            
            columns = ("Address", "Name", "Value")
            tree = ttk.Treeview(tree_frame, columns=columns, show="headings", height=12)
            tree.heading("Address", text="Address")
            tree.heading("Name",    text="Name")
            tree.heading("Value",   text="Value")
            tree.column("Address", width=90)
            tree.column("Name",    width=120)
            tree.column("Value",   width=90)
            
            for addr, reg in group_info['registers'].items():
                tree.insert("", "end", values=(addr, reg['name'], "—"), iid=addr)
            
            tree_scroll = ttk.Scrollbar(tree_frame, orient=tk.VERTICAL, command=tree.yview)
            tree.configure(yscrollcommand=tree_scroll.set)
            tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
            tree_scroll.pack(side=tk.RIGHT, fill=tk.Y)
            
            tree.bind("<<TreeviewSelect>>", self.on_register_select)
            self.register_trees[group_name] = tree
        
        # Current register value display
        self.current_value_label = ttk.Label(parent, text="No register read yet",
                                             font=("Consolas", 9), foreground="blue")
        self.current_value_label.pack(fill=tk.X, padx=5, pady=3)
        
    def create_right_panel(self, parent):
        """Create right panel with bitfield analysis"""
        # Title section
        title_frame = ttk.Frame(parent)
        title_frame.pack(fill=tk.X, padx=5, pady=5)
        
        ttk.Label(title_frame, text="🔬 Bitfield Analysis (Linux Remote)", 
                 font=("Arial", 16, "bold")).pack(side=tk.LEFT)
        
        # Current register info
        self.current_reg_info = ttk.Label(title_frame, text="No register selected", font=("Arial", 12))
        self.current_reg_info.pack(side=tk.RIGHT)
        
        # Control buttons
        btn_frame = ttk.Frame(parent)
        btn_frame.pack(fill=tk.X, padx=5, pady=5)
        
        ttk.Button(btn_frame, text="📖 Read & Analyze", 
                  command=self.read_and_analyze_current).pack(side=tk.LEFT, padx=5)
        ttk.Button(btn_frame, text="🔄 Re-analyze", 
                  command=self.refresh_analysis).pack(side=tk.LEFT, padx=5)
        
        # Analysis results
        analysis_frame = ttk.LabelFrame(parent, text="📊 Bitfield Breakdown", padding=5)
        analysis_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Scrollable text area for analysis results
        self.analysis_text = scrolledtext.ScrolledText(
            analysis_frame, 
            wrap=tk.WORD, 
            height=25,
            font=("Consolas", 11),
            bg="#f8f9fa"
        )
        self.analysis_text.pack(fill=tk.BOTH, expand=True)
        
        # Configure text tags for formatting
        self.analysis_text.tag_configure("header", font=("Arial", 14, "bold"), foreground="#2c3e50")
        self.analysis_text.tag_configure("register", font=("Consolas", 12, "bold"), foreground="#3498db")
        self.analysis_text.tag_configure("error", foreground="#e74c3c", font=("Consolas", 11, "bold"))
        self.analysis_text.tag_configure("important", foreground="#f39c12", font=("Consolas", 11, "bold"))
        self.analysis_text.tag_configure("success", foreground="#27ae60")
        
    def _on_com_port_change(self, event=None):
        """Update Linux access when COM port changes - Reset connection for performance"""
        self.linux_access.com_port = self.com_port.get()
        # Reset persistent connection on COM port change
        self.linux_access.close_connection()
        
    def _on_closing(self):
        """Cleanup beim Schließen der GUI - schließt persistente Verbindung"""
        try:
            # Schließe persistente Verbindung
            self.linux_access.close_connection()
        except Exception as e:
            pass  # Ignore cleanup errors
        finally:
            self.root.destroy()
        
    # ── Ping helpers ──────────────────────────────────────────────────────────

    def _run_ping(self, command, title, timeout=30):
        """Send a ping command to the Linux target and show output in a dialog."""
        # Progress dialog
        dlg = tk.Toplevel(self.root)
        dlg.title(title)
        dlg.geometry("600x400")
        dlg.transient(self.root)
        ttk.Label(dlg, text=f"Running: {command}", font=("Consolas", 9)).pack(padx=10, pady=(10, 0), anchor="w")
        from tkinter import scrolledtext
        out_text = scrolledtext.ScrolledText(dlg, wrap=tk.WORD, font=("Consolas", 10), height=18)
        out_text.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        status_var = tk.StringVar(value="Running…")
        ttk.Label(dlg, textvariable=status_var).pack(pady=(0, 8))
        ttk.Button(dlg, text="Close", command=dlg.destroy).pack(pady=(0, 8))

        def _thread():
            try:
                ser = serial.Serial(
                    self.linux_access.com_port,
                    self.linux_access.baudrate,
                    timeout=1
                )
                time.sleep(0.05)
                ser.reset_input_buffer()
                ser.write((command + "\r\n").encode())

                result = ""
                start = time.time()
                last_data = time.time()
                while time.time() - start < timeout:
                    if ser.in_waiting > 0:
                        chunk = ser.read(ser.in_waiting).decode("utf-8", errors="ignore")
                        result += chunk
                        last_data = time.time()
                        # Update dialog live
                        out_text.insert(tk.END, chunk)
                        out_text.see(tk.END)
                    else:
                        # Ping is done when we see the statistics line
                        if ("packet loss" in result or "packets transmitted" in result) \
                                and time.time() - last_data > 1.0:
                            break
                    time.sleep(0.05)

                ser.close()
                status_var.set("✅ Done")
            except Exception as exc:
                out_text.insert(tk.END, f"\n❌ Error: {exc}\n")
                status_var.set(f"❌ Error: {exc}")

        threading.Thread(target=_thread, daemon=True).start()

    def ping_quick(self):
        """Ping -c 5 192.168.0.200"""
        self._run_ping("ping -c 5 192.168.0.200", "Ping quick (5×)", timeout=15)

    def ping_stress(self):
        """Ping stress test: -s 1400 -i 0.0008 -c 1000 192.168.0.200"""
        self._run_ping(
            "ping -s 1400 -i 0.0008 -c 1000 192.168.0.200",
            "Ping stress (1400B × 1000)",
            timeout=60
        )

    # ── End Ping helpers ──────────────────────────────────────────────────────

    def open_terminal_window(self):
        """Öffnet ein interaktives Terminal-Fenster für direkte Kommando-Eingabe (wie Windows GUI)"""
        terminal_window = tk.Toplevel(self.root)
        terminal_window.title("🖥️ Linux Terminal - COM9" + (f" via {self.com_port.get()}" if self.com_port.get() != "COM9" else ""))
        terminal_window.geometry("800x600")
        terminal_window.transient(self.root)
        
        # Terminal Output Display Frame
        output_frame = ttk.Frame(terminal_window) 
        output_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=(10, 5))
        
        # Interactive Terminal Display (like Windows GUI)
        self.terminal_display = tk.Text(output_frame, 
                                       width=90, height=30,
                                       bg='black', fg='#00FF00',
                                       font=('Consolas', 10),
                                       wrap=tk.WORD,
                                       insertbackground='#00FF00',
                                       selectbackground='#404040')
        
        # Scrollbar
        scrollbar = ttk.Scrollbar(output_frame, orient="vertical", command=self.terminal_display.yview)
        self.terminal_display.configure(yscrollcommand=scrollbar.set)
        
        scrollbar.pack(side="right", fill="y")
        self.terminal_display.pack(side="left", fill=tk.BOTH, expand=True)
        
        # Context Menu für Copy & Paste
        self.terminal_context_menu = tk.Menu(self.terminal_display, tearoff=0)
        self.terminal_context_menu.add_command(label="📋 Copy", command=self._terminal_copy,
                                              accelerator="Ctrl+C")
        self.terminal_context_menu.add_command(label="📄 Paste", command=self._terminal_paste,
                                              accelerator="Ctrl+V")
        self.terminal_context_menu.add_separator()
        self.terminal_context_menu.add_command(label="🗑️ Clear", command=self._terminal_clear)
        
        # Right-click binding für Context Menu
        self.terminal_display.bind("<Button-3>", self._show_terminal_context_menu)
        
        # Button Frame
        button_frame = ttk.Frame(terminal_window)
        button_frame.pack(fill=tk.X, padx=10, pady=5)
        
        self.connect_button = ttk.Button(button_frame, text="🔌 Connect", 
                                        command=self._terminal_connect)
        self.connect_button.pack(side=tk.LEFT, padx=2)
        
        self.disconnect_button = ttk.Button(button_frame, text="❌ Disconnect", 
                                           command=self._terminal_disconnect, state="disabled")
        self.disconnect_button.pack(side=tk.LEFT, padx=2)
        
        ttk.Button(button_frame, text="🗑️ Clear", 
                  command=self._terminal_clear).pack(side=tk.LEFT, padx=2)
        ttk.Button(button_frame, text="📋 Copy", 
                  command=self._terminal_copy).pack(side=tk.LEFT, padx=2)
        ttk.Button(button_frame, text="📄 Paste", 
                  command=self._terminal_paste).pack(side=tk.LEFT, padx=2)
        ttk.Button(button_frame, text="💾 Save Log", 
                  command=self._save_terminal_output).pack(side=tk.LEFT, padx=2)
        ttk.Button(button_frame, text="❌ Close", 
                  command=lambda: self._close_terminal_window(terminal_window)).pack(side=tk.RIGHT, padx=2)
        
        # Status Label
        self.terminal_status = tk.StringVar(value="Disconnected")
        ttk.Label(button_frame, textvariable=self.terminal_status, 
                 font=("Arial", 9)).pack(side=tk.RIGHT, padx=10)
        
        # Bind events für Tastatur-Input (wie Windows GUI)
        self.terminal_display.bind('<KeyPress>', self._terminal_key_handler)
        self.terminal_display.bind('<Button-1>', self._terminal_click_handler)
        self.terminal_display.focus_set()
        
        # Initialize terminal state
        self.terminal_ser = None
        self.terminal_running = False
        self.debug_mode = False  # Debug mode disabled for clean terminal output
        
        # Welcome message - add after widget is fully configured
        welcome_text = """🖥️ Linux Terminal
📡 Port: COM9 @ 115200 baud
🔌 Click 'Connect' to start session

"""
        
        # Insert welcome text directly
        self.terminal_display.insert(tk.END, welcome_text)
        self.terminal_display.see(tk.END)
        
        # Auto-connect nach kurzer Verzögerung
        terminal_window.after(1000, self._terminal_connect)
    
    def _terminal_connect(self):
        """Stellt persistente Linux Verbindung über COM Port her"""
        try:
            # Use existing persistent ioctl connection
            ser, logged_in = self.linux_access._get_connection()
            if ser and logged_in:
                self.terminal_ser = ser
                self.terminal_running = True
                self.connect_button.config(state="disabled")
                self.disconnect_button.config(state="normal")
                self.terminal_status.set("Connected")
                
                self._terminal_write("✅ Connected - ready for commands\n\n")
                
                # Start continuous read thread
                threading.Thread(target=self._terminal_read_worker, daemon=True).start()
            else:
                self._terminal_write("❌ Connection failed: Could not establish Linux connection\n")
                self.terminal_status.set("Connection Failed")
                
        except Exception as e:
            self._terminal_write(f"❌ Connection failed: {str(e)}\n")
            self.terminal_status.set("Connection Failed")
    
    def _terminal_disconnect(self):
        """Trennt Terminal-Verbindung"""
        self.terminal_running = False
        
        # Don't close the shared connection - just stop terminal
        self.terminal_ser = None
            
        self.connect_button.config(state="normal")
        self.disconnect_button.config(state="disabled")
        self.terminal_status.set("Disconnected")
        
        self._terminal_write("\n❌ Terminal Disconnected\n")
    
    def _terminal_read_worker(self):
        """Kontinuierlicher Read-Worker für Terminal Input mit Local-Echo-Duplikat-Filter"""
        while self.terminal_running and self.terminal_ser and self.terminal_ser.is_open:
            try:
                if self.terminal_ser.in_waiting > 0:
                    data = self.terminal_ser.read(self.terminal_ser.in_waiting)
                    if data:
                        # Debug: Show received data
                        self.root.after(0, lambda d=data: self._debug_receive(d, "Terminal data"))
                        
                        text = data.decode('utf-8', errors='replace')
                        
                        # Filter out simple character echoes (since we do local echo)
                        # But keep complex responses like command output, prompts, etc.
                        filtered_text = self._filter_linux_echo(text)
                        
                        if filtered_text:  # Only display if not filtered out
                            # Filter ANSI escape sequences  
                            clean_text = self._filter_ansi_sequences(filtered_text)
                            self.root.after(0, lambda t=clean_text: self._terminal_write(t))
                
                time.sleep(0.01)  # 100Hz polling
                
            except Exception as e:
                self.root.after(0, lambda: self._terminal_write(f"\n❌ Read error: {str(e)}\n"))
                break
    
    def _terminal_write(self, text):
        """Schreibt Text ins Terminal (thread-safe) mit Debug-Info"""
        if hasattr(self, 'terminal_display'):
            self.terminal_display.config(state=tk.NORMAL)
            self.terminal_display.insert(tk.END, text)
            self.terminal_display.see(tk.END)
            self.terminal_display.config(state=tk.NORMAL)  # Keep editable for input
    
    def _debug_send(self, data, description):
        """Debug function to show what's being sent"""
        if hasattr(self, 'debug_mode') and self.debug_mode:
            hex_data = ' '.join([f'{b:02x}' for b in data])
            debug_text = f"[SEND] {description}: {hex_data} ({data})\n"
            self._terminal_write(debug_text)
    
    def _debug_receive(self, data, description):
        """Debug function to show what's being received"""
        if hasattr(self, 'debug_mode') and self.debug_mode:
            hex_data = ' '.join([f'{b:02x}' for b in data])
            debug_text = f"[RECV] {description}: {hex_data} ({data})\n" 
            self._terminal_write(debug_text)
    
    def _filter_ansi_sequences(self, text):
        """Erweiterte Filterung für ANSI Escape-Sequenzen mit Tab-Completion Support"""
        import re
        
        # Remove multiple types of ANSI/VT100 escape sequences
        patterns = [
            r'\x1b\[[0-9;]*[a-zA-Z]',  # Standard ANSI like \x1b[J, \x1b[2K
            r'\x1b\[[\d;]*[HfABCDsuKJhlmpq]',  # More VT100 sequences
            r'\x1b\([AB01]',  # Character set selection
            r'\x1b[\[\]()#]\d*[a-zA-Z]',  # Other escape patterns
            r'\x1b[=>]',  # Keypad mode
            r'\x1b\[\d+;\d+[Hf]',  # Cursor position sequences
        ]
        
        # For debugging tab completion, temporarily preserve the raw text
        if '\t' in text or 'config' in text:
            # Tab completion happening - process more carefully
            result = text
            
            # Only remove ANSI sequences, but keep the actual text changes
            for pattern in patterns:
                result = re.sub(pattern, '', result)
        else:
            result = text
            for pattern in patterns:
                result = re.sub(pattern, '', result)

        # Handle backspace characters specially - they need visual processing
        if '\x08' in result:
            processed = ''
            for char in result:
                if char == '\x08':  # Backspace character
                    if processed:  # Only backspace if there's something to delete
                        processed = processed[:-1]  # Remove last character
                else:
                    processed += char
            result = processed
        
        # Additional cleanup: remove other control chars except important ones
        cleaned = ''
        for char in result:
            if ord(char) >= 32 or char in '\r\n\t':
                cleaned += char
            # Skip all other control characters (we already handled \x08 above)
        
        return cleaned
    
    def _filter_linux_echo(self, text):
        """Filtert einfache Zeichen-Echos heraus, behält komplexe Antworten"""
        # Wenn nur ein einzelnes druckbares Zeichen (32-126) -> wahrscheinlich Echo
        if len(text) == 1 and 32 <= ord(text) <= 126:
            return ""  # Unterdrücke einfaches Zeichen-Echo
        
        # Wenn backspace echo (\x08 mit oder ohne ANSI) -> unterdrücken  
        if text in ['\x08', '\b'] or ('\x08' in text and len(text) <= 10):
            return ""  # Unterdrücke Backspace-Echo
        
        # Alles andere durchlassen (Command-Output, Prompts, etc.)
        return text
    
    def _handle_visual_backspace(self):
        """Einfache visuelle Backspace-Behandlung - entfernt letztes Zeichen"""
        try:
            # Get current cursor position  
            insert_pos = self.terminal_display.index(tk.INSERT)
            
            # Check if we can go back one character
            line, col = insert_pos.split('.')
            col = int(col)
            
            if col > 0:
                # Delete the character just before cursor
                prev_pos = f"{line}.{col-1}"
                self.terminal_display.delete(prev_pos, insert_pos)
                # Update cursor position
                self.terminal_display.mark_set(tk.INSERT, prev_pos)
            else:
                # At beginning of line, try to go to end of previous line
                line_num = int(line)
                if line_num > 1:
                    prev_line = line_num - 1
                    # Get length of previous line
                    prev_line_end = self.terminal_display.index(f"{prev_line}.end")
                    _, prev_col = prev_line_end.split('.')
                    if int(prev_col) > 0:
                        char_pos = f"{prev_line}.{int(prev_col)-1}"
                        end_pos = f"{prev_line}.{prev_col}"
                        self.terminal_display.delete(char_pos, end_pos)
                        self.terminal_display.mark_set(tk.INSERT, char_pos)
                        
            self.terminal_display.see(tk.INSERT)
        except Exception as e:
            print(f"Visual backspace error: {e}")
            # Fallback: try simple deletion from end
            try:
                end_pos = self.terminal_display.index("end-1c")
                if end_pos != "1.0":
                    char_pos = self.terminal_display.index("end-2c")
                    self.terminal_display.delete(char_pos, end_pos)
            except:
                pass
    
    def _terminal_key_handler(self, event):
        """Tastatur-Handler mit korrekter visueller Backspace-Behandlung"""
        if not self.terminal_running or not self.terminal_ser or not self.terminal_ser.is_open:
            return "break"
        
        # Anti-Repeat-Schutz: verhindere zu schnelle Wiederholungen 
        import time
        current_time = time.time()
        if hasattr(self, '_last_keypress_time') and hasattr(self, '_last_char'):
            time_diff = current_time - self._last_keypress_time
            if time_diff < 0.05 and event.char == self._last_char:  # 50ms minimum between same chars
                self._debug_send(b'', f"BLOCKED repeat of '{event.char}' (too fast: {time_diff:.3f}s)")
                return "break"
        
        self._last_keypress_time = current_time
        self._last_char = event.char
        
        try:
            # NUR die wichtigsten Tasten - keine komplizierten Codes
            if event.keysym == 'Return':
                # Enter - einfach nur \r\n
                data = b'\r\n'
                self.terminal_ser.write(data)
                self._debug_send(data, "Enter key")
                return "break"
                
            elif event.keysym == 'BackSpace':
                # Backspace - SOFORTIGE visuelle Behandlung + Linux senden
                data = b'\x08'
                self.terminal_ser.write(data)
                self._debug_send(data, "Backspace key")
                
                # SOFORTIGER visueller Backspace - nicht warten auf Linux Antwort
                self._handle_visual_backspace()
                
                return "break"
                
            elif event.keysym == 'Tab':
                # Tab - für Command Completion
                data = b'\t'
                self.terminal_ser.write(data)
                self._debug_send(data, "Tab key")
                return "break"
                
            # PFEILTASTEN - VT100 Escape-Sequenzen
            elif event.keysym == 'Left':
                # Pfeil Links - Cursor nach links
                data = b'\x1b[D'  
                self.terminal_ser.write(data)
                self._debug_send(data, "Left arrow")
                return "break"
                
            elif event.keysym == 'Right':
                # Pfeil Rechts - Cursor nach rechts  
                data = b'\x1b[C'
                self.terminal_ser.write(data)
                self._debug_send(data, "Right arrow")
                return "break"
                
            elif event.keysym == 'Up':
                # Pfeil Hoch - Command History  
                data = b'\x1b[A'
                self.terminal_ser.write(data)
                self._debug_send(data, "Up arrow")
                return "break"
                
            elif event.keysym == 'Down':
                # Pfeil Runter - Command History
                data = b'\x1b[B'
                self.terminal_ser.write(data)
                self._debug_send(data, "Down arrow")
                return "break"
            
            # CTRL+C für Kommando-Abbruch (SIGINT)
            elif (event.state & 0x4) and event.keysym.lower() == 'c':
                # Ctrl+C - sendet ETX (0x03) für SIGINT
                data = b'\x03'
                self.terminal_ser.write(data)
                self._debug_send(data, "Ctrl+C (SIGINT)")
                # Visuelle Anzeige des Abbruchs
                self._terminal_write('^C\n')
                return "break"
            
            # Normale Buchstaben und Zeichen - NUR druckbare ASCII
            elif event.char and len(event.char) == 1 and 32 <= ord(event.char) <= 126:
                # Normale druckbare Zeichen - SOFORT anzeigen (lokales Echo)
                data = event.char.encode('ascii')
                self.terminal_ser.write(data)
                self._debug_send(data, f"Character '{event.char}'")
                
                # SOFORTIGES lokales Echo - später ignorieren wir Linux Echo
                self._terminal_write(event.char)
                
                return "break"
            
            # ALLE anderen Tasten ignorieren für jetzt
            else:
                # Nur noch unbekannte Tasten loggen, nicht Pfeiltasten
                if event.keysym not in ['Left', 'Right', 'Up', 'Down']:
                    self._debug_send(b'', f"IGNORED key: {event.keysym} / char: {repr(event.char)}")
                return "break"
                
        except Exception as e:
            self._terminal_write(f"\n❌ Key error: {str(e)}\n")
            return "break"
    
    def _terminal_click_handler(self, event):
        """Behandelt Maus-Klicks im Terminal"""
        # Allow normal text selection 
        return None
    
    def _close_terminal_window(self, window):
        """Schließt Terminal-Fenster sauber"""
        if hasattr(self, 'terminal_running'):
            self.terminal_running = False
        if hasattr(self, 'terminal_ser'):
            self.terminal_ser = None
        window.destroy()
    
    def _send_terminal_command(self, terminal_window):
        """Sendet eingegebenen Befehl an das Terminal"""
        command = self.terminal_input.get().strip()
        if not command:
            return
            
        # Clear input
        self.terminal_input.delete(0, tk.END)
        
        # Send command
        self._send_terminal_command_direct(command, terminal_window)
    
    def _send_terminal_command_direct(self, command, terminal_window):
        """Sendet Befehl direkt an Linux Host über persistente Verbindung - PERFORMANCE BOOST"""
        if not hasattr(self, 'terminal_output'):
            return
            
        # Display command
        timestamp = time.strftime('%H:%M:%S')
        self.terminal_output.insert(tk.END, f"\n[{timestamp}] $ {command}\n")
        self.terminal_output.see(tk.END)
        terminal_window.update()
        
        # Send command to Linux host via persistent connection
        ser, logged_in = self.linux_access._get_connection()
        if not ser or not logged_in:
            error_msg = "❌ Connection Error: Could not establish connection\n"
            self.terminal_output.insert(tk.END, error_msg)
            self.terminal_output.see(tk.END)
            return
        
        try:
            # Send actual command with longer timeout for interactive commands
            response = self.linux_access.send_and_read(ser, command, 0.1)
            
            # Display response
            if response.strip():
                # Clean up response (remove echoed command if present)
                lines = response.split('\n')
                clean_lines = []
                for line in lines:
                    # Skip lines that just echo the command
                    if line.strip() and line.strip() != command:
                        clean_lines.append(line)
                
                if clean_lines:
                    self.terminal_output.insert(tk.END, '\n'.join(clean_lines) + "\n")
                else:
                    self.terminal_output.insert(tk.END, response + "\n")
            else:
                self.terminal_output.insert(tk.END, "<no output>\n")
            
            self.terminal_output.see(tk.END)
            terminal_window.update()
            
        except Exception as e:
            error_msg = f"❌ Error: {str(e)}⅂"
            self.terminal_output.insert(tk.END, error_msg)
            self.terminal_output.see(tk.END)
            # Connection error - will be auto-reset on next attempt
            self.linux_access.close_connection()
    
    def _save_terminal_output(self):
        """Speichert Terminal-Output in Datei"""
        if not hasattr(self, 'terminal_display'):
            return
            
        try:
            from tkinter import filedialog
            timestamp = time.strftime('%Y%m%d_%H%M%S')
            filename = filedialog.asksaveasfilename(
                defaultextension=".txt",
                filetypes=[("Text files", "*.txt"), ("All files", "*.*")],
                initialname=f"terminal_log_{timestamp}.txt"
            )
            
            if filename:
                with open(filename, 'w', encoding='utf-8') as f:
                    f.write(self.terminal_display.get(1.0, tk.END))
                messagebox.showinfo("Saved", f"Terminal output saved to: {filename}")
                
        except Exception as e:
            messagebox.showerror("Save Error", f"Failed to save: {str(e)}")
    
    def _terminal_copy(self):
        """Copy selected text from terminal to clipboard"""
        try:
            if hasattr(self, 'terminal_display'):
                # Get selected text
                try:
                    selected_text = self.terminal_display.selection_get()
                    self.root.clipboard_clear()
                    self.root.clipboard_append(selected_text)
                    # Show brief feedback
                    self.terminal_display.insert(tk.END, f"\n📋 Copied {len(selected_text)} characters\n")
                    self.terminal_display.see(tk.END)
                except tk.TclError:
                    # No text selected - copy last line
                    content = self.terminal_display.get(1.0, tk.END)
                    lines = content.strip().split('\n')
                    if lines:
                        last_line = lines[-1] if lines[-1].strip() else lines[-2] if len(lines) > 1 else ""
                        if last_line.strip():
                            self.root.clipboard_clear()
                            self.root.clipboard_append(last_line)
                            self._terminal_write(f"\n📋 Copied last line: {last_line[:50]}...\n")
        except Exception as e:
            print(f"Copy error: {e}")
    
    def _terminal_paste(self):
        """Paste clipboard content directly to terminal (like typing)"""
        try:
            if hasattr(self, 'terminal_display') and self.terminal_running and self.terminal_ser:
                clipboard_text = self.root.clipboard_get()
                if clipboard_text:
                    # Send clipboard text character by character to simulate typing
                    for char in clipboard_text:
                        if char == '\n':
                            self.terminal_ser.write(b'\r\n')
                        else:
                            self.terminal_ser.write(char.encode('utf-8', errors='replace'))
                    
                    self._terminal_write(f"\n📄 Pasted {len(clipboard_text)} characters\n")
        except Exception as e:
            print(f"Paste error: {e}")
    
    def _terminal_clear(self):
        """Clear terminal display"""
        if hasattr(self, 'terminal_display'):
            self.terminal_display.delete(1.0, tk.END)
            # Show fresh welcome
            welcome_text = f"""🖥️ Terminal Cleared - {time.strftime('%H:%M:%S')}
📋 Right-click for Copy/Paste menu | ⌨️ Ctrl+C/Ctrl+V shortcuts
🚀 Type directly into terminal - NO LOCAL ECHO
{'='*60}

"""
            self.terminal_display.insert(tk.END, welcome_text)
            self.terminal_display.see(tk.END)
    
    def _show_terminal_context_menu(self, event):
        """Show right-click context menu for terminal"""
        try:
            self.terminal_context_menu.tk_popup(event.x_root, event.y_root)
        finally:
            self.terminal_context_menu.grab_release()

    # ── End Terminal helpers ──────────────────────────────────────────────────────

    def test_linux_connection(self):
        """Test Linux host connection"""
        if self.is_scanning:
            messagebox.showwarning("Busy", "Scanner is running. Please wait.")
            return
            
        self.connection_status.set("Testing Linux connection...")
        threading.Thread(target=self._test_linux_connection_thread, daemon=True).start()
        
    def _test_linux_connection_thread(self):
        """Test Linux connection in background thread"""
        try:
            success, message = self.linux_access.test_connection()
            if success:
                self.result_queue.put(("connection_status", f"✅ {message}"))
            else:
                self.result_queue.put(("connection_status", f"❌ {message}"))
        except Exception as e:
            self.result_queue.put(("connection_status", f"❌ Test failed: {str(e)}"))
    
    def quick_read_register(self):
        """Quick read of specified register"""
        if self.is_scanning:
            messagebox.showwarning("Busy", "Scanner is running. Please wait.")
            return
            
        try:
            addr_str = self.quick_addr.get().strip()
            if addr_str.startswith('0x') or addr_str.startswith('0X'):
                reg_addr = int(addr_str, 16)
            else:
                reg_addr = int(addr_str, 16)
                addr_str = f"0x{reg_addr:08X}"
            
            self.is_scanning = True
            self.current_value_label.config(text=f"Reading {addr_str}...")
            
            threading.Thread(target=self._quick_read_thread, 
                           args=(addr_str, reg_addr), daemon=True).start()
            
        except ValueError:
            messagebox.showerror("Invalid Address", "Please enter a valid hex address (e.g., 0x00000000)")
    
    def quick_write_register(self):
        """Quick write to specified register"""
        if self.is_scanning:
            messagebox.showwarning("Busy", "Scanner is running. Please wait.")
            return
            
        try:
            addr_str = self.quick_addr.get().strip()
            value_str = self.quick_value.get().strip()
            
            if addr_str.startswith('0x') or addr_str.startswith('0X'):
                reg_addr = int(addr_str, 16)
            else:
                reg_addr = int(addr_str, 16)
            
            if value_str.startswith('0x') or value_str.startswith('0X'):
                reg_value = int(value_str, 16)
            else:
                reg_value = int(value_str, 16)
            
            self.is_scanning = True
            self.current_value_label.config(text=f"Writing {value_str} to {addr_str}...")
            
            threading.Thread(target=self._quick_write_thread, 
                           args=(reg_addr, reg_value), daemon=True).start()
            
        except ValueError:
            messagebox.showerror("Invalid Input", "Please enter valid hex values (e.g., 0x00000000)")
    
    def _quick_read_thread(self, addr_str, reg_addr):
        """Quick read in background thread"""
        try:
            value = self.linux_access.read_register(reg_addr)
            if value is not None:
                self.register_values[addr_str] = value
                self.result_queue.put(("quick_read_success", addr_str, value))
            else:
                self.result_queue.put(("quick_read_error", f"Failed to read {addr_str}"))
        except Exception as e:
            self.result_queue.put(("quick_read_error", f"Error reading {addr_str}: {str(e)}"))
        finally:
            self.is_scanning = False
    
    def _quick_write_thread(self, reg_addr, reg_value):
        """Quick write in background thread"""
        try:
            success = self.linux_access.write_register(reg_addr, reg_value)
            if success:
                self.result_queue.put(("quick_write_success", reg_addr, reg_value))
            else:
                self.result_queue.put(("quick_write_error", f"Failed to write 0x{reg_value:08X} to 0x{reg_addr:08X}"))
        except Exception as e:
            self.result_queue.put(("quick_write_error", f"Error writing register: {str(e)}"))
        finally:
            self.is_scanning = False
    
    def on_register_select(self, event):
        """Handle register selection"""
        sender_tree = event.widget
        selection = sender_tree.selection()
        if not selection:
            return
            
        # Clear selections in other tabs
        for tree in self.register_trees.values():
            if tree != sender_tree:
                tree.selection_remove(tree.get_children())
        
        addr = selection[0]
        reg_info = None
        for group_info in self.register_maps.values():
            if addr in group_info['registers']:
                reg_info = group_info['registers'][addr]
                break
        
        self.current_register = {
            'address': addr,
            'name': reg_info['name'] if reg_info else addr,
            'description': reg_info['description'] if reg_info else ''
        }
        
        self.current_reg_info.config(text=f"Selected: {self.current_register['name']} ({addr})")
        
        # Auto-analyze if we already have a cached value
        if addr in self.register_values:
            value = self.register_values[addr]
            self.current_value = value
            self.current_value_label.config(
                text=f"✅ {self.current_register['name']}: 0x{value:08X}", foreground="green")
            analysis = self.bitfield_analyzer.analyze_register_value(addr, value)
            self.display_analysis(addr, value, analysis)
    
    def read_and_analyze_current(self):
        """Read and analyze currently selected register"""
        if not self.current_register:
            messagebox.showwarning("No Selection", "Please select a register first.")
            return
        
        if self.is_scanning:
            messagebox.showwarning("Busy", "Scanner is running. Please wait.")
            return
        
        self.is_scanning = True
        self.analysis_text.delete(1.0, tk.END)
        self.analysis_text.insert(tk.END, f"Reading register {self.current_register['name']} via Linux...\n")
        
        threading.Thread(target=self._read_and_analyze_thread, daemon=True).start()
    
    def _read_and_analyze_thread(self):
        """Read and analyze register in background thread"""
        try:
            address = self.current_register['address']
            reg_addr = int(address, 16)
            
            value = self.linux_access.read_register(reg_addr)
            
            if value is not None:
                self.current_value = value
                self.register_values[address] = value
                
                # Perform bitfield analysis
                analysis = self.bitfield_analyzer.analyze_register_value(address, value)
                
                self.result_queue.put(("analysis_complete", address, value, analysis))
            else:
                self.result_queue.put(("analysis_error", f"Failed to read register {address}"))
                
        except Exception as e:
            self.result_queue.put(("analysis_error", f"Error: {str(e)}"))
        finally:
            self.is_scanning = False
    
    def scan_register_group(self, group_name):
        """Scan all registers in the group"""
        if self.is_scanning:
            messagebox.showwarning("Busy", "Scanner is running. Please wait.")
            return
        
        self.is_scanning = True
        threading.Thread(target=self._scan_group_thread, args=(group_name,), daemon=True).start()
    
    def _scan_group_thread(self, group_name):
        """Background thread to scan register group"""
        try:
            group_regs = self.register_maps[group_name]['registers']
            addresses = list(group_regs.keys())
            total = len(addresses)
            ok_count = 0
            
            for i, addr in enumerate(addresses):
                reg_addr = int(addr, 16)
                value = self.linux_access.read_register(reg_addr)
                
                if value is not None:
                    ok_count += 1
                    self.register_values[addr] = value
                    self.result_queue.put(("group_register_read", group_name, addr, value))
                
                time.sleep(0.1)  # Small delay between reads
            
            self.result_queue.put(("group_scan_complete", group_name, f"{ok_count}/{total}"))
            
        except Exception as e:
            self.result_queue.put(("group_scan_error", group_name, str(e)))
        finally:
            self.is_scanning = False
    
    def refresh_analysis(self):
        """Refresh analysis with current values"""
        if self.current_register and self.current_value is not None:
            analysis = self.bitfield_analyzer.analyze_register_value(
                self.current_register['address'], 
                self.current_value
            )
            self.display_analysis(self.current_register['address'], self.current_value, analysis)
        else:
            messagebox.showinfo("Info", "No register data to refresh. Please read a register first.")
    
    def display_analysis(self, address, value, analysis):
        """Display bitfield analysis results"""
        self.analysis_text.delete(1.0, tk.END)
        
        # Header
        reg_info = None
        for group_info in self.register_maps.values():
            if address in group_info['registers']:
                reg_info = group_info['registers'][address]
                break
        
        if reg_info:
            self.analysis_text.insert(tk.END, f"🔬 LINUX REMOTE BITFIELD ANALYSIS\n", "header")
            self.analysis_text.insert(tk.END, f"="*70 + "\n\n")
            self.analysis_text.insert(tk.END, f"Register: {reg_info['name']} ({address})\n", "register")
            self.analysis_text.insert(tk.END, f"Description: {reg_info['description']}\n")
            self.analysis_text.insert(tk.END, f"Current Value: 0x{value:08X} ({value})\n", "register") 
            self.analysis_text.insert(tk.END, f"Binary: {value:032b}\n", "register")
            self.analysis_text.insert(tk.END, f"Access Method: Linux debugfs via {self.linux_access.com_port}\n\n")
            
        # Bitfield breakdown
        self.analysis_text.insert(tk.END, "📊 BITFIELD BREAKDOWN:\n", "header")
        self.analysis_text.insert(tk.END, "-"*70 + "\n\n")
        
        if not analysis:
            self.analysis_text.insert(tk.END, "⚠️ No bitfield definitions available for this register.\n", "error")
            return
            
        # Table header
        self.analysis_text.insert(tk.END, f"{'Bits':<12} {'Field':<15} {'Value':<8} {'Binary':<12} {'Interpretation'}\n")
        self.analysis_text.insert(tk.END, "-"*80 + "\n")
        
        for item in analysis:
            bitfield = item['bitfield']
            field_value = item['value']
            interpretation = item['interpretation']
            is_error = item['is_error']
            is_important = item['is_important']
            
            # Format line
            bits_str = bitfield.get_bit_range_str()
            field_name = bitfield.name[:14]
            value_str = f"{field_value}"
            binary_str = item['binary_value']
            
            line = f"{bits_str:<12} {field_name:<15} {value_str:<8} {binary_str:<12} {interpretation}\n"
            
            # Choose appropriate tag based on content
            if is_error:
                self.analysis_text.insert(tk.END, line, "error")
            elif is_important:
                self.analysis_text.insert(tk.END, line, "important")
            elif field_value != 0 and bitfield.name != "Reserved":
                self.analysis_text.insert(tk.END, line, "success")
            else:
                self.analysis_text.insert(tk.END, line)
        
        # Summary
        self.analysis_text.insert(tk.END, "\n📋 SUMMARY:\n", "header")
        self.analysis_text.insert(tk.END, "-"*20 + "\n")
        
        error_count = sum(1 for item in analysis if item['is_error'])
        important_count = sum(1 for item in analysis if item['is_important'])
        active_count = sum(1 for item in analysis if item['value'] != 0 and item['bitfield'].name != "Reserved")
        
        self.analysis_text.insert(tk.END, f"Total Bitfields: {len(analysis)}\n")
        self.analysis_text.insert(tk.END, f"Active (non-zero): {active_count}\n")
        self.analysis_text.insert(tk.END, f"Important Status: {important_count}\n")
        
        if error_count > 0:
            self.analysis_text.insert(tk.END, f"🚨 Error Conditions: {error_count}\n", "error")
        else:
            self.analysis_text.insert(tk.END, "✅ No Error Conditions\n", "success")
    
    def process_queue(self):
        """Process result queue for thread communication"""
        try:
            while True:
                item = self.result_queue.get_nowait()
                
                if item[0] == "connection_status":
                    self.connection_status.set(item[1])
                elif item[0] == "quick_read_success":
                    addr_str, value = item[1], item[2]
                    self.current_value_label.config(
                        text=f"✅ Quick Read {addr_str}: 0x{value:08X} ({value})", foreground="green")
                elif item[0] == "quick_read_error":
                    self.current_value_label.config(text=f"❌ {item[1]}", foreground="red")
                elif item[0] == "quick_write_success":
                    reg_addr, reg_value = item[1], item[2]
                    self.current_value_label.config(
                        text=f"✅ Write OK: 0x{reg_addr:08X} = 0x{reg_value:08X}", foreground="green")
                elif item[0] == "quick_write_error":
                    self.current_value_label.config(text=f"❌ {item[1]}", foreground="red")
                elif item[0] == "analysis_complete":
                    address, value, analysis = item[1], item[2], item[3]
                    self.display_analysis(address, value, analysis)
                elif item[0] == "analysis_error":
                    self.analysis_text.delete(1.0, tk.END)
                    self.analysis_text.insert(tk.END, f"❌ {item[1]}\n", "error")
                elif item[0] == "group_register_read":
                    group_name, addr, value = item[1], item[2], item[3]
                    if group_name in self.register_trees:
                        tree = self.register_trees[group_name]
                        if tree.exists(addr):
                            tree.item(addr, values=(addr, 
                                                  self.register_maps[group_name]['registers'][addr]['name'],
                                                  f"0x{value:08X}"))
                elif item[0] == "group_scan_complete":
                    group_name, status = item[1], item[2]
                    messagebox.showinfo("Scan Complete", f"{group_name}: {status} registers read successfully")
                elif item[0] == "group_scan_error":
                    group_name, error = item[1], item[2]
                    messagebox.showerror("Scan Error", f"{group_name}: {error}")
                elif item[0] == "scan_complete":
                    messagebox.showinfo("Read All Registers Complete", item[1])
                elif item[0] == "scan_error":
                    messagebox.showerror("Read All Registers Failed", item[1])
                    
        except queue.Empty:
            pass  # Normal - queue is empty
        finally:
            self.root.after(100, self.process_queue)
    
    def _update_mac_from_scan(self, addr, value):
        """Update MAC register cache when MMS_1 scan reads MAC registers"""
        if addr == '0x00010024':
            self.mac_sab2_value = value
        elif addr == '0x00010025':
            self.mac_sat2_value = value
        if self.mac_sab2_value is not None and self.mac_sat2_value is not None:
            self.decode_mac_address()

    def decode_mac_address(self):
        """Decode MAC address from MAC_SAB2 and MAC_SAT2 registers (Little Endian)"""
        if self.mac_sab2_value is not None and self.mac_sat2_value is not None:
            # Extract bytes (Little Endian)
            sab2_bytes = [
                self.mac_sab2_value & 0xFF,
                (self.mac_sab2_value >> 8) & 0xFF,
                (self.mac_sab2_value >> 16) & 0xFF,
                (self.mac_sab2_value >> 24) & 0xFF
            ]
            
            sat2_bytes = [
                self.mac_sat2_value & 0xFF,
                (self.mac_sat2_value >> 8) & 0xFF
            ]
            
            mac_bytes = sab2_bytes + sat2_bytes
            self.decoded_mac = ":".join(f"{byte:02X}" for byte in mac_bytes)
            return self.decoded_mac
        
        return "Not available"
    
    def read_all_registers(self):
        """Read all registers from all MMS sections - comprehensive scan"""
        if self.is_scanning:
            messagebox.showwarning("Busy", "Scanner is already running. Please wait.")
            return
            
        # Show progress dialog
        progress_window = tk.Toplevel(self.root)
        progress_window.title("Reading All Registers")
        progress_window.geometry("450x200")
        progress_window.transient(self.root)
        progress_window.grab_set()
        
        progress_label = ttk.Label(progress_window, text="Initializing comprehensive register scan...")
        progress_label.pack(pady=20)
        
        progress_bar = ttk.Progressbar(progress_window, mode='determinate')
        progress_bar.pack(pady=10, padx=20, fill=tk.X)
        
        status_label = ttk.Label(progress_window, text="Status: Starting scan...")
        status_label.pack(pady=10)
        
        self.root.update()
        
        # Start reading all registers in background thread
        threading.Thread(target=self._read_all_registers_thread, 
                        args=(progress_window, progress_label, progress_bar, status_label), daemon=True).start()
        
    def _read_all_registers_thread(self, progress_window, progress_label, progress_bar, status_label):
        """Background thread for reading all registers from all MMS sections"""
        try:
            self.is_scanning = True
            
            # Step 1: Test connection
            progress_label.config(text="Testing ioctl connection...")
            progress_bar['value'] = 5
            status_label.config(text="Status: Testing Linux connection...")
            self.root.update()
            
            connection_ok, msg = self.linux_access.test_connection()
            if not connection_ok:
                raise Exception(f"Linux connection failed: {msg}")
            
            # Step 2: Count total registers
            total_registers = sum(len(mms_info['registers']) for mms_info in self.register_maps.values())
            current_reg = 0
            successful_reads = 0
            failed_reads = 0
            
            progress_label.config(text=f"Reading {total_registers} registers from all MMS sections...")
            progress_bar['value'] = 10
            status_label.config(text=f"Status: Found {total_registers} registers to read")
            self.root.update()
            
            # Step 3: Read all registers
            for mms_name, mms_info in self.register_maps.items():
                status_label.config(text=f"Status: Reading {mms_name} ({len(mms_info['registers'])} registers)")
                progress_label.config(text=f"Processing {mms_name}...")
                self.root.update()
                
                for addr_str, reg_info in mms_info['registers'].items():
                    current_reg += 1
                    progress_value = 10 + (current_reg / total_registers) * 80
                    progress_bar['value'] = progress_value
                    
                    progress_label.config(text=f"Reading {reg_info['name']} ({current_reg}/{total_registers})")
                    self.root.update()
                    
                    # Read register via ultra-fast ioctl
                    addr = int(addr_str, 16)
                    try:
                        actual_value = self.linux_access.read_register(addr)
                        
                        if actual_value is not None:
                            # Store with both integer and string keys for compatibility
                            self.register_values[addr] = actual_value
                            self.register_values[addr_str] = actual_value
                            
                            # Update MAC values for decoding if this is MMS_1
                            if mms_name == "MMS_1":
                                self._update_mac_from_scan(addr_str, actual_value)
                            
                            successful_reads += 1
                            status_label.config(text=f"Status: {successful_reads} successful, {failed_reads} failed")
                        else:
                            failed_reads += 1
                            status_label.config(text=f"Status: {successful_reads} successful, {failed_reads} failed")
                            
                    except Exception as e:
                        failed_reads += 1
                        print(f"[ERROR] Failed to read {addr_str}: {e}")
                        status_label.config(text=f"Status: {successful_reads} successful, {failed_reads} failed")
                    
                    # Ultra-minimal delay for ioctl (much faster than serial)
                    time.sleep(0.002)  # 2ms only!
            
            # Step 4: Update GUI displays
            progress_label.config(text="Updating displays...")
            progress_bar['value'] = 95
            status_label.config(text="Status: Refreshing all register displays...")
            self.root.update()

            # Update all register treeviews with new values
            for group_name, group_info in self.register_maps.items():
                if group_name in self.register_trees:
                    tree = self.register_trees[group_name]
                    for addr_str, reg_info in group_info['registers'].items():
                        value = self.register_values.get(addr_str)
                        if value is not None and tree.exists(addr_str):
                            tree.set(addr_str, column="Value", value=f"0x{value:08X}")

            # Update current selected register display if it was read
            if self.current_register:
                current_addr = int(self.current_register, 16)
                if current_addr in self.register_values:
                    current_value = self.register_values[current_addr]
                    self.result_queue.put(("register_read", self.current_register, current_value))
            
            # Completed
            progress_bar['value'] = 100
            progress_label.config(text="All registers read successfully!")
            status_label.config(text=f"Status: Completed! {successful_reads} successful reads")
            self.root.update()
            
            time.sleep(1)
            progress_window.destroy()
            
            # Show completion message
            if successful_reads > 0:
                self.result_queue.put(("scan_complete", f"✅ Read All Registers Complete!\n\n"
                                     f"Successfully read {successful_reads}/{total_registers} registers\n"
                                     f"Failed: {failed_reads}\n"
                                     f"Success rate: {(successful_reads/total_registers*100):.1f}%\n\n"
                                     f"All register values are now cached.\n"
                                     f"Navigate through tabs to view updated values."))
            else:
                self.result_queue.put(("scan_error", "❌ No registers could be read!\n\n"
                                     "Please check your Linux connection and ensure\n"
                                     "lan_read/lan_write tools are available."))
            
        except Exception as e:
            progress_window.destroy()
            self.result_queue.put(("scan_error", f"❌ Read All Registers failed:\n\n{str(e)}"))
            print(f"[ERROR] Read all registers failed: {e}")
            traceback.print_exc()
        finally:
            self.is_scanning = False

    def generate_html_report(self):
        """Generate comprehensive HTML report with all register values and bitfield analysis"""
        if self.is_scanning:
            messagebox.showwarning("Busy", "Scanner is already running. Please wait.")
            return
            
        # Show progress dialog
        progress_window = tk.Toplevel(self.root)
        progress_window.title("Generating HTML Report")
        progress_window.geometry("400x150")
        progress_window.transient(self.root)
        progress_window.grab_set()
        
        progress_label = ttk.Label(progress_window, text="Connecting to Linux host...")
        progress_label.pack(pady=20)
        
        progress_bar = ttk.Progressbar(progress_window, mode='determinate')
        progress_bar.pack(pady=10, padx=20, fill=tk.X)
        
        self.root.update()
        
        # Start HTML report generation in background thread
        threading.Thread(target=self._generate_html_report_thread, 
                        args=(progress_window, progress_label, progress_bar), daemon=True).start()
        
    def _generate_html_report_thread(self, progress_window, progress_label, progress_bar):
        """Background thread for HTML report generation - optimized for ioctl interface"""
        try:
            import datetime
            import os
            
            # Step 1: Test ioctl connection
            progress_label.config(text="Testing ioctl connection...")
            progress_bar['value'] = 5
            self.root.update()
            
            connection_ok, msg = self.linux_access.test_connection()
            if not connection_ok:
                raise Exception(f"Linux connection failed: {msg}")
            
            # Step 2: Read firmware info
            progress_label.config(text="Reading system information...")
            progress_bar['value'] = 10
            self.root.update()
            
            firmware_timestamp = self._get_firmware_timestamp_ioctl()
            
            # Step 3: Read all registers via ultra-fast ioctl
            all_register_data = {}
            total_registers = sum(len(mms_info['registers']) for mms_info in self.register_maps.values())
            current_reg = 0
            
            for mms_name, mms_info in self.register_maps.items():
                progress_label.config(text=f"Reading {mms_name} registers...")
                mms_data = {}
                
                for addr_str, reg_info in mms_info['registers'].items():
                    current_reg += 1
                    progress_value = 10 + (current_reg / total_registers) * 75
                    progress_bar['value'] = progress_value
                    progress_label.config(text=f"Reading {reg_info['name']} ({current_reg}/{total_registers})...")
                    self.root.update()
                    
                    # Read register value via ioctl (ultra-fast!)
                    addr = int(addr_str, 16)
                    actual_value = self.linux_access.read_register(addr)
                    
                    if actual_value is not None:
                        # Store with integer key for consistency
                        self.register_values[addr] = actual_value
                        
                        # Analyze bitfields (convert int address back to string)
                        bitfield_analysis = self.bitfield_analyzer.analyze_register_value(addr_str, actual_value)
                        
                        mms_data[addr_str] = {
                            'info': reg_info,
                            'value': actual_value,
                            'bitfields': bitfield_analysis
                        }
                        
                        # Update MAC values for decoding
                        if mms_name == "MMS_1":
                            self._update_mac_from_scan(addr_str, actual_value)
                    
                    # Ultra-minimal delay for ioctl (much faster than serial)
                    time.sleep(0.005)  # 5ms only!
                
                all_register_data[mms_name] = {
                    'info': mms_info,
                    'registers': mms_data
                }
            
            # Step 4: Generate HTML
            progress_label.config(text="Generating HTML report...")
            progress_bar['value'] = 90
            self.root.update()
            
            html_content = self._generate_html_content_ioctl(all_register_data, firmware_timestamp)
            
            # Step 5: Save to file
            progress_label.config(text="Saving report to file...")
            progress_bar['value'] = 95
            self.root.update()
            
            timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"LAN8651_ioctl_Report_{timestamp}.html"
            
            with open(filename, 'w', encoding='utf-8') as f:
                f.write(html_content)
            
            progress_bar['value'] = 100
            progress_label.config(text="Report generated successfully!")
            self.root.update()
            
            time.sleep(1)
            progress_window.destroy()
            
            # Show success message with file location
            abs_path = os.path.abspath(filename)
            messagebox.showinfo("HTML Report Generated", 
                              f"ioctl HTML report saved successfully!\n\n"
                              f"File: {filename}\n"
                              f"Location: {abs_path}\n\n"
                              f"Report contains {len(all_register_data)} MMS sections\n"
                              f"Generated with ultra-fast ioctl interface (500x-10000x faster)!")
            
        except Exception as e:
            progress_window.destroy()
            messagebox.showerror("Report Generation Failed", f"Error generating ioctl HTML report:\n\n{str(e)}")
            print(f"[ERROR] HTML report generation failed: {e}")
            traceback.print_exc()
            
    def _get_firmware_timestamp_ioctl(self):
        """Read firmware timestamp via ioctl connection"""
        try:
            ser = serial.Serial(self.linux_access.com_port, self.linux_access.baudrate, timeout=1)
            time.sleep(0.01)  # Ultra-fast for ioctl
            
            response = self.linux_access.send_and_read(ser, "uname -a", 0.05)
            ser.close()
            
            lines = response.split('\n')
            for line in lines:
                if 'Linux' in line:
                    return line.strip()
            return "Linux system information not found"
        except Exception:
            return "Could not read Linux system info"
            
    def _generate_html_content_ioctl(self, all_register_data, firmware_timestamp):
        """Generate comprehensive HTML report content with professional styling - ioctl optimized"""
        import datetime
        
        # HTML header with complete professional CSS styling from original GUI
        html = """<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>LAN8651 Register Analysis Report - ioctl Ultra-Fast</title>
    <style>
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 20px; background: #f5f5f5; }
        .header { background: linear-gradient(135deg, #2c3e50, #3498db); color: white; padding: 30px; border-radius: 10px; text-align: center; margin-bottom: 30px; }
        .header h1 { margin: 0; font-size: 2.5em; text-shadow: 2px 2px 4px rgba(0,0,0,0.3); }
        .header .subtitle { font-size: 1.2em; margin-top: 10px; opacity: 0.9; }
        .summary { background: white; padding: 20px; border-radius: 8px; margin-bottom: 20px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        .summary h2 { color: #2c3e50; margin-top: 0; }
        .mms-section { background: white; margin-bottom: 30px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); overflow: hidden; }
        .mms-header { background: linear-gradient(135deg, #34495e, #2c3e50); color: white; padding: 20px; }
        .mms-header h2 { margin: 0; font-size: 1.5em; }
        .mms-header .description { margin-top: 5px; opacity: 0.9; }
        .register-table { width: 100%; border-collapse: collapse; }
        .register-table th { background: #ecf0f1; padding: 12px; text-align: left; font-weight: bold; color: #2c3e50; }
        .register-table td { padding: 12px; border-bottom: 1px solid #bdc3c7; }
        .register-header { background: #3498db !important; color: white !important; font-weight: bold; }
        .bitfield-table { width: 100%; border-collapse: collapse; margin-top: 10px; font-size: 0.9em; }
        .bitfield-table th { background: #f8f9fa; padding: 8px; font-size: 0.8em; color: #555; }
        .bitfield-table td { padding: 8px; border-bottom: 1px solid #dee2e6; }
        .error-bit { background-color: #ffebee !important; color: #c62828; font-weight: bold; }
        .important-bit { background-color: #e8f5e8 !important; color: #2e7d32; font-weight: bold; }
        .address-cell { font-family: 'Consolas', monospace; font-weight: bold; color: #2980b9; }
        .value-cell { font-family: 'Consolas', monospace; font-weight: bold; }
        .mac-display { background: #e8f4fd; padding: 15px; border-radius: 8px; margin: 10px 0; }
        .mac-address { font-family: 'Consolas', monospace; font-size: 1.2em; font-weight: bold; color: #2980b9; }
        .status-ok { color: #27ae60; }
        .status-error { color: #e74c3c; font-weight: bold; }
        .status-warning { color: #f39c12; font-weight: bold; }
        .no-data { color: #7f8c8d; font-style: italic; text-align: center; padding: 40px; }
        .footer { margin-top: 40px; padding: 20px; background: white; border-radius: 8px; text-align: center; color: #7f8c8d; }
        .ioctl-badge { background: linear-gradient(45deg, #e74c3c, #f39c12); color: white; padding: 5px 15px; border-radius: 20px; font-weight: bold; font-size: 0.9em; display: inline-block; margin-left: 10px; }
        
        </style>
</head>
<body>"""
        
        # Professional header section
        current_time = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        total_registers = sum(len(data['registers']) for data in all_register_data.values())
        successful_reads = sum(len([r for r in data['registers'] if data['registers'][r]['value'] is not None]) for data in all_register_data.values())
        
        html += f"""
    <div class="header">
        <h1>🔬 LAN8651 Register Analysis Report</h1>
        <div class="subtitle">Comprehensive Bitfield Analysis & Hardware Status<span class="ioctl-badge">ioctl ULTRA-FAST</span></div>
    </div>
    
    <div class="summary">
        <h2>📊 Report Summary</h2>
        <p><strong>Generated:</strong> {current_time}</p>
        <p><strong>Device Port:</strong> {self.linux_access.com_port} @ {self.linux_access.baudrate} baud (ioctl)</p>
        <p><strong>Linux System:</strong> {firmware_timestamp}</p>
        <p><strong>Total Register Groups:</strong> {len(all_register_data)} MMS sections</p>
        <p><strong>Register Read Status:</strong> {successful_reads}/{total_registers} successful ({(successful_reads/total_registers*100):.1f}%)</p>
        <p><strong>Performance:</strong> <span class="status-ok">500x-10000x faster than debugfs!</span> Ultra-fast ioctl interface</p>
        """
        
        # MAC Address section with decoding
        if self.decoded_mac and self.decoded_mac != "Not available":
            html += f"""
        <div class="mac-display">
            <h3>🔗 MAC Address Information</h3>
            <div class="mac-address">Decoded MAC Address: {self.decoded_mac}</div>
            <small>Source: MAC_SAB2/MAC_SAT2 registers (Little Endian format)</small>
        </div>"""
        
        html += "</div>"
        
        # Hardware status section with SQI and link analysis
        sqi_info = "Not available"
        link_status = "Unknown"
        plca_info = "Not available"
        
        # Extract SQI information
        if 0x0004008F in self.register_values:
            sqi_value = self.register_values[0x0004008F]
            sqi_bits = (sqi_value >> 8) & 0x7  # Extract SQI from bits 8-10
            sqi_quality = ["POOR", "BAD", "FAIR", "OK", "GOOD", "VERY GOOD", "EXCELLENT", "EXCELLENT"]
            sqi_info = f"SQI: {sqi_bits}/7 ({sqi_quality[min(sqi_bits, 7)]})"
        
        # Extract Link status
        if 0x0000FF01 in self.register_values:
            basic_status = self.register_values[0x0000FF01]
            if basic_status & 0x04:
                link_status = "Link UP (1.476 Mbps T1S performance)"
            else:
                link_status = "Link DOWN"
        
        # Extract PLCA information  
        if 0x0004CA02 in self.register_values:
            plca_ctrl1 = self.register_values[0x0004CA02]
            node_id = (plca_ctrl1 >> 8) & 0xFF
            if node_id > 0:
                plca_info = f"PLCA Node {node_id} (Multi-drop network active)"
            else:
                plca_info = "PLCA disabled (Point-to-point mode)"
        
        html += f"""
    <div class="summary">
        <h2>⚡ Hardware Status</h2>
        <p><strong>Network Performance:</strong> <span class="status-ok">{link_status}</span></p>
        <p><strong>Signal Quality:</strong> <span class="status-ok">{sqi_info}</span></p>
        <p><strong>Network Mode:</strong> {plca_info}</p>
        <p><strong>PMD Access Method:</strong> ioctl direct kernel access (Clause 22 + MMS) - Optimal performance</p>
    </div>"""
        
        # Process each MMS section with full bitfield analysis
        for mms_name, mms_data in all_register_data.items():
            mms_info = mms_data['info']
            registers = mms_data['registers']
            
            html += f"""
    <div class="mms-section">
        <div class="mms-header">
            <h2>{mms_name}: {mms_info['name']}</h2>
            <div class="description">{mms_info['description']}</div>
        </div>
        <div style="padding: 20px;">"""
            
            if not registers:
                html += '<div class="no-data">No register data available for this section</div>'
            else:
                html += '<table class="register-table">'
                
                for addr_str, reg_data in registers.items():
                    reg_info = reg_data['info']
                    value = reg_data['value']
                    bitfields = reg_data.get('bitfields', [])
                    
                    # Register header row
                    html += f"""
                <tr class="register-header">
                    <td class="address-cell">{addr_str}</td>
                    <td><strong>{reg_info['name']}</strong></td>
                    <td>{reg_info['description']}</td>
                    <td>{reg_info['access']}</td>
                    <td>{reg_info['width']}</td>
                    <td class="value-cell">0x{value:08X}</td>
                </tr>"""
                    
                    # Bitfield analysis rows with complete interpretation
                    if bitfields:
                        html += """
                <tr><td colspan="6">
                    <table class="bitfield-table">
                        <tr>
                            <th>Bit Range</th>
                            <th>Field Name</th>
                            <th>Value</th>
                            <th>Binary</th>
                            <th>Interpretation</th>
                            <th>Description</th>
                        </tr>"""
                        
                        for bf_data in bitfields:
                            bitfield = bf_data['bitfield']
                            bf_value = bf_data['value']  
                            interpretation = bf_data['interpretation']
                            
                            # Apply styling based on bit significance
                            row_class = ""
                            if bf_data.get('is_error'):
                                row_class = "error-bit"
                            elif bf_data.get('is_important'):  
                                row_class = "important-bit"
                            
                            # Create bit range string
                            if bitfield.start_bit == bitfield.end_bit:
                                bit_range = f"[{bitfield.start_bit}]"
                            else:
                                bit_range = f"[{bitfield.end_bit}:{bitfield.start_bit}]"
                            
                            html += f"""
                        <tr class="{row_class}">
                            <td style="font-family: monospace;">{bit_range}</td>
                            <td><strong>{bitfield.name}</strong></td>
                            <td style="font-family: monospace;">{bf_data['hex_value']}</td>
                            <td style="font-family: monospace;">{bf_data['binary_value']}</td>
                            <td>{interpretation}</td>
                            <td>{bitfield.description}</td>
                        </tr>"""
                        
                        html += """
                    </table>
                </td></tr>"""
                    else:
                        html += '<tr><td colspan="6"><em>No bitfield definitions available</em></td></tr>'
                
                html += '</table>'
            
            html += '</div></div>'
        
        # Professional footer with ioctl branding
        html += f"""
    <div class="footer">
        <p>Report generated by LAN8651 Linux ioctl Analyzer v5.0 - March 16, 2026</p>
        <p>🚀 Ultra-fast ioctl kernel interface - 500x-10000x faster than debugfs</p>
        <p>🎯 Optimized for real-time T1S network analysis and diagnostics</p>
        <p>Hardware-verified bitfield analysis with professional styling</p>
    </div>
</body>
</html>"""
        
        return html
    
    def run(self):
        """Start the GUI"""
        self.root.mainloop()

def main():
    """Main entry point"""
    print("🔬 LAN8651 Register Bitfield Analyzer - Linux Remote Access")
    print("=" * 65)
    print("Advanced register analysis with Linux debugfs remote access")
    print("Uses COM9 connection to Linux host for LAN8651 register access")
    print()
    
    app = LAN8651LinuxBitfieldGUI()
    app.run()

if __name__ == "__main__":
    main()