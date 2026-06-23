#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
MMS0 Register Reader für Windows
Kommunikation über COM Port mit Linux Terminal für LAN865X Register-Zugriff

Author: Martin
Date: March 11, 2026
"""

import serial
import time
import re
import sys
from typing import Dict, Optional, Tuple

class MMS0RegisterReader:
    """MMS0 Register Reader über COM Port"""
    
    def __init__(self, com_port: str = "COM9", baudrate: int = 115200, timeout: int = 5):
        self.com_port = com_port
        self.baudrate = baudrate
        self.timeout = timeout
        self.serial_conn = None
        self.debug_dir = "/sys/kernel/debug/lan865x_spi0.0"
        
        # MMS0 Register Definition basierend auf Open Alliance Standard
        self.mms0_registers = {
            0x00000000: {"name": "OA_ID", "desc": "Open Alliance ID Register", "expected": "0x00000011"},
            0x00000001: {"name": "OA_PHYID", "desc": "Open Alliance PHY ID", "expected": "Variable"},
            0x00000002: {"name": "OA_STDCAP", "desc": "Standard Capabilities Register", "expected": "Variable"},
            0x00000003: {"name": "OA_RESET", "desc": "Reset Control and Status Register", "expected": "0x00000000"},
            0x00000004: {"name": "OA_CONFIG0", "desc": "Configuration Register 0", "expected": "0x00000000"},
            0x00000008: {"name": "OA_STATUS0", "desc": "Status Register 0", "expected": "Variable"},
            0x00000009: {"name": "OA_STATUS1", "desc": "Status Register 1", "expected": "Variable"},
            0x0000000B: {"name": "OA_BUFSTS", "desc": "Buffer Status Register", "expected": "Variable"},
            0x0000000C: {"name": "OA_IMASK0", "desc": "Interrupt Mask Register 0", "expected": "0x00000000"},
            0x0000000D: {"name": "OA_IMASK1", "desc": "Interrupt Mask Register 1", "expected": "0x00000000"},
            0x0000FF00: {"name": "BASIC_CONTROL", "desc": "PHY Basic Control Register", "expected": "0x0000"},
            0x0000FF01: {"name": "BASIC_STATUS", "desc": "PHY Basic Status Register", "expected": "Variable"},
            0x0000FF02: {"name": "PHY_ID1", "desc": "PHY Identifier Register 1", "expected": "0x0007"},
            0x0000FF03: {"name": "PHY_ID2", "desc": "PHY Identifier Register 2", "expected": "0xC0F0"},
            0x0000FF20: {"name": "PMD_CONTROL", "desc": "PMD Control (Clause 22) - Working!", "expected": "0x0000"},
            0x0000FF21: {"name": "PMD_STATUS", "desc": "PMD Status (Clause 22) - Link UP!", "expected": "0x0805"},
            0x0000FF22: {"name": "PMD_ID1", "desc": "PMD ID1 (Clause 22) - Real Data", "expected": "0x0007"},
            0x0000FF23: {"name": "PMD_ID2", "desc": "PMD ID2 (Clause 22) - Real Data", "expected": "0xC1B3"},
        }
    
    def connect(self) -> bool:
        """Verbindung zum COM Port herstellen"""
        try:
            print(f"🔌 Verbinde zu {self.com_port} ({self.baudrate} baud)...")
            self.serial_conn = serial.Serial(
                port=self.com_port,
                baudrate=self.baudrate,
                timeout=self.timeout,
                xonxoff=False,
                rtscts=False,
                dsrdtr=False
            )
            
            if self.serial_conn.is_open:
                print("✅ COM Port erfolgreich geöffnet")
                # Initial setup
                time.sleep(1)
                self._send_command("\n")  # Enter drücken für Prompt
                return True
            else:
                print("❌ COM Port konnte nicht geöffnet werden")
                return False
                
        except serial.SerialException as e:
            print(f"❌ Fehler beim Öffnen des COM Ports: {e}")
            return False
        except Exception as e:
            print(f"❌ Unerwarteter Fehler: {e}")
            return False
    
    def disconnect(self):
        """Verbindung trennen"""
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.close()
            print("🔌 COM Port geschlossen")
    
    def _send_command(self, command: str) -> str:
        """Kommando über COM Port senden und Antwort empfangen"""
        if not self.serial_conn or not self.serial_conn.is_open:
            return ""
        
        try:
            # Kommando senden
            self.serial_conn.write(f"{command}\n".encode('utf-8'))
            self.serial_conn.flush()
            
            # Antwort lesen
            response = ""
            start_time = time.time()
            
            while time.time() - start_time < self.timeout:
                if self.serial_conn.in_waiting > 0:
                    chunk = self.serial_conn.read(self.serial_conn.in_waiting).decode('utf-8', errors='ignore')
                    response += chunk
                    
                    # Check for prompt (einfache Heuristik)
                    if response.endswith("# ") or response.endswith("$ ") or response.endswith("> "):
                        break
                        
                time.sleep(0.1)
            
            return response.strip()
            
        except Exception as e:
            print(f"❌ Fehler beim Senden des Kommandos '{command}': {e}")
            return ""
    
    def check_debug_interface(self) -> bool:
        """Prüfen ob LAN865X Debug Interface verfügbar ist"""
        print("🔍 Prüfe Debug Interface...")
        
        # Nach Debug-Verzeichnis suchen
        response = self._send_command("find /sys/kernel/debug -name '*lan865x*' 2>/dev/null")
        
        if "lan865x" in response:
            # Debug-Verzeichnis aus der Antwort extrahieren
            lines = response.split('\n')
            found_path = None
            for line in lines:
                line = line.strip()
                # Suche nach Zeile die mit /sys/kernel/debug anfängt und lan865x enthält
                if line.startswith('/sys/kernel/debug') and 'lan865x' in line:
                    found_path = line
                    break
            
            if found_path:
                self.debug_dir = found_path
                print(f"✅ Debug Interface gefunden: {self.debug_dir}")
            else:
                print("❌ Konnte Debug-Pfad nicht aus Antwort extrahieren")
                return False
            
            # Testen ob register file verfügbar ist
            test_response = self._send_command(f"ls -la {self.debug_dir}/register 2>/dev/null")
            
            if "register" in test_response and ("-rw-" in test_response or "-r--r--r--" in test_response):
                print("✅ Register-Interface verfügbar")
                return True
            else:
                print("❌ Register-File nicht verfügbar")
                return False
        else:
            print("❌ Kein LAN865X Debug Interface gefunden")
            print("💡 Versuche: sudo mount -t debugfs none /sys/kernel/debug")
            return False
    
    def read_register(self, address: int) -> Optional[str]:
        """Einzelnes Register lesen über debugfs interface"""
        addr_hex = f"0x{address:08X}"
        
        # Register-Adresse in register file schreiben
        cmd1 = f"echo {addr_hex} > {self.debug_dir}/register"
        response1 = self._send_command(cmd1)
        
        if "Permission denied" in response1:
            print(f"❌ Keine Berechtigung für Register {addr_hex}")
            return None
        
        time.sleep(0.05)  # Minimum Wartezeit für Register-Verarbeitung
        
        # dmesg für die letzten 3 Zeilen lesen (für bessere Chance auf neuen Eintrag)
        cmd2 = "dmesg | tail -3"
        response2 = self._send_command(cmd2)
        
        # Nach Register read Zeile suchen
        # Format: [timestamp] lan8650 spi0.0: Register read: 0x12345678 = 0x87654321
        reg_pattern = rf'Register read: {addr_hex} = (0x[0-9A-Fa-f]{{8}})'
        match = re.search(reg_pattern, response2, re.IGNORECASE)
        
        if match:
            return match.group(1)  # Return the register value
        else:
            # Fallback: suche nach beliebigem Register read Pattern in der letzten Zeit
            general_pattern = r'Register read: (0x[0-9A-Fa-f]{8}) = (0x[0-9A-Fa-f]{8})'
            match = re.search(general_pattern, response2, re.IGNORECASE)
            if match and match.group(1).upper() == addr_hex.upper():
                return match.group(2)
            else:
                return None
    
    def scan_mms0_registers(self):
        """Alle MMS0 Register scannen"""
        print("\n" + "="*80)
        print("📊 MMS0 REGISTER SCAN - Open Alliance Standard + PHY Clause 22")
        print("="*80)
        
        if not self.check_debug_interface():
            print("❌ Debug Interface nicht verfügbar - Abbruch")
            return
        
        print(f"\n🎯 Scanne {len(self.mms0_registers)} Register...\n")
        
        success_count = 0
        
        # Header
        print(f"{'Address':>12} {'Register Name':>20} {'Expected':>12} {'Actual':>12} {'Status':>8} {'Description'}")
        print("-" * 90)
        
        for address, reg_info in self.mms0_registers.items():
            value = self.read_register(address)
            
            if value:
                success_count += 1
                
                # Status bestimmen
                if reg_info["expected"] == "Variable":
                    status = "✅ OK"
                elif value.upper() == reg_info["expected"].upper():
                    status = "✅ MATCH"
                else:
                    status = "⚠️ DIFF"
                
                # Ausgabe formatieren
                print(f"0x{address:08X} {reg_info['name']:>20} {reg_info['expected']:>12} {value:>12} {status:>8} {reg_info['desc']}")
                
            else:
                print(f"0x{address:08X} {reg_info['name']:>20} {reg_info['expected']:>12} {'FAILED':>12} {'❌ ERR':>8} {reg_info['desc']}")
            
            time.sleep(0.2)  # Kurze Pause zwischen Registern
        
        print("-" * 90)
        print(f"📊 Scan abgeschlossen: {success_count}/{len(self.mms0_registers)} Register erfolgreich gelesen ({success_count/len(self.mms0_registers)*100:.1f}%)")
        
        if success_count == len(self.mms0_registers):
            print("🎉 Alle Register erfolgreich ausgelesen!")
        elif success_count > len(self.mms0_registers) * 0.7:
            print("✅ Großteil der Register erfolgreich - System funktional")
        else:
            print("⚠️ Viele Register-Lesefehler - System möglicherweise nicht bereit")
    
    def interactive_mode(self):
        """Interaktiver Modus für manuelle Register-Abfragen"""
        print("\n🔧 Interaktiver Modus gestartet")
        print("Befehle: scan, read <addr>, quit")
        print("Beispiel: read 0x00000000")
        
        while True:
            try:
                cmd = input("\nmms0> ").strip().lower()
                
                if not cmd:
                    continue
                
                if cmd in ['quit', 'exit', 'q']:
                    break
                elif cmd == 'scan':
                    self.scan_mms0_registers()
                elif cmd.startswith('read '):
                    try:
                        addr_str = cmd.split()[1]
                        address = int(addr_str, 16)
                        value = self.read_register(address)
                        if value:
                            print(f"Register {addr_str}: {value}")
                        else:
                            print(f"❌ Konnte Register {addr_str} nicht lesen")
                    except (IndexError, ValueError):
                        print("❌ Ungültiges Format. Beispiel: read 0x00000000")
                else:
                    print("❌ Unbekannter Befehl. Verfügbar: scan, read <addr>, quit")
                    
            except KeyboardInterrupt:
                print("\n👋 Beende interaktiven Modus...")
                break


def main():
    """Hauptfunktion"""
    print("🚀 MMS0 Register Reader v1.0")
    print("Windows -> COM9 -> Linux Terminal -> LAN865X debugfs")
    print("-" * 60)
    
    reader = MMS0RegisterReader("COM9")
    
    try:
        if not reader.connect():
            print("❌ Verbindung fehlgeschlagen")
            return 1
        
        # Automatischen Scan durchführen
        reader.scan_mms0_registers()
        
        # Interaktiven Modus anbieten
        print("\n🔧 Möchten Sie in den interaktiven Modus wechseln? (y/n)")
        response = input("Antwort: ").strip().lower()
        
        if response in ['y', 'yes', 'ja', 'j']:
            reader.interactive_mode()
        
        print("\n👋 Programm beendet")
        return 0
        
    except KeyboardInterrupt:
        print("\n🛑 Benutzerabbruch")
        return 1
    except Exception as e:
        print(f"\n❌ Unerwarteter Fehler: {e}")
        return 1
    finally:
        reader.disconnect()


if __name__ == "__main__":
    sys.exit(main())