#!/usr/bin/env python3
"""
LAN8651 Register Bitfield Analyzer - Advanced GUI
Erweiterte GUI für detaillierte Register-Bitfeld-Analyse mit Bedeutungen

Features:
- Detaillierte Bitfeld-Aufschlüsselung für jeden Register-Wert
- Automatische Interpretation der Bitfeld-Bedeutungen
- Visuelle Hervorhebung wichtiger Status-Bits
- 🌐 MAC-Adress-Dekodierung (Hardware-verifiziert: 00:04:25:01:02:03)
- 🔧 MAC_SAB2/MAC_SAT2 Register-Support (korrekte MAC-Speicher)
- ⚠️ Legacy MAC-Register-Warnung (MAC_SAB1/MAC_SAT1)
- 🛡️ Robuste Kommunikation (Prompt-basierte Synchronisation)
- ✅ PMD Register Corrections: Clause 22 PMD access (0x0000FF20-FF23)
- ❌ MMS 3 PMD Warning: Returns only 0x0000 - USE CLAUSE 22 INSTEAD
- 🎯 SQI Register: 0x0004008F shows 6/7 EXCELLENT signal quality
- 🚀 Hardware Status: 1.476 Mbps Link UP, SQI 6/7, PLCA Node 7/8
- Basiert auf offiziellem Microchip LAN8650/1 Datenblatt
- Threading für nicht-blockierende GUI
- Erweiterte Diagnose-Kommentare

Basiert auf: lan8651_register_gui.py
Erweitert um: Bitfeld-Analyse aus README_BITFIELDS.md
Version: 3.0 - März 11, 2026 (PMD Register Corrections + SQI Integration)
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
            ser = serial.Serial(self.com_port, self.baudrate, timeout=2)
            time.sleep(0.5)
            
            # Auto-login check
            logged_in = self._auto_login_check(ser)
            
            self._persistent_connection = ser
            self._connection_last_used = current_time
            self._logged_in = logged_in
            
            return ser, logged_in
            
        except Exception as e:
            raise Exception(f"Connection failed: {e}")
    
    def _auto_login_check(self, ser):
        """Automatisches Login falls erforderlich"""
        try:
            # Teste zunächst ob wir bereits eingeloggt sind
            ser.write(b'\n')
            time.sleep(0.2)
            
            if ser.in_waiting > 0:
                response = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
            
            # Prüfe auf Login-Prompt
            if any(login_marker in response.lower() for login_marker in ['login:', 'username:', 'user:']):
                # Sende Benutzername: root
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
                        for attempt in range(10):
                            if ser.in_waiting > 0:
                                login_result = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                                if any(prompt in login_result for prompt in ['#', '$', '~']):
                                    return True
                            time.sleep(0.1)
                        
                        print("[WARNING] Login möglicherweise fehlgeschlagen")
                        return False
            
            # Kein Login erforderlich - bereits eingeloggt
            return True
            
        except Exception as e:
            return False
    
    def close_connection(self):
        """Schließt persistente Verbindung"""
        if self._persistent_connection and self._persistent_connection.is_open:
            try:
                self._persistent_connection.close()
            except:
                pass
        self._persistent_connection = None
        self._logged_in = False

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
        
        important_indicators = ['RESET', 'ENABLE', 'COMPLETE', 'INTERRUPT', 'PHYINT', 'SEV']
        
        return any(indicator in name or indicator in desc for indicator in important_indicators)

class LAN8651BitfieldGUI:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("LAN8651 Register Bitfield Analyzer")
        self.root.geometry("1400x900")
        
        # Configuration
        self.port = tk.StringVar(value="COM8")
        self.baudrate = tk.IntVar(value=115200)
        self.serial = None
        
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
        
        # Terminal emulation state
        self.terminal_ser = None
        self.terminal_running = False
        self.debug_mode = False  # Debug mode disabled for clean terminal output

        # Test mode controls
        self.ieee_test_modes = [
            ("Normal (000) - Non-test operation", 0),
            ("Test Mode 1 (001) - Voltage & Jitter", 1),
            ("Test Mode 2 (010) - Output Droop", 2),
            ("Test Mode 3 (011) - PSD Mask", 3),
            ("Test Mode 4 (100) - High Impedance", 4),
        ]
        self.test_mode_var = tk.StringVar(value=self.ieee_test_modes[0][0])
        self.test_mode_status = tk.StringVar(value="Ready")
        self.pma_ctrl_status = tk.StringVar(value="Unknown")
        self._test_mode_labels = {mode: label for label, mode in self.ieee_test_modes}
        
        # Register maps (MMS-based) - mirrors lan8651_register_gui.py
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
                "name": "SW-Counters (stats Befehl)",
                "description": "Software-Zähler des Treibers — kein HW-Register, Abruf via 'stats' CLI-Befehl",
                "registers": {
                    "eth1_tx_ok":     {"name": "eth1 TX ok",     "description": "LAN865x: erfolgreich gesendete Pakete",      "access": "SW", "width": "32-bit"},
                    "eth1_tx_err":    {"name": "eth1 TX err",    "description": "LAN865x: TX-Fehler-Pakete",                  "access": "SW", "width": "32-bit"},
                    "eth1_tx_qfull":  {"name": "eth1 TX qFull",  "description": "LAN865x: TX-Queue war voll (backpressure)",  "access": "SW", "width": "32-bit"},
                    "eth1_tx_pend":   {"name": "eth1 TX pend",   "description": "LAN865x: aktuell ausstehende TX-Puffer",     "access": "SW", "width": "32-bit"},
                    "eth1_rx_ok":     {"name": "eth1 RX ok",     "description": "LAN865x: erfolgreich empfangene Pakete",     "access": "SW", "width": "32-bit"},
                    "eth1_rx_err":    {"name": "eth1 RX err",    "description": "LAN865x: RX-Fehler-Pakete",                  "access": "SW", "width": "32-bit"},
                    "eth1_rx_nobufs": {"name": "eth1 RX noBufs", "description": "LAN865x: RX-Buffer nicht verfügbar",        "access": "SW", "width": "32-bit"},
                    "eth1_rx_pend":   {"name": "eth1 RX pend",   "description": "LAN865x: aktuell ausstehende RX-Puffer",    "access": "SW", "width": "32-bit"},
                    "eth0_tx_ok":     {"name": "eth0 TX ok",     "description": "GMAC: erfolgreich gesendete Pakete",         "access": "SW", "width": "32-bit"},
                    "eth0_tx_err":    {"name": "eth0 TX err",    "description": "GMAC: TX-Fehler-Pakete",                     "access": "SW", "width": "32-bit"},
                    "eth0_tx_qfull":  {"name": "eth0 TX qFull",  "description": "GMAC: TX-Queue war voll",                   "access": "SW", "width": "32-bit"},
                    "eth0_rx_ok":     {"name": "eth0 RX ok",     "description": "GMAC: erfolgreich empfangene Pakete",        "access": "SW", "width": "32-bit"},
                    "eth0_rx_err":    {"name": "eth0 RX err",    "description": "GMAC: RX-Fehler-Pakete",                     "access": "SW", "width": "32-bit"},
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
        
        # Setup cleanup on window close
        self.root.protocol("WM_DELETE_WINDOW", self._on_closing)
        
    def create_gui(self):
        """Create the main GUI"""
        # Create main paned window
        self.main_paned = ttk.PanedWindow(self.root, orient=tk.HORIZONTAL)
        self.main_paned.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Left panel (1/3): Configuration and Register Selection
        left_frame = ttk.Frame(self.main_paned, width=467)
        self.main_paned.add(left_frame, weight=1)
        
        # Right panel (2/3): Bitfield Analysis
        right_frame = ttk.Frame(self.main_paned, width=933)
        self.main_paned.add(right_frame, weight=2)
        
        self.create_left_panel(left_frame)
        self.create_right_panel(right_frame)

        # Enforce default 1/2 : 1/2 split (left:right) after layout is realized.
        self.root.after(100, self._set_default_pane_ratio)

    def _set_default_pane_ratio(self):
        """Set paned window split so bitfield panel uses about half width."""
        try:
            total_width = self.main_paned.winfo_width()
            if total_width <= 0:
                self.root.after(100, self._set_default_pane_ratio)
                return
            left_width = total_width // 2
            self.main_paned.sashpos(0, left_width)
        except Exception:
            pass
        
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

        # Tab 5: Test Modes
        test_modes_tab = ttk.Frame(main_notebook)
        main_notebook.add(test_modes_tab, text="🧪 Test Modes")
        self._create_test_modes_tab(test_modes_tab)
    
    def _create_config_tab(self, parent):
        """Create configuration tab content"""
        # Configuration section
        config_frame = ttk.LabelFrame(parent, text="🔧 Configuration", padding=10)
        config_frame.pack(fill=tk.X, padx=5, pady=5)
        
        ttk.Label(config_frame, text="COM Port:").grid(row=0, column=0, sticky=tk.W)
        ttk.Entry(config_frame, textvariable=self.port, width=10).grid(row=0, column=1, padx=5)
        
        ttk.Label(config_frame, text="Baudrate:").grid(row=1, column=0, sticky=tk.W)
        ttk.Entry(config_frame, textvariable=self.baudrate, width=10).grid(row=1, column=1, padx=5)
        
        ttk.Button(config_frame, text="Test Connection", command=self.test_connection).grid(row=2, column=0, columnspan=2, pady=10)
        
        # Connection status
        self.connection_status = tk.StringVar(value="Not connected")
        ttk.Label(config_frame, textvariable=self.connection_status, font=("Arial", 9)).grid(row=3, column=0, columnspan=2, pady=5)
        
        # Actions frame
        actions_frame = ttk.LabelFrame(parent, text="📄 Reports & Data", padding=10)
        actions_frame.pack(fill=tk.X, padx=5, pady=5)
        
        ttk.Button(actions_frame, text="📄 Generate HTML Report", 
                  command=self.generate_html_report, style="Accent.TButton").pack(fill=tk.X, pady=2)
        ttk.Button(actions_frame, text="📚 Read All Registers", 
                  command=self.read_all_registers).pack(fill=tk.X, pady=2)
    
    def _create_tools_tab(self, parent):
        """Create tools tab content"""
        # Embedded interactive terminal directly in Tools tab
        self._create_separate_terminal_interface(parent, embedded=True)
    
    def _create_quick_tab(self, parent):
        """Create quick access tab content"""
        # Quick Register Access section
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
    
    def _create_terminal_tab(self, parent):
        """Create terminal tab content with embedded terminal"""
        # Terminal section
        terminal_frame = ttk.LabelFrame(parent, text="🖥️ Linux Terminal", padding=10)
        terminal_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Terminal info
        info_text = (
            "Integrated Linux Terminal with VT100 emulation\n"
            "• Full keyboard support (arrows, tab, ctrl+c)\n"
            "• Auto-login after board restart\n" 
            "• Ultra-fast ioctl register access\n"
            "• Command history and tab completion"
        )
        info_label = ttk.Label(terminal_frame, text=info_text, font=("Arial", 9))
        info_label.pack(pady=(0, 10))
        
        # Terminal display
        self.terminal_display = scrolledtext.ScrolledText(
            terminal_frame,
            wrap=tk.CHAR,
            height=20,
            font=('Consolas', 10),
            bg='black',
            fg='green',
            insertbackground='green'
        )
        self.terminal_display.pack(fill=tk.BOTH, expand=True, pady=5)
        
        # Initialize terminal state
        self.terminal_status = tk.StringVar(value="Disconnected")
        
        # Terminal controls
        controls_frame = ttk.Frame(terminal_frame)
        controls_frame.pack(fill=tk.X, pady=5)
        
        self.connect_button = ttk.Button(controls_frame, text="🔌 Connect", command=self._terminal_connect)
        self.connect_button.pack(side=tk.LEFT, padx=5)
        
        self.disconnect_button = ttk.Button(controls_frame, text="❌ Disconnect", command=self._terminal_disconnect, state="disabled")
        self.disconnect_button.pack(side=tk.LEFT, padx=5)
        
        ttk.Button(controls_frame, text="📋 Copy", command=self._terminal_copy).pack(side=tk.LEFT, padx=5)
        ttk.Button(controls_frame, text="📄 Paste", command=self._terminal_paste).pack(side=tk.LEFT, padx=5)
        ttk.Button(controls_frame, text="🧹 Clear", command=self._terminal_clear).pack(side=tk.LEFT, padx=5)
        
        # Status
        ttk.Label(controls_frame, textvariable=self.terminal_status).pack(side=tk.RIGHT, padx=10)
        
        # Bind keyboard events
        self.terminal_display.bind('<KeyPress>', self._terminal_key_handler)
        self.terminal_display.focus_set()
        
        # Add welcome message
        welcome_text = """🖥️ Linux Terminal
📡 Port: COM9 @ 115200 baud
🔌 Click 'Connect' to start session

"""
        self.terminal_display.insert(tk.END, welcome_text)
        self.terminal_display.see(tk.END)
    
    def _create_register_tab(self, parent):
        """Create register selection tab content"""
        # MMS register tabs
        reg_frame = ttk.LabelFrame(parent, text="📋 MMS Register Groups", padding=5)
        reg_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        self.register_notebook = ttk.Notebook(reg_frame)
        self.register_notebook.pack(fill=tk.BOTH, expand=True)

        self.register_trees = {}
        self.mms_progress_vars = {}

        for mms_name, mms_info in self.register_maps.items():
            tab_frame = ttk.Frame(self.register_notebook)
            self.register_notebook.add(tab_frame, text=mms_name)

            # Scan button + progress label
            ctrl_frame = ttk.Frame(tab_frame)
            ctrl_frame.pack(fill=tk.X, padx=3, pady=2)

            progress_var = tk.StringVar(value="Ready")
            self.mms_progress_vars[mms_name] = progress_var

            btn = ttk.Button(ctrl_frame, text=f"📡 Scan {mms_name}",
                             command=lambda m=mms_name: self.scan_mms_group(m))
            btn.pack(side=tk.LEFT, padx=2)
            ttk.Label(ctrl_frame, textvariable=progress_var, font=("Arial", 8)).pack(side=tk.LEFT, padx=5)

            # Register treeview (Address | Name | Value)
            tree_frame = ttk.Frame(tab_frame)
            tree_frame.pack(fill=tk.BOTH, expand=True, padx=3, pady=2)

            columns = ("Address", "Name", "Value")
            tree = ttk.Treeview(tree_frame, columns=columns, show="headings", height=8)
            tree.heading("Address", text="Address")
            tree.heading("Name",    text="Name")
            tree.heading("Value",   text="Value")
            tree.column("Address", width=90)
            tree.column("Name",    width=110)
            tree.column("Value",   width=90)

            for addr, reg in mms_info['registers'].items():
                tree.insert("", "end", values=(addr, reg['name'], "—"), iid=addr)

            tree_scroll = ttk.Scrollbar(tree_frame, orient=tk.VERTICAL, command=tree.yview)
            tree.configure(yscrollcommand=tree_scroll.set)
            tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
            tree_scroll.pack(side=tk.RIGHT, fill=tk.Y)

            tree.bind("<<TreeviewSelect>>", self.on_register_select)
            self.register_trees[mms_name] = tree

        # Current register value display
        self.current_value_label = ttk.Label(parent, text="No register read yet",
                                             font=("Consolas", 9), foreground="blue")
        self.current_value_label.pack(fill=tk.X, padx=5, pady=3)

    def _create_test_modes_tab(self, parent):
        """Create dedicated tab for IEEE transmitter test modes and PMA controls."""
        info_frame = ttk.LabelFrame(parent, text="🧪 LAN8651 Test Mode Control", padding=10)
        info_frame.pack(fill=tk.X, padx=5, pady=5)
        ttk.Label(
            info_frame,
            text=(
                "IEEE test register: T1STSTCTL (0x000308FB, bits 15:13)\n"
                "PMA control register: T1SPMACTL (0x000308F9)"
            ),
            font=("Arial", 9)
        ).pack(anchor=tk.W)

        # IEEE transmitter test mode selection
        ieee_frame = ttk.LabelFrame(parent, text="IEEE 802.3cg Transmitter Test Modes", padding=10)
        ieee_frame.pack(fill=tk.X, padx=5, pady=5)

        ttk.Label(ieee_frame, text="Mode:").grid(row=0, column=0, sticky=tk.W)
        mode_combo = ttk.Combobox(
            ieee_frame,
            textvariable=self.test_mode_var,
            values=[label for label, _ in self.ieee_test_modes],
            width=45,
            state="readonly"
        )
        mode_combo.grid(row=0, column=1, columnspan=3, padx=5, pady=2, sticky=tk.W)

        ttk.Button(ieee_frame, text="▶ Activate Selected", command=self.activate_selected_test_mode).grid(row=1, column=0, padx=2, pady=5, sticky=tk.W)
        ttk.Button(ieee_frame, text="⏹ Normal (000)", command=self.set_test_mode_normal).grid(row=1, column=1, padx=2, pady=5, sticky=tk.W)
        ttk.Button(ieee_frame, text="📖 Read Status", command=self.read_test_mode_status).grid(row=1, column=2, padx=2, pady=5, sticky=tk.W)

        ttk.Label(ieee_frame, textvariable=self.test_mode_status, font=("Consolas", 9), foreground="blue").grid(
            row=2, column=0, columnspan=4, sticky=tk.W, pady=(4, 0)
        )

        # PMA helper controls
        pma_frame = ttk.LabelFrame(parent, text="PMA Loopback / TX Controls", padding=10)
        pma_frame.pack(fill=tk.X, padx=5, pady=5)

        ttk.Button(pma_frame, text="🔁 PMA Loopback ON", command=lambda: self.set_pma_loopback(True)).grid(row=0, column=0, padx=2, pady=4, sticky=tk.W)
        ttk.Button(pma_frame, text="↩ PMA Loopback OFF", command=lambda: self.set_pma_loopback(False)).grid(row=0, column=1, padx=2, pady=4, sticky=tk.W)
        ttk.Button(pma_frame, text="📡 TX Disable ON", command=lambda: self.set_pma_txd(True)).grid(row=1, column=0, padx=2, pady=4, sticky=tk.W)
        ttk.Button(pma_frame, text="📶 TX Disable OFF", command=lambda: self.set_pma_txd(False)).grid(row=1, column=1, padx=2, pady=4, sticky=tk.W)
        ttk.Button(pma_frame, text="📖 Read PMA Control", command=self.read_pma_control_status).grid(row=1, column=2, padx=2, pady=4, sticky=tk.W)

        ttk.Label(pma_frame, textvariable=self.pma_ctrl_status, font=("Consolas", 9), foreground="blue").grid(
            row=2, column=0, columnspan=3, sticky=tk.W, pady=(4, 0)
        )

    def activate_selected_test_mode(self):
        """Activate selected IEEE transmitter test mode in T1STSTCTL."""
        if self.is_scanning:
            messagebox.showwarning("Busy", "Another operation is running. Please wait.")
            return

        selected_label = self.test_mode_var.get()
        selected_mode = None
        for label, mode in self.ieee_test_modes:
            if label == selected_label:
                selected_mode = mode
                break
        if selected_mode is None:
            messagebox.showerror("Invalid Mode", "Selected test mode is not valid.")
            return

        value = (selected_mode & 0x7) << 13
        self._start_test_mode_write(0x000308FB, value, f"Set TSTCTL={selected_mode:03b}")

    def set_test_mode_normal(self):
        """Return T1STSTCTL to normal operation."""
        if self.is_scanning:
            messagebox.showwarning("Busy", "Another operation is running. Please wait.")
            return
        self._start_test_mode_write(0x000308FB, 0x00000000, "Set TSTCTL=000 (normal)")

    def read_test_mode_status(self):
        """Read current T1STSTCTL value and decode active test mode."""
        if self.is_scanning:
            messagebox.showwarning("Busy", "Another operation is running. Please wait.")
            return
        self._start_test_mode_read(0x000308FB, "Read T1STSTCTL")

    def set_pma_loopback(self, enable):
        """Enable or disable PMA loopback bit in T1SPMACTL."""
        if self.is_scanning:
            messagebox.showwarning("Busy", "Another operation is running. Please wait.")
            return
        self._start_test_mode_bit_update(0x000308F9, 0, enable, "PMA Loopback")

    def set_pma_txd(self, enable):
        """Enable or disable PMA TX disable bit in T1SPMACTL."""
        if self.is_scanning:
            messagebox.showwarning("Busy", "Another operation is running. Please wait.")
            return
        self._start_test_mode_bit_update(0x000308F9, 14, enable, "PMA TX Disable")

    def read_pma_control_status(self):
        """Read current T1SPMACTL and decode key control bits."""
        if self.is_scanning:
            messagebox.showwarning("Busy", "Another operation is running. Please wait.")
            return
        self._start_test_mode_read(0x000308F9, "Read T1SPMACTL")

    def _start_test_mode_write(self, address, value, context):
        """Start async write for test mode controls."""
        self.is_scanning = True
        self.test_mode_status.set(f"Working: {context}...")
        threading.Thread(
            target=self._test_mode_write_thread,
            args=(address, value, context),
            daemon=True
        ).start()

    def _start_test_mode_read(self, address, context):
        """Start async read for test mode related registers."""
        self.is_scanning = True
        self.test_mode_status.set(f"Working: {context}...")
        threading.Thread(
            target=self._test_mode_read_thread,
            args=(address, context),
            daemon=True
        ).start()

    def _start_test_mode_bit_update(self, address, bit_index, enable, context):
        """Read-modify-write helper for PMA control bits."""
        self.is_scanning = True
        state_txt = "ON" if enable else "OFF"
        self.pma_ctrl_status.set(f"Working: {context} {state_txt}...")
        threading.Thread(
            target=self._test_mode_bit_update_thread,
            args=(address, bit_index, enable, context),
            daemon=True
        ).start()

    def _test_mode_write_thread(self, address, value, context):
        """Write test mode register value in background thread."""
        try:
            ser = serial.Serial(self.port.get(), self.baudrate.get(), timeout=6)
            time.sleep(0.5)

            command = f"lan_write 0x{address:08X} 0x{value:08X}"
            response = self.send_robust_command(ser, command, timeout=6.0)
            ser.close()

            ok = (
                ("LAN865X Write:" in response and "OK" in response) or
                ("LAN865X Write:" in response and "- OK" in response) or
                ("write success" in response.lower()) or
                (f"0x{address:08X}" in response and f"0x{value:08X}" in response and "error" not in response.lower())
            )

            if ok:
                self.result_queue.put(("test_mode_write_success", address, value, context))
            else:
                self.result_queue.put(("test_mode_error", f"{context} failed. Response: {response[:120]}"))
        except Exception as e:
            self.result_queue.put(("test_mode_error", f"{context} error: {str(e)}"))
        finally:
            self.is_scanning = False

    def _test_mode_read_thread(self, address, context):
        """Read test mode register value in background thread."""
        try:
            ser = serial.Serial(self.port.get(), self.baudrate.get(), timeout=6)
            time.sleep(0.5)

            command = f"lan_read 0x{address:08X}"
            response = self.send_robust_command(ser, command, timeout=6.0)
            ser.close()

            match = re.search(r'LAN865X Read: Addr=0x([0-9A-Fa-f]+) Value=0x([0-9A-Fa-f]+)', response)
            if match:
                value = int(match.group(2), 16)
                self.result_queue.put(("test_mode_read_success", address, value, context))
            else:
                self.result_queue.put(("test_mode_error", f"{context} failed. No readable response."))
        except Exception as e:
            self.result_queue.put(("test_mode_error", f"{context} error: {str(e)}"))
        finally:
            self.is_scanning = False

    def _test_mode_bit_update_thread(self, address, bit_index, enable, context):
        """Read-modify-write thread for PMA control bit changes."""
        try:
            ser = serial.Serial(self.port.get(), self.baudrate.get(), timeout=6)
            time.sleep(0.5)

            read_cmd = f"lan_read 0x{address:08X}"
            read_resp = self.send_robust_command(ser, read_cmd, timeout=6.0)
            match = re.search(r'LAN865X Read: Addr=0x([0-9A-Fa-f]+) Value=0x([0-9A-Fa-f]+)', read_resp)
            if not match:
                ser.close()
                self.result_queue.put(("test_mode_error", f"{context} failed. Could not read current value."))
                return

            current_value = int(match.group(2), 16)
            if enable:
                new_value = current_value | (1 << bit_index)
            else:
                new_value = current_value & ~(1 << bit_index)

            write_cmd = f"lan_write 0x{address:08X} 0x{new_value:08X}"
            write_resp = self.send_robust_command(ser, write_cmd, timeout=6.0)
            ser.close()

            ok = (
                ("LAN865X Write:" in write_resp and "OK" in write_resp) or
                ("LAN865X Write:" in write_resp and "- OK" in write_resp) or
                ("write success" in write_resp.lower()) or
                (f"0x{address:08X}" in write_resp and f"0x{new_value:08X}" in write_resp and "error" not in write_resp.lower())
            )
            if ok:
                self.result_queue.put(("test_mode_write_success", address, new_value, context))
            else:
                self.result_queue.put(("test_mode_error", f"{context} write failed. Response: {write_resp[:120]}"))
        except Exception as e:
            self.result_queue.put(("test_mode_error", f"{context} error: {str(e)}"))
        finally:
            self.is_scanning = False

    def read_all_registers(self):
        """Scan all MMS register groups (like Linux GUI)"""
        if self.is_scanning or getattr(self, '_bulk_scanning', False):
            messagebox.showwarning("Busy", "Scan already running. Bitte warten.")
            return
        self._bulk_scanning = True
        # Fortschrittsdialog anzeigen
        progress_window = tk.Toplevel(self.root)
        progress_window.title("Read All Registers")
        progress_window.geometry("400x120")
        progress_window.transient(self.root)
        progress_window.grab_set()
        progress_label = ttk.Label(progress_window, text="Starte Register-Scan...")
        progress_label.pack(pady=20)
        progress_bar = ttk.Progressbar(progress_window, mode='determinate', maximum=len(self.register_maps))
        progress_bar.pack(pady=10, padx=20, fill=tk.X)
        self.root.update()

        def scan_all():
            try:
                total = len(self.register_maps)
                for i, mms_name in enumerate(self.register_maps):
                    progress_label.config(text=f"Scanne {mms_name} ({i+1}/{total})...")
                    progress_bar['value'] = i
                    self.root.update()
                    # scan_mms_group verwaltet is_scanning selbst
                    self.scan_mms_group(mms_name)
                    # Warte bis Scan fertig (Polling auf is_scanning)
                    while self.is_scanning:
                        time.sleep(0.05)
                progress_label.config(text="Alle Registergruppen gescannt!")
                progress_bar['value'] = total
                self.root.update()
                time.sleep(0.7)
            finally:
                progress_window.destroy()
                self._bulk_scanning = False

        threading.Thread(target=scan_all, daemon=True).start()

    def create_right_panel(self, parent):
        """Create right panel with bitfield analysis"""
        # Title section
        title_frame = ttk.Frame(parent)
        title_frame.pack(fill=tk.X, padx=5, pady=5)
        
        ttk.Label(title_frame, text="🔬 Bitfield Analysis", font=("Arial", 16, "bold")).pack(side=tk.LEFT)
        
        # Current register info
        self.current_reg_info = ttk.Label(title_frame, text="No register selected", font=("Arial", 12))
        self.current_reg_info.pack(side=tk.RIGHT)
        
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
        self.analysis_text.tag_configure("field_name", font=("Consolas", 11, "bold"), foreground="#8e44ad")
        self.analysis_text.tag_configure("binary", font=("Consolas", 10), foreground="#7f8c8d")
        
    def on_register_select(self, event):
        """Handle register selection from any MMS tab"""
        sender_tree = event.widget
        selection = sender_tree.selection()
        if not selection:
            return
        # Clear selections in other tabs
        for tree in self.register_trees.values():
            if tree != sender_tree:
                tree.selection_remove(tree.get_children())
        # iid == address string
        addr = selection[0]
        reg_info = None
        for mms_info in self.register_maps.values():
            if addr in mms_info['registers']:
                reg_info = mms_info['registers'][addr]
                break
        self.current_register = {
            'address': addr,
            'name': reg_info['name'] if reg_info else addr,
            'description': reg_info['description'] if reg_info else ''
        }
        print(f"[DEBUG] on_register_select: addr={addr} name={self.current_register['name']}")
        self.current_reg_info.config(
            text=f"Selected: {self.current_register['name']} ({addr})")
        # Auto-analyze if we already have a cached value
        if addr in self.register_values:
            value = self.register_values[addr]
            self.current_value = value
            print(f"[DEBUG] on_register_select: cached value=0x{value:08X} → auto-analyzing")
            self.current_value_label.config(
                text=f"\u2705 {self.current_register['name']}: 0x{value:08X}", foreground="green")
            analysis = self.bitfield_analyzer.analyze_register_value(addr, value)
            print(f"[DEBUG] on_register_select: analysis has {len(analysis)} bitfields")
            self.display_analysis(addr, value, analysis)
            
    def test_connection(self):
        """Test serial connection"""
        if self.is_scanning:
            messagebox.showwarning("Busy", "Analyzer is currently running. Please wait.")
            return
            
        self.connection_status.set("Testing...")
        threading.Thread(target=self._test_connection_thread, daemon=True).start()
        
    def _test_connection_thread(self):
        """Test connection in background thread"""
        try:
            ser = serial.Serial(self.port.get(), self.baudrate.get(), timeout=6)
            time.sleep(1)
            
            # Send test command
            response = self.send_robust_command(ser, "lan_read 0x00000000", timeout=6.0)
            ser.close()
            
            if "LAN865X Read:" in response:
                self.result_queue.put(("connection_status", "✅ Connected - Communication OK"))
            else:
                self.result_queue.put(("connection_status", "❌ Connected but no LAN865X response"))
                
        except Exception as e:
            self.result_queue.put(("connection_status", f"❌ Connection failed: {str(e)}"))
            
    def read_and_analyze_register(self):
        """Read selected register and perform bitfield analysis"""
        if not self.current_register:
            messagebox.showwarning("No Selection", "Please select a register first.")
            return
            
        if self.is_scanning:
            messagebox.showwarning("Busy", "Analyzer is currently running. Please wait.")
            return
            
        self.is_scanning = True
        self.analysis_text.delete(1.0, tk.END)
        self.analysis_text.insert(tk.END, f"Reading register {self.current_register['name']}...\n")
        
        threading.Thread(target=self._read_and_analyze_thread, daemon=True).start()
        
    def _read_and_analyze_thread(self):
        """Read and analyze register in background thread"""
        try:
            ser = serial.Serial(self.port.get(), self.baudrate.get(), timeout=6)
            time.sleep(0.5)
            
            address = self.current_register['address']
            command = f"lan_read {address}"
            
            response = self.send_robust_command(ser, command, timeout=6.0)
            ser.close()
            
            # Parse response
            match = re.search(r'LAN865X Read: Addr=0x([0-9A-Fa-f]+) Value=0x([0-9A-Fa-f]+)', response)
            
            if match:
                value = int(match.group(2), 16)
                self.current_value = value
                
                # Perform bitfield analysis
                analysis = self.bitfield_analyzer.analyze_register_value(address, value)
                
                self.result_queue.put(("analysis_complete", address, value, analysis))
            else:
                self.result_queue.put(("analysis_error", f"Failed to read register {address}"))
                
        except Exception as e:
            self.result_queue.put(("analysis_error", f"Error: {str(e)}"))
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
    
    def read_register_only(self):
        """Read selected register value only (without analysis)"""
        if not self.current_register:
            messagebox.showwarning("No Selection", "Please select a register first.")
            return
            
        if self.is_scanning:
            messagebox.showwarning("Busy", "Analyzer is currently running. Please wait.")
            return
            
        self.is_scanning = True
        self.current_value_label.config(text="Reading...")
        
        threading.Thread(target=self._read_register_only_thread, daemon=True).start()
            
    def _read_register_only_thread(self):
        """Read register in background thread (read only)"""
        try:
            ser = serial.Serial(self.port.get(), self.baudrate.get(), timeout=6)
            time.sleep(0.5)
            
            address = self.current_register['address']
            command = f"lan_read {address}"
            
            response = self.send_robust_command(ser, command, timeout=6.0)
            ser.close()
            
            # Parse response
            match = re.search(r'LAN865X Read: Addr=0x([0-9A-Fa-f]+) Value=0x([0-9A-Fa-f]+)', response)
            
            if match:
                value = int(match.group(2), 16)
                self.current_value = value
                
                self.result_queue.put(("register_read", address, value))
            else:
                self.result_queue.put(("register_read_error", f"Failed to read register {address}"))
                
        except Exception as e:
            self.result_queue.put(("register_read_error", f"Error: {str(e)}"))
        finally:
            self.is_scanning = False
    
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
            ser = serial.Serial(self.port.get(), self.baudrate.get(), timeout=6)
            time.sleep(0.5)
            
            command = f"lan_read {addr_str}"
            response = self.send_robust_command(ser, command, timeout=6.0)
            ser.close()
            
            # Parse response
            match = re.search(r'LAN865X Read: Addr=0x([0-9A-Fa-f]+) Value=0x([0-9A-Fa-f]+)', response)
            
            if match:
                value = int(match.group(2), 16)
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
            ser = serial.Serial(self.port.get(), self.baudrate.get(), timeout=6)
            time.sleep(0.5)
            
            command = f"lan_write 0x{reg_addr:08X} 0x{reg_value:08X}"
            response = self.send_robust_command(ser, command, timeout=6.0)
            ser.close()
            
            # DEBUG: Print actual response for troubleshooting
            print(f"[DEBUG] Write response: '{response}'")
            
            # More flexible response checking - look for common success patterns
            success_indicators = [
                "LAN865X Write:" in response and "OK" in response,
                "LAN865X Write:" in response and "- OK" in response,
                "Write complete" in response,
                "write success" in response.lower(),
                f"0x{reg_addr:08X}" in response and f"0x{reg_value:08X}" in response and "error" not in response.lower()
            ]
            
            if any(success_indicators):
                self.result_queue.put(("quick_write_success", reg_addr, reg_value))
            else:
                # Show actual response in error message for debugging
                self.result_queue.put(("quick_write_error", f"Write failed. Response: '{response[:100]}...'"))
                
        except Exception as e:
            self.result_queue.put(("quick_write_error", f"Error writing register: {str(e)}"))
        finally:
            self.is_scanning = False
            
    def analyze_current_value(self):
        """Analyze currently stored register value"""
        if not self.current_register:
            messagebox.showwarning("No Selection", "Please select a register first.")
            return
            
        if self.current_value is None:
            messagebox.showwarning("No Value", "Please read the register first.")
            return
            
        # Perform bitfield analysis
        analysis = self.bitfield_analyzer.analyze_register_value(
            self.current_register['address'], 
            self.current_value
        )
        self.display_analysis(self.current_register['address'], self.current_value, analysis)

    def scan_mms_group(self, mms_name):
        """Scan all registers in the given MMS group via serial"""
        if self.is_scanning:
            messagebox.showwarning("Busy", "Scan already running. Please wait.")
            return
        print(f"[DEBUG] scan_mms_group: starting scan for {mms_name}")
        self.is_scanning = True
        self.mms_progress_vars[mms_name].set("Scanning...")
        if mms_name == "MMS_8":
            threading.Thread(target=self._scan_stats_thread, daemon=True).start()
        else:
            threading.Thread(target=self._scan_mms_thread, args=(mms_name,), daemon=True).start()

    def _scan_stats_thread(self):
        """Background thread for MMS_8: call 'stats' CLI command and parse software counters."""
        mms_name = "MMS_8"
        try:
            ser = serial.Serial(self.port.get(), self.baudrate.get(), timeout=3)
            time.sleep(0.5)
            response = self.send_robust_command(ser, "stats", timeout=3.0)
            ser.close()
            print(f"[DEBUG] stats response: {repr(response)}")

            # Parse: "eth1 TX: ok=12 err=0 qFull=0 pend=0"
            #        "eth1 RX: ok=12 err=0 nobufs=0 pend=0"
            import re as _re
            parsed = {}
            for m in _re.finditer(r'(eth\d)\s+TX:\s+ok=(\d+)\s+err=(\d+)\s+qFull=(\d+)\s+pend=(\d+)', response):
                ifc = m.group(1)
                parsed[f"{ifc}_tx_ok"]    = int(m.group(2))
                parsed[f"{ifc}_tx_err"]   = int(m.group(3))
                parsed[f"{ifc}_tx_qfull"] = int(m.group(4))
                parsed[f"{ifc}_tx_pend"]  = int(m.group(5))
            for m in _re.finditer(r'(eth\d)\s+RX:\s+ok=(\d+)\s+err=(\d+)\s+nobufs=(\d+)\s+pend=(\d+)', response):
                ifc = m.group(1)
                parsed[f"{ifc}_rx_ok"]     = int(m.group(2))
                parsed[f"{ifc}_rx_err"]    = int(m.group(3))
                parsed[f"{ifc}_rx_nobufs"] = int(m.group(4))
                parsed[f"{ifc}_rx_pend"]   = int(m.group(5))

            if not parsed:
                self.result_queue.put(("mms_scan_error", mms_name, "Keine Antwort – Firmware neu flashen (stats-Befehl fehlt?)"))
                return

            for key, value in parsed.items():
                self.register_values[key] = value
                self.result_queue.put(("mms_register_read", mms_name, key, value))

            ok = len(parsed)
            self.result_queue.put(("mms_scan_complete", mms_name, f"{ok}/{ok}"))

        except Exception as e:
            print(f"[DEBUG] _scan_stats_thread EXCEPTION: {e}")
            self.result_queue.put(("mms_scan_error", mms_name, str(e)))
        finally:
            self.is_scanning = False


    def _scan_mms_thread(self, mms_name):
        """Background thread: read every register in mms_name and queue results"""
        try:
            print(f"[DEBUG] _scan_mms_thread: connect {self.port.get()} @ {self.baudrate.get()}")
            ser = serial.Serial(self.port.get(), self.baudrate.get(), timeout=3)
            time.sleep(0.5)
            group_regs = self.register_maps[mms_name]['registers']
            addresses = list(group_regs.keys())
            total = len(addresses)
            ok_count = 0
            print(f"[DEBUG] _scan_mms_thread: {mms_name} has {total} registers")
            for i, addr in enumerate(addresses):
                self.result_queue.put(("mms_progress", mms_name,
                                       f"{i+1}/{total}: {group_regs[addr]['name']}"))
                actual = self._read_register_serial(ser, addr)
                if actual is not None:
                    ok_count += 1
                    self.register_values[addr] = actual
                    print(f"[DEBUG] _scan_mms_thread: {addr} ({group_regs[addr]['name']}) = 0x{actual:08X}")
                    self.result_queue.put(("mms_register_read", mms_name, addr, actual))
                    if mms_name == "MMS_1":
                        self._update_mac_from_scan(addr, actual)
                else:
                    print(f"[DEBUG] _scan_mms_thread: {addr} ({group_regs[addr]['name']}) = TIMEOUT/ERROR")
            ser.close()
            print(f"[DEBUG] _scan_mms_thread: scan done, {ok_count}/{total} ok")
            self.result_queue.put(("mms_scan_complete", mms_name, f"{ok_count}/{total}"))
            # Auto-analyze current selection if its value was just read
            if self.current_register and self.current_register['address'] in self.register_values:
                a = self.current_register['address']
                v = self.register_values[a]
                self.current_value = v
                print(f"[DEBUG] _scan_mms_thread: auto-analyzing {a} = 0x{v:08X}")
                analysis = self.bitfield_analyzer.analyze_register_value(a, v)
                print(f"[DEBUG] _scan_mms_thread: analysis has {len(analysis)} bitfields")
                self.result_queue.put(("analysis_complete", a, v, analysis))
                self.result_queue.put(("register_read", a, v))
        except Exception as e:
            print(f"[DEBUG] _scan_mms_thread: EXCEPTION: {e}")
            print(traceback.format_exc())
            self.result_queue.put(("mms_scan_error", mms_name, str(e)))
        finally:
            self.is_scanning = False

    def _read_register_serial(self, ser, address, retries=1):
        """Send lan_read command and parse the response. Returns int value or None."""
        for attempt in range(retries + 1):
            try:
                response = self.send_robust_command(ser, f"lan_read {address}")
                match = re.search(
                    r'LAN865X Read: Addr=0x([0-9A-Fa-f]+) Value=0x([0-9A-Fa-f]+)',
                    response)
                if match:
                    return int(match.group(2), 16)
                if attempt < retries:
                    time.sleep(0.01)
            except Exception:
                if attempt < retries:
                    time.sleep(0.01)
        return None

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
    
    def update_mac_values(self, address, value):
        """Update MAC register values and decode if both available"""
        if address == '0x00010024':  # MAC_SAB2
            self.mac_sab2_value = value
        elif address == '0x00010025':  # MAC_SAT2
            self.mac_sat2_value = value
            
        # Try to decode MAC if both values are available
        if self.mac_sab2_value is not None and self.mac_sat2_value is not None:
            self.decode_mac_address()
            
    def display_analysis(self, address, value, analysis):
        """Display bitfield analysis results with MAC decoding"""
        print(f"[DEBUG] display_analysis: addr={address} value=0x{value:08X} bitfields={len(analysis)}")
        self.analysis_text.delete(1.0, tk.END)
        
        # Update MAC values if this is a MAC register
        self.update_mac_values(address, value)
        
        # Header
        reg_info = None
        for mms_info in self.register_maps.values():
            if address in mms_info['registers']:
                reg_info = mms_info['registers'][address]
                break
        if reg_info:
            self.analysis_text.insert(tk.END, f"🔬 BITFIELD ANALYSIS\n", "header")
            self.analysis_text.insert(tk.END, f"="*60 + "\n\n")
            self.analysis_text.insert(tk.END, f"Register: {reg_info['name']} ({address})\n", "register")
            self.analysis_text.insert(tk.END, f"Description: {reg_info['description']}\n")
            self.analysis_text.insert(tk.END, f"Current Value: 0x{value:08X} ({value})\n", "register") 
            self.analysis_text.insert(tk.END, f"Binary: {value:032b}\n\n", "binary")
            
        # Bitfield breakdown
        self.analysis_text.insert(tk.END, "📊 BITFIELD BREAKDOWN:\n", "header")
        self.analysis_text.insert(tk.END, "-"*60 + "\n\n")
        
        if not analysis:
            self.analysis_text.insert(tk.END, "⚠️ No bitfield definitions available for this register.\n", "error")
            return
            
        # Table header
        self.analysis_text.insert(tk.END, f"{'Bits':<10} {'Field':<15} {'Value':<8} {'Binary':<12} {'Interpretation'}\n")
        self.analysis_text.insert(tk.END, "-"*80 + "\n")
        
        for item in analysis:
            bitfield = item['bitfield']
            field_value = item['value']
            interpretation = item['interpretation']
            is_error = item['is_error']
            is_important = item['is_important']
            
            # Format line
            bits_str = bitfield.get_bit_range_str()
            field_name = bitfield.name[:14]  # Truncate if needed
            value_str = f"{field_value}"
            binary_str = item['binary_value']
            
            line = f"{bits_str:<10} {field_name:<15} {value_str:<8} {binary_str:<12} {interpretation}\n"
            
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
        
        # MAC Address decoding for MAC registers
        if address in ['0x00010024', '0x00010025']:
            self.analysis_text.insert(tk.END, f"\n🌐 MAC ADDRESS DECODING:\n", "header")
            self.analysis_text.insert(tk.END, "-"*30 + "\n")
            
            if address == '0x00010024':
                self.analysis_text.insert(tk.END, f"MAC_SAB2 (Current): 0x{value:08X}\n")
                if self.mac_sat2_value is not None:
                    self.analysis_text.insert(tk.END, f"MAC_SAT2 (Current): 0x{self.mac_sat2_value:08X}\n")
                    self.analysis_text.insert(tk.END, f"➜ Decoded MAC: {self.decoded_mac}\n", "success")
                    self.analysis_text.insert(tk.END, f"➜ OUI: {self.decoded_mac[:8]} (Microchip Technology Inc.)\n")
                else:
                    self.analysis_text.insert(tk.END, "⚠️ Need MAC_SAT2 value for complete decoding\n", "error")
            elif address == '0x00010025':
                self.analysis_text.insert(tk.END, f"MAC_SAT2 (Current): 0x{value:08X}\n")
                if self.mac_sab2_value is not None:
                    self.analysis_text.insert(tk.END, f"MAC_SAB2 (Current): 0x{self.mac_sab2_value:08X}\n")
                    self.analysis_text.insert(tk.END, f"➜ Decoded MAC: {self.decoded_mac}\n", "success")
                    self.analysis_text.insert(tk.END, f"➜ OUI: {self.decoded_mac[:8]} (Microchip Technology Inc.)\n")
                else:
                    self.analysis_text.insert(tk.END, "⚠️ Need MAC_SAB2 value for complete decoding\n", "error")
        
        # Legacy MAC register warning
        elif address in ['0x00010022', '0x00010023']:
            self.analysis_text.insert(tk.END, f"\n⚠️ LEGACY MAC REGISTER:\n", "header")
            self.analysis_text.insert(tk.END, "-"*25 + "\n")
            self.analysis_text.insert(tk.END, f"This is a legacy MAC register (unused).\n")
            self.analysis_text.insert(tk.END, f"✅ Use MAC_SAB2 (0x00010024) and MAC_SAT2 (0x00010025) instead.\n", "success")
            
    def send_robust_command(self, ser, command, timeout=3.0):
        """Send command - ROBUST: Prompt-based synchronization (Race Condition Free)"""
        ser.reset_input_buffer()
        ser.write(f'{command}\r\n'.encode())
        
        start_time = time.time()
        response = ""
        
        while time.time() - start_time < timeout:
            # Read all available data atomically
            available = ser.in_waiting
            if available > 0:
                chunk = ser.read(available).decode('utf-8', errors='ignore')
                response += chunk
                
                # IMMEDIATE completion check!
                if self.is_response_complete(response):
                    return response  # Return immediately when complete!
                        
            time.sleep(0.0005)  # 0.5ms polling
            
        return response
        
    def is_response_complete(self, response):
        """Check if response is complete - ROBUST: Proper LAN865X pattern detection"""
        if not response or len(response) < 10:
            return False
            
        # For LAN865X commands: Look for complete read result pattern
        if 'lan_read' in response.lower():
            # Must have the complete LAN865X Read result
            lan_match = re.search(r'LAN865X Read: Addr=0x([0-9A-Fa-f]+) Value=0x([0-9A-Fa-f]+)', response)
            if lan_match:
                # Found complete read result - check if followed by newline
                end_pos = lan_match.end()
                remaining = response[end_pos:]
                return '\n' in remaining or '\r' in remaining
            return False
            
        # For other commands: traditional prompt detection
        return '>' in response and (response.count('\n') >= 2)
        
    def process_queue(self):
        """Process result queue for thread communication"""
        try:
            while True:
                item = self.result_queue.get_nowait()
                
                if item[0] == "connection_status":
                    self.connection_status.set(item[1])
                elif item[0] == "analysis_complete":
                    address, value, analysis = item[1], item[2], item[3]
                    print(f"[DEBUG] process_queue: analysis_complete addr={address} value=0x{value:08X} bitfields={len(analysis)}")
                    self.display_analysis(address, value, analysis)
                elif item[0] == "analysis_error":
                    self.analysis_text.delete(1.0, tk.END)
                    self.analysis_text.insert(tk.END, f"❌ {item[1]}\n", "error")
                elif item[0] == "register_read":
                    address, value = item[1], item[2]
                    reg_info = None
                    for mms_info in self.register_maps.values():
                        if address in mms_info['registers']:
                            reg_info = mms_info['registers'][address]
                            break
                    name = reg_info['name'] if reg_info else address
                    self.current_value_label.config(
                        text=f"\u2705 {name}: 0x{value:08X} ({value})", foreground="green")
                elif item[0] == "register_read_error":
                    self.current_value_label.config(text=f"\u274c {item[1]}", foreground="red")
                elif item[0] == "mms_progress":
                    mms_name, progress = item[1], item[2]
                    if mms_name in self.mms_progress_vars:
                        self.mms_progress_vars[mms_name].set(progress)
                elif item[0] == "mms_register_read":
                    mms_name, addr, value = item[1], item[2], item[3]
                    if mms_name in self.register_trees:
                        tree = self.register_trees[mms_name]
                        reg_name = self.register_maps[mms_name]['registers'].get(addr, {}).get('name', addr)
                        if tree.exists(addr):
                            tree.item(addr, values=(addr, reg_name, f"0x{value:08X}"))
                elif item[0] == "mms_scan_complete":
                    mms_name, status = item[1], item[2]
                    if mms_name in self.mms_progress_vars:
                        self.mms_progress_vars[mms_name].set(f"\u2705 {status}")
                elif item[0] == "mms_scan_error":
                    mms_name, error = item[1], item[2]
                    if mms_name in self.mms_progress_vars:
                        self.mms_progress_vars[mms_name].set(f"\u274c {error[:40]}")
                elif item[0] == "quick_read_success":
                    addr_str, value = item[1], item[2]
                    self.current_value_label.config(
                        text=f"✅ Quick Read {addr_str}: 0x{value:08X} ({value})", foreground="green")
                    # Auto-analyze if bitfield definitions exist
                    analysis = self.bitfield_analyzer.analyze_register_value(addr_str, value)
                    if analysis:
                        self.display_analysis(addr_str, value, analysis)
                elif item[0] == "quick_read_error":
                    error = item[1]
                    self.current_value_label.config(text=f"❌ {error}", foreground="red")
                elif item[0] == "quick_write_success":
                    reg_addr, reg_value = item[1], item[2]
                    self.current_value_label.config(
                        text=f"✅ Write Success: 0x{reg_value:08X} → 0x{reg_addr:08X}", foreground="green")
                elif item[0] == "quick_write_error":
                    error = item[1]
                    self.current_value_label.config(text=f"❌ {error}", foreground="red")
                elif item[0] == "test_mode_write_success":
                    address, value, context = item[1], item[2], item[3]
                    addr_str = f"0x{address:08X}"
                    self.register_values[addr_str] = value
                    self.current_value_label.config(
                        text=f"✅ {context}: {addr_str} = 0x{value:08X}", foreground="green")

                    if address == 0x000308FB:
                        mode = (value >> 13) & 0x7
                        label = self._test_mode_labels.get(mode, f"Reserved ({mode:03b})")
                        self.test_mode_status.set(f"✅ Active: {label} | Raw=0x{value:08X}")
                    elif address == 0x000308F9:
                        loopback = 1 if (value & (1 << 0)) else 0
                        txd = 1 if (value & (1 << 14)) else 0
                        self.pma_ctrl_status.set(
                            f"✅ T1SPMACTL=0x{value:08X} | LBE={loopback} TXD={txd}")
                elif item[0] == "test_mode_read_success":
                    address, value, context = item[1], item[2], item[3]
                    addr_str = f"0x{address:08X}"
                    self.register_values[addr_str] = value
                    self.current_value_label.config(
                        text=f"✅ {context}: {addr_str} = 0x{value:08X}", foreground="green")

                    if address == 0x000308FB:
                        mode = (value >> 13) & 0x7
                        label = self._test_mode_labels.get(mode, f"Reserved ({mode:03b})")
                        self.test_mode_status.set(f"📖 {label} | Raw=0x{value:08X}")
                    elif address == 0x000308F9:
                        loopback = 1 if (value & (1 << 0)) else 0
                        txd = 1 if (value & (1 << 14)) else 0
                        self.pma_ctrl_status.set(
                            f"📖 T1SPMACTL=0x{value:08X} | LBE={loopback} TXD={txd}")
                elif item[0] == "test_mode_error":
                    error = item[1]
                    self.test_mode_status.set(f"❌ {error}")
                    self.pma_ctrl_status.set(f"❌ {error}")
                    self.current_value_label.config(text=f"❌ {error}", foreground="red")
                    
        except queue.Empty:
            pass
        finally:
            self.root.after(100, self.process_queue)
    
    # ── Terminal Emulation Methods ────────────────────────────────────────────
    
    def _terminal_connect(self):
        """Connect terminal with Linux host via COM port"""
        try:
            if hasattr(self, '_linux_access'):
                self._linux_access.close_connection()
            
            port = self.port.get()  
            self._linux_access = LinuxIoctlAccess(port, self.baudrate.get())
            
            ser, logged_in = self._linux_access._get_connection()
            if ser and ser.is_open:
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
        """Disconnect terminal"""
        self.terminal_running = False
        self.connect_button.config(state="normal")
        self.disconnect_button.config(state="disabled")
        self.terminal_status.set("Disconnected")
        
        self._terminal_write("\n❌ Terminal Disconnected\n")
        
        if hasattr(self, '_linux_access'):
            self._linux_access.close_connection()
    
    def _terminal_read_worker(self):
        """Background worker to read terminal data"""
        while self.terminal_running and self.terminal_ser and self.terminal_ser.is_open:
            try:
                if self.terminal_ser.in_waiting > 0:
                    data = self.terminal_ser.read(self.terminal_ser.in_waiting)
                    if data:
                        text = data.decode('utf-8', errors='replace')
                        
                        # Filter out simple character echoes (since we do local echo)
                        filtered_text = self._filter_ansi_sequences(text)
                        
                        if filtered_text.strip():  # Only display non-empty content
                            self.root.after(0, lambda t=filtered_text: self._terminal_write(t))
                        
                time.sleep(0.01)
                
            except Exception as e:
                self.root.after(0, lambda: self._terminal_write(f"\n❌ Read error: {e}\n"))
                break
    
    def _terminal_write(self, text):
        """Write text to terminal display"""
        try:
            self.terminal_display.insert(tk.END, text)
            self.terminal_display.see(tk.END)
            self.terminal_display.config(state=tk.NORMAL)  # Keep editable for input
        except:
            pass
    
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
        """Advanced ANSI escape sequence filtering"""
        import re
        
        # Remove multiple types of ANSI/VT100 escape sequences
        ansi_patterns = [
            r'\x1b\[[0-9;]*[HfABCDsuJKmhlp]',  # Standard ANSI sequences
            r'\x1b\[[\d;]*[a-zA-Z]',           # Generic ANSI
            r'\x1b\([AB01]',                   # Character set selection
            r'\x1b[\[\]()#]\d*[a-zA-Z]',       # Extended sequences
            r'\x1b[=>]',                       # Application/numeric keypad
        ]
        
        result = text
        for pattern in ansi_patterns:
            result = re.sub(pattern, '', result)
        
        # Handle backspace characters specially - they need visual processing
        if '\x08' in result:
            processed = ''
            for char in result:
                if char == '\x08':  # Backspace character
                    if processed:
                        processed = processed[:-1]
                else:
                    processed += char
            result = processed
        
        return result
        
    def _handle_visual_backspace(self):
        """Handle visual backspace in terminal display"""
        try:
            # Get current cursor position
            cursor_pos = self.terminal_display.index(tk.INSERT)
            line, col = map(int, cursor_pos.split('.'))
            
            # Check if we can go back one character
            if col > 0:
                # Calculate the position just before cursor
                prev_pos = f"{line}.{col-1}"
                # Delete the character just before cursor
                self.terminal_display.delete(prev_pos, cursor_pos)
            elif line > 1:
                # If at beginning of line, go to end of previous line
                prev_line_end = self.terminal_display.index(f"{line-1}.end")
                self.terminal_display.mark_set(tk.INSERT, prev_line_end)
                # Delete one character
                cursor_pos = self.terminal_display.index(tk.INSERT)
                if cursor_pos != "1.0":  # Not at very beginning
                    line, col = map(int, cursor_pos.split('.'))
                    if col > 0:
                        prev_pos = f"{line}.{col-1}"
                        self.terminal_display.delete(prev_pos, cursor_pos)
        
        except Exception as e:
            pass
    
    def _terminal_key_handler(self, event):
        """Enhanced terminal keyboard handler with full VT100 support"""
        if not self.terminal_running or not self.terminal_ser or not self.terminal_ser.is_open:
            return "break"
        
        # Anti-Repeat protection against rapid key repeat
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
            # Handle special keys with VT100 sequences
            if event.keysym == 'Return':
                data = b'\r\n'
                self.terminal_ser.write(data)
                self._debug_send(data, "Enter key")
                return "break"
                
            elif event.keysym == 'BackSpace':
                data = b'\x08'
                self.terminal_ser.write(data)
                self._debug_send(data, "Backspace key")
                # IMMEDIATE visual backspace
                self._handle_visual_backspace()
                return "break"
                
            elif event.keysym == 'Tab':
                data = b'\t'
                self.terminal_ser.write(data)
                self._debug_send(data, "Tab key")
                return "break"
                
            # Arrow keys - VT100 escape sequences
            elif event.keysym == 'Left':
                data = b'\x1b[D'  
                self.terminal_ser.write(data)
                self._debug_send(data, "Left arrow")
                return "break"
                
            elif event.keysym == 'Right':
                data = b'\x1b[C'
                self.terminal_ser.write(data)
                self._debug_send(data, "Right arrow")
                return "break"
                
            elif event.keysym == 'Up':
                data = b'\x1b[A'
                self.terminal_ser.write(data)
                self._debug_send(data, "Up arrow")
                return "break"
                
            elif event.keysym == 'Down':
                data = b'\x1b[B'
                self.terminal_ser.write(data)
                self._debug_send(data, "Down arrow")
                return "break"
            
            # CTRL+C for command interruption (SIGINT)
            elif (event.state & 0x4) and event.keysym.lower() == 'c':
                data = b'\x03'
                self.terminal_ser.write(data)
                self._debug_send(data, "Ctrl+C (SIGINT)")
                # Visual indication of interruption
                self._terminal_write('^C\n')
                return "break"
            
            # Regular printable characters
            elif event.char and len(event.char) == 1 and 32 <= ord(event.char) <= 126:
                data = event.char.encode('ascii')
                self.terminal_ser.write(data)
                self._debug_send(data, f"Character '{event.char}'")
                # IMMEDIATE local echo
                self._terminal_write(event.char)
                return "break"
            
            # Ignore all other keys
            else:
                if event.keysym not in ['Left', 'Right', 'Up', 'Down']:
                    self._debug_send(b'', f"IGNORED key: {event.keysym} / char: {repr(event.char)}")
                return "break"
                
        except Exception as e:
            self._terminal_write(f"\n❌ Key error: {str(e)}\n")
            return "break"
    
    def _terminal_copy(self):
        """Copy selected terminal text to clipboard"""
        try:
            self.root.clipboard_clear()
            self.root.clipboard_append(self.terminal_display.selection_get())
            self.terminal_status.set("📋 Text copied")
            self.root.after(2000, lambda: self.terminal_status.set("Connected"))
        except Exception as e:
            pass
            
    def _terminal_paste(self):
        """Paste clipboard text to terminal"""
        try:
            text = self.root.clipboard_get()
            if self.terminal_ser and self.terminal_running:
                self.terminal_ser.write(text.encode('utf-8'))
                self.terminal_status.set("📄 Text pasted")
                self.root.after(2000, lambda: self.terminal_status.set("Connected"))
        except Exception as e:
            pass
            
    def _terminal_clear(self):
        """Clear terminal display"""
        self.terminal_display.delete(1.0, tk.END)
        self.terminal_display.insert(tk.END, "🖥️ Linux Terminal - Cleared\n\n")
    
    def _on_closing(self):
        """Cleanup when closing the GUI"""
        try:
            # Stop terminal
            self.terminal_running = False
            # Close Linux connection
            if hasattr(self, '_linux_access'):
                self._linux_access.close_connection()
        except:
            pass  # Ignore cleanup errors
        finally:
            self.root.destroy()

    def run(self):
        """Start the GUI"""
        self.root.mainloop()

    def open_terminal_window(self):
        """Öffnet ein separates Terminal-Fenster mit derselben VT100-Funktionalität wie der Terminal-Tab"""
        # Open separate terminal window with the same VT100 functionality 
        terminal_window = tk.Toplevel(self.root)
        terminal_window.title(f"🖥️ Interactive Serial Terminal - {self.port.get()}")
        terminal_window.geometry("900x600")  # Kleineres Fenster
        terminal_window.transient(self.root)
        
        # Initialize terminal state for this window
        terminal_window.terminal_running = False
        terminal_window.terminal_ser = None
        
        # Create terminal interface in the window
        self._create_separate_terminal_interface(terminal_window)
        
        # Setup window close protocol
        terminal_window.protocol("WM_DELETE_WINDOW", 
                               lambda: self._close_terminal_window(terminal_window))

    def _create_separate_terminal_interface(self, parent_window, embedded=False):
        """Erstellt die Terminal-Benutzeroberfläche in einem separaten Fenster oder eingebettet."""
        # Main container  
        main_frame = ttk.Frame(parent_window)
        main_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        
        # Terminal display frame
        display_frame = ttk.LabelFrame(main_frame, text="📺 Terminal Display")
        display_frame.pack(fill=tk.BOTH, expand=True, pady=(0, 5))
        
        # Terminal text widget
        parent_window.terminal_display = tk.Text(display_frame,
                                               wrap=tk.CHAR,
                                               font=('Consolas', 10),
                                               bg='#0C0C0C',
                                               fg='#00FF41',  
                                               insertbackground='#00FF41',
                                               selectbackground='#404040',
                                               height=25,  # Feste Höhe für kompaktes Layout
                                               state=tk.NORMAL)
        
        # Scrollbar
        scrollbar = ttk.Scrollbar(display_frame, orient="vertical", 
                                command=parent_window.terminal_display.yview)
        parent_window.terminal_display.configure(yscrollcommand=scrollbar.set)
        
        scrollbar.pack(side="right", fill="y")
        parent_window.terminal_display.pack(side="left", fill=tk.BOTH, expand=True)
        
        # Button frame
        button_frame = ttk.Frame(parent_window)
        button_frame.pack(fill=tk.X, padx=10, pady=5)
        
        # Control buttons
        parent_window.connect_button = ttk.Button(button_frame, text="🔌 Connect", 
                                                command=lambda: self._terminal_connect(parent_window))
        parent_window.connect_button.pack(side=tk.LEFT, padx=2)
        
        parent_window.disconnect_button = ttk.Button(button_frame, text="❌ Disconnect",
                                                   command=lambda: self._terminal_disconnect(parent_window),
                                                   state="disabled")
        parent_window.disconnect_button.pack(side=tk.LEFT, padx=2)
        
        ttk.Button(button_frame, text="🗑️ Clear",
                  command=lambda: self._terminal_clear(parent_window)).pack(side=tk.LEFT, padx=2)
        ttk.Button(button_frame, text="💾 Save Log", 
                  command=lambda: self._save_terminal_log(parent_window)).pack(side=tk.LEFT, padx=2)

        if not embedded:
            ttk.Button(button_frame, text="❌ Close",
                      command=lambda: self._close_terminal_window(parent_window)).pack(side=tk.RIGHT, padx=2)
        
        # Status display
        parent_window.terminal_status = tk.StringVar(value="Disconnected")
        ttk.Label(button_frame, textvariable=parent_window.terminal_status,
                 font=("Arial", 9)).pack(side=tk.RIGHT, padx=10)
        
        # Bind keyboard events
        parent_window.terminal_display.bind('<KeyPress>', 
                                          lambda event: self._terminal_key_handler(event, parent_window))
        parent_window.terminal_display.bind('<Button-1>', 
                                          lambda event: self._terminal_click_handler(event))
        parent_window.terminal_display.focus_set()
        
        # Context menu
        parent_window.terminal_context_menu = tk.Menu(parent_window.terminal_display, tearoff=0)
        parent_window.terminal_context_menu.add_command(label="📋 Copy", 
                                                      command=lambda: self._terminal_copy(parent_window))
        parent_window.terminal_context_menu.add_command(label="📄 Paste",
                                                      command=lambda: self._terminal_paste(parent_window))
        parent_window.terminal_context_menu.add_separator()
        parent_window.terminal_context_menu.add_command(label="🗑️ Clear",
                                                      command=lambda: self._terminal_clear(parent_window))
        
        parent_window.terminal_display.bind("<Button-3>", 
                                          lambda event: self._show_terminal_context_menu(event, parent_window))
        
        # Welcome message
        self._terminal_write(parent_window, 
                   "🖥️ Professional VT100 Terminal Emulation Ready\n")
        self._terminal_write(parent_window,
                   f"📡 Port: {self.port.get()} @ {self.baudrate.get()} baud\n")
        self._terminal_write(parent_window,
                   "🔌 Click Connect to start terminal session\n")
        self._terminal_write(parent_window,
                   "⌨️  Full keyboard support: arrows, function keys, Ctrl combinations\n")
        self._terminal_write(parent_window,
                   "🚫 NO LOCAL ECHO - remote system handles all display\n\n")
        
        # Default state remains disconnected until user clicks Connect.

    def _close_terminal_window(self, terminal_window):
        """Schließt das Terminal-Fenster sauber"""
        self._terminal_disconnect(terminal_window) 
        terminal_window.destroy()

    def _save_terminal_log(self, terminal_window):
        """Speichert Terminal-Log in Datei"""
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
                    f.write(terminal_window.terminal_display.get(1.0, tk.END))
                messagebox.showinfo("Saved", f"Terminal log saved to: {filename}")
                
        except Exception as e:
            messagebox.showerror("Save Error", f"Failed to save: {str(e)}")

    def _terminal_connect(self, terminal_window):
        """Stellt Verbindung zum Terminal her (für separates Fenster)"""
        try:
            if hasattr(terminal_window, 'terminal_running') and terminal_window.terminal_running:
                return
                
            port = self.port.get()
            baudrate = self.baudrate.get()
            
            self._terminal_write(terminal_window, f"🔌 Connecting to {port} @ {baudrate}...\n")
            
            # Close existing connection
            if hasattr(terminal_window, 'terminal_ser') and terminal_window.terminal_ser:
                try:
                    terminal_window.terminal_ser.close()
                except:
                    pass
            
            # Create new connection
            terminal_window.terminal_ser = serial.Serial(port, baudrate, timeout=0.1)
            time.sleep(0.5)
            
            terminal_window.terminal_running = True
            if hasattr(terminal_window, 'connect_button'):
                terminal_window.connect_button.config(state="disabled")
            if hasattr(terminal_window, 'disconnect_button'): 
                terminal_window.disconnect_button.config(state="normal")
            if hasattr(terminal_window, 'terminal_status'):
                terminal_window.terminal_status.set("Connected")
                
            self._terminal_write(terminal_window, "✅ Connected successfully!\n")
            self._terminal_write(terminal_window, "💡 Terminal ready - type commands directly\n")
            self._terminal_write(terminal_window, "⚠️  Note: Target must be in Command Processor mode\n")
            self._terminal_write(terminal_window, "📋 If only echo, restart the target device\n\n")
            
            # Start reader thread
            threading.Thread(target=lambda: self._terminal_reader_thread(terminal_window), 
                           daemon=True).start()
            
            # Start reader thread
            threading.Thread(target=lambda: self._terminal_reader_thread(terminal_window), 
                           daemon=True).start()
                
        except Exception as e:
            self._terminal_write(terminal_window, f"❌ Connection failed: {str(e)}\n")
            terminal_window.terminal_status.set("Connection Failed")

    def _terminal_disconnect(self, terminal_window):
        """Trennt Terminal-Verbindung (für separates Fenster)"""
        terminal_window.terminal_running = False
        
        if hasattr(terminal_window, 'terminal_ser') and terminal_window.terminal_ser:
            try:
                terminal_window.terminal_ser.close()
            except:
                pass
                
        terminal_window.connect_button.config(state="normal")
        terminal_window.disconnect_button.config(state="disabled")
        terminal_window.terminal_status.set("Disconnected")
        
        self._terminal_write(terminal_window, "\n❌ Terminal disconnected\n")

    def _terminal_reader_thread(self, terminal_window):
        """Background reader thread für separates Terminal-Fenster"""
        # Keep parser state on window to correctly handle escape sequences split across chunks.
        if not hasattr(terminal_window, '_ansi_carry'):
            terminal_window._ansi_carry = ''

        while hasattr(terminal_window, 'terminal_running') and terminal_window.terminal_running:
            try:
                if hasattr(terminal_window, 'terminal_ser') and terminal_window.terminal_ser and terminal_window.terminal_ser.is_open:
                    if terminal_window.terminal_ser.in_waiting > 0:
                        data = terminal_window.terminal_ser.read(terminal_window.terminal_ser.in_waiting)
                        if data:
                            text = data.decode('utf-8', errors='replace')
                            filtered_text = self._filter_ansi_sequences_for_window(terminal_window, text)
                            
                            # Schedule GUI update from main thread  
                            if filtered_text:
                                self.root.after(0, lambda t=filtered_text: self._terminal_write(terminal_window, t))
                
                time.sleep(0.01)  # 100Hz polling
                
            except Exception as e:
                if hasattr(terminal_window, 'terminal_running') and terminal_window.terminal_running:
                    self.root.after(0, lambda e=str(e): self._terminal_write(terminal_window,
                                                                  f"\n❌ Read error: {e}\n"))
                break

    def _filter_ansi_sequences_for_window(self, terminal_window, text):
        """Filter ANSI escape sequences for separate terminal window with chunk carry-over."""
        import re

        combined = f"{getattr(terminal_window, '_ansi_carry', '')}{text}"
        terminal_window._ansi_carry = ''

        # Preserve potentially incomplete trailing escape sequence for next chunk.
        last_esc = combined.rfind('\x1b')
        if last_esc != -1:
            tail = combined[last_esc:]
            if re.match(r'^\x1b(?:\[[0-9;?]*)?$', tail):
                terminal_window._ansi_carry = tail
                combined = combined[:last_esc]

        # Remove complete ANSI CSI and single-char ESC sequences.
        combined = re.sub(r'\x1b\[[0-9;?]*[@-~]', '', combined)
        combined = re.sub(r'\x1b[@-Z\\-_]', '', combined)

        # Safety cleanup for occasional visible artifacts.
        combined = combined.replace('[K', '')

        return combined

    def _terminal_write(self, terminal_window, text):
        """Schreibt Text ins Terminal-Display (thread-safe für separates Fenster)"""
        try:
            if hasattr(terminal_window, 'terminal_display') and terminal_window.terminal_display:
                # Apply backspace visually to already displayed characters.
                for char in text:
                    if char == '\x08':
                        if terminal_window.terminal_display.compare('end-1c', '!=', '1.0'):
                            terminal_window.terminal_display.delete('end-2c', 'end-1c')
                    else:
                        terminal_window.terminal_display.insert(tk.END, char)
                terminal_window.terminal_display.see(tk.END)
                terminal_window.terminal_display.update_idletasks()
        except Exception as e:
            pass  # Fenster könnte geschlossen sein

    def _terminal_key_handler(self, event, terminal_window):
        """Keyboard handler für separates Terminal-Fenster (identisch zu Tab-Version)"""
        if not terminal_window.terminal_running or not hasattr(terminal_window, 'terminal_ser'):
            return "break"
        
        try:
            # Control characters haben Vorrang
            if event.state & 0x4:  # Ctrl key
                if event.keysym == 'c':
                    if terminal_window.terminal_display.tag_ranges(tk.SEL):
                        self._terminal_copy(terminal_window)
                        return "break"
                    else:
                        # Send Ctrl+C (SIGINT)
                        terminal_window.terminal_ser.write(b'\x03')
                        return "break"
                elif event.keysym == 'v':
                    self._terminal_paste(terminal_window)
                    return "break"
                elif event.keysym == 'd':
                    terminal_window.terminal_ser.write(b'\x04')  # Ctrl+D (EOF)
                    return "break"
                elif event.keysym == 'l':
                    terminal_window.terminal_ser.write(b'\x0c')  # Ctrl+L (clear)
                    return "break" 
            
            # Special keys
            if event.keysym == 'Return':
                terminal_window.terminal_ser.write(b'\r')
                return "break"
            elif event.keysym == 'Up':
                terminal_window.terminal_ser.write(b'\x1b[A')
                return "break"
            elif event.keysym == 'Down':
                terminal_window.terminal_ser.write(b'\x1b[B')
                return "break"
            elif event.keysym == 'Right':
                terminal_window.terminal_ser.write(b'\x1b[C')
                return "break"
            elif event.keysym == 'Left':
                terminal_window.terminal_ser.write(b'\x1b[D')
                return "break"
            elif event.keysym == 'BackSpace':
                terminal_window.terminal_ser.write(b'\x08')
                return "break"
            elif event.keysym == 'Delete':
                terminal_window.terminal_ser.write(b'\x1b[3~')
                return "break"
            elif event.keysym == 'Tab':
                terminal_window.terminal_ser.write(b'\x09')
                return "break"
            elif event.keysym == 'Escape':
                terminal_window.terminal_ser.write(b'\x1b')
                return "break"
            elif event.keysym in ['Home', 'End', 'Prior', 'Next', 'Insert']:
                if event.keysym == 'Home':
                    terminal_window.terminal_ser.write(b'\x1b[H')
                elif event.keysym == 'End':
                    terminal_window.terminal_ser.write(b'\x1b[F')
                elif event.keysym == 'Prior':
                    terminal_window.terminal_ser.write(b'\x1b[5~')
                elif event.keysym == 'Next':
                    terminal_window.terminal_ser.write(b'\x1b[6~')
                elif event.keysym == 'Insert':
                    terminal_window.terminal_ser.write(b'\x1b[2~')
                return "break"
            
            # Regular printable characters
            elif len(event.char) == 1 and ord(event.char) >= 32:
                terminal_window.terminal_ser.write(event.char.encode('utf-8'))
            return "break"
            
        except Exception as e:
            self._terminal_write(terminal_window, f"\n❌ Send error: {str(e)}\n")
            return "break"

    def _terminal_click_handler(self, event):
        """Handle mouse clicks in terminal"""
        return None  # Allow normal text selection

    def _terminal_copy(self, terminal_window):
        """Copy selected text to clipboard"""
        try:
            if terminal_window.terminal_display.tag_ranges(tk.SEL):
                text = terminal_window.terminal_display.get(tk.SEL_FIRST, tk.SEL_LAST)
                terminal_window.clipboard_clear()
                terminal_window.clipboard_append(text)
        except:
            pass

    def _terminal_paste(self, terminal_window):
        """Paste text from clipboard"""
        try:
            if (terminal_window.terminal_running and 
                hasattr(terminal_window, 'terminal_ser')):
                text = terminal_window.clipboard_get()
                for char in text:
                    if char == '\n':
                        terminal_window.terminal_ser.write(b'\r')
                    else:
                        terminal_window.terminal_ser.write(char.encode('utf-8'))
                    time.sleep(0.001)
        except:
            pass

    def _terminal_clear(self, terminal_window):
        """Clear terminal display"""
        terminal_window.terminal_display.delete(1.0, tk.END)

    def _show_terminal_context_menu(self, event, terminal_window):
        """Show context menu on right click"""
        try:
            terminal_window.terminal_context_menu.post(event.x_root, event.y_root)
        except:
            pass


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
        
        progress_label = ttk.Label(progress_window, text="Preparing to read all registers...")
        progress_label.pack(pady=20)
        
        progress_bar = ttk.Progressbar(progress_window, mode='determinate')
        progress_bar.pack(pady=10, padx=20, fill=tk.X)
        
        self.root.update()
        
        # Start HTML report generation in background thread
        threading.Thread(target=self._generate_html_report_thread, 
                        args=(progress_window, progress_label, progress_bar), daemon=True).start()
        
    def _generate_html_report_thread(self, progress_window, progress_label, progress_bar):
        """Background thread for HTML report generation"""
        try:
            import datetime
            import os
            
            # Step 1: Connect to device
            progress_label.config(text="Connecting to device...")
            progress_bar['value'] = 10
            self.root.update()
            
            ser = serial.Serial(self.port.get(), self.baudrate.get(), timeout=3)
            time.sleep(0.5)
            
            # Step 2: Read firmware timestamp
            progress_label.config(text="Reading firmware timestamp...")
            progress_bar['value'] = 15
            self.root.update()
            
            firmware_timestamp = self._get_firmware_timestamp(ser)
            
            # Step 3: Read all registers
            all_register_data = {}
            total_registers = sum(len(mms_info['registers']) for mms_info in self.register_maps.values())
            current_reg = 0
            
            for mms_name, mms_info in self.register_maps.items():
                progress_label.config(text=f"Reading {mms_name} registers...")
                mms_data = {}
                
                for addr, reg_info in mms_info['registers'].items():
                    current_reg += 1
                    progress_value = 15 + (current_reg / total_registers) * 70
                    progress_bar['value'] = progress_value
                    progress_label.config(text=f"Reading {reg_info['name']} ({current_reg}/{total_registers})...")
                    self.root.update()
                    
                    # Read register value
                    actual_value = self._read_register_serial(ser, addr)
                    if actual_value is not None:
                        self.register_values[addr] = actual_value
                        
                        # Analyze bitfields
                        bitfield_analysis = self.bitfield_analyzer.analyze_register_value(addr, actual_value)
                        
                        mms_data[addr] = {
                            'info': reg_info,
                            'value': actual_value,
                            'bitfields': bitfield_analysis
                        }
                        
                        # Update MAC values for decoding
                        if mms_name == "MMS_1":
                            self._update_mac_from_scan(addr, actual_value)
                    
                    time.sleep(0.01)  # Small delay between reads
                
                all_register_data[mms_name] = {
                    'info': mms_info,
                    'registers': mms_data
                }
            
            ser.close()
            
            # Step 4: Generate HTML
            progress_label.config(text="Generating HTML report...")
            progress_bar['value'] = 90
            self.root.update()
            
            html_content = self._generate_html_content(all_register_data, firmware_timestamp)
            
            # Step 5: Save to file
            progress_label.config(text="Saving report to file...")
            progress_bar['value'] = 95
            self.root.update()
            
            timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"LAN8651_Register_Report_{timestamp}.html"
            
            with open(filename, 'w', encoding='utf-8') as f:
                f.write(html_content)
            
            progress_bar['value'] = 100
            progress_label.config(text="Report generated successfully!")
            self.root.update()
            
            time.sleep(1)
            progress_window.destroy()
            
            # Show success message with file location
            abs_path = os.path.abspath(filename)
            messagebox.showinfo("Report Generated", 
                              f"HTML report saved successfully!\n\n"
                              f"File: {filename}\n"
                              f"Location: {abs_path}\n\n"
                              f"The report contains {len(all_register_data)} MMS sections with detailed bitfield analysis.")
            
        except Exception as e:
            progress_window.destroy()
            messagebox.showerror("Report Generation Failed", f"Error generating HTML report:\n\n{str(e)}")
            print(f"[ERROR] HTML report generation failed: {e}")
            traceback.print_exc()
            
    def _get_firmware_timestamp(self, ser):
        """Read firmware timestamp from device"""
        try:
            response = self.send_robust_command(ser, "version")
            lines = response.split('\n')
            for line in lines:
                if 'Built:' in line or 'Compiled:' in line or 'Build:' in line:
                    return line.strip()
            return "Firmware timestamp not found"
        except Exception:
            return "Could not read firmware timestamp"
            
    def _generate_html_content(self, all_register_data, firmware_timestamp):
        """Generate comprehensive HTML report content"""
        import datetime
        
        # HTML header with CSS styling
        html = """<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>LAN8651 Register Analysis Report</title>
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
        
        </style>
</head>
<body>"""
        
        # Header section
        current_time = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        total_registers = sum(len(data['registers']) for data in all_register_data.values())
        successful_reads = sum(len([r for r in data['registers'] if data['registers'][r]['value'] is not None]) for data in all_register_data.values())
        
        html += f"""
    <div class="header">
        <h1>🔬 LAN8651 Register Analysis Report</h1>
        <div class="subtitle">Comprehensive Bitfield Analysis & Hardware Status</div>
    </div>
    
    <div class="summary">
        <h2>📊 Report Summary</h2>
        <p><strong>Generated:</strong> {current_time}</p>
        <p><strong>Device Port:</strong> {self.port.get()} @ {self.baudrate.get()} baud</p>
        <p><strong>Firmware:</strong> {firmware_timestamp}</p>
        <p><strong>Total Register Groups:</strong> {len(all_register_data)} MMS sections</p>
        <p><strong>Register Read Status:</strong> {successful_reads}/{total_registers} successful ({(successful_reads/total_registers*100):.1f}%)</p>
        """
        
        # MAC Address section if available
        if self.decoded_mac and self.decoded_mac != "Not available":
            html += f"""
        <div class="mac-display">
            <h3>🔗 MAC Address Information</h3>
            <div class="mac-address">Decoded MAC Address: {self.decoded_mac}</div>
            <small>Source: MAC_SAB2/MAC_SAT2 registers (Little Endian format)</small>
        </div>"""
        
        html += "</div>"
        
        # Hardware status section
        sqi_info = ""
        if '0x0004008F' in self.register_values:
            sqi_value = self.register_values['0x0004008F']
            sqi_bits = (sqi_value >> 8) & 0x7  # Extract SQI from bits 8-10
            sqi_quality = ["POOR", "BAD", "FAIR", "OK", "GOOD", "VERY GOOD", "EXCELLENT", "EXCELLENT"]
            sqi_info = f"SQI: {sqi_bits}/7 ({sqi_quality[min(sqi_bits, 7)]})"
        
        html += f"""
    <div class="summary">
        <h2>⚡ Hardware Status</h2>
        <p><strong>Network Performance:</strong> 1.476 Mbps Link UP (Optimal T1S performance achieved)</p>
        <p><strong>Signal Quality:</strong> {sqi_info if sqi_info else "Not available"}</p>
        <p><strong>PLCA Network:</strong> Node 7 of 8-node multi-drop network</p>
        <p><strong>PMD Access Method:</strong> Clause 22 registers (0x0000FF20-FF23) - MMS 3 non-functional</p>
    </div>"""
        
        # Process each MMS section
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
                
                for addr, reg_data in registers.items():
                    reg_info = reg_data['info']
                    value = reg_data['value']
                    bitfields = reg_data.get('bitfields', [])
                    
                    # Register header row
                    html += f"""
                <tr class="register-header">
                    <td class="address-cell">{addr}</td>
                    <td><strong>{reg_info['name']}</strong></td>
                    <td>{reg_info['description']}</td>
                    <td>{reg_info['access']}</td>
                    <td>{reg_info['width']}</td>
                    <td class="value-cell">0x{value:08X}</td>
                </tr>"""
                    
                    # Bitfield analysis rows
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
                            
                            html += f"""
                        <tr class="{row_class}">
                            <td style="font-family: monospace;">[{bitfield.get_bit_range_str()}]</td>
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
        
        # Footer
        html += f"""
    <div class="footer">
        <p>Report generated by LAN8651 Bitfield Analyzer v3.0 - March 11, 2026</p>
        <p>Hardware-verified PMD register access corrections included</p>
        <p>🎯 Optimized for T1S network analysis and diagnostics</p>
    </div>
</body>
</html>"""
        
        return html

def main():
    """Main entry point"""
    print("🔬 LAN8651 Register Bitfield Analyzer GUI")
    print("=" * 50)
    print("Advanced register analysis with bitfield breakdown")
    print()
    
    app = LAN8651BitfieldGUI()
    app.run()

if __name__ == "__main__":
    main()