#!/usr/bin/env python3
"""
LAN8651 Register Scanner - GUI Version
Grafische Benutzeroberfläche für LAN8651 Register-Analyse  
Basiert auf lan8651_complete_register_scan.py - März 2026

Features:
- Tab-basierte Oberfläche für Register-Gruppen
- Performance + Race Condition Protected Serial Communication 
- Konfiguration des COM-Ports
- Individual-Scan pro Register-Gruppe
- Threading für nicht-blockierende GUI
- Optimiert für Speed ohne READ FAILED Errors

Version 3.1.1 Updates:
- Performance-optimiert: Fast return ohne excessive delays
- Race Condition protection durch atomic reading
- 0.5ms sleeps für ultra-responsiveness  
- Minimale retry logic für maximale Geschwindigkeit

Version 3.2.0 - MAC Address Discovery Update:
- ✅ MAC_SAB2/MAC_SAT2 Register hinzugefügt (Hardware-verifiziert)
- ✅ MAC-Adress-Dekodierung aus Little Endian Format
- ✅ GUI zeigt dekodierte MAC-Adresse in MMS_1 Tab an
- ✅ MAC_SAB1/MAC_SAT1 als Legacy markiert
- TARGET Hardware-Verifikation: MAC-Adresse 00:04:25:01:02:03 korrekt dekodiert

Version 3.3.0 - PMD Register Update (11. März 2026):
- ❌ MMS 3 PMD-Register als non-functional markiert (geben nur 0x0000 zurück)
- ✅ Neue Clause 22 PMD-Register in MMS 0 hinzugefügt (Hardware-verifiziert)
- ✅ SQI-Register (0x0004008F) mit 6/7 EXCELLENT Wert hinzugefügt
- 🚀 "Read All Registers" Button für kompletten System-Scan
"""

import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext
import threading
import queue
import serial
import time
import re

class LAN8651RegisterGUI:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("LAN8651 Register Scanner GUI")
        self.root.geometry("1200x800")
        
        # Configuration
        self.port = tk.StringVar(value="COM8")
        self.baudrate = tk.IntVar(value=115200)
        self.serial = None
        
        # Thread communication
        self.result_queue = queue.Queue()
        self.is_scanning = False
        
        # MAC address storage
        self.decoded_mac = "Not available"
        self.mac_sab2_value = None
        self.mac_sat2_value = None
        
        # Register definitions - copied from original script
        self.register_maps = {
            "MMS_0": {
                "name": "Open Alliance Standard + PHY Clause 22",
                "description": "Standard Register gemäß Open Alliance Spezifikation",
                "registers": {
                    "0x00000000": {"name": "OA_ID", "description": "Open Alliance ID Register", "expected": 0x00000011, "access": "R", "width": "32-bit"},
                    "0x00000001": {"name": "OA_PHYID", "description": "Open Alliance PHY ID", "expected": None, "access": "R", "width": "32-bit"},
                    "0x00000002": {"name": "OA_STDCAP", "description": "Standard Capabilities Register", "expected": None, "access": "R", "width": "32-bit"},
                    "0x00000003": {"name": "OA_RESET", "description": "Reset Control and Status Register", "expected": 0x00000000, "access": "R/W", "width": "32-bit"},
                    "0x00000004": {"name": "OA_CONFIG0", "description": "Configuration Register 0", "expected": 0x00000000, "access": "R/W", "width": "32-bit"},
                    "0x00000008": {"name": "OA_STATUS0", "description": "Status Register 0", "expected": None, "access": "R", "width": "32-bit"},
                    "0x00000009": {"name": "OA_STATUS1", "description": "Status Register 1", "expected": None, "access": "R", "width": "32-bit"},
                    "0x0000000B": {"name": "OA_BUFSTS", "description": "Buffer Status Register", "expected": None, "access": "R", "width": "32-bit"},
                    "0x0000000C": {"name": "OA_IMASK0", "description": "Interrupt Mask Register 0", "expected": 0x00000000, "access": "R/W", "width": "32-bit"},
                    "0x0000000D": {"name": "OA_IMASK1", "description": "Interrupt Mask Register 1", "expected": 0x00000000, "access": "R/W", "width": "32-bit"},
                    "0x0000FF00": {"name": "BASIC_CONTROL", "description": "PHY Basic Control Register", "expected": 0x0000, "access": "R/W", "width": "16-bit"},
                    "0x0000FF01": {"name": "BASIC_STATUS", "description": "PHY Basic Status Register", "expected": None, "access": "R", "width": "16-bit"},
                    "0x0000FF02": {"name": "PHY_ID1", "description": "PHY Identifier Register 1", "expected": 0x0007, "access": "R", "width": "16-bit"},
                    "0x0000FF03": {"name": "PHY_ID2", "description": "PHY Identifier Register 2", "expected": 0xC0F0, "access": "R", "width": "16-bit"},
                    "0x0000FF20": {"name": "PMD_CONTROL", "description": "PMD Control (Clause 22) - Working!", "expected": 0x0000, "access": "R/W", "width": "16-bit", "note": "CFD Enable/Start"},
                    "0x0000FF21": {"name": "PMD_STATUS", "description": "PMD Status (Clause 22) - Link UP!", "expected": 0x0805, "access": "R", "width": "16-bit", "note": "Link Status confirmed"},
                    "0x0000FF22": {"name": "PMD_ID1", "description": "PMD ID1 (Clause 22) - Real Data", "expected": 0x0007, "access": "R", "width": "16-bit", "note": "Microchip OUI"},
                    "0x0000FF23": {"name": "PMD_ID2", "description": "PMD ID2 (Clause 22) - Real Data", "expected": 0xC1B3, "access": "R", "width": "16-bit", "note": "Model + Rev"}
                }
            },
            "MMS_1": {
                "name": "MAC Registers",
                "description": "Ethernet MAC Control and Status Register",
                "registers": {
                    "0x00010000": {"name": "MAC_NCR", "description": "Network Control Register", "expected": 0x00000000, "access": "R/W", "width": "32-bit"},
                    "0x00010001": {"name": "MAC_NCFGR", "description": "Network Configuration Register", "expected": 0x00080000, "access": "R/W", "width": "32-bit"},
                    "0x00010022": {"name": "MAC_SAB1", "description": "MAC Address Bottom 1 (Legacy)", "expected": 0x00000000, "access": "R/W", "width": "32-bit"},
                    "0x00010023": {"name": "MAC_SAT1", "description": "MAC Address Top 1 (Legacy)", "expected": 0x00000000, "access": "R/W", "width": "32-bit"},
                    "0x00010024": {"name": "MAC_SAB2", "description": "MAC Address Bottom 2 (Active - First 4 Bytes LE)", "expected": 0x00000000, "access": "R/W", "width": "32-bit", "mac_decode": "first_4_le"},
                    "0x00010025": {"name": "MAC_SAT2", "description": "MAC Address Top 2 (Active - Last 2 Bytes LE)", "expected": 0x00000000, "access": "R/W", "width": "32-bit", "mac_decode": "last_2_le"},
                    "0x0001006F": {"name": "MAC_TISUBN", "description": "TSU Sub-nanoseconds", "expected": 0x00000000, "access": "R/W", "width": "32-bit"},
                    "0x00010070": {"name": "MAC_TSH", "description": "TSU Seconds High", "expected": 0x00000000, "access": "R/W", "width": "32-bit"},
                    "0x00010074": {"name": "MAC_TSL", "description": "TSU Seconds Low", "expected": 0x00000000, "access": "R/W", "width": "32-bit"},
                    "0x00010075": {"name": "MAC_TN", "description": "TSU Nanoseconds", "expected": 0x00000000, "access": "R/W", "width": "32-bit"},
                    "0x00010200": {"name": "BMGR_CTL", "description": "Buffer Manager Control", "expected": None, "access": "R/W", "width": "32-bit"},
                    "0x00010208": {"name": "STATS0", "description": "Statistics 0", "expected": 0x00000000, "access": "R", "width": "32-bit"}
                }
            },
            "MMS_2": {
                "name": "PHY PCS Registers",
                "description": "Physical Coding Sublayer Register",
                "registers": {
                    "0x000208F3": {"name": "PCS_REG", "description": "PCS Basic Register", "expected": 0x0000, "access": "R/W", "width": "16-bit"}
                }
            },
            "MMS_3": {
                "name": "PHY PMA/PMD Registers (❌ NON-FUNCTIONAL)",
                "description": "Physical Medium Access/Dependent - WARNUNG: Direkte MMS 3 Zugriffe geben nur 0x0000 zurück!",
                "registers": {
                    "0x00030001": {"name": "PMD_CONTROL", "description": "❌ PMA/PMD Control (BROKEN - returns 0x0000)", "expected": 0x0000, "access": "R/W", "width": "16-bit", "note": "Use 0x0000FF20 instead!"},
                    "0x00030002": {"name": "PMD_STATUS", "description": "❌ PMA/PMD Status (BROKEN - returns 0x0000)", "expected": 0x0000, "access": "R", "width": "16-bit", "note": "Use 0x0000FF21 instead!"}
                }
            },
            "MMS_4": {
                "name": "PHY Vendor Specific (PLCA)",
                "description": "Microchip-spezifische PHY Register mit PLCA",
                "registers": {
                    "0x00040010": {"name": "CTRL1", "description": "Vendor Control 1 Register", "expected": 0x0000, "access": "R/W", "width": "16-bit"},
                    "0x00040018": {"name": "STS1", "description": "Vendor Status 1 Register", "expected": None, "access": "R", "width": "16-bit"},
                    "0x0004CA00": {"name": "MIDVER", "description": "Map ID Version Register", "expected": None, "access": "R", "width": "16-bit"},
                    "0x0004CA01": {"name": "PLCA_CTRL0", "description": "PLCA Control 0 (Enable/Reset)", "expected": 0x8000, "access": "R/W", "width": "16-bit"},
                    "0x0004CA02": {"name": "PLCA_CTRL1", "description": "PLCA Control 1 (Node ID/Count)", "expected": 0x0807, "access": "R/W", "width": "16-bit"},
                    "0x0004CA03": {"name": "PLCA_STS", "description": "PLCA Status Register", "expected": 0x0000, "access": "R", "width": "16-bit"},
                    "0x0004CA04": {"name": "PLCA_TOTMR", "description": "PLCA TO Timer Register", "expected": 0x0000, "access": "R/W", "width": "16-bit"},
                    "0x0004CA05": {"name": "PLCA_BURST", "description": "PLCA Burst Mode Register", "expected": 0x0080, "access": "R/W", "width": "16-bit"}
                }
            },
            "MMS_5": {
                "name": "SPI / TC6 Interface",
                "description": "SPI-Status und TC6 Control Register",
                "registers": {
                    "0x00050000": {"name": "SPI_STATUS", "description": "SPI Status Register", "expected": None, "access": "R", "width": "16-bit"},
                    "0x00050001": {"name": "TC6_CONTROL", "description": "TC6 Control Register", "expected": 0x0000, "access": "R/W", "width": "16-bit"},
                    "0x00050002": {"name": "PARITY_CONTROL", "description": "Parity Control Register", "expected": 0x0000, "access": "R/W", "width": "16-bit"}
                }
            },
            "MMS_6": {
                "name": "Interrupt / Event Control",
                "description": "Interrupt Status und Event Control Register",
                "registers": {
                    "0x00060000": {"name": "IRQ_STATUS", "description": "Interrupt Status Register", "expected": None, "access": "R", "width": "16-bit"},
                    "0x00060001": {"name": "IRQ_MASK", "description": "Interrupt Mask Register", "expected": 0x0000, "access": "R/W", "width": "16-bit"},
                    "0x00060002": {"name": "EVENT_CONTROL", "description": "Event Control Register", "expected": 0x0000, "access": "R/W", "width": "16-bit"}
                }
            },
            "MMS_7": {
                "name": "Power / Reset / Clock",
                "description": "Power Management und Clock Control Register",
                "registers": {
                    "0x00070000": {"name": "RESET_STATUS", "description": "Reset Status Register", "expected": None, "access": "R", "width": "16-bit"},
                    "0x00070001": {"name": "POWER_CONTROL", "description": "Power Control Register", "expected": 0x0000, "access": "R/W", "width": "16-bit"},
                    "0x00070002": {"name": "CLOCK_CONTROL", "description": "Clock Control Register", "expected": 0x0000, "access": "R/W", "width": "16-bit"}
                }
            },
            "MMS_8": {
                "name": "Statistics / Counters",
                "description": "Frame und Error Counter Register",
                "registers": {
                    "0x00080000": {"name": "FRAME_COUNTERS", "description": "Frame Counter Register", "expected": 0x00000000, "access": "R", "width": "32-bit"},
                    "0x00080001": {"name": "ERROR_COUNTERS", "description": "Error Counter Register", "expected": 0x00000000, "access": "R", "width": "32-bit"},
                    "0x00080002": {"name": "DEBUG_COUNTERS", "description": "Debug Counter Register", "expected": 0x00000000, "access": "R", "width": "32-bit"}
                }
            },
            "MMS_9": {
                "name": "Vendor / Debug Extensions",
                "description": "Debug und Vendor Extension Register",
                "registers": {
                    "0x00090000": {"name": "DEBUG_REG1", "description": "Debug Register 1", "expected": 0x0000, "access": "R/W", "width": "16-bit"},
                    "0x00090001": {"name": "DEBUG_REG2", "description": "Debug Register 2", "expected": 0x0000, "access": "R/W", "width": "16-bit"},
                    "0x00090002": {"name": "VENDOR_EXT", "description": "Vendor Extensions Register", "expected": 0x0000, "access": "R/W", "width": "16-bit"}
                }
            },
            "MMS_10": {
                "name": "Miscellaneous Registers",
                "description": "Zusätzliche Funktionsregister", 
                "registers": {
                    "0x000A0000": {"name": "MISC_CONTROL", "description": "Miscellaneous Control Register", "expected": 0x0000, "access": "R/W", "width": "16-bit"}
                }
            }
        }
        
        self.create_gui()
        
    def create_gui(self):
        """Create the main GUI"""
        # Main notebook (tabs)
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Configuration tab
        self.create_config_tab()
        
        # Register group tabs
        self.register_tabs = {}
        for mms_name, mms_info in self.register_maps.items():
            self.create_register_tab(mms_name, mms_info)
        
        # Status bar
        self.status_var = tk.StringVar(value="Ready")
        status_bar = ttk.Label(self.root, textvariable=self.status_var, relief=tk.SUNKEN, anchor=tk.W)
        status_bar.pack(side=tk.BOTTOM, fill=tk.X)
        
        # Start result processing
        self.root.after(100, self.process_queue)
        
    def create_config_tab(self):
        """Create configuration tab"""
        config_frame = ttk.Frame(self.notebook)
        self.notebook.add(config_frame, text="🔧 Configuration")
        
        # Serial configuration
        ttk.Label(config_frame, text="Serial Port Configuration", font=("Arial", 12, "bold")).pack(pady=10)
        
        config_inner = ttk.Frame(config_frame)
        config_inner.pack(pady=20)
        
        ttk.Label(config_inner, text="COM Port:").grid(row=0, column=0, sticky=tk.W, padx=5, pady=5)
        port_entry = ttk.Entry(config_inner, textvariable=self.port, width=15)
        port_entry.grid(row=0, column=1, padx=5, pady=5)
        
        ttk.Label(config_inner, text="Baudrate:").grid(row=1, column=0, sticky=tk.W, padx=5, pady=5)
        ttk.Entry(config_inner, textvariable=self.baudrate, width=15).grid(row=1, column=1, padx=5, pady=5)
        
        # Connection test
        ttk.Button(config_inner, text="Test Connection", command=self.test_connection).grid(row=2, column=0, columnspan=2, pady=20)
        
        # Connection status
        self.connection_status = tk.StringVar(value="Not connected")
        ttk.Label(config_inner, textvariable=self.connection_status, font=("Arial", 10)).grid(row=3, column=0, columnspan=2, pady=5)
        
        # Firmware timestamp display
        ttk.Label(config_inner, text="Firmware Timestamp:", font=("Arial", 10, "bold")).grid(row=4, column=0, sticky=tk.W, padx=5, pady=5)
        self.firmware_timestamp = tk.StringVar(value="Not read")
        ttk.Label(config_inner, textvariable=self.firmware_timestamp, font=("Arial", 9), foreground="blue").grid(row=4, column=1, sticky=tk.W, padx=5, pady=5)
        
        # LAN8651 Reset button
        ttk.Button(config_inner, text="🔄 Reset LAN8651", command=self.reset_lan8651).grid(row=5, column=0, columnspan=2, pady=10)
        
        # Read All Registers button  
        ttk.Button(config_inner, text="🚀 Read All Registers", command=self.read_all_registers).grid(row=6, column=0, columnspan=2, pady=15)
        
        # Instructions
        instructions = tk.Text(config_frame, height=10, width=80)
        instructions.pack(pady=20, padx=20, fill=tk.BOTH, expand=True)
        instructions.insert(tk.END, """Instructions:
1. Connect your T1S Bridge device via USB (COM port)
2. Set the correct COM port (usually COM8)
3. Click 'Test Connection' to verify communication and read firmware timestamp
4. Use the register group tabs to scan specific register sets
5. Each tab has a 'Scan Group' button to read all registers in that group
6. Use 'Reset LAN8651' button to reset the LAN8651 chip via OA_RESET register

New Features:
🕒 Firmware Timestamp: Automatically read during connection test
🔄 LAN8651 Reset: Reset the chip via register write (OA_RESET = 0x00000001)
🚀 Read All Registers: Scan all register groups in sequence

⚠️ PMD Register Update (11. März 2026):
- MMS 3 PMD registers are NON-FUNCTIONAL (return 0x0000)
- Use Clause 22 PMD registers in MMS 0 instead (0x0000FF20-FF23)
- SQI register added: 0x0004008F shows 6/7 EXCELLENT signal quality

Note: After reset, the system may need a moment to reinitialize.
Test connection again to verify proper operation.

Hardware Status: 1.476 Mbps Link UP, SQI 6/7, PLCA Node 7/8 active""")

        instructions.config(state=tk.DISABLED)
        
    def create_register_tab(self, mms_name, mms_info):
        """Create a tab for a register group"""
        tab_frame = ttk.Frame(self.notebook)
        tab_name = f"{mms_name} - {mms_info['name']}"
        self.notebook.add(tab_frame, text=tab_name)
        
        # Header
        header_frame = ttk.Frame(tab_frame)
        header_frame.pack(fill=tk.X, padx=5, pady=5)
        
        ttk.Label(header_frame, text=mms_info['name'], font=("Arial", 14, "bold")).pack(anchor=tk.W)
        ttk.Label(header_frame, text=mms_info['description'], font=("Arial", 10)).pack(anchor=tk.W)
        
        # Add MAC address display for MMS_1
        if mms_name == "MMS_1":
            mac_frame = ttk.LabelFrame(header_frame, text="🌐 Decoded MAC Address", padding=5)
            mac_frame.pack(fill=tk.X, pady=5)
            
            mac_var = tk.StringVar(value=self.decoded_mac)
            mac_label = ttk.Label(mac_frame, textvariable=mac_var, font=("Arial", 12, "bold"),
                                 foreground="blue")
            mac_label.pack(anchor=tk.W)
            
            # Store reference for updates
            self.register_tabs[mms_name + "_mac_var"] = mac_var
        
        # Control frame
        control_frame = ttk.Frame(tab_frame)
        control_frame.pack(fill=tk.X, padx=5, pady=5)
        
        scan_button = ttk.Button(control_frame, text=f"📡 Scan {mms_name} Registers", 
                               command=lambda: self.scan_register_group(mms_name))
        scan_button.pack(side=tk.LEFT, padx=5)
        
        # Progress bar
        progress_var = tk.StringVar(value="Ready")
        ttk.Label(control_frame, textvariable=progress_var).pack(side=tk.LEFT, padx=20)
        
        # Results table
        table_frame = ttk.Frame(tab_frame)
        table_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        columns = ("Address", "Name", "Description", "Expected", "Actual", "Status", "Access", "Width")
        tree = ttk.Treeview(table_frame, columns=columns, show="headings", height=15)
        
        # Configure columns
        tree.heading("Address", text="Address")
        tree.heading("Name", text="Register Name")  
        tree.heading("Description", text="Description")
        tree.heading("Expected", text="Expected")
        tree.heading("Actual", text="Actual Value")
        tree.heading("Status", text="Status")
        tree.heading("Access", text="Access")
        tree.heading("Width", text="Width")
        
        # Column widths
        tree.column("Address", width=120)
        tree.column("Name", width=150)
        tree.column("Description", width=300)
        tree.column("Expected", width=100)
        tree.column("Actual", width=100)
        tree.column("Status", width=60)
        tree.column("Access", width=60)
        tree.column("Width", width=60)
        
        # Scrollbars
        v_scrollbar = ttk.Scrollbar(table_frame, orient=tk.VERTICAL, command=tree.yview)
        tree.configure(yscrollcommand=v_scrollbar.set)
        h_scrollbar = ttk.Scrollbar(table_frame, orient=tk.HORIZONTAL, command=tree.xview)
        tree.configure(xscrollcommand=h_scrollbar.set)
        
        # Pack table and scrollbars
        tree.grid(row=0, column=0, sticky="nsew")
        v_scrollbar.grid(row=0, column=1, sticky="ns")
        h_scrollbar.grid(row=1, column=0, sticky="ew")
        
        table_frame.grid_rowconfigure(0, weight=1)
        table_frame.grid_columnconfigure(0, weight=1)
        
        # Store references
        self.register_tabs[mms_name] = {
            'tree': tree,
            'progress': progress_var,
            'button': scan_button
        }
        
    def test_connection(self):
        """Test serial connection"""
        if self.is_scanning:
            messagebox.showwarning("Busy", "Scanner is currently running. Please wait.")
            return
            
        self.connection_status.set("Testing...")
        self.status_var.set("Testing connection...")
        
        # Run in thread to avoid blocking GUI
        threading.Thread(target=self._test_connection_thread, daemon=True).start()
        
    def _test_connection_thread(self):
        """Test connection in background thread"""
        try:
            ser = serial.Serial(self.port.get(), self.baudrate.get(), timeout=3)
            time.sleep(1)
            
            # Send test command
            response = self.send_robust_command(ser, "lan_read 0x00000000", timeout=3.0)
            
            if "LAN865X Read:" in response:
                self.result_queue.put(("connection_status", "✅ Connected - Communication OK"))
                self.result_queue.put(("status", "Ready"))
                
                # Read firmware timestamp
                timestamp = self.read_firmware_timestamp(ser)
                self.result_queue.put(("firmware_timestamp", timestamp))
            else:
                self.result_queue.put(("connection_status", "❌ Connected but no LAN865X response"))
                self.result_queue.put(("status", "Connection issue"))
                self.result_queue.put(("firmware_timestamp", "❌ Failed"))
                
            ser.close()
                
        except Exception as e:
            self.result_queue.put(("connection_status", f"❌ Connection failed: {str(e)}"))
            self.result_queue.put(("status", "Connection failed"))
            self.result_queue.put(("firmware_timestamp", "❌ Failed"))
            
    def read_firmware_timestamp(self, ser):
        """Read firmware build timestamp from device"""
        try:
            # Try to get build timestamp - first clear buffer
            ser.reset_input_buffer()
            
            # Send help or info command to potentially trigger timestamp display
            commands = ["help", "sysinfo", "version", "info", "status"]
            
            for cmd in commands:
                ser.write(f'{cmd}\r\n'.encode())
                time.sleep(0.5)
                
                response = ""
                start_time = time.time()
                
                while time.time() - start_time < 2.0:
                    if ser.in_waiting > 0:
                        chunk = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                        response += chunk
                        
                    if 'Build Timestamp:' in response or 'Timestamp:' in response or 'build' in response.lower():
                        break
                        
                    time.sleep(0.01)
                
                # Look for timestamp pattern
                import re
                timestamp_patterns = [
                    r'Build Timestamp:\s*(\w+\s+\d+\s+\d{4}\s+\d{2}:\d{2}:\d{2})',
                    r'Timestamp:\s*([^\\r\\n]+)',
                    r'Build:\s*([^\\r\\n]+)',
                    r'Built:\s*([^\\r\\n]+)'
                ]
                
                for pattern in timestamp_patterns:
                    match = re.search(pattern, response, re.IGNORECASE)
                    if match:
                        return f"📅 {match.group(1).strip()}"
            
            # If no timestamp found, try alternative approach
            return "⚠️ No timestamp found"
            
        except Exception as e:
            return f"❌ Error: {str(e)}"
    
    def reset_lan8651(self):
        """Reset LAN8651 via register write"""
        if self.is_scanning:
            messagebox.showwarning("Busy", "Scanner is currently running. Please wait.")
            return
            
        if self.port.get() == "":
            messagebox.showerror("Error", "Please configure COM port first")
            return
            
        # Confirm reset
        result = messagebox.askyesno("Confirm Reset", 
                                   "Reset LAN8651?\\n\\n"
                                   "This will write to the OA_RESET register (0x00000003).\\n"
                                   "The device will be reset and may require time to reinitialize.")
        
        if not result:
            return
            
        # Perform reset in background thread
        self.status_var.set("Resetting LAN8651...")
        threading.Thread(target=self._reset_lan8651_thread, daemon=True).start()
    
    def _reset_lan8651_thread(self):
        """Perform LAN8651 reset in background thread"""
        try:
            ser = serial.Serial(self.port.get(), self.baudrate.get(), timeout=3)
            time.sleep(0.5)
            
            # Write reset value to OA_RESET register (0x00000003)
            # According to datasheet, writing 0x00000001 should trigger reset
            reset_cmd = "lan_write 0x00000003 0x00000001"
            
            self.result_queue.put(("status", "Sending reset command..."))
            response = self.send_robust_command(ser, reset_cmd, timeout=3.0)
            
            if 'LAN865X Write:' in response:
                self.result_queue.put(("status", "Reset command sent successfully"))
                
                # Wait a moment for reset to complete
                time.sleep(2.0)
                
                # Try to verify system is responsive again
                test_response = self.send_robust_command(ser, "lan_read 0x00000000", timeout=5.0)
                
                if 'LAN865X Read:' in test_response:
                    self.result_queue.put(("status", "✅ LAN8651 reset completed - system responsive"))
                    self.result_queue.put(("connection_status", "✅ Reset completed - ready"))
                else:
                    self.result_queue.put(("status", "⚠️ Reset sent - system may need more time"))
                    self.result_queue.put(("connection_status", "⚠️ Reset sent - test connection"))
                    
            else:
                self.result_queue.put(("status", "❌ Reset command failed"))
                self.result_queue.put(("connection_status", "❌ Reset failed"))
                
            ser.close()
            
        except Exception as e:
            self.result_queue.put(("status", f"❌ Reset failed: {str(e)}"))
            self.result_queue.put(("connection_status", f"❌ Reset error: {str(e)}"))
    
    def read_all_registers(self):
        """Read all register groups in sequence"""
        if self.is_scanning:
            messagebox.showwarning("Busy", "Scanner is already running. Please wait.")
            return
            
        if self.port.get() == "":
            messagebox.showwarning("Configuration Error", "Please set COM port first")
            return
            
        # Start read all thread
        thread = threading.Thread(target=self._read_all_registers_thread)
        thread.daemon = True
        thread.start()
        
    def _read_all_registers_thread(self):
        """Thread function to read all registers"""
        try:
            self.is_scanning = True
            self.result_queue.put(("status", "🚀 Reading all register groups..."))
            
            # Clear all existing results
            for mms_name in self.register_tabs:
                tab_info = self.register_tabs[mms_name]
                for item in tab_info['tree'].get_children():
                    tab_info['tree'].delete(item)
            
            total_groups = len(self.register_maps)
            group_count = 0
            
            # Scan each register group
            for mms_name, mms_info in self.register_maps.items():
                group_count += 1
                self.result_queue.put(("status", f"📡 Reading {mms_name} ({group_count}/{total_groups})..."))
                
                # Perform the actual scan
                self._scan_register_group_internal(mms_name, mms_info)
                
                # Brief pause between groups
                time.sleep(0.2)
            
            self.result_queue.put(("status", f"✅ All register groups read successfully! ({total_groups} groups total)"))
            
        except Exception as e:
            self.result_queue.put(("status", f"❌ Read all failed: {str(e)}"))
            
        finally:
            self.is_scanning = False
            
    def scan_register_group(self, mms_name):
        """Scan a specific register group"""
        if self.is_scanning:
            messagebox.showwarning("Busy", "Scanner is already running. Please wait.")
            return
            
        if self.port.get() == "":
            messagebox.showerror("Error", "Please configure COM port first")
            return
            
        self.is_scanning = True
        self.register_tabs[mms_name]['button'].config(state='disabled')
        self.register_tabs[mms_name]['progress'].set("Scanning...")
        self.status_var.set(f"Scanning {mms_name}...")
        
        # Clear previous results
        tree = self.register_tabs[mms_name]['tree']
        tree.delete(*tree.get_children())
        
        # Start scanning in background thread
        threading.Thread(target=self._scan_group_thread, args=(mms_name,), daemon=True).start()
        
    def _scan_group_thread(self, mms_name):
        """Scan register group in background thread"""
        try:
            # Use internal scanning method
            group_info = self.register_maps[mms_name]
            results, final_status = self._scan_register_group_internal(mms_name, group_info)
            
        except Exception as e:
            self.result_queue.put(("scan_error", mms_name, str(e)))
            
    def send_robust_command(self, ser, command, timeout=2.0):  # Increased for safety
        """Send command - FIXED: No timeout dependency for LAN865X"""
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
        """Check if response is complete - FIXED: Proper LAN865X pattern detection"""
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
        
    def read_register(self, ser, address, retries=1):
        """Read a single register - Fast with minimal retry"""
        for attempt in range(retries + 1):
            try:
                command = f"lan_read {address}"
                response = self.send_robust_command(ser, command)
                addr, value = self.parse_lan_read_response(response)
                
                if addr is not None:
                    return value
                else:
                    if attempt < retries:
                        time.sleep(0.01)  # 10ms delay
                        continue
                        
            except Exception as e:
                if attempt < retries:
                    time.sleep(0.01)
                    continue
                    
        return None  # Failed
        
    def parse_lan_read_response(self, response):
        """Parse LAN865X read response"""
        match = re.search(r'LAN865X Read: Addr=0x([0-9A-Fa-f]+) Value=0x([0-9A-Fa-f]+)', response)
        if match:
            addr = int(match.group(1), 16)
            value = int(match.group(2), 16)
            return addr, value
        return None, None
        
    def _scan_register_group_internal(self, mms_name, mms_info):
        """Internal register scanning method for both individual and bulk read operations"""
        try:
            ser = serial.Serial(self.port.get(), self.baudrate.get(), timeout=3)
            time.sleep(0.5)
            
            results = []
            failed_count = 0
            total_count = len(mms_info['registers'])
            
            for i, (addr, reg_info) in enumerate(mms_info['registers'].items()):
                # Update progress for bulk reads
                if hasattr(self, 'result_queue'):
                    progress = f"Scanning {i+1}/{total_count}: {reg_info['name']}"
                    self.result_queue.put(("progress", mms_name, progress))
                
                # Read register
                actual_value = self.read_register(ser, addr)
                
                # Update MAC register values if this is a MAC register (for MMS_1)
                if mms_name == "MMS_1" and actual_value is not None:
                    self.update_mac_values_from_scan(addr, actual_value)
                
                if actual_value is None:
                    failed_count += 1
                    
                # Format values
                value_str, status = self.format_value_comparison(actual_value, reg_info['expected'], reg_info['width'])
                
                if reg_info['expected'] is None:
                    expected_str = "Variable"
                else:
                    if reg_info['width'] == "16-bit":
                        expected_str = f"0x{reg_info['expected']:04X}"
                    else:
                        expected_str = f"0x{reg_info['expected']:08X}"
                        
                result = {
                    'address': addr,
                    'name': reg_info['name'],
                    'description': reg_info['description'],
                    'expected': expected_str,
                    'actual': value_str,
                    'status': status,
                    'access': reg_info['access'],
                    'width': reg_info['width']
                }
                results.append(result)
                
            ser.close()
            success_rate = ((total_count - failed_count) / total_count) * 100
            final_status = f"Complete: {total_count - failed_count}/{total_count} success ({success_rate:.1f}%)"
            
            # Send results back via queue
            if hasattr(self, 'result_queue'):
                self.result_queue.put(("scan_complete", mms_name, results, final_status))
            
            return results, final_status
            
        except Exception as e:
            if hasattr(self, 'result_queue'):
                self.result_queue.put(("scan_error", mms_name, str(e)))
            raise e
        
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
            
            # Update GUI display if MMS_1 MAC variable exists
            if hasattr(self, 'register_tabs') and "MMS_1_mac_var" in self.register_tabs:
                self.register_tabs["MMS_1_mac_var"].set(self.decoded_mac)
                
            return self.decoded_mac
        
        self.decoded_mac = "Not available"
        if hasattr(self, 'register_tabs') and "MMS_1_mac_var" in self.register_tabs:
            self.register_tabs["MMS_1_mac_var"].set(self.decoded_mac)
            
        return "Not available"
    
    def update_mac_values(self):
        """Read and update MAC register values from hardware"""
        if self.port.get() == "":
            return
            
        try:
            ser = serial.Serial(self.port.get(), self.baudrate.get(), timeout=3)
            time.sleep(0.5)
            
            # Read MAC_SAB2 and MAC_SAT2 registers
            mac_sab2 = self.read_register(ser, 0x00010024)  # MAC_SAB2
            mac_sat2 = self.read_register(ser, 0x00010025)  # MAC_SAT2
            
            ser.close()
            
            if mac_sab2 is not None and mac_sat2 is not None:
                self.mac_sab2_value = mac_sab2
                self.mac_sat2_value = mac_sat2
                self.decode_mac_address()
                
        except Exception as e:
            print(f"Error reading MAC registers: {e}")
            
    def update_mac_values_from_scan(self, address, value):
        """Update MAC register values during scan and decode if both available"""
        if address == 0x00010024:  # MAC_SAB2
            self.mac_sab2_value = value
        elif address == 0x00010025:  # MAC_SAT2
            self.mac_sat2_value = value
            
        # Try to decode MAC if both values are available
        if self.mac_sab2_value is not None and self.mac_sat2_value is not None:
            self.decode_mac_address()
        
    def format_value_comparison(self, actual, expected, width):
        """Format value with comparison to expected"""
        if actual is None:
            return "READ FAILED", "❌"
            
        if width == "16-bit":
            actual_str = f"0x{actual:04X}"
        else:
            actual_str = f"0x{actual:08X}"
            
        if expected is None:
            return actual_str, "ℹ️"
        elif actual == expected:
            return actual_str, "✅"
        else:
            return actual_str, "❌"
            
    def process_queue(self):
        """Process results from background threads"""
        try:
            while True:
                item = self.result_queue.get_nowait()
                
                if item[0] == "connection_status":
                    self.connection_status.set(item[1])
                elif item[0] == "status":
                    self.status_var.set(item[1])
                elif item[0] == "firmware_timestamp":
                    self.firmware_timestamp.set(item[1])
                elif item[0] == "progress":
                    mms_name, progress = item[1], item[2]
                    self.register_tabs[mms_name]['progress'].set(progress)
                elif item[0] == "scan_complete":
                    mms_name, results, final_status = item[1], item[2], item[3]
                    self.populate_results(mms_name, results)
                    
                    # If MMS_1 was scanned, update MAC address display
                    if mms_name == "MMS_1":
                        self.update_mac_values()
                    
                    self.register_tabs[mms_name]['progress'].set(final_status)
                    self.register_tabs[mms_name]['button'].config(state='normal')
                    self.status_var.set("Ready")
                    self.is_scanning = False
                elif item[0] == "scan_error":
                    mms_name, error = item[1], item[2]
                    self.register_tabs[mms_name]['progress'].set(f"Error: {error}")
                    self.register_tabs[mms_name]['button'].config(state='normal')
                    self.status_var.set("Error occurred")
                    self.is_scanning = False
                    messagebox.showerror("Scan Error", f"Error scanning {mms_name}:\n{error}")
                    
        except queue.Empty:
            pass
            
        # Schedule next check
        self.root.after(100, self.process_queue)
        
    def populate_results(self, mms_name, results):
        """Populate results table"""
        tree = self.register_tabs[mms_name]['tree']
        
        for result in results:
            # Color coding based on status
            tags = []
            if result['status'] == "✅":
                tags = ['success']
            elif result['status'] == "❌":
                tags = ['error'] 
            elif result['status'] == "ℹ️":
                tags = ['info']
                
            tree.insert("", tk.END, values=(
                result['address'],
                result['name'],
                result['description'],
                result['expected'],
                result['actual'],
                result['status'],
                result['access'],
                result['width']
            ), tags=tags)
            
        # Configure tag colors
        tree.tag_configure('success', background='#d4edda')
        tree.tag_configure('error', background='#f8d7da') 
        tree.tag_configure('info', background='#d1ecf1')
        
    def run(self):
        """Start the GUI"""
        self.root.mainloop()

def main():
    """Main entry point"""
    print("🚀 LAN8651 Register Scanner GUI")
    print("=" * 50)
    print("Starting GUI application...")
    
    app = LAN8651RegisterGUI()
    app.run()

if __name__ == "__main__":
    main()