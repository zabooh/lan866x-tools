#!/usr/bin/env python3
"""
PLCA Register Check - GUI korrekte Adressen
"""
import serial
import time

def check_plca_gui_addresses():
    print("🔍 PLCA Register Check - GUI Adressen")
    print("=" * 50)
    
    try:
        ser = serial.Serial('COM8', 115200, timeout=2)
        print("✅ Connected to firmware")
        
        ser.write(b"\n")
        time.sleep(1)
        ser.read_all()
        
        print("\n📊 PLCA Register aus GUI (korrekte Adressen):")
        print("=" * 50)
        
        # Korrekte Adressen aus der GUI
        plca_gui_registers = [
            (0x00040010, "CTRL1", "Control Register 1"),
            (0x00040018, "STS1", "Status Register 1"),
            (0x0004CA00, "MIDVER", "Version/Model ID"),
            (0x0004CA01, "PLCA_CTRL0", "PLCA Main Control"),
            (0x0004CA02, "PLCA_CTRL1", "PLCA Extended Control"),
            (0x0004CA03, "PLCA_STS", "PLCA Status"),  
            (0x0004CA04, "PLCA_TOTMR", "PLCA TO Timer"),
            (0x0004CA05, "PLCA_BURST", "PLCA Burst Control"),
        ]
        
        plca_values = {}
        
        for addr, name, desc in plca_gui_registers:
            ser.write(f"lan_read 0x{addr:08X}\n".encode())
            time.sleep(0.5)
            response = ser.read_all().decode('utf-8', errors='ignore')
            
            value = None
            for line in response.split('\n'):
                if 'Value=' in line:
                    try:
                        value_part = line.split('Value=')[1]
                        value = int(value_part.split()[0], 16)
                        break
                    except:
                        pass
            
            plca_values[name] = value
            
            if value is not None:
                if value > 0:
                    print(f"🔥 0x{addr:08X} {name:15} = 0x{value:08X} ({value:5}) - {desc}")
                else:
                    print(f"❌ 0x{addr:08X} {name:15} = 0x{value:08X} ({value:5}) - {desc}")
            else:
                print(f"🚫 0x{addr:08X} {name:15} = READ FAILED - {desc}")
        
        print("\n🎯 PLCA ANALYSE - GUI Werte:")
        print("=" * 50)
        
        # PLCA_CTRL0 Analyse (0x0004CA01)
        ctrl0 = plca_values.get("PLCA_CTRL0", 0)
        if ctrl0:
            print(f"\n📊 PLCA_CTRL0 = 0x{ctrl0:04X} Bit-Analyse:")
            print(f"   Bit 15 (PLCA_EN):    {'✅ PLCA ENABLED' if ctrl0 & 0x8000 else '❌ PLCA DISABLED'}")
            print(f"   Bit 14 (BEACON):     {'🔥 COORDINATOR' if ctrl0 & 0x4000 else '👥 NODE'}")
            print(f"   Bit 0  (RST):        {'🔄 RESET' if ctrl0 & 0x0001 else '✅ NORMAL'}")
        
        # PLCA_CTRL1 Analyse (0x0004CA02)  
        ctrl1 = plca_values.get("PLCA_CTRL1", 0)
        if ctrl1:
            print(f"\n📊 PLCA_CTRL1 = 0x{ctrl1:04X} Extended Control:")
            print(f"   Bits 7:0 (NODE_ID): Node {ctrl1 & 0xFF}")
            print(f"   Bits 15:8 (MAXNODE): Max {(ctrl1 >> 8) & 0xFF} nodes")
        
        # PLCA Status Analyse
        status = plca_values.get("PLCA_STS", 0)
        if status:
            print(f"\n📊 PLCA_STS = 0x{status:04X} Status:")
            print(f"   Bit 15 (PST):        {'✅ PLCA ACTIVE' if status & 0x8000 else '❌ PLCA INACTIVE'}")
        
        # Burst Analyse
        burst = plca_values.get("PLCA_BURST", 0)
        if burst:
            print(f"\n🚀 PLCA_BURST = 0x{burst:04X} Burst Config:")
            print(f"   Burst Count:         {burst & 0xFF}")
            print(f"   Burst Timer:         {(burst >> 8) & 0xFF}")
        
        # Gesamte PLCA Konfiguration
        print(f"\n🎯 PLCA KONFIGURATION ZUSAMMENFASSUNG:")
        print("=" * 50)
        
        if ctrl0 & 0x8000:
            print("✅ PLCA IST AKTIVIERT!")
            
            if ctrl0 & 0x4000:
                print("🔥 Dieses Gerät ist der PLCA COORDINATOR")
            else:
                node_id = ctrl1 & 0xFF
                max_nodes = (ctrl1 >> 8) & 0xFF
                print(f"👥 Dieses Gerät ist Node {node_id} von {max_nodes}")
            
            if status & 0x8000:
                print("✅ PLCA Status: AKTIV")
            else:
                print("❌ PLCA Status: INAKTIV")
                
            if burst > 0:
                burst_count = burst & 0xFF
                if burst_count > 1:
                    print(f"🚀 BURST MODE: {burst_count} Pakete pro Opportunity")
                else:
                    print("📦 STANDARD MODE: 1 Paket pro Opportunity")
            else:
                print("📦 BURST: Standard (1 Paket)")
        else:
            print("❌ PLCA IST DEAKTIVIERT - Point-to-Point Modus?")
        
        ser.close()
        print("\n✅ PLCA GUI-Adressen Check complete")
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    check_plca_gui_addresses()