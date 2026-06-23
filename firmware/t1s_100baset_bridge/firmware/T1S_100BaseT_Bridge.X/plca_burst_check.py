#!/usr/bin/env python3
"""
PLCA Burst Mode Check - T1S Performance Konfiguration
"""
import serial
import time

def check_plca_burst_mode():
    print("🔍 PLCA Burst Mode Analysis")
    print("=" * 40)
    
    try:
        ser = serial.Serial('COM8', 115200, timeout=2)
        print("✅ Connected to firmware interface")
        
        # Clear buffer
        ser.write(b"\n")
        time.sleep(1)
        ser.read_all()
        
        print("\n📊 PLCA Configuration Registers:")
        print("=" * 40)
        
        # PLCA registers in MMS 4 (confirmed from previous tests)
        plca_registers = [
            (0x00040CA0, "PLCA_CTRL0", "Main Control"),
            (0x00040CA1, "PLCA_CTRL1", "Extended Control"), 
            (0x00040CA2, "PLCA_STATUS", "Status Register"),
            (0x00040CA3, "PLCA_ID", "Node ID"),
            (0x00040CA4, "PLCA_TOTMR", "TO Timer"),
            (0x00040CA5, "PLCA_BURST", "Burst Control/Count"),
            (0x00040CA6, "PLCA_BURST_TMR", "Burst Timer"),
            (0x00040CA7, "PLCA_BURST_MODE", "Burst Mode Config"),
        ]
        
        plca_values = {}
        
        for addr, name, desc in plca_registers:
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
                    print(f"🔥 0x{addr:08X} {name:15} = 0x{value:04X} ({value:5}) - {desc}")
                else:
                    print(f"❌ 0x{addr:08X} {name:15} = 0x{value:04X} ({value:5}) - {desc}")
            else:
                print(f"🚫 0x{addr:08X} {name:15} = READ FAILED - {desc}")
        
        print("\n🎯 PLCA BURST MODE ANALYSIS:")
        print("=" * 40)
        
        # Analyze PLCA_CTRL0 bits
        ctrl0 = plca_values.get("PLCA_CTRL0")
        if ctrl0 is not None:
            print(f"\n📊 PLCA_CTRL0 = 0x{ctrl0:04X} bit analysis:")
            print(f"   Bit 15 (ENABLE):     {'✅ ENABLED' if ctrl0 & 0x8000 else '❌ DISABLED'}")
            print(f"   Bit 0  (RST):        {'🔄 RESET' if ctrl0 & 0x0001 else '✅ NORMAL'}")
            
        # Analyze PLCA_CTRL1 (Extended control)
        ctrl1 = plca_values.get("PLCA_CTRL1") 
        if ctrl1 is not None:
            print(f"\n📊 PLCA_CTRL1 = 0x{ctrl1:04X} extended control:")
            print(f"   Bit 8 (BURST_EN):    {'🔥 BURST ENABLED' if ctrl1 & 0x0100 else '❌ BURST DISABLED'}")
            print(f"   Bit 7 (PROMIS):      {'✅ PROMISCUOUS' if ctrl1 & 0x0080 else '❌ NORMAL'}")
        
        # Analyze Burst specific registers
        burst_ctrl = plca_values.get("PLCA_BURST")
        burst_timer = plca_values.get("PLCA_BURST_TMR")
        burst_mode = plca_values.get("PLCA_BURST_MODE")
        
        print(f"\n🚀 BURST CONFIGURATION:")
        if burst_ctrl is not None:
            print(f"   BURST Control:       0x{burst_ctrl:04X} ({burst_ctrl})")
        if burst_timer is not None:
            print(f"   BURST Timer:         0x{burst_timer:04X} ({burst_timer})")
        if burst_mode is not None:
            print(f"   BURST Mode:          0x{burst_mode:04X} ({burst_mode})")
        
        # Node ID analysis
        node_id = plca_values.get("PLCA_ID")
        if node_id is not None:
            print(f"\n🏷️  NODE CONFIGURATION:")
            print(f"   Node ID:             {node_id & 0xFF}")
            print(f"   Max Nodes:           {(node_id >> 8) & 0xFF}")
        
        # Status analysis
        status = plca_values.get("PLCA_STATUS")
        if status is not None:
            print(f"\n📊 PLCA STATUS = 0x{status:04X}:")
            print(f"   PST (PHY Status):    {'✅ ACTIVE' if status & 0x8000 else '❌ INACTIVE'}")
            print(f"   Receive Status:      {'✅ RX OK' if status & 0x0400 else '❌ RX ISSUE'}")
        
        # Overall conclusion
        print(f"\n🎯 BURST MODE CONCLUSION:")
        print("=" * 40)
        
        burst_enabled = False
        if ctrl1 is not None and (ctrl1 & 0x0100):
            burst_enabled = True
            print("✅ PLCA BURST MODE IS ENABLED!")
            print("   → Can send multiple packets per opportunity")
            print("   → Higher throughput potential")
            
            if burst_ctrl and burst_ctrl > 0:
                print(f"   → Burst count: {burst_ctrl} packets/burst")
            if burst_timer and burst_timer > 0:
                print(f"   → Burst timer: {burst_timer}")
        else:
            print("❌ PLCA BURST MODE IS DISABLED")
            print("   → Single packet per PLCA opportunity") 
            print("   → Standard T1S operation")
            print("   → Potential performance limitation")
        
        # Performance impact analysis
        if not burst_enabled and plca_values.get("PLCA_CTRL0", 0) & 0x8000:
            print(f"\n💡 PERFORMANCE IMPACT:")
            print("   → Our 1.476 Mbps achieved WITHOUT burst mode")
            print("   → Burst mode could potentially increase throughput")
            print("   → Current performance is still excellent!")
        
        ser.close()
        print("\n✅ PLCA burst mode analysis complete")
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    check_plca_burst_mode()