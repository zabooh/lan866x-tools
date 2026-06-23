#!/usr/bin/env python3
"""
T1S Live Traffic Monitor - Linux eth0 Interface
"""
import serial
import time
import re

def t1s_live_monitor():
    print("🔍 T1S Live Traffic Monitor (eth0)")
    print("=" * 50)
    
    try:
        ser = serial.Serial('COM9', 115200, timeout=2)
        print("✅ Connected to Linux shell") 
        
        # Clear buffer
        ser.write(b"\n")
        time.sleep(1)
        ser.read_all()
        
        print("\n📊 Starting live T1S monitoring...")
        print("Press Ctrl+C to stop\n")
        
        prev_rx_packets = None
        prev_tx_packets = None
        prev_rx_bytes = None
        prev_tx_bytes = None
        
        while True:
            # Get current eth0 stats
            ser.write(b"cat /proc/net/dev | grep eth0\n")
            time.sleep(0.5)
            response = ser.read_all().decode('utf-8', errors='ignore')
            
            # Parse eth0 line
            for line in response.split('\n'):
                if 'eth0:' in line:
                    # Parse: eth0: RX_bytes RX_packets ... TX_bytes TX_packets
                    parts = line.split()
                    if len(parts) >= 10:
                        try:
                            rx_bytes = int(parts[1])
                            rx_packets = int(parts[2])
                            tx_bytes = int(parts[9]) 
                            tx_packets = int(parts[10])
                            
                            # Calculate rates if we have previous data
                            if prev_rx_packets is not None:
                                rx_pps = rx_packets - prev_rx_packets
                                tx_pps = tx_packets - prev_tx_packets
                                rx_bps = (rx_bytes - prev_rx_bytes) * 8  # bits per second
                                tx_bps = (tx_bytes - prev_tx_bytes) * 8
                                
                                print(f"⚡ T1S eth0: RX {rx_packets:6} pkt (+{rx_pps:3}/s) {rx_bps/1000:6.1f} kbps | TX {tx_packets:6} pkt (+{tx_pps:3}/s) {tx_bps/1000:6.1f} kbps")
                            else:
                                print(f"📊 T1S eth0: RX {rx_packets:6} packets | TX {tx_packets:6} packets")
                            
                            # Store for next iteration
                            prev_rx_packets = rx_packets
                            prev_tx_packets = tx_packets  
                            prev_rx_bytes = rx_bytes
                            prev_tx_bytes = tx_bytes
                            
                        except (ValueError, IndexError):
                            print("❌ Failed to parse eth0 stats")
                    break
            
            time.sleep(1)  # Update every second
            
    except KeyboardInterrupt:
        print("\n\n✅ Monitoring stopped")
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    t1s_live_monitor()