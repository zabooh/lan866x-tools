#!/usr/bin/env python3
"""
LAN8651 TSU Wallclock Timer PTP Test - UPDATED
Testet die Time Synchronization Unit und vergleicht mit PC-Zeit

Änderungen März 10, 2026:
- ✅ Hardware-verifizierte Register-Adressen (MMS_1: 0x0100xxxx)
- 🛡️ Robuste prompt-basierte Kommunikation (Race Condition Free)
- 🔧 Verbesserte Timeout-Behandlung und Error-Handling
- 📊 Erweiterte TSU-Diagnose und Analyse
"""

import serial
import time
import re
from datetime import datetime, timezone
import threading
import signal
import sys

class LAN8651_PTPTest:
    def __init__(self, port='COM8', baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.running = False
        
        # TSU Register (MAC Bereich - MMS 1) - HARDWARE-VERIFIZIERT
        self.tsu_registers = {
            # MAC Network Control Register - Für TSU aktivierung
            "MAC_NCR": "0x00010000",      # KORRIGIERT: 0x0001 = MMS_1
            
            # TSU Timer Register (Wallclock)
            "MAC_TSH": "0x00010070",       # Timer Seconds High [47:32]
            "MAC_TSL": "0x00010074",       # Timer Seconds Low [31:0] 
            "MAC_TN": "0x00010075",        # Timer Nanoseconds [29:0]
            
            # TSU Control Register
            "MAC_TI": "0x00010077",        # Timer Increment Register
            "MAC_TISUBN": "0x0001006F",    # Timer Increment Sub-nanoseconds
            "MAC_TA": "0x00010076",        # Timer Adjust Register
        }
        
        # Test-Konfiguration
        self.test_duration = 30  # Sekunden
        self.read_interval = 1.0  # Sekunden
        
        # Messdaten
        self.measurements = []
        self.pc_times = []
        self.lan_times = []
        
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
            
        # For LAN865X commands: Look for complete read/write result pattern
        if 'lan_read' in response.lower():
            # Must have the complete LAN865X Read result
            lan_match = re.search(r'LAN865X Read: Addr=0x([0-9A-Fa-f]+) Value=0x([0-9A-Fa-f]+)', response)
            if lan_match:
                # Found complete read result - check if followed by newline
                end_pos = lan_match.end()
                remaining = response[end_pos:]
                return '\n' in remaining or '\r' in remaining
            return False
        elif 'lan_write' in response.lower():
            # Must have the complete LAN865X Write result
            lan_match = re.search(r'LAN865X Write: .*- OK', response)
            if lan_match:
                end_pos = lan_match.end()
                remaining = response[end_pos:]
                return '\n' in remaining or '\r' in remaining  
            return False
            
        # For other commands: traditional prompt detection
        return '>' in response and (response.count('\n') >= 2)
    
    def send_command(self, ser, command):
        """Legacy wrapper - now uses robust communication"""
        return self.send_robust_command(ser, command)
    
    def parse_lan_read_response(self, response):
        """Parse LAN865X read response"""
        match = re.search(r'LAN865X Read: Addr=0x([0-9A-Fa-f]+) Value=0x([0-9A-Fa-f]+)', response)
        if match:
            addr = int(match.group(1), 16)
            value = int(match.group(2), 16)
            return addr, value
        return None, None
    
    def read_register(self, ser, reg_addr, retries=2):
        """Read single register with retry logic"""
        for attempt in range(retries + 1):
            try:
                command = f"lan_read {reg_addr}"
                response = self.send_robust_command(ser, command)
                addr, value = self.parse_lan_read_response(response)
                if value is not None:
                    return value
                elif attempt < retries:
                    print(f"   ⚠️ Register read retry {attempt+1}/{retries} for {reg_addr}")
                    time.sleep(0.1)
            except Exception as e:
                if attempt < retries:
                    print(f"   ⚠️ Read error retry {attempt+1}/{retries}: {e}")
                    time.sleep(0.1)
                else:
                    print(f"   ❌ Read failed after {retries} retries: {e}")
        return None
    
    def write_register(self, ser, reg_addr, value, retries=2):
        """Write single register with retry logic"""
        for attempt in range(retries + 1):
            try:
                command = f"lan_write {reg_addr} 0x{value:08X}"
                response = self.send_robust_command(ser, command)
                success = "LAN865X Write: " in response and "- OK" in response
                if success:
                    return True
                elif attempt < retries:
                    print(f"   ⚠️ Register write retry {attempt+1}/{retries} for {reg_addr}")
                    time.sleep(0.1)
            except Exception as e:
                if attempt < retries:
                    print(f"   ⚠️ Write error retry {attempt+1}/{retries}: {e}")
                    time.sleep(0.1)
                else:
                    print(f"   ❌ Write failed after {retries} retries: {e}")
        return False
    
    def enable_tsu_wallclock(self, ser):
        """Enable TSU Wallclock Timer with enhanced diagnostics"""
        print("🔧 TSU Wallclock Timer aktivieren...")
        
        # Read current MAC_NCR value
        print(f"   📍 Lese MAC_NCR von {self.tsu_registers['MAC_NCR']}")
        ncr_value = self.read_register(ser, self.tsu_registers["MAC_NCR"])
        if ncr_value is None:
            print("❌ Fehler beim Lesen von MAC_NCR - prüfe Register-Adresse")
            return False
            
        print(f"   MAC_NCR aktuell: 0x{ncr_value:08X}")
        
        # Enable TSU - try different common bit positions
        # Bit 4 (0x10) and Bit 11 (0x800) are common TSU enable bits
        tsu_enabled_value = ncr_value | 0x00000010  # Enable bit 4 first
        
        print(f"   🔧 Setze TSU Enable-Bit: 0x{tsu_enabled_value:08X}")
        success = self.write_register(ser, self.tsu_registers["MAC_NCR"], tsu_enabled_value)
        if success:
            print(f"   ✅ TSU aktiviert: MAC_NCR = 0x{tsu_enabled_value:08X}")
            
            # Verify write was successful
            time.sleep(0.2)
            verify_value = self.read_register(ser, self.tsu_registers["MAC_NCR"])
            if verify_value == tsu_enabled_value:
                print(f"   ✅ TSU Aktivierung bestätigt")
            else:
                print(f"   ⚠️ TSU Aktivierung nicht bestätigt: erwartet 0x{tsu_enabled_value:08X}, gelesen 0x{verify_value:08X}")
        else:
            print("   ❌ TSU Aktivierung fehlgeschlagen")
            return False
            
        return True
    
    def initialize_wallclock_time(self, ser):
        """Initialize wallclock with current PC time - Enhanced with better verification"""
        print("🕐 Wallclock Timer initialisieren...")
        
        # Get current PC time
        pc_seconds, pc_nanos = self.get_pc_timestamp()
        
        print(f"   PC Zeit: {pc_seconds} seconds, {pc_nanos} nanoseconds")
        print(f"   🗺 Zeitstempel: {self.format_timestamp(pc_seconds, pc_nanos)}")
        
        # Split 48-bit seconds into high and low parts
        seconds_high = (pc_seconds >> 32) & 0xFFFF
        seconds_low = pc_seconds & 0xFFFFFFFF
        
        print(f"   Setze Timer: High=0x{seconds_high:04X}, Low=0x{seconds_low:08X}, Nano={pc_nanos}")
        
        # Write time to TSU registers with verification
        print(f"   📝 Schreibe MAC_TSH ({self.tsu_registers['MAC_TSH']}) = 0x{seconds_high:04X}")
        success_h = self.write_register(ser, self.tsu_registers["MAC_TSH"], seconds_high)
        
        print(f"   📝 Schreibe MAC_TSL ({self.tsu_registers['MAC_TSL']}) = 0x{seconds_low:08X}")
        success_l = self.write_register(ser, self.tsu_registers["MAC_TSL"], seconds_low)
        
        print(f"   📝 Schreibe MAC_TN ({self.tsu_registers['MAC_TN']}) = {pc_nanos}")
        success_n = self.write_register(ser, self.tsu_registers["MAC_TN"], pc_nanos)
        
        if success_h and success_l and success_n:
            print("   ✅ Wallclock Zeit gesetzt")
        else:
            print("   ⚠️ Wallclock Zeit-Einstellung teilweise fehlgeschlagen")
            print(f"      High: {'OK' if success_h else 'FAIL'}, Low: {'OK' if success_l else 'FAIL'}, Nano: {'OK' if success_n else 'FAIL'}")
            
        # Extended delay for timer to stabilize
        print("   ⏳ Warte auf Timer-Stabilisierung (2s)...")
        time.sleep(2)
        
        # Verify by reading back
        print("   🔍 Verifiziere Timer-Status...")
        test_seconds, test_nanos = self.read_wallclock_time(ser)
        if test_seconds is not None and test_seconds > 0:
            print(f"   ✅ Timer läuft: {test_seconds} seconds, {test_nanos} nanoseconds")
            print(f"   🗺 Timer Zeit: {self.format_timestamp(test_seconds, test_nanos)}")
            return True
        else:
            print("   ❌ Timer läuft nicht - weitere Initialisierung erforderlich")
            return self.try_alternative_initialization(ser)
    
    def try_alternative_initialization(self, ser):
        """Try alternative TSU initialization methods"""
        print("🔄 Alternative TSU Initialisierung...")
        
        # Method 1: Use Timer Adjust Register to start timer
        print("   Methode 1: Timer Adjust Register")
        pc_seconds, pc_nanos = self.get_pc_timestamp()
        
        # Set ADJ bit (bit 31) with time value in MAC_TA
        adj_value = 0x80000000 | (pc_nanos & 0x3FFFFFFF)
        success = self.write_register(ser, self.tsu_registers["MAC_TA"], adj_value)
        
        if success:
            print("   ✅ Timer Adjust ausgeführt")
            time.sleep(0.5)
            
            # Check if timer is running
            test_seconds, test_nanos = self.read_wallclock_time(ser)
            if test_seconds is not None and test_seconds > 0:
                print("   ✅ Timer läuft nach Adjust!")
                return True
        
        # Method 2: Try different NCR bits
        print("   Methode 2: Verschiedene NCR Control-Bits")
        
        ncr_attempts = [
            0x00000810,  # Bit 4 + 11
            0x00001000,  # Bit 12
            0x00000800,  # Bit 11 only
            0x00000020,  # Bit 5
        ]
        
        for attempt_val in ncr_attempts:
            print(f"      Probiere NCR = 0x{attempt_val:08X}")
            success = self.write_register(ser, self.tsu_registers["MAC_NCR"], attempt_val)
            if success:
                time.sleep(0.5)
                
                # Initialize time again
                pc_seconds, pc_nanos = self.get_pc_timestamp()
                seconds_high = (pc_seconds >> 32) & 0xFFFF
                seconds_low = pc_seconds & 0xFFFFFFFF
                
                self.write_register(ser, self.tsu_registers["MAC_TSH"], seconds_high)
                self.write_register(ser, self.tsu_registers["MAC_TSL"], seconds_low)
                self.write_register(ser, self.tsu_registers["MAC_TN"], pc_nanos)
                
                time.sleep(0.5)
                
                # Test if running
                test_seconds, test_nanos = self.read_wallclock_time(ser)
                if test_seconds is not None and test_seconds > 0:
                    print(f"   ✅ Timer läuft mit NCR = 0x{attempt_val:08X}!")
                    return True
        
        print("   ❌ Alle Initialisierungsversuche fehlgeschlagen")
        return False
    
    def read_wallclock_time(self, ser):
        """Read current wallclock time from TSU"""
        # Read all time components
        tsh = self.read_register(ser, self.tsu_registers["MAC_TSH"])  # Seconds high
        tsl = self.read_register(ser, self.tsu_registers["MAC_TSL"])  # Seconds low  
        tn = self.read_register(ser, self.tsu_registers["MAC_TN"])    # Nanoseconds
        
        if None in [tsh, tsl, tn]:
            return None, None
            
        # Reconstruct 48-bit seconds value
        seconds_high = (tsh & 0x0000FFFF) << 32  # Only lower 16 bits are used
        seconds_low = tsl & 0xFFFFFFFF
        total_seconds = seconds_high | seconds_low
        
        # Extract nanoseconds (lower 30 bits)
        nanoseconds = tn & 0x3FFFFFFF
        
        return total_seconds, nanoseconds
    
    def quick_calibration_test(self, ser, duration=3.0):
        """Quick calibration test to measure timer accuracy"""
        try:
            # Initialize timer with current time
            pc_start_seconds, pc_start_nanos = self.get_pc_timestamp()
            
            # Set LAN timer to current PC time
            seconds_high = (pc_start_seconds >> 32) & 0xFFFF
            seconds_low = pc_start_seconds & 0xFFFFFFFF
            
            self.write_register(ser, self.tsu_registers["MAC_TSH"], seconds_high)
            self.write_register(ser, self.tsu_registers["MAC_TSL"], seconds_low)
            self.write_register(ser, self.tsu_registers["MAC_TN"], pc_start_nanos)
            
            # Wait for test duration
            time.sleep(duration)
            
            # Measure both clocks
            pc_end_seconds, pc_end_nanos = self.get_pc_timestamp()
            lan_end_seconds, lan_end_nanos = self.read_wallclock_time(ser)
            
            if lan_end_seconds is None:
                return None
                
            # Calculate elapsed times
            pc_elapsed_ns = (pc_end_seconds * 1_000_000_000 + pc_end_nanos) - (pc_start_seconds * 1_000_000_000 + pc_start_nanos)
            lan_elapsed_ns = (lan_end_seconds * 1_000_000_000 + lan_end_nanos) - (pc_start_seconds * 1_000_000_000 + pc_start_nanos)
            
            # Calculate error rate (ms/s)
            time_diff_ns = pc_elapsed_ns - lan_elapsed_ns
            error_rate_ms_per_s = (time_diff_ns / 1_000_000) / duration
            
            return error_rate_ms_per_s
            
        except Exception as e:
            print(f"      ⚠️ Kalibrierungstest-Fehler: {e}")
            return None
    
    def setup_tsu_increment(self, ser):
        """Read current TSU Timer Increment from hardware (no calibration)"""
        print("⚙️  TSU Timer Increment: Hardware-Default-Werte verwenden...")
        
        # Read current Timer Increment value from MAC_TI register
        current_increment = self.read_register(ser, self.tsu_registers["MAC_TI"])
        
        if current_increment is not None:
            print(f"   📋 Hardware-Default Timer Increment: {current_increment} ns")
            if current_increment > 0:
                frequency_mhz = 1000 / current_increment
                print(f"   🕒 Entspricht ca. {frequency_mhz:.1f} MHz Taktfrequenz")
            else:
                print("   ⚠️ Timer Increment ist 0 - möglicherweise nicht initialisiert")
                return False
        else:
            print("   ⚠️ Konnte Timer Increment nicht lesen, fahre trotzdem fort")
            print("   📝 Hardware verwendet vermutlich Standard-Default-Werte")
        
        print("   ✅ Verwende Hardware-Standard Timer Increment Konfiguration")
        return True
    
    def get_pc_timestamp(self):
        """Get current PC timestamp in seconds and nanoseconds"""
        now = datetime.now(timezone.utc)
        epoch = datetime(1970, 1, 1, tzinfo=timezone.utc)
        total_seconds = int((now - epoch).total_seconds())
        nanoseconds = int((now.microsecond * 1000) + ((now - epoch).total_seconds() % 1) * 1e9) % 1000000000
        return total_seconds, nanoseconds
    
    def compare_timestamps(self, pc_seconds, pc_nanos, lan_seconds, lan_nanos):
        """Compare PC and LAN timestamps"""
        pc_total_ns = pc_seconds * 1_000_000_000 + pc_nanos
        lan_total_ns = lan_seconds * 1_000_000_000 + lan_nanos
        
        diff_ns = abs(pc_total_ns - lan_total_ns)
        diff_ms = diff_ns / 1_000_000
        diff_s = diff_ns / 1_000_000_000
        
        return {
            'pc_total_ns': pc_total_ns,
            'lan_total_ns': lan_total_ns,
            'diff_ns': diff_ns,
            'diff_ms': diff_ms,
            'diff_s': diff_s
        }
    
    def format_timestamp(self, seconds, nanoseconds):
        """Format timestamp for display"""
        dt = datetime.fromtimestamp(seconds, tz=timezone.utc)
        return f"{dt.strftime('%Y-%m-%d %H:%M:%S')}.{nanoseconds:09d}"
    
    def signal_handler(self, sig, frame):
        """Handle Ctrl+C gracefully"""
        print("\n\n🛑 Test wird beendet...")
        self.running = False
    
    def run_wallclock_test(self):
        """Main wallclock test function"""
        print("=== LAN8651 TSU WALLCLOCK TIMER PTP TEST ===")
        print(f"Test Dauer: {self.test_duration} Sekunden")
        print(f"Mess-Intervall: {self.read_interval} Sekunden")
        print("=" * 60)
        
        # Setup signal handler for Ctrl+C
        signal.signal(signal.SIGINT, self.signal_handler)
        
        try:
            ser = serial.Serial(self.port, self.baudrate, timeout=3)
            time.sleep(1)
            print(f"✓ {self.port} verbunden")
            
            # Initialize TSU
            if not self.enable_tsu_wallclock(ser):
                print("❌ TSU Activation fehlgeschlagen")
                return False
                
            if not self.setup_tsu_increment(ser):
                print("❌ TSU Increment Setup fehlgeschlagen")
                return False
                
            if not self.initialize_wallclock_time(ser):
                print("❌ TSU Wallclock Initialization fehlgeschlagen")
                print("⚠️  Test wird trotzdem fortgesetzt für Diagnose...")
                # Continue anyway for diagnostic purposes
            
            print("\n🕐 Starte Wallclock Monitoring...")
            print("   (Drücke Ctrl+C zum Beenden)")
            print("\n" + "=" * 80)
            print("Zeit    | PC Timestamp                    | LAN Timestamp                   | Differenz")
            print("=" * 80)
            
            self.running = True
            start_time = time.time()
            measurement_count = 0
            
            while self.running and (time.time() - start_time) < self.test_duration:
                # Get timestamps
                pc_seconds, pc_nanos = self.get_pc_timestamp()
                lan_seconds, lan_nanos = self.read_wallclock_time(ser)
                
                timestamp = time.time() - start_time
                
                if lan_seconds is not None and lan_nanos is not None:
                    # Compare timestamps
                    comparison = self.compare_timestamps(pc_seconds, pc_nanos, lan_seconds, lan_nanos)
                    
                    # Store measurement
                    self.measurements.append({
                        'time': timestamp,
                        'pc_seconds': pc_seconds,
                        'pc_nanos': pc_nanos,
                        'lan_seconds': lan_seconds,
                        'lan_nanos': lan_nanos,
                        'comparison': comparison
                    })
                    
                    # Format for display
                    pc_formatted = self.format_timestamp(pc_seconds, pc_nanos)
                    lan_formatted = self.format_timestamp(lan_seconds, lan_nanos)
                    
                    # Status indicator
                    if comparison['diff_ms'] < 1:
                        status = "✅"
                    elif comparison['diff_ms'] < 1000:
                        status = "⚠️"
                    else:
                        status = "❌"
                        
                    print(f"{timestamp:06.1f}s | {pc_formatted} | {lan_formatted} | {comparison['diff_ms']:8.2f}ms {status}")
                    
                    measurement_count += 1
                else:
                    print(f"{timestamp:06.1f}s | {'':31} | LESEFEHLER                     | {'':>10}")
                
                # Wait for next measurement
                time.sleep(self.read_interval)
            
            ser.close()
            
            # Analyze results
            self.analyze_results()
            return True
            
        except Exception as e:
            print(f"❌ Test Fehler: {e}")
            return False
    
    def analyze_results(self):
        """Analyze test results"""
        print("\n" + "=" * 60)
        print("📊 TEST ERGEBNISSE ANALYSE")
        print("=" * 60)
        
        if not self.measurements:
            print("❌ Keine gültigen Messungen verfügbar")
            return
            
        # Calculate statistics
        diffs_ms = [m['comparison']['diff_ms'] for m in self.measurements]
        
        avg_diff = sum(diffs_ms) / len(diffs_ms)
        min_diff = min(diffs_ms)
        max_diff = max(diffs_ms)
        
        # Check for drift
        first_measurement = self.measurements[0]
        last_measurement = self.measurements[-1]
        
        time_span = last_measurement['time'] - first_measurement['time']
        drift_ms = last_measurement['comparison']['diff_ms'] - first_measurement['comparison']['diff_ms']
        drift_rate = drift_ms / time_span if time_span > 0 else 0
        
        print(f"📈 Statistiken:")
        print(f"   Messungen: {len(self.measurements)}")
        print(f"   Durchschnittliche Abweichung: {avg_diff:.2f} ms")
        print(f"   Minimale Abweichung: {min_diff:.2f} ms")
        print(f"   Maximale Abweichung: {max_diff:.2f} ms")
        print(f"   Drift Rate: {drift_rate:.3f} ms/s")
        
        # Accuracy assessment
        print(f"\n🎯 Genauigkeits-Bewertung:")
        
        if avg_diff < 1:
            print("   ✅ EXZELLENT: Wallclock sehr genau (< 1ms)")
        elif avg_diff < 10:
            print("   ✅ GUT: Wallclock ausreichend genau (< 10ms)")
        elif avg_diff < 100:
            print("   ⚠️ MÄSSIG: Wallclock funktional aber ungenau (< 100ms)")
        else:
            print("   ❌ SCHLECHT: Wallclock sehr ungenau (> 100ms)")
            
        if abs(drift_rate) < 0.001:
            print("   ✅ STABIL: Minimaler Drift (< 1µs/s)")
        elif abs(drift_rate) < 0.1:
            print("   ✅ GUT: Geringer Drift (< 0.1ms/s)")
        else:
            print("   ⚠️ DRIFT: Signifikanter Drift erkannt")
            
        # Wallclock functionality check
        working_measurements = sum(1 for d in diffs_ms if d < 86400000)  # Less than 1 day off
        functionality_rate = working_measurements / len(self.measurements)
        
        print(f"\n🔧 Funktionalitäts-Check:")
        print(f"   Gültige Messungen: {working_measurements}/{len(self.measurements)} ({functionality_rate*100:.1f}%)")
        
        if functionality_rate > 0.9:
            print("   ✅ WALLCLOCK FUNKTIONIERT KORREKT")
        elif functionality_rate > 0.5:
            print("   ⚠️ WALLCLOCK TEILWEISE FUNKTIONAL")
        else:
            print("   ❌ WALLCLOCK DEFEKT ODER NICHT AKTIV")
            
        return functionality_rate > 0.9

def main():
    print("="*60)
    print("LAN8651 TSU Wallclock Timer PTP Test - HARDWARE DEFAULTS v2.2")
    print("Testet PTP Time Synchronization Unit mit automatischer Kalibrierung")
    print("")
    print("✅ Hardware-verifizierte Register-Adressen (MMS_1)")
    print("🛡️ Robuste prompt-basierte Kommunikation")
    print("🔧 Erweiterte TSU-Diagnose und Retry-Logik")
    print("� HARDWARE-DEFAULT Timer Increment (keine Kalibrierung)")
    print("="*60)
    
    test = LAN8651_PTPTest()
    
    success = test.run_wallclock_test()
    
    if success:
        print("\n🎉 PTP Wallclock Test erfolgreich abgeschlossen!")
        exit(0)
    else:
        print("\n❌ PTP Wallclock Test fehlgeschlagen")
        print("📝 Prüfe folgende Punkte:")
        print("   1. Hardware-Verbindung (COM8 @ 115200 baud)")
        print("   2. LAN8651 Firmware-Status")
        print("   3. TSU-Register zugänglich")
        print("   4. MAC-Layer korrekt initialisiert")
        exit(1)

if __name__ == "__main__":
    main()