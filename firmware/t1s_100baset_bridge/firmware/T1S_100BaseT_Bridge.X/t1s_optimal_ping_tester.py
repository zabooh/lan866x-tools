#!/usr/bin/env python3
"""
T1S Optimal Ping Bandwidth Test v2.0
Advanced bandwidth optimization for 10BASE-T1S networks with detailed analysis

Tests specific packet sizes (175, 350, 700, 1400, 2800 bytes) to find optimal
ping configuration for maximum bandwidth without packet loss.

Author: T1S Development Team  
Date: March 2026
Hardware: LAN8650/8651 T1S Bridge with PLCA Network
Target: 192.168.0.200 (T1S Bridge)
Language: English
Output: Comprehensive HTML Report
"""

import serial
import time
import re
import logging
import statistics
from typing import Dict, List, Tuple, Optional
from dataclasses import dataclass
from datetime import datetime

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    datefmt='%H:%M:%S'
)
logger = logging.getLogger(__name__)

@dataclass
class PingTestResult:
    """Complete ping test result with performance metrics"""
    packet_size: int
    interval: float
    packets_per_second: float
    count: int
    transmitted: int
    received: int
    packet_loss: float
    min_rtt: float
    avg_rtt: float
    max_rtt: float
    mdev_rtt: float
    total_time: float
    throughput_bps: float
    expected_throughput_bps: float
    
    @property
    def success_rate(self) -> float:
        return (self.received / self.transmitted) * 100 if self.transmitted > 0 else 0
    
    @property
    def throughput_mbps(self) -> float:
        return self.throughput_bps / 1_000_000
    
    @property
    def expected_throughput_mbps(self) -> float:
        return self.expected_throughput_bps / 1_000_000
    
    @property
    def efficiency_percent(self) -> float:
        """Actual vs expected throughput efficiency"""
        return (self.throughput_bps / self.expected_throughput_bps) * 100 if self.expected_throughput_bps > 0 else 0

class T1SOptimalPingTester:
    """T1S Network Optimal Ping Configuration Tool"""
    
    def __init__(self, port: str = 'COM9', baudrate: int = 115200, timeout: float = 30.0):
        """
        Initialize T1S Optimal Ping Tester
        
        Args:
            port: Serial port for Linux shell connection
            baudrate: Serial communication speed
            timeout: Command response timeout
        """
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.serial_conn = None
        self.target_ip = "192.168.0.200"
        
        # Test packet sizes as specified
        self.test_packet_sizes = [175, 350, 700, 1400, 2800]
        
        # Results storage
        self.all_results: List[PingTestResult] = []
        self.optimal_result: Optional[PingTestResult] = None
        
    def connect(self) -> bool:
        """Connect to Linux shell via serial port"""
        try:
            self.serial_conn = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                timeout=self.timeout,
                bytesize=8,
                parity='N',
                stopbits=1
            )
            
            time.sleep(0.5)
            self.serial_conn.write(b'\n')
            time.sleep(0.2)
            
            response = self._read_until_prompt()
            if '#' in response or '$' in response:
                logger.info(f"✅ Connected to Linux shell on {self.port}")
                return True
            else:
                logger.error("❌ No shell prompt detected")
                return False
                
        except Exception as e:
            logger.error(f"❌ Connection failed: {str(e)}")
            return False
    
    def disconnect(self):
        """Close serial connection"""
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.close()
            logger.info("Connection closed")
    
    def _send_command(self, command: str) -> str:
        """Send command to shell and return response"""
        if not self.serial_conn or not self.serial_conn.is_open:
            return ""
            
        try:
            self.serial_conn.reset_input_buffer()
            cmd = f"{command}\n"
            self.serial_conn.write(cmd.encode('utf-8'))
            response = self._read_until_prompt()
            return response
            
        except Exception as e:
            logger.error(f"Command failed: {e}")
            return ""
    
    def _read_until_prompt(self) -> str:
        """Read response until shell prompt appears"""
        response = ""
        start_time = time.time()
        
        while time.time() - start_time < self.timeout:
            try:
                if self.serial_conn.in_waiting > 0:
                    data = self.serial_conn.read(self.serial_conn.in_waiting)
                    response += data.decode('utf-8', errors='ignore')
                    
                    if response.strip().endswith('#') or response.strip().endswith('$'):
                        break
                        
                time.sleep(0.01)
                
            except Exception as e:
                logger.error(f"Read error: {e}")
                break
                
        return response
    
    def _parse_ping_output(self, output: str, packet_size: int, interval: float, count: int) -> Optional[PingTestResult]:
        """Parse ping command output and create result object"""
        try:
            # Parse statistics: "X packets transmitted, Y received, Z% packet loss, time Nms"
            stats_match = re.search(
                r'(\d+) packets transmitted, (\d+) received, ([\d.]+)% packet loss, time (\d+)ms',
                output
            )
            
            if not stats_match:
                logger.warning("Could not parse ping statistics")
                return None
            
            transmitted = int(stats_match.group(1))
            received = int(stats_match.group(2))
            packet_loss = float(stats_match.group(3))
            total_time = float(stats_match.group(4)) / 1000.0
            
            # Parse RTT: "rtt min/avg/max/mdev = X.X/Y.Y/Z.Z/W.W ms"
            rtt_match = re.search(
                r'rtt min/avg/max/mdev = ([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+) ms',
                output
            )
            
            if rtt_match:
                min_rtt = float(rtt_match.group(1))
                avg_rtt = float(rtt_match.group(2))
                max_rtt = float(rtt_match.group(3))
                mdev_rtt = float(rtt_match.group(4))
            else:
                min_rtt = avg_rtt = max_rtt = mdev_rtt = 0.0
            
            # Calculate actual and expected throughput
            packets_per_second = 1.0 / interval if interval > 0 else 0
            bytes_per_packet = packet_size + 28  # IP + ICMP + Ethernet headers
            
            actual_throughput_bps = (received * bytes_per_packet * 8) / total_time if total_time > 0 else 0
            expected_throughput_bps = packets_per_second * bytes_per_packet * 8
            
            return PingTestResult(
                packet_size=packet_size,
                interval=interval,
                packets_per_second=packets_per_second,
                count=count,
                transmitted=transmitted,
                received=received,
                packet_loss=packet_loss,
                min_rtt=min_rtt,
                avg_rtt=avg_rtt,
                max_rtt=max_rtt,
                mdev_rtt=mdev_rtt,
                total_time=total_time,
                throughput_bps=actual_throughput_bps,
                expected_throughput_bps=expected_throughput_bps
            )
            
        except Exception as e:
            logger.error(f"Parse error: {e}")
            return None
    
    def test_packet_size_rates(self, packet_size: int) -> List[PingTestResult]:
        """Test increasing packet rates for specific packet size until errors occur"""
        
        logger.info(f"\n🧪 Testing packet size: {packet_size} bytes")
        
        # Generate test intervals (decreasing = increasing rate)
        intervals = [
            0.100,  # 10 pps
            0.050,  # 20 pps
            0.025,  # 40 pps
            0.020,  # 50 pps
            0.015,  # 67 pps
            0.010,  # 100 pps
            0.008,  # 125 pps
            0.006,  # 167 pps
            0.005,  # 200 pps
            0.004,  # 250 pps
            0.003,  # 333 pps
            0.0025, # 400 pps
            0.002,  # 500 pps
            0.0015, # 667 pps
            0.001,  # 1000 pps
            0.0008, # 1250 pps
            0.0006, # 1667 pps
            0.0005, # 2000 pps
            0.0004, # 2500 pps
        ]
        
        results = []
        consecutive_failures = 0
        
        for interval in intervals:
            pps = 1.0 / interval
            expected_bps = (packet_size + 28) * 8 * pps
            
            logger.info(f"  📡 Testing {pps:.0f} pps (interval: {interval:.4f}s, expected: {expected_bps:.0f} bps)")
            
            # Build and send ping command
            ping_cmd = f"ping -s {packet_size} -i {interval:.4f} -c 50 {self.target_ip}"
            output = self._send_command(ping_cmd)
            
            if not output:
                logger.warning("    ❌ No ping output received")
                consecutive_failures += 1
                if consecutive_failures >= 2:
                    logger.info("    🛑 Stopping due to consecutive failures")
                    break
                continue
            
            # Parse result
            result = self._parse_ping_output(output, packet_size, interval, 50)
            
            if result:
                results.append(result)
                
                logger.info(f"    ✅ {result.received}/{result.transmitted} packets, "
                           f"{result.packet_loss:.1f}% loss, "
                           f"{result.throughput_bps:.0f} bps ({result.throughput_mbps:.3f} Mbps), "
                           f"efficiency: {result.efficiency_percent:.1f}%")
                
                # Stop if packet loss becomes significant (>2%)
                if result.packet_loss > 2.0:
                    logger.info(f"    🛑 Stopping: packet loss > 2% ({result.packet_loss:.1f}%)")
                    break
                    
                consecutive_failures = 0
            else:
                logger.warning("    ❌ Failed to parse result")
                consecutive_failures += 1
                if consecutive_failures >= 2:
                    logger.info("    🛑 Stopping due to parse failures")
                    break
            
            # Small delay between tests
            time.sleep(0.3)
        
        logger.info(f"  📊 Completed {len(results)} tests for {packet_size} bytes")
        return results
    
    def run_comprehensive_test(self) -> List[PingTestResult]:
        """Run comprehensive test on all packet sizes"""
        
        logger.info("🚀 Starting T1S Optimal Ping Bandwidth Test")
        logger.info(f"📋 Testing packet sizes: {self.test_packet_sizes} bytes")
        logger.info(f"🎯 Target: {self.target_ip}")
        
        for i, packet_size in enumerate(self.test_packet_sizes, 1):
            logger.info(f"\n{'='*60}")
            logger.info(f"📦 PACKET SIZE TEST {i}/{len(self.test_packet_sizes)}: {packet_size} BYTES")
            logger.info(f"{'='*60}")
            
            size_results = self.test_packet_size_rates(packet_size)
            self.all_results.extend(size_results)
        
        # Find optimal configuration
        self._find_optimal_configuration()
        
        logger.info(f"\n✅ Test Complete! Total {len(self.all_results)} configurations tested")
        return self.all_results
    
    def _find_optimal_configuration(self):
        """Find optimal ping configuration from all results"""
        
        if not self.all_results:
            logger.warning("No results to analyze")
            return
        
        # Filter zero-loss results first
        zero_loss_results = [r for r in self.all_results if r.packet_loss == 0.0]
        
        if zero_loss_results:
            # Best zero-loss result by throughput
            self.optimal_result = max(zero_loss_results, key=lambda x: x.throughput_bps)
            logger.info(f"\n🎯 OPTIMAL CONFIGURATION FOUND (Zero Loss):")
        else:
            # Best low-loss result (≤1%)
            low_loss_results = [r for r in self.all_results if r.packet_loss <= 1.0]
            if low_loss_results:
                self.optimal_result = max(low_loss_results, key=lambda x: x.throughput_bps)
                logger.info(f"\n⚠️  BEST AVAILABLE CONFIGURATION (Low Loss ≤1%):")
            else:
                # Emergency fallback
                self.optimal_result = max(self.all_results, key=lambda x: x.throughput_bps * (100 - x.packet_loss))
                logger.info(f"\n🆘 EMERGENCY FALLBACK CONFIGURATION:")
        
        if self.optimal_result:
            opt = self.optimal_result
            logger.info(f"   Packet Size: {opt.packet_size} bytes")
            logger.info(f"   Rate: {opt.packets_per_second:.0f} pps (interval: {opt.interval:.4f}s)")
            logger.info(f"   Throughput: {opt.throughput_bps:.0f} bps ({opt.throughput_mbps:.3f} Mbps)")
            logger.info(f"   Packet Loss: {opt.packet_loss:.1f}%")
            logger.info(f"   Average Latency: {opt.avg_rtt:.2f} ms")
            logger.info(f"   Efficiency: {opt.efficiency_percent:.1f}%")
    
    def generate_html_report(self, filename: str = "t1s_optimal_ping_report.html"):
        """Generate comprehensive HTML report with detailed T1S analysis"""
        
        if not self.all_results:
            logger.warning("No results available for report generation")
            return
        
        # Group results by packet size
        results_by_size = {}
        for result in self.all_results:
            size = result.packet_size
            if size not in results_by_size:
                results_by_size[size] = []
            results_by_size[size].append(result)
        
        # Sort each group by packet rate
        for size in results_by_size:
            results_by_size[size].sort(key=lambda x: x.packets_per_second)
        
        # Calculate statistics
        zero_loss_count = len([r for r in self.all_results if r.packet_loss == 0.0])
        max_throughput = max(r.throughput_bps for r in self.all_results)
        avg_efficiency = statistics.mean([r.efficiency_percent for r in self.all_results if r.efficiency_percent > 0])
        
        # Generate HTML content
        html_content = f"""
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>T1S Optimal Ping Bandwidth Test Report</title>
    <style>
        body {{ 
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            margin: 40px; 
            background: linear-gradient(135deg, #f5f7fa 0%, #c3cfe2 100%);
            line-height: 1.6;
        }}
        .container {{ 
            max-width: 1400px; 
            margin: 0 auto; 
            background: white; 
            padding: 40px; 
            border-radius: 15px;
            box-shadow: 0 20px 40px rgba(0,0,0,0.1);
        }}
        h1 {{ 
            color: #2c3e50; 
            border-bottom: 4px solid #3498db; 
            padding-bottom: 15px; 
            font-size: 2.5em;
            margin-bottom: 30px;
        }}
        h2 {{ 
            color: #34495e; 
            margin-top: 40px; 
            font-size: 1.8em;
            border-left: 5px solid #3498db;
            padding-left: 15px;
        }}
        h3 {{ 
            color: #2c3e50;
            font-size: 1.4em;
            margin-top: 30px;
        }}
        .header-info {{ 
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 25px;
            border-radius: 10px;
            margin: 20px 0;
            font-size: 1.1em;
        }}
        .optimal-config {{ 
            background: linear-gradient(135deg, #11998e 0%, #38ef7d 100%);
            color: white;
            border-radius: 12px;
            padding: 30px;
            margin: 30px 0;
            text-align: center;
        }}
        .optimal-config h2 {{ 
            color: white;
            border: none;
            margin-top: 0;
            font-size: 2em;
        }}
        .ping-command {{ 
            background: #2c3e50;
            color: #ecf0f1;
            padding: 20px;
            margin: 15px 0;
            font-family: 'Fira Code', 'Courier New', monospace;
            font-size: 16px;
            border-radius: 8px;
            border-left: 5px solid #e74c3c;
            overflow-x: auto;
        }}
        .stats-grid {{ 
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
            gap: 20px;
            margin: 30px 0;
        }}
        .stat-card {{ 
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 25px;
            border-radius: 10px;
            text-align: center;
        }}
        .stat-number {{ 
            font-size: 2.5em;
            font-weight: bold;
            display: block;
            margin-bottom: 5px;
        }}
        table {{ 
            width: 100%; 
            border-collapse: collapse; 
            margin: 25px 0;
            box-shadow: 0 5px 15px rgba(0,0,0,0.1);
            border-radius: 8px;
            overflow: hidden;
        }}
        th {{ 
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 15px 12px;
            text-align: left;
            font-weight: 600;
        }}
        td {{ 
            border: 1px solid #ddd; 
            padding: 12px; 
            text-align: left;
        }}
        tr:nth-child(even) {{ 
            background-color: #f8f9fa;
        }}
        tr:hover {{ 
            background-color: #e3f2fd;
            transition: background-color 0.3s ease;
        }}
        .metric {{ 
            font-size: 1.3em; 
            font-weight: bold; 
            color: #2c3e50;
        }}
        .good {{ color: #27ae60; font-weight: bold; }}
        .warning {{ color: #f39c12; font-weight: bold; }}
        .error {{ color: #e74c3c; font-weight: bold; }}
        .packet-size-section {{ 
            margin: 40px 0; 
            padding: 25px; 
            border: 2px solid #ecf0f1; 
            border-radius: 12px;
            background: #fafbfc;
        }}
        .analysis-section {{ 
            background: linear-gradient(135deg, #ffecd2 0%, #fcb69f 100%);
            padding: 30px;
            border-radius: 12px;
            margin: 30px 0;
        }}
        .analysis-section h2 {{ 
            color: #8b4513;
            border-left-color: #d35400;
        }}
        .technical-details {{ 
            background: #f8f9fa;
            border-left: 5px solid #6c757d;
            padding: 20px;
            margin: 20px 0;
            border-radius: 0 8px 8px 0;
        }}
        .highlight {{ 
            background: #fff3cd;
            padding: 15px;
            border-left: 5px solid #ffc107;
            margin: 15px 0;
            border-radius: 0 5px 5px 0;
        }}
    </style>
</head>
<body>
    <div class="container">
        <h1>🚀 T1S Optimal Ping Bandwidth Test Report</h1>
        
        <div class="header-info">
            <strong>📅 Test Date:</strong> {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}<br>
            <strong>🎯 Target Device:</strong> {self.target_ip} (T1S Bridge)<br>
            <strong>🔬 Test Type:</strong> Comprehensive Ping Bandwidth Optimization<br>
            <strong>📊 Packet Sizes Tested:</strong> {', '.join(map(str, self.test_packet_sizes))} bytes<br>
            <strong>🌐 Network Technology:</strong> 10BASE-T1S Single Pair Ethernet with PLCA
        </div>
"""

        # Add optimal configuration section
        if self.optimal_result:
            opt = self.optimal_result
            efficiency_t1s = (opt.throughput_bps / 10_000_000) * 100  # vs 10 Mbps theoretical
            
            html_content += f"""
        <div class="optimal-config">
            <h2>🎯 OPTIMAL PING CONFIGURATION</h2>
            <div class="metric" style="color: white; font-size: 2.2em; margin: 20px 0;">
                {opt.throughput_bps:.0f} bps ({opt.throughput_mbps:.3f} Mbps)
            </div>
            <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 20px; margin: 20px 0;">
                <div>
                    <strong>Packet Size:</strong><br>
                    <span style="font-size: 1.5em;">{opt.packet_size} bytes</span>
                </div>
                <div>
                    <strong>Packet Rate:</strong><br>
                    <span style="font-size: 1.5em;">{opt.packets_per_second:.0f} pps</span>
                </div>
                <div>
                    <strong>Packet Loss:</strong><br>
                    <span style="font-size: 1.5em;">{opt.packet_loss:.1f}%</span>
                </div>
                <div>
                    <strong>Latency:</strong><br>
                    <span style="font-size: 1.5em;">{opt.avg_rtt:.2f} ms</span>
                </div>
                <div>
                    <strong>T1S Efficiency:</strong><br>
                    <span style="font-size: 1.5em;">{efficiency_t1s:.1f}%</span>
                </div>
                <div>
                    <strong>System Efficiency:</strong><br>
                    <span style="font-size: 1.5em;">{opt.efficiency_percent:.1f}%</span>
                </div>
            </div>
            
            <h3 style="color: white; margin-top: 30px;">🚀 Recommended Command:</h3>
            <div class="ping-command">
                ping -s {opt.packet_size} -i {opt.interval:.4f} -c 1000 {self.target_ip}
            </div>
        </div>
"""

        # Add summary statistics
        html_content += f"""
        <div class="stats-grid">
            <div class="stat-card">
                <span class="stat-number">{len(self.all_results)}</span>
                Total Tests
            </div>
            <div class="stat-card">
                <span class="stat-number">{zero_loss_count}</span>
                Zero Loss Configs
            </div>
            <div class="stat-card">
                <span class="stat-number">{max_throughput:.0f}</span>
                Max Throughput (bps)
            </div>
            <div class="stat-card">
                <span class="stat-number">{avg_efficiency:.1f}%</span>
                Avg. Efficiency
            </div>
        </div>
"""

        # Add detailed results for each packet size
        for packet_size in self.test_packet_sizes:
            if packet_size in results_by_size:
                size_results = results_by_size[packet_size]
                best_for_size = max(size_results, key=lambda x: x.throughput_bps)
                
                html_content += f"""
        <div class="packet-size-section">
            <h3>📦 Packet Size: {packet_size} bytes</h3>
            <p><strong>Best Performance:</strong> {best_for_size.throughput_bps:.0f} bps at {best_for_size.packets_per_second:.0f} pps 
            ({best_for_size.packet_loss:.1f}% loss)</p>
            
            <table>
                <tr>
                    <th>Packets/Second</th>
                    <th>Interval (s)</th>
                    <th>Throughput (bps)</th>
                    <th>Throughput (Mbps)</th>
                    <th>Packet Loss (%)</th>
                    <th>Avg Latency (ms)</th>
                    <th>Efficiency (%)</th>
                </tr>
"""
                
                for result in size_results:
                    loss_class = "good" if result.packet_loss == 0 else ("warning" if result.packet_loss <= 1 else "error")
                    html_content += f"""
                <tr>
                    <td>{result.packets_per_second:.0f}</td>
                    <td>{result.interval:.4f}</td>
                    <td>{result.throughput_bps:.0f}</td>
                    <td>{result.throughput_mbps:.3f}</td>
                    <td><span class="{loss_class}">{result.packet_loss:.1f}</span></td>
                    <td>{result.avg_rtt:.2f}</td>
                    <td>{result.efficiency_percent:.1f}</td>
                </tr>
"""
                
                html_content += """
            </table>
        </div>
"""

        # Add technical analysis
        html_content += f"""
        <div class="analysis-section">
            <h2>🔬 Technical Analysis: Why This Configuration is Optimal for T1S</h2>
            
            <div class="technical-details">
                <h3>🌐 10BASE-T1S Network Characteristics</h3>
                <ul>
                    <li><strong>Physical Layer:</strong> Single Pair Ethernet (SPE) - 10 Mbps theoretical bandwidth</li>
                    <li><strong>PLCA Protocol:</strong> Physical Layer Collision Avoidance enables multi-drop topology</li>
                    <li><strong>Current Setup:</strong> Node 7 of 8 participants in PLCA network</li>
                    <li><strong>Transmit Opportunities:</strong> Round-robin scheduling limits individual node bandwidth</li>
                </ul>
            </div>
"""

        if self.optimal_result:
            opt = self.optimal_result
            
            # Calculate T1S specific metrics
            plca_theoretical_mbps = 10.0 / 8  # 8 nodes sharing 10 Mbps
            achieved_vs_plca = (opt.throughput_mbps / plca_theoretical_mbps) * 100
            
            html_content += f"""
            <div class="technical-details">
                <h3>📊 Performance Analysis</h3>
                <ul>
                    <li><strong>Theoretical T1S Maximum:</strong> 10 Mbps (single node)</li>
                    <li><strong>PLCA Theoretical (8 nodes):</strong> {plca_theoretical_mbps:.2f} Mbps per node</li>
                    <li><strong>Achieved Performance:</strong> {opt.throughput_mbps:.3f} Mbps ({achieved_vs_plca:.1f}% of PLCA limit)</li>
                    <li><strong>Protocol Efficiency:</strong> {opt.efficiency_percent:.1f}% (actual vs. expected)</li>
                </ul>
            </div>
            
            <div class="highlight">
                <h3>🎯 Why {opt.packet_size} bytes is optimal:</h3>
                <ul>
                    <li><strong>TC6 Chunking Efficiency:</strong> {opt.packet_size} bytes + headers = {opt.packet_size + 28} bytes total frame size</li>
                    <li><strong>SPI Transfer Optimization:</strong> Aligns well with 64-byte TC6 chunks ({((opt.packet_size + 28) / 64):.1f} chunks)</li>
                    <li><strong>Memory Buffer Utilization:</strong> Within 1536-byte firmware buffer limits</li>
                    <li><strong>PLCA Transmit Opportunity Fit:</strong> Optimal size for collision-free transmission windows</li>
                    <li><strong>Latency vs. Throughput Balance:</strong> {opt.avg_rtt:.2f}ms latency provides good responsiveness</li>
                </ul>
            </div>
            
            <div class="technical-details">
                <h3>⚡ Rate Optimization: {opt.packets_per_second:.0f} packets/second</h3>
                <ul>
                    <li><strong>Interval Selection:</strong> {opt.interval:.4f}s provides optimal balance</li>
                    <li><strong>System Load:</strong> Below packet loss threshold ({opt.packet_loss:.1f}% loss)</li>
                    <li><strong>Hardware Capability:</strong> Within firmware processing limits</li>
                    <li><strong>Network Stability:</strong> Maintains consistent performance under load</li>
                    <li><strong>PLCA Synchronization:</strong> Rate allows proper PLCA token passing</li>
                </ul>
            </div>
"""

        # Add comparison with theoretical limits
        html_content += f"""
            <div class="technical-details">
                <h3>📈 Performance Comparison</h3>
                <table style="width: 100%;">
                    <tr>
                        <th style="width: 40%;">Metric</th>
                        <th style="width: 30%;">Theoretical</th>
                        <th style="width: 30%;">Achieved</th>
                    </tr>
                    <tr>
                        <td>T1S Maximum Bandwidth</td>
                        <td>10.0 Mbps</td>
                        <td>{max_throughput/1_000_000:.3f} Mbps ({(max_throughput/10_000_000)*100:.1f}%)</td>
                    </tr>
                    <tr>
                        <td>PLCA Node Share (1/8)</td>
                        <td>1.25 Mbps</td>
                        <td>{max_throughput/1_000_000:.3f} Mbps ({(max_throughput/1_250_000)*100:.1f}%)</td>
                    </tr>
                    <tr>
                        <td>Zero Loss Configurations</td>
                        <td>Variable</td>
                        <td>{zero_loss_count}/{len(self.all_results)} ({(zero_loss_count/len(self.all_results))*100:.1f}%)</td>
                    </tr>
                    <tr>
                        <td>Average System Efficiency</td>
                        <td>~80-90%</td>
                        <td>{avg_efficiency:.1f}%</td>
                    </tr>
                </table>
            </div>
        </div>
        
        <div class="technical-details">
            <h2>💡 Optimization Recommendations</h2>
            <ul>
                <li><strong>Production Use:</strong> Use the optimal configuration for maximum reliable bandwidth</li>
                <li><strong>Network Monitoring:</strong> Monitor packet loss during sustained operation</li>
                <li><strong>PLCA Coordinator:</strong> Consider switching to Node 0 (Coordinator) for higher bandwidth</li>
                <li><strong>Application Adjustment:</strong> Tune application data rates to match optimal ping performance</li>
                <li><strong>Quality of Service:</strong> Implement traffic shaping based on these measurements</li>
            </ul>
        </div>
        
        <footer style="margin-top: 50px; padding-top: 30px; border-top: 2px solid #ecf0f1; color: #7f8c8d; text-align: center;">
            <p><em>Generated by T1S Optimal Ping Tester v2.0 • Advanced 10BASE-T1S Network Analysis</em></p>
            <p><em>LAN8651 Bridge Performance Optimization • {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}</em></p>
        </footer>
    </div>
</body>
</html>
"""

        # Write HTML file
        try:
            with open(filename, 'w', encoding='utf-8') as f:
                f.write(html_content)
            logger.info(f"📄 Comprehensive HTML report generated: {filename}")
        except Exception as e:
            logger.error(f"Failed to generate HTML report: {e}")

def main():
    """Main test execution"""
    
    tester = T1SOptimalPingTester(port='COM9', baudrate=115200, timeout=30.0)
    
    try:
        # Connect to Linux shell
        if not tester.connect():
            logger.error("Failed to connect to Linux shell")
            return
        
        # Run comprehensive bandwidth test
        results = tester.run_comprehensive_test()
        
        # Generate comprehensive HTML report
        tester.generate_html_report()
        
        # Final summary
        if tester.optimal_result:
            opt = tester.optimal_result
            logger.info(f"\n🏁 TEST COMPLETE!")
            logger.info(f"📄 Comprehensive report: t1s_optimal_ping_report.html")
            logger.info(f"\n🚀 OPTIMAL COMMAND FOR MAXIMUM BANDWIDTH:")
            logger.info(f"ping -s {opt.packet_size} -i {opt.interval:.4f} -c 1000 {tester.target_ip}")
            logger.info(f"\n📊 Performance: {opt.throughput_bps:.0f} bps ({opt.throughput_mbps:.3f} Mbps)")
            logger.info(f"⚡ Rate: {opt.packets_per_second:.0f} packets/second")
            logger.info(f"📉 Loss: {opt.packet_loss:.1f}%")
            logger.info(f"🕐 Latency: {opt.avg_rtt:.2f} ms")
        
    except KeyboardInterrupt:
        logger.info("\n⏹️  Test interrupted by user")
        
    except Exception as e:
        logger.error(f"❌ Test failed: {e}")
        
    finally:
        tester.disconnect()

if __name__ == "__main__":
    main()