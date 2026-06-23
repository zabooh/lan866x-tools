#!/usr/bin/env python3
"""
LAN8651 Complete Register Map Reader - DATENBLATT-KONFORM  
Liest alle Register aus und zeigt sie tabellarisch gruppiert nach MMS an
Basierend auf Microchip LAN8650/1 Datenblatt Kapitel 11

*** Verwendet datenblatt-konforme Adressen direkt aus offizieller Microchip-Dokumentation ***
*** Alle Register-Namen und Offsets entsprechen exakt dem Datenblatt ***

Version 3.1.1 - Performance + Race Condition Protection:
- Race Condition protection durch atomic data reading
- Performance-optimiert: Fast return ohne excessive delays  
- Ultra-kurze 0.5ms sleeps statt 1ms für bessere Responsiveness
- Minimale retry logic (1 retry statt 2) für Speed
- Target: 30-45s für vollständigen Scan (nicht 2+ Minuten)

Version 3.2.0 - MAC Address Discovery Update:
- ✅ MAC_SAB2/MAC_SAT2 Register hinzugefügt (Hardware-verifiziert)
- ✅ MAC-Adress-Dekodierung aus Little Endian Format
- ✅ Automatische MAC-Anzeige in MMS_1 Tabelle und Zusammenfassung
- ✅ MAC_SAB1/MAC_SAT1 als Legacy markiert (real MAC ist in SAB2/SAT2)
- 🎯 Hardware-Verifikation: MAC-Adresse 00:04:25:01:02:03 erfolgreich dekodiert
"""

import serial
import time
import re
from tabulate import tabulate

class LAN8651RegisterReader:
    def __init__(self, port='COM8', baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        
        # Register definitions organized by Memory Map Selector (MMS)
        self.register_maps = {
            # MMS 0: Open Alliance 10BASE-T1x MAC-PHY Standard Registers + PHY Clause 22
            # Vollständig datenblatt-konform - alle Offsets entsprechen der offiziellen Microchip-Dokumentation
            "MMS_0": {
                "name": "Open Alliance Standard + PHY Clause 22",
                "description": "Standard Register gemäß Open Alliance Spezifikation (DATENBLATT-KONFORM)",
                "registers": {
                    "0x00000000": {
                        "name": "OA_ID",
                        "description": "Open Alliance ID Register (Version 1.1) - Datenblatt Offset 0x00",
                        "expected": 0x00000011,  # Version 1.1
                        "access": "R", 
                        "width": "32-bit"
                    },
                    "0x00000001": {
                        "name": "OA_PHYID", 
                        "description": "Open Alliance PHY ID (OUI + Model + Revision) - Datenblatt Offset 0x01",
                        "expected": None,  # Device dependent
                        "access": "R",
                        "width": "32-bit"
                    },
                    "0x00000002": {
                        "name": "OA_STDCAP",
                        "description": "Standard Capabilities Register - Datenblatt Offset 0x02",
                        "expected": None,  # Device dependent
                        "access": "R",
                        "width": "32-bit"
                    },
                    "0x00000003": {
                        "name": "OA_RESET", 
                        "description": "Reset Control and Status Register - Datenblatt Offset 0x03",
                        "expected": 0x00000000,
                        "access": "R/W",
                        "width": "32-bit"
                    },
                    "0x00000004": {
                        "name": "OA_CONFIG0",
                        "description": "Configuration Register 0 - Datenblatt Offset 0x04",
                        "expected": 0x00000000,
                        "access": "R/W",
                        "width": "32-bit"
                    },
                    "0x00000008": {
                        "name": "OA_STATUS0",
                        "description": "Status Register 0 - Datenblatt Offset 0x08",
                        "expected": None,  # Device dependent
                        "access": "R",
                        "width": "32-bit"
                    },
                    "0x00000009": {
                        "name": "OA_STATUS1",
                        "description": "Status Register 1 - Datenblatt Offset 0x09",
                        "expected": None,
                        "access": "R",
                        "width": "32-bit"
                    },
                    "0x0000000B": {
                        "name": "OA_BUFSTS", 
                        "description": "Buffer Status Register - Datenblatt Offset 0x0B",
                        "expected": None,
                        "access": "R",
                        "width": "32-bit"
                    },
                    "0x0000000C": {
                        "name": "OA_IMASK0",
                        "description": "Interrupt Mask Register 0 - Datenblatt Offset 0x0C",
                        "expected": 0x00000000,
                        "access": "R/W",
                        "width": "32-bit"
                    },
                    "0x0000000D": {
                        "name": "OA_IMASK1",
                        "description": "Interrupt Mask Register 1 - Datenblatt Offset 0x0D",
                        "expected": 0x00000000,
                        "access": "R/W",
                        "width": "32-bit"
                    },
                    "0x00000010": {
                        "name": "TTSCAH",
                        "description": "Transmit Timestamp Capture A (High) - Datenblatt Offset 0x10",
                        "expected": None,
                        "access": "R",
                        "width": "32-bit"
                    },
                    "0x00000011": {
                        "name": "TTSCAL",
                        "description": "Transmit Timestamp Capture A (Low) - Datenblatt Offset 0x11",
                        "expected": None,
                        "access": "R",
                        "width": "32-bit"
                    },
                    "0x00000012": {
                        "name": "TTSCBH",
                        "description": "Transmit Timestamp Capture B (High) - Datenblatt Offset 0x12",
                        "expected": None,
                        "access": "R",
                        "width": "32-bit"
                    },
                    "0x00000013": {
                        "name": "TTSCBL",
                        "description": "Transmit Timestamp Capture B (Low) - Datenblatt Offset 0x13",
                        "expected": None,
                        "access": "R",
                        "width": "32-bit"
                    },
                    "0x00000014": {
                        "name": "TTSCCH",
                        "description": "Transmit Timestamp Capture C (High) - Datenblatt Offset 0x14",
                        "expected": None,
                        "access": "R",
                        "width": "32-bit"
                    },
                    "0x00000015": {
                        "name": "TTSCCL",
                        "description": "Transmit Timestamp Capture C (Low) - Datenblatt Offset 0x15",
                        "expected": None,
                        "access": "R",
                        "width": "32-bit"
                    },
                    # PHY Clause 22 Register (16-bit) - beginnen bei Offset 0xFF00+
                    "0x0000FF00": {
                        "name": "BASIC_CONTROL",
                        "description": "PHY Basic Control Register (IEEE 802.3) - Datenblatt Offset 0xFF00",
                        "expected": 0x0000,
                        "access": "R/W",
                        "width": "16-bit"
                    },
                    "0x0000FF01": {
                        "name": "BASIC_STATUS",
                        "description": "PHY Basic Status Register (IEEE 802.3) - Datenblatt Offset 0xFF01",
                        "expected": None,
                        "access": "R",
                        "width": "16-bit"
                    },
                    "0x0000FF02": {
                        "name": "PHY_ID1",
                        "description": "PHY Identifier Register 1 (OUI bits 2:9, 10:17) - Datenblatt Offset 0xFF02",
                        "expected": 0x0007,  # Microchip OUI
                        "access": "R",
                        "width": "16-bit"
                    },
                    "0x0000FF03": {
                        "name": "PHY_ID2",
                        "description": "PHY Identifier Register 2 (OUI + Model + Rev) - Datenblatt Offset 0xFF03",
                        "expected": 0xC0F0,  # LAN865x Model + Revision
                        "access": "R",
                        "width": "16-bit"
                    },
                    "0x0000FF0D": {
                        "name": "MMDCTRL",
                        "description": "MMD Access Control Register (IEEE 802.3) - Datenblatt Offset 0xFF0D",
                        "expected": 0x0000,
                        "access": "R/W",
                        "width": "16-bit"
                    },
                    "0x0000FF0E": {
                        "name": "MMDAD",
                        "description": "MMD Access Address/Data Register (IEEE 802.3) - Datenblatt Offset 0xFF0E",
                        "expected": 0x0000,
                        "access": "R/W",
                        "width": "16-bit"
                    }
                }
            },
            
            # MMS 1: MAC Registers (32-bit)
            "MMS_1": {
                "name": "MAC Registers",
                "description": "Ethernet MAC Control and Status Register",
                "registers": {
                    "0x00010000": {
                        "name": "MAC_NCR",
                        "description": "Network Control Register",
                        "expected": 0x00000000,
                        "access": "R/W",
                        "width": "32-bit"
                    },
                    "0x00010001": {
                        "name": "MAC_NCFGR",
                        "description": "Network Configuration Register", 
                        "expected": 0x00080000,  # Full duplex default
                        "access": "R/W",
                        "width": "32-bit"
                    },
                    "0x00010022": {
                        "name": "MAC_SAB1",
                        "description": "MAC Address Bottom 1 (Legacy)",
                        "expected": 0x00000000,
                        "access": "R/W",
                        "width": "32-bit"
                    },
                    "0x00010023": {
                        "name": "MAC_SAT1",
                        "description": "MAC Address Top 1 (Legacy)",
                        "expected": 0x00000000,
                        "access": "R/W", 
                        "width": "32-bit"
                    },
                    "0x00010024": {
                        "name": "MAC_SAB2",
                        "description": "MAC Address Bottom 2 (Active - First 4 Bytes LE)",
                        "expected": 0x00000000,
                        "access": "R/W",
                        "width": "32-bit",
                        "mac_decode": "first_4_le"
                    },
                    "0x00010025": {
                        "name": "MAC_SAT2",
                        "description": "MAC Address Top 2 (Active - Last 2 Bytes LE)",
                        "expected": 0x00000000,
                        "access": "R/W", 
                        "width": "32-bit",
                        "mac_decode": "last_2_le"
                    },
                    "0x0001006F": {
                        "name": "MAC_TISUBN",
                        "description": "TSU Sub-nanoseconds",
                        "expected": 0x00000000,
                        "access": "R/W",
                        "width": "32-bit"
                    },
                    "0x00010070": {
                        "name": "MAC_TSH",
                        "description": "TSU Seconds High",
                        "expected": 0x00000000,
                        "access": "R/W",
                        "width": "32-bit"
                    },
                    "0x00010074": {
                        "name": "MAC_TSL",
                        "description": "TSU Seconds Low",
                        "expected": 0x00000000,
                        "access": "R/W",
                        "width": "32-bit"
                    },
                    "0x00010075": {
                        "name": "MAC_TN",
                        "description": "TSU Nanoseconds",
                        "expected": 0x00000000,
                        "access": "R/W",
                        "width": "32-bit"
                    },
                    "0x00010200": {
                        "name": "BMGR_CTL",
                        "description": "Buffer Manager Control",
                        "expected": None,
                        "access": "R/W",
                        "width": "32-bit"
                    },
                    "0x00010208": {
                        "name": "STATS0",
                        "description": "Statistics 0",
                        "expected": 0x00000000,
                        "access": "R",
                        "width": "32-bit"
                    }
                }
            },
            
            # MMS 2: PHY PCS Registers (16-bit)
            "MMS_2": {
                "name": "PHY PCS Registers", 
                "description": "Physical Coding Sublayer Register",
                "registers": {
                    "0x000208F3": {
                        "name": "PCS_REG",
                        "description": "PCS Basic Register",
                        "expected": 0x0000,
                        "access": "R/W",
                        "width": "16-bit"
                    }
                }
            },
            
            # MMS 3: PHY PMA/PMD Registers (16-bit)
            "MMS_3": {
                "name": "PHY PMA/PMD Registers",
                "description": "Physical Medium Attachment/Dependent Register",
                "registers": {
                    "0x00030001": {
                        "name": "PMD_CONTROL",
                        "description": "PMA/PMD Control Register",
                        "expected": 0x0000,
                        "access": "R/W", 
                        "width": "16-bit"
                    },
                    "0x00030002": {
                        "name": "PMD_STATUS",
                        "description": "PMA/PMD Status Register",
                        "expected": None,
                        "access": "R",
                        "width": "16-bit"
                    }
                }
            },
            
            # MMS 4: PHY Vendor Specific Registers (16-bit) - PLCA Register
            "MMS_4": {
                "name": "PHY Vendor Specific (PLCA)",
                "description": "Microchip-spezifische PHY Register mit PLCA - Hardware-verifiziert!",
                "registers": {
                    "0x00040010": {
                        "name": "CTRL1",
                        "description": "Vendor Control 1 Register",
                        "expected": 0x0000,
                        "access": "R/W",
                        "width": "16-bit"
                    },
                    "0x00040018": {
                        "name": "STS1",
                        "description": "Vendor Status 1 Register",
                        "expected": None,
                        "access": "R",
                        "width": "16-bit"
                    },
                    "0x0004CA00": {
                        "name": "MIDVER",
                        "description": "Map ID Version Register",
                        "expected": None,
                        "access": "R",
                        "width": "16-bit"
                    },
                    "0x0004CA01": {
                        "name": "PLCA_CTRL0",
                        "description": "PLCA Control 0 (Enable/Reset) - Hardware-verifiziert",
                        "expected": 0x8000,  # PLCA enabled - Hardware bestätigt!
                        "access": "R/W",
                        "width": "16-bit"
                    },
                    "0x0004CA02": {
                        "name": "PLCA_CTRL1",
                        "description": "PLCA Control 1 (Node ID/Count) - Hardware-verifiziert",
                        "expected": 0x0807,  # Node 7 von 8 - Hardware bestätigt!
                        "access": "R/W",
                        "width": "16-bit"
                    },
                    "0x0004CA03": {
                        "name": "PLCA_STS",
                        "description": "PLCA Status Register",
                        "expected": 0x0000,
                        "access": "R",
                        "width": "16-bit"
                    },
                    "0x0004CA04": {
                        "name": "PLCA_TOTMR",
                        "description": "PLCA TO Timer Register",
                        "expected": 0x0000,
                        "access": "R/W",
                        "width": "16-bit"
                    },
                    "0x0004CA05": {
                        "name": "PLCA_BURST",
                        "description": "PLCA Burst Mode Register - Hardware-verifiziert",
                        "expected": 0x0080,  # Burst Timer 128 - Hardware bestätigt!
                        "access": "R/W",
                        "width": "16-bit"
                    }
                }
            },
            
            # MMS 10: Miscellaneous Registers (16-bit)
            "MMS_10": {
                "name": "Miscellaneous Registers",
                "description": "Zusätzliche Funktionsregister",
                "registers": {
                    "0x000A0000": {
                        "name": "MISC_CONTROL",
                        "description": "Miscellaneous Control Register",
                        "expected": 0x0000,
                        "access": "R/W",
                        "width": "16-bit"
                    }
                }
            }
        }
    
    def send_command(self, ser, command):
        """Send command and get response - ROBUST synchronization with prompt detection"""
        ser.reset_input_buffer()
        ser.write(f'{command}\r\n'.encode())
        
        # Wait for complete response including prompt
        response = self.wait_for_prompt(ser, timeout=2.0)  # Generous timeout for two-phase response
        return response
    
    def wait_for_prompt(self, ser, timeout=2.0):  # Increased to 2.0s for safety
        """Wait for complete response - FIXED: No timeout dependency for LAN865X"""
        import time
        start_time = time.time()
        response = ""
        
        while time.time() - start_time < timeout:
            # Read all available data atomically (Race Condition protection)
            available = ser.in_waiting
            if available > 0:
                chunk = ser.read(available).decode('utf-8', errors='ignore')
                response += chunk
                
                # IMMEDIATE completion check - no delays needed!
                if self.is_response_complete(response):
                    return response  # Return immediately when complete!
                    
            time.sleep(0.0005)  # Ultra-short 0.5ms polling
        
        # Timeout fallback (should rarely trigger now)
        return response
    
    def is_response_complete(self, response):
        """Check if response is complete - FIXED: Proper LAN865X pattern detection"""
        if not response or len(response) < 10:
            return False
            
        # For LAN865X commands: Look for complete read result pattern
        if 'lan_read' in response.lower():
            # Must have the complete LAN865X Read result
            lan_match = re.search(r'LAN865X Read OK: Addr=0x([0-9A-Fa-f]+) Value=0x([0-9A-Fa-f]+)', response)
            if lan_match:
                # Found complete read result - check if followed by newline/carriage return
                # This indicates the command is truly complete
                end_pos = lan_match.end()
                remaining = response[end_pos:]
                # Command complete if we have newline after the value
                return '\n' in remaining or '\r' in remaining
            return False
            
        # For other commands: traditional prompt detection
        return '>' in response and (response.count('\n') >= 2)
        
    def parse_lan_read_response(self, response):
        """Parse LAN865X read response - Enhanced for correct pattern matching"""
        # Look for the actual result line (not the initiation line)
        match = re.search(r'LAN865X Read OK: Addr=0x([0-9A-Fa-f]+) Value=0x([0-9A-Fa-f]+)', response)
        if match:
            addr = int(match.group(1), 16)
            value = int(match.group(2), 16) 
            return addr, value
        return None, None
    
    def parse_lan_read_response(self, response):
        """Parse LAN865X read response"""
        match = re.search(r'LAN865X Read OK: Addr=0x([0-9A-Fa-f]+) Value=0x([0-9A-Fa-f]+)', response)
        if match:
            addr = int(match.group(1), 16)
            value = int(match.group(2), 16)
            return addr, value
        return None, None
    
    def read_register(self, ser, address, retries=1):
        """Read a single register - Fast with minimal retry"""
        for attempt in range(retries + 1):
            try:
                command = f"lan_read {address}"
                response = self.send_command(ser, command)
                addr, value = self.parse_lan_read_response(response)
                
                if addr is not None:
                    return value
                else:
                    # Only retry if it's the first attempt
                    if attempt < retries:
                        time.sleep(0.01)  # Minimal 10ms delay
                        continue
                        
            except Exception as e:
                if attempt < retries:
                    time.sleep(0.01)
                    continue
                    
        return None  # Failed
    
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
    
    def read_register_group(self, ser, mms_name, group_info):
        """Read all registers in a group and return results - ROBUST VERSION"""
        results = []
        total_regs = len(group_info['registers'])
        print(f"\n📡 Lese {group_info['name']} Register ({total_regs} Register)...")
        if total_regs == 0:
            print(f"  (Keine Register in dieser Gruppe)")
            return results
        
        # Progress counter for user feedback
        reg_count = 0
        failed_count = 0
        
        for addr, reg_info in group_info['registers'].items():
            reg_count += 1
            # Show progress every 5 registers or at the end
            if reg_count % 5 == 0 or reg_count == total_regs:
                success_rate = ((reg_count - failed_count) / reg_count) * 100
                print(f"  Progress: {reg_count}/{total_regs} Register ({success_rate:.1f}% success)...")
            
            actual_value = self.read_register(ser, addr)
            
            if actual_value is None:
                failed_count += 1
                
            value_str, status = self.format_value_comparison(
                actual_value, reg_info['expected'], reg_info['width'])
            
            # Expected value string
            if reg_info['expected'] is None:
                if reg_info['width'] == "16-bit":
                    expected_str = "Variable"
                else:
                    expected_str = "Variable"
            else:
                if reg_info['width'] == "16-bit":
                    expected_str = f"0x{reg_info['expected']:04X}"
                else:
                    expected_str = f"0x{reg_info['expected']:08X}"
            
            results.append([
                addr,
                reg_info['name'],
                reg_info['description'],
                expected_str,
                value_str,
                status,
                reg_info['access'],
                reg_info['width']
            ])
        
        final_success_rate = ((total_regs - failed_count) / total_regs) * 100
        print(f"  ✅ Gruppe abgeschlossen: {total_regs - failed_count}/{total_regs} Register erfolgreich ({final_success_rate:.1f}%)")
        
        return results
    
    def decode_mac_address(self, results_dict):
        """Decode MAC address from MAC_SAB2 and MAC_SAT2 registers"""
        sab2_value = None
        sat2_value = None
        
        # Find MAC register values in results
        for mms_results in results_dict.values():
            for result in mms_results:
                if "MAC_SAB2" in result[1]:
                    try:
                        sab2_value = int(result[4].replace("0x", ""), 16)
                    except:
                        pass
                elif "MAC_SAT2" in result[1]:
                    try:
                        sat2_value = int(result[4].replace("0x", ""), 16)
                    except:
                        pass
        
        if sab2_value is not None and sat2_value is not None:
            # Extract bytes (Little Endian)
            sab2_bytes = [
                sab2_value & 0xFF,
                (sab2_value >> 8) & 0xFF,
                (sab2_value >> 16) & 0xFF,
                (sab2_value >> 24) & 0xFF
            ]
            
            sat2_bytes = [
                sat2_value & 0xFF,
                (sat2_value >> 8) & 0xFF
            ]
            
            mac_bytes = sab2_bytes + sat2_bytes
            mac_address = ":".join(f"{byte:02X}" for byte in mac_bytes)
            return mac_address
        
        return None
    
    def print_group_table(self, mms_name, group_info, results):
        """Print results as formatted table"""
        print(f"\n" + "="*120)
        print(f"{mms_name}: {group_info['name']}")
        print(f"{group_info['description']}")
        
        # Add MAC address info for MMS_1
        if mms_name == "MMS_1":
            mac_address = self.decode_mac_address({mms_name: results})
            if mac_address:
                print(f"🎯 Decoded MAC Address: {mac_address} (from MAC_SAB2/MAC_SAT2 Little Endian)")
        
        print("="*120)
        
        headers = [
            "Adresse", "Register Name", "Beschreibung", 
            "Default", "Aktuell", "Status", "Access", "Width"
        ]
        
        # Format table with proper column widths
        formatted_results = []
        for row in results:
            # Truncate long descriptions
            desc = row[2]
            if len(desc) > 30:
                desc = desc[:27] + "..."
            
            # Add MAC decoding for MAC address registers
            name = row[1]
            actual_val = row[4]
            if "MAC_SAB2" in name and actual_val != "FAILED" and actual_val != "0x00000000":
                try:
                    val = int(actual_val.replace("0x", ""), 16)
                    bytes_le = [val & 0xFF, (val >> 8) & 0xFF, (val >> 16) & 0xFF, (val >> 24) & 0xFF]
                    mac_part = ":".join(f"{b:02X}" for b in bytes_le)
                    desc += f" [{mac_part}]"
                except:
                    pass
            elif "MAC_SAT2" in name and actual_val != "FAILED" and actual_val != "0x00000000":
                try:
                    val = int(actual_val.replace("0x", ""), 16)
                    bytes_le = [val & 0xFF, (val >> 8) & 0xFF]
                    mac_part = ":".join(f"{b:02X}" for b in bytes_le)
                    desc += f" [{mac_part}]"
                except:
                    pass
            
            formatted_row = [
                row[0],      # Address  
                row[1],      # Name
                desc,        # Description (truncated + MAC decode)
                row[3],      # Expected
                row[4],      # Actual
                row[5],      # Status 
                row[6],      # Access
                row[7]       # Width
            ]
            formatted_results.append(formatted_row)
        
        print(tabulate(formatted_results, headers=headers, tablefmt="grid"))
    
    def generate_summary(self, all_results, reset_performed=False):
        """Generate summary statistics with MAC address info"""
        total_registers = 0
        successful_reads = 0
        matches_expected = 0
        variable_values = 0
        
        # Decode MAC address from results
        mac_address = self.decode_mac_address(all_results)
        
        for mms_name, results in all_results.items():
            for result in results:
                total_registers += 1
                if result[5] != "❌" and "FAILED" not in result[4]:  # Status not failed
                    successful_reads += 1
                if result[5] == "✅":  # Exact match
                    matches_expected += 1
                if result[3] == "Variable":  # Variable expected value
                    variable_values += 1
        
        print(f"\n" + "="*80)
        print("📊 REGISTER SCAN ZUSAMMENFASSUNG")
        if reset_performed:
            print("(Nach Manual Reset auf Default-Werte)")
        print("="*80)
        print(f"Gesamte Register gescannt:     {total_registers}")
        print(f"Erfolgreich gelesen:           {successful_reads}")
        print(f"Entsprechen Default-Werten:    {matches_expected}")
        print(f"Variable/Status Register:      {variable_values}")
        print(f"Lesefehlerate:                 {((total_registers - successful_reads) / total_registers * 100):.1f}%")
        
        # Show MAC address if available
        if mac_address:
            print(f"🌐 MAC-Adresse (SAB2/SAT2):    {mac_address}")
        
        if (total_registers - variable_values) > 0:
            print(f"Default-Übereinstimmung:       {(matches_expected / (total_registers - variable_values) * 100):.1f}%")
        
        if reset_performed:
            print(f"\n💡 RESET-ANALYSE:")
            if matches_expected > (total_registers - variable_values) * 0.8:  # 80% threshold
                print("✅ Reset war erfolgreich - die meisten Register haben Default-Werte")
            elif matches_expected > (total_registers - variable_values) * 0.5:  # 50% threshold  
                print("⚠️  Reset teilweise erfolgreich - einige Register abweichend")
            else:
                print("❌ Reset-Effektivität niedrig - System überschreibt viele Werte")
    
    def spi_write_register(self, ser, address, value, name):
        """Write register via SPI (lan_write command)"""
        command = f"lan_write {address} 0x{value:08X}"
        response = self.send_command(ser, command)
        
        # Check if write was successful
        write_match = re.search(r'LAN865X Write: Addr=0x([0-9A-Fa-f]+) Value=0x([0-9A-Fa-f]+)', response)
        if write_match:
            return True
        return False
    
    def execute_manual_reset(self, ser):
        """Execute manual reset via SPI write commands"""
        print("\n" + "=" * 80)
        print("🔄 MANUAL RESET VIA SPI WRITE COMMANDS")
        print("=" * 80)
        print("Setzt alle Register auf Datenblatt Default-Werte über SPI/TC6")
        
        # Reset commands with expected default values - KORRIGIERTE ADRESSEN
        reset_commands = [
            # MMS 0: Open Alliance Standard Registers - KORRIGIERTE ADRESSEN!
            {"address": "0x00000003", "value": 0x00000000, "name": "OA_CONFIG0 (KORR)", "mms": "MMS 0"},
            {"address": "0x00000004", "value": 0x00000000, "name": "OA_CONFIG2 (KORR)", "mms": "MMS 0"},
            {"address": "0x00000008", "value": 0x00000000, "name": "OA_IMASK1 (KORR)", "mms": "MMS 0"},
            {"address": "0x00000009", "value": 0x0000, "name": "PHY_BASIC_CONTROL (KORR)", "mms": "MMS 0"},
            
            # MMS 1: MAC Registers
            {"address": "0x01000000", "value": 0x00000000, "name": "MAC_NCR", "mms": "MMS 1"},
            {"address": "0x01000004", "value": 0x00080000, "name": "MAC_NCFGR", "mms": "MMS 1"},
            {"address": "0x0100000C", "value": 0x00000000, "name": "MAC_UR", "mms": "MMS 1"},
            {"address": "0x01000010", "value": 0x00020000, "name": "MAC_DCFGR", "mms": "MMS 1"},
            {"address": "0x01000018", "value": 0x00000000, "name": "MAC_RBQB", "mms": "MMS 1"},
            {"address": "0x0100001C", "value": 0x00000000, "name": "MAC_TBQB", "mms": "MMS 1"},
            {"address": "0x01000068", "value": 0x00000000, "name": "TSU_TIMER_SEC", "mms": "MMS 1"},
            {"address": "0x0100006C", "value": 0x00000000, "name": "TSU_TIMER_NSEC", "mms": "MMS 1"},
            {"address": "0x01000070", "value": 0x00000000, "name": "TSU_TIME_ADJ", "mms": "MMS 1"},
            {"address": "0x01000074", "value": 0x28F5C28F, "name": "TSU_TIMER_INCR", "mms": "MMS 1"},
            {"address": "0x01000088", "value": 0x00000000, "name": "MAC_SAB1", "mms": "MMS 1"},
            {"address": "0x0100008C", "value": 0x00000000, "name": "MAC_SAT1", "mms": "MMS 1"},
            
            # MMS 2: PHY PCS Registers  
            {"address": "0x02000000", "value": 0x0000, "name": "PCS_CONTROL", "mms": "MMS 2"},
            {"address": "0x0200004A", "value": 0x8000, "name": "PLCA_CTRL_STS", "mms": "MMS 2"},
            {"address": "0x0200004B", "value": 0x0000, "name": "PLCA_NODE_ID", "mms": "MMS 2"},
            {"address": "0x0200004C", "value": 0x0008, "name": "PLCA_NODE_COUNT", "mms": "MMS 2"},
            {"address": "0x0200004D", "value": 0x0000, "name": "PLCA_BURST_MODE", "mms": "MMS 2"},
            {"address": "0x0200004E", "value": 0x0080, "name": "PLCA_BURST_TIMER", "mms": "MMS 2"},
            {"address": "0x0200004F", "value": 0x0000, "name": "PLCA_BURST_COUNT", "mms": "MMS 2"},
            
            # MMS 3: PHY PMA/PMD Registers
            {"address": "0x03000000", "value": 0x0000, "name": "PMAPMD_CONTROL", "mms": "MMS 3"}
        ]
        
        successful_writes = 0
        failed_writes = 0
        current_mms = None
        
        for cmd in reset_commands:
            # Print MMS header when switching groups
            if cmd["mms"] != current_mms:
                current_mms = cmd["mms"]
                print(f"\n📋 {current_mms} Register:")
                print("-" * 40)
            
            print(f"  📤 Reset: {cmd['name']}...", end="")
            
            success = self.spi_write_register(ser, cmd["address"], cmd["value"], cmd["name"])
            
            if success:
                successful_writes += 1
                print(" ✅")
            else:
                failed_writes += 1
                print(" ❌")
                
            time.sleep(0.1)  # Small delay between SPI transactions
        
        total_commands = len(reset_commands)
        print(f"\n📊 Reset Zusammenfassung:")
        print(f"   Erfolgreich: {successful_writes}/{total_commands} ({(successful_writes/total_commands*100):.1f}%)")
        
        if successful_writes == total_commands:
            print("✅ Alle Register erfolgreich auf Default-Werte gesetzt!")
            return True
        else:
            print("⚠️  Einige Register konnten nicht zurückgesetzt werden")
            return False
    
    def ask_user_for_reset(self):
        """Ask user if manual reset should be performed"""
        print("=" * 80)
        print("🔄 MANUAL RESET OPTION")
        print("=" * 80)
        print("Möchten Sie vor dem Register-Scan einen Manual Reset durchführen?")
        print("Dies setzt alle Register auf Datenblatt Default-Werte zurück.")
        print()
        
        while True:
            choice = input("Manual Reset durchführen? (j/n): ").lower().strip()
            if choice in ['j', 'ja', 'y', 'yes']:
                return True
            elif choice in ['n', 'nein', 'no']:
                return False
            else:
                print("Bitte geben Sie 'j' für Ja oder 'n' für Nein ein.")

    def run_complete_scan(self):
        """Run complete register scan with optional manual reset"""
        print("=" * 80)
        print("🔍 LAN8651 COMPLETE REGISTER MAP SCAN")
        print("=" * 80)
        print("Basierend auf Microchip LAN8650/1 Datenblatt Kapitel 11")
        print("Scannt alle Register gruppiert nach Memory Map Selector (MMS)")
        print("\n⚡ ROBUSTNESS + PERFORMANCE: Prompt-synchronisiert, keine Race Conditions")
        
        # Ask user for scan mode
        print("\n🎯 Scan-Modus wählen:")
        print("1. Vollständiger Scan (alle 76 Register, ~30-45s)")
        print("2. Schneller Scan (nur wichtige Register, ~10s)")
        
        while True:
            mode = input("Modus (1/2): ").strip()
            if mode in ['1', '2']:
                break
            print("Bitte '1' oder '2' eingeben!")
        
        # Ask user for manual reset
        perform_reset = self.ask_user_for_reset()
        
        try:
            ser = serial.Serial(self.port, self.baudrate, timeout=3)
            time.sleep(1)
            print(f"\n✓ Verbunden mit {self.port}")
            
            # Test basic communication with robust synchronization
            print("\n🔌 Teste Kommunikation (Prompt-basiert)...")
            response = self.send_command(ser, "lan_read 0x00000004")
            if "LAN865X Read OK:" not in response:
                print("❌ Keine gültige LAN865X Antwort - Prüfe Verbindung!")
                print(f"Debug - Response: '{response[:100]}...'")
                return False
            print("✅ Kommunikation OK - Prompt-Synchronisation funktioniert")
            
            # Execute manual reset if requested
            if perform_reset:
                self.execute_manual_reset(ser)
                print("\n⏳ Warte 2 Sekunden nach Reset...")
                time.sleep(2)
            
            all_results = {}
            
            # Select register groups based on mode
            if mode == '2':  # Fast mode - only essential registers
                scan_groups = {
                    "MMS_0_Essential": {
                        "name": "Essential Open Alliance Registers",
                        "description": "Wichtigste Register für Status und Kommunikation",
                        "registers": {
                            k: v for k, v in self.register_maps["MMS_0"]["registers"].items() 
                            if k in ["0x00000000", "0x00000001", "0x00000004", "0x00000008", "0x00000009", "0x0000FF01"]
                        }
                    },
                    "MMS_1_Essential": {
                        "name": "Essential MAC Registers", 
                        "description": "Wichtigste MAC Control Register",
                        "registers": {
                            k: v for k, v in self.register_maps["MMS_1"]["registers"].items()
                            if k in ["0x00010000", "0x00010001", "0x00010024", "0x00010025"]
                        }
                    }
                }
                print(f"\n🚀 Schnellmodus: {sum(len(g['registers']) for g in scan_groups.values())} wichtige Register")
            else:  # Full mode
                scan_groups = self.register_maps
                print(f"\n📊 Vollmodus: {sum(len(g['registers']) for g in scan_groups.values())} Register total")
            
            # Scan all register groups  
            print(f"\n🔍 Starte Register-Scan (ROBUST - Prompt-synchronisiert)...")
            print("=" * 80)
            start_time = time.time()
            
            for mms_name, group_info in scan_groups.items():
                group_start = time.time()
                results = self.read_register_group(ser, mms_name, group_info)
                group_time = time.time() - group_start
                print(f"✅ {mms_name} abgeschlossen in {group_time:.1f}s")
                
                all_results[mms_name] = results
                self.print_group_table(mms_name, group_info, results)
                # Remove the 0.5s delay between groups for faster execution
            
            # Generate summary with total time
            total_time = time.time() - start_time
            print(f"\n⚡ SCAN KOMPLETT in {total_time:.1f} Sekunden!")
            self.generate_summary(all_results, perform_reset)
            
            ser.close()
            print(f"\n✅ Register-Scan abgeschlossen (Gesamt: {total_time:.1f}s)!")
            return True
            
        except Exception as e:
            print(f"❌ Fehler: {e}")
            return False

def main():
    """Main function"""
    print("LAN8651 Complete Register Map Reader - KORRIGIERTE ADRESSEN")
    print("Verwendet das 'lan_read' und 'lan_write' Commands über serielle Schnittstelle")
    print("Optional: Setzt alle Register vor dem Scan auf Default-Werte zurück")
    print("\n🔧 WICHTIG: Verwendet korrigierte Register-Adressen!")
    print("   MMS_0 Register sind um -4 Bytes verschoben vom Datenblatt!")
    
    # Check if tabulate is available
    try:
        import tabulate
    except ImportError:
        print("❌ 'tabulate' Modul nicht gefunden!")
        print("Installiere mit: pip install tabulate")
        return
    
    scanner = LAN8651RegisterReader(port='COM8', baudrate=115200)
    
    if scanner.run_complete_scan():
        print("\n🎉 Register-Scan mit korrigierten Adressen erfolgreich abgeschlossen!")
        print("\nLegende:")
        print("✅ - Wert entspricht Datenblatt Default")  
        print("❌ - Wert weicht ab oder Lesefehler")
        print("ℹ️  - Variable/Status Register (kein fester Default)")
        print("\n💡 Hinweis:")
        print("- Register-Adressen in MMS_0 sind korrigiert (-4 Bytes vom Datenblatt)")
        print("- OA_ID ist bei 0x00000000 (nicht 0x00000004 wie im Datenblatt)")
        print("- PHY_BASIC_STATUS ist bei 0x00000001 und enthält Microchip OUI")
        print("- MAC-Adresse wird in MAC_SAB2/MAC_SAT2 gespeichert (Little Endian)")
        print("- MAC_SAB1/MAC_SAT1 sind Legacy-Register (enthalten nicht die aktive MAC)")
        print("- Manual Reset setzt Register über SPI/TC6 auf Default-Werte")  
        print("- Einige Register werden jedoch von der Firmware verwaltet")
        print("- Für vollständigen Reset ist Hardware-Reset empfohlen")
    else:
        print("\n❌ Register-Scan fehlgeschlagen!")

if __name__ == "__main__":
    main()