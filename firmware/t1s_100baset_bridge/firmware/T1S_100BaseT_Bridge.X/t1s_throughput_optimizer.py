#!/usr/bin/env python3
"""
T1S Network Throughput Optimizer v1.0
Systematically tests ping parameters to find maximum throughput without packet loss

Connects to Linux shell via COM port and runs optimized ping tests
to determine the sweet spot for 10BASE-T1S network performance.

Author: T1S Development Team  
Date: March 2026
Hardware: LAN8650/8651 T1S Bridge with Linux Host
"""

import serial
import time
import re
import logging
from typing import Dict, List, Tuple, Optional
from dataclasses import dataclass
import statistics

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    datefmt='%H:%M:%S'
)
logger = logging.getLogger(__name__)

@dataclass
class PingResult:
    """Ping test results"""
    packet_size: int
    interval: float
    count: int
    transmitted: int
    received: int
    packet_loss: float
    min_rtt: float
    avg_rtt: float
    max_rtt: float
    mdev_rtt: float
    total_time: float
    throughput_kbps: float
    
    @property
    def success_rate(self) -> float:
        return (self.received / self.transmitted) * 100 if self.transmitted > 0 else 0
    
    @property
    def throughput_bps(self) -> float:
        """Throughput in bits per second"""
        return self.throughput_kbps * 1024 * 8
    
    @property 
    def throughput_mbps(self) -> float:
        """Throughput in Mbps"""
        return self.throughput_bps / 1_000_000
    
    @property
    def throughput_bps(self) -> float:
        """Throughput in bits per second"""
        return self.throughput_kbps * 1024 * 8
    
    @property 
    def throughput_mbps(self) -> float:
        """Throughput in Mbps"""
        return self.throughput_bps / 1_000_000

@dataclass 
class TestConfig:
    """Test configuration parameters"""
    packet_size: int        # bytes
    interval: float         # seconds  
    count: int             # number of pings
    target_ip: str = "192.168.0.200"
    
    @property
    def expected_throughput_kbps(self) -> float:
        """Calculate expected throughput in KB/s"""
        bytes_per_packet = self.packet_size + 28  # IP + ICMP + Ethernet headers
        packets_per_sec = 1.0 / self.interval if self.interval > 0 else 1000
        return (bytes_per_packet * packets_per_sec) / 1024
    
    @property
    def expected_throughput_bps(self) -> float:
        """Calculate expected throughput in bits per second"""
        return self.expected_throughput_kbps * 1024 * 8
    
    @property
    def expected_throughput_bps(self) -> float:
        """Calculate expected throughput in bits per second"""
        return self.expected_throughput_kbps * 1024 * 8

class T1SThroughputOptimizer:
    """T1S Network Throughput Optimization Tool"""
    
    def __init__(self, port: str = 'COM9', baudrate: int = 115200, timeout: float = 30.0):
        """
        Initialize throughput optimizer
        
        Args:
            port: Serial port for Linux shell (e.g., 'COM9')
            baudrate: Serial communication speed
            timeout: Command timeout in seconds
        """
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.serial_conn = None
        self.results: List[PingResult] = []
        self.phase1_results: List[PingResult] = []
        self.phase2_results: List[PingResult] = []
        self.optimal_packet_size: int = 0
        
    def _generate_packet_size_tests(self) -> List[TestConfig]:
        """Phase 1: Generate packet size optimization tests"""
        configs = []
        
        # Test packet sizes from 64 to 1472 bytes (max Ethernet payload)
        # Use conservative interval to avoid losses during size optimization
        test_interval = 0.010  # 10ms = 100 packets/sec
        
        packet_sizes = [
            64, 100, 128, 200, 256, 300, 400, 500, 
            600, 700, 800, 900, 1000, 1100, 1200, 
            1300, 1400, 1472  # 1472 = max ping payload (1500 - 28 headers)
        ]
        
        for size in packet_sizes:
            configs.append(TestConfig(packet_size=size, interval=test_interval, count=50))
            
        logger.info(f"Phase 1: Generated {len(configs)} packet size tests")
        return configs
    
    def _generate_rate_optimization_tests(self, optimal_packet_size: int) -> List[TestConfig]:
        """Phase 2: Generate rate optimization tests with optimal packet size"""
        configs = []
        
        # Test increasing packet rates until losses occur
        # Start conservative, increase aggressively
        intervals = [
            0.100,  # 10 pps
            0.050,  # 20 pps  
            0.020,  # 50 pps
            0.010,  # 100 pps
            0.008,  # 125 pps
            0.006,  # 167 pps
            0.005,  # 200 pps
            0.004,  # 250 pps
            0.003,  # 333 pps
            0.002,  # 500 pps
            0.0015, # 667 pps
            0.001,  # 1000 pps
            0.0008, # 1250 pps
            0.0006, # 1667 pps
            0.0005, # 2000 pps
            0.0004, # 2500 pps (very aggressive)
        ]
        
        for interval in intervals:
            configs.append(TestConfig(packet_size=optimal_packet_size, interval=interval, count=100))
            
        logger.info(f"Phase 2: Generated {len(configs)} rate tests with {optimal_packet_size} byte packets")
        return configs
    
    def connect(self) -> bool:
        """Connect to Linux shell via serial"""
        try:
            self.serial_conn = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                timeout=self.timeout,
                bytesize=8,
                parity='N',
                stopbits=1
            )
            
            # Wait for connection
            time.sleep(0.5)
            
            # Send newline to get shell prompt
            self.serial_conn.write(b'\n')
            time.sleep(0.2)
            
            # Check if shell responds
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
        """Send command and wait for response"""
        if not self.serial_conn or not self.serial_conn.is_open:
            return ""
            
        try:
            # Clear input buffer
            self.serial_conn.reset_input_buffer()
            
            # Send command
            cmd = f"{command}\n"
            self.serial_conn.write(cmd.encode('utf-8'))
            
            # Wait for response  
            response = self._read_until_prompt()
            return response
            
        except Exception as e:
            logger.error(f"Command failed: {e}")
            return ""
    
    def _read_until_prompt(self) -> str:
        """Read serial data until shell prompt"""
        response = ""
        start_time = time.time()
        
        while time.time() - start_time < self.timeout:
            try:
                if self.serial_conn.in_waiting > 0:
                    data = self.serial_conn.read(self.serial_conn.in_waiting)
                    response += data.decode('utf-8', errors='ignore')
                    
                    # Check for shell prompts
                    if response.strip().endswith('#') or response.strip().endswith('$'):
                        break
                        
                time.sleep(0.01)
                
            except Exception as e:
                logger.error(f"Read error: {e}")
                break
                
        return response
    
    def _parse_ping_output(self, output: str, config: TestConfig) -> Optional[PingResult]:
        """Parse ping command output"""
        try:
            # Parse statistics line: "10 packets transmitted, 10 received, 0% packet loss, time 45ms"
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
            total_time = float(stats_match.group(4)) / 1000.0  # Convert to seconds
            
            # Parse RTT line: "rtt min/avg/max/mdev = 3.279/3.484/3.742/0.091 ms"
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
                # No RTT data if no packets received
                min_rtt = avg_rtt = max_rtt = mdev_rtt = 0.0
            
            # Calculate actual throughput
            bytes_per_packet = config.packet_size + 28  # Headers
            actual_throughput = (received * bytes_per_packet) / total_time / 1024 if total_time > 0 else 0
            
            return PingResult(
                packet_size=config.packet_size,
                interval=config.interval,
                count=config.count,
                transmitted=transmitted,
                received=received,
                packet_loss=packet_loss,
                min_rtt=min_rtt,
                avg_rtt=avg_rtt,
                max_rtt=max_rtt,
                mdev_rtt=mdev_rtt,
                total_time=total_time,
                throughput_kbps=actual_throughput
            )
            
        except Exception as e:
            logger.error(f"Parse error: {e}")
            return None
    
    def run_ping_test(self, config: TestConfig) -> Optional[PingResult]:
        """Run single ping test"""
        
        # Build ping command - using milliseconds for interval
        interval_ms = int(config.interval * 1000)
        if interval_ms < 1:
            interval_ms = 1
        
        ping_cmd = f"ping -s {config.packet_size} -i {config.interval:.3f} -c {config.count} {config.target_ip}"
        
        expected_bps = config.expected_throughput_bps
        logger.info(f"Testing: {config.packet_size}B, {config.interval:.3f}s interval, expected {expected_bps:.0f} bps ({expected_bps/1_000_000:.2f} Mbps)")
        
        # Send ping command
        output = self._send_command(ping_cmd)
        
        if not output:
            logger.warning("No ping output received")
            return None
        
        # Parse results
        result = self._parse_ping_output(output, config)
        
        if result:
            logger.info(f"  ✅ {result.received}/{result.transmitted} packets, "
                       f"{result.packet_loss:.1f}% loss, "
                       f"{result.avg_rtt:.2f}ms avg, "
                       f"{result.throughput_bps:.0f} bps ({result.throughput_mbps:.2f} Mbps)")
            self.results.append(result)
        else:
            logger.warning("  ❌ Failed to parse result")
        
        # Small delay between tests
        time.sleep(0.5)
        return result
    
    def run_optimization(self) -> Tuple[List[PingResult], List[PingResult]]:
        """Run 2-phase optimization: packet size then rate optimization"""
        
        logger.info("🚀 Starting T1S 2-Phase Throughput Optimization...")
        
        # Phase 1: Packet Size Optimization
        logger.info("\n" + "="*60)
        logger.info("📏 PHASE 1: PACKET SIZE OPTIMIZATION")
        logger.info("="*60)
        
        phase1_configs = self._generate_packet_size_tests()
        logger.info(f"Testing {len(phase1_configs)} packet sizes at 100 pps...")
        
        for i, config in enumerate(phase1_configs, 1):
            logger.info(f"\n--- Phase 1: Test {i}/{len(phase1_configs)} ---")
            result = self.run_ping_test(config)
            if result:
                self.phase1_results.append(result)
        
        # Find optimal packet size (best throughput with 0% loss)
        zero_loss_phase1 = [r for r in self.phase1_results if r.packet_loss == 0.0]
        
        if zero_loss_phase1:
            optimal_result = max(zero_loss_phase1, key=lambda x: x.throughput_bps)
            self.optimal_packet_size = optimal_result.packet_size
            logger.info(f"\n✅ PHASE 1 COMPLETE!")
            logger.info(f"🎯 Optimal packet size: {self.optimal_packet_size} bytes")
            logger.info(f"📊 Best throughput: {optimal_result.throughput_bps:.0f} bps ({optimal_result.throughput_mbps:.2f} Mbps)")
        else:
            # Fallback to best overall result
            best_result = max(self.phase1_results, key=lambda x: x.throughput_bps * (100 - x.packet_loss))
            self.optimal_packet_size = best_result.packet_size
            logger.warning(f"⚠️  No zero-loss results in Phase 1, using best overall: {self.optimal_packet_size} bytes")
        
        # Phase 2: Rate Optimization
        logger.info("\n" + "="*60)
        logger.info("⚡ PHASE 2: RATE OPTIMIZATION")
        logger.info("="*60)
        
        phase2_configs = self._generate_rate_optimization_tests(self.optimal_packet_size)
        logger.info(f"Testing {len(phase2_configs)} rates with {self.optimal_packet_size}-byte packets...")
        
        for i, config in enumerate(phase2_configs, 1):
            logger.info(f"\n--- Phase 2: Test {i}/{len(phase2_configs)} ---")
            result = self.run_ping_test(config)
            
            if result:
                self.phase2_results.append(result)
                
                # Stop if we hit significant packet loss
                if result.packet_loss > 5.0:
                    logger.info(f"🛑 Stopping Phase 2: Packet loss > 5% ({result.packet_loss:.1f}%)")
                    break
        
        logger.info(f"\n✅ PHASE 2 COMPLETE!")
        logger.info(f"📈 Tested {len(self.phase2_results)} rate configurations")
        
        # Combine all results
        self.results = self.phase1_results + self.phase2_results
        
        return self.phase1_results, self.phase2_results
    
    def generate_html_report(self, filename: str = "t1s_optimization_report.html"):
        """Generate comprehensive HTML report"""
        
        if not self.results:
            logger.warning("No results to generate report")
            return
            
        # Find best results
        zero_loss = [r for r in self.results if r.packet_loss == 0.0]
        best_throughput = max(self.results, key=lambda x: x.throughput_bps) if self.results else None
        best_zero_loss = max(zero_loss, key=lambda x: x.throughput_bps) if zero_loss else None
        
        html_content = f"""
<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>T1S Network Optimization Report</title>
    <style>
        body {{ font-family: Arial, sans-serif; margin: 40px; background: #f5f5f5; }}
        .container {{ max-width: 1200px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; }}
        h1 {{ color: #2c5aa0; border-bottom: 3px solid #2c5aa0; padding-bottom: 10px; }}
        h2 {{ color: #4a7c59; margin-top: 30px; }}
        .summary {{ background: #e8f4fd; border-left: 5px solid #2c5aa0; padding: 20px; margin: 20px 0; }}
        .optimal {{ background: #d4edda; border-left: 5px solid #28a745; padding: 20px; margin: 20px 0; }}
        .warning {{ background: #fff3cd; border-left: 5px solid #ffc107; padding: 20px; margin: 20px 0; }}
        table {{ width: 100%; border-collapse: collapse; margin: 20px 0; }}
        th, td {{ border: 1px solid #ddd; padding: 12px; text-align: left; }}
        th {{ background-color: #f8f9fa; font-weight: bold; }}
        tr:nth-child(even) {{ background-color: #f8f9fa; }}
        .metric {{ font-size: 1.2em; font-weight: bold; color: #2c5aa0; }}
        .good {{ color: #28a745; }}
        .warning-text {{ color: #dc3545; }}
        .ping-command {{ background: #f8f9fa; border: 1px solid #dee2e6; padding: 15px; margin: 10px 0; font-family: monospace; font-size: 14px; border-radius: 5px; }}
        .phase-section {{ margin: 30px 0; padding: 20px; border: 1px solid #dee2e6; border-radius: 8px; }}
    </style>
</head>
<body>
    <div class="container">
        <h1>🚀 T1S Network Optimization Report</h1>
        <p><strong>Generated:</strong> {time.strftime('%Y-%m-%d %H:%M:%S')}<br>
        <strong>Target:</strong> 192.168.0.200<br>
        <strong>Network:</strong> 10BASE-T1S über LAN8651 Bridge</p>

        <div class="summary">
            <h2>📊 Executive Summary</h2>
            <ul>
                <li><strong>Total Tests:</strong> {len(self.results)}</li>
                <li><strong>Phase 1 (Packet Size):</strong> {len(self.phase1_results)} tests</li>
                <li><strong>Phase 2 (Rate Optimization):</strong> {len(self.phase2_results)} tests</li>
                <li><strong>Zero Loss Configs:</strong> {len(zero_loss)} configurations</li>
                <li><strong>Optimal Packet Size:</strong> {self.optimal_packet_size} bytes</li>
            </ul>
        </div>
"""

        if best_zero_loss:
            pps = 1.0 / best_zero_loss.interval if best_zero_loss.interval > 0 else 0
            html_content += f"""
        <div class="optimal">
            <h2>🎯 OPTIMAL CONFIGURATION (Zero Loss)</h2>
            <div class="metric">Maximum Throughput: {best_zero_loss.throughput_bps:.0f} bps ({best_zero_loss.throughput_mbps:.2f} Mbps)</div>
            <ul>
                <li><strong>Packet Size:</strong> {best_zero_loss.packet_size} bytes</li>
                <li><strong>Rate:</strong> {pps:.0f} packets/second (interval: {best_zero_loss.interval:.4f}s)</li>
                <li><strong>Latency:</strong> {best_zero_loss.avg_rtt:.2f} ms ±{best_zero_loss.mdev_rtt:.2f} ms</li>
                <li><strong>Success Rate:</strong> {best_zero_loss.success_rate:.1f}%</li>
            </ul>
            
            <h3>🚀 Recommended Ping Command:</h3>
            <div class="ping-command">
                ping -s {best_zero_loss.packet_size} -i {best_zero_loss.interval:.4f} -c 1000 192.168.0.200
            </div>
            <p><em>This configuration achieves maximum bandwidth without packet loss.</em></p>
        </div>
"""
        elif best_throughput:
            html_content += f"""
        <div class="warning">
            <h2>⚠️ BEST AVAILABLE CONFIGURATION</h2>
            <p>No zero-loss configurations found. Best available:</p>
            <div class="metric">Maximum Throughput: {best_throughput.throughput_bps:.0f} bps ({best_throughput.throughput_mbps:.2f} Mbps)</div>
            <ul>
                <li><strong>Packet Size:</strong> {best_throughput.packet_size} bytes</li>
                <li><strong>Packet Loss:</strong> <span class="warning-text">{best_throughput.packet_loss:.1f}%</span></li>
                <li><strong>Latency:</strong> {best_throughput.avg_rtt:.2f} ms</li>
            </ul>
        </div>
"""

        # Phase 1 Results Table
        html_content += f"""
        <div class="phase-section">
            <h2>📏 Phase 1: Packet Size Optimization</h2>
            <p>Testing verschiedener Paketgrößen bei 100 pps (10ms Intervall)</p>
            <table>
                <tr>
                    <th>Packet Size (bytes)</th>
                    <th>Throughput (bps)</th>
                    <th>Throughput (Mbps)</th>
                    <th>Packet Loss (%)</th>
                    <th>Avg Latency (ms)</th>
                    <th>Success Rate (%)</th>
                </tr>
"""
        
        for result in sorted(self.phase1_results, key=lambda x: x.packet_size):
            loss_class = "good" if result.packet_loss == 0 else "warning-text"
            html_content += f"""
                <tr>
                    <td>{result.packet_size}</td>
                    <td>{result.throughput_bps:.0f}</td>
                    <td>{result.throughput_mbps:.3f}</td>
                    <td><span class="{loss_class}">{result.packet_loss:.1f}</span></td>
                    <td>{result.avg_rtt:.2f}</td>
                    <td>{result.success_rate:.1f}</td>
                </tr>
"""
        
        html_content += """
            </table>
        </div>
"""

        # Phase 2 Results Table  
        if self.phase2_results:
            html_content += f"""
        <div class="phase-section">
            <h2>⚡ Phase 2: Rate Optimization</h2>
            <p>Testing verschiedener Raten mit optimal {self.optimal_packet_size}-byte Paketen</p>
            <table>
                <tr>
                    <th>Packets/Second</th>
                    <th>Interval (s)</th>
                    <th>Throughput (bps)</th>
                    <th>Throughput (Mbps)</th>
                    <th>Packet Loss (%)</th>
                    <th>Avg Latency (ms)</th>
                    <th>Success Rate (%)</th>
                </tr>
"""
            
            for result in sorted(self.phase2_results, key=lambda x: x.interval, reverse=True):
                pps = 1.0 / result.interval if result.interval > 0 else 0
                loss_class = "good" if result.packet_loss == 0 else "warning-text"
                html_content += f"""
                <tr>
                    <td>{pps:.0f}</td>
                    <td>{result.interval:.4f}</td>
                    <td>{result.throughput_bps:.0f}</td>
                    <td>{result.throughput_mbps:.3f}</td>
                    <td><span class="{loss_class}">{result.packet_loss:.1f}</span></td>
                    <td>{result.avg_rtt:.2f}</td>
                    <td>{result.success_rate:.1f}</td>
                </tr>
"""
            
            html_content += """
            </table>
        </div>
"""

        # Network Analysis
        html_content += """
        <div class="phase-section">
            <h2>🔬 Network Analysis</h2>
            <ul>
                <li><strong>Physical Layer:</strong> 10BASE-T1S Single Pair Ethernet</li>
                <li><strong>PLCA Configuration:</strong> Node 7 von 8 (Follower)</li>
                <li><strong>Bridge Architecture:</strong> T1S ↔ Standard Ethernet</li>
                <li><strong>Protocol Stack:</strong> Ethernet → IP → ICMP (bidirectional ping)</li>
"""

        if zero_loss:
            avg_latency = sum(r.avg_rtt for r in zero_loss) / len(zero_loss)
            max_throughput = max(r.throughput_bps for r in zero_loss)
            efficiency = (max_throughput / 10_000_000) * 100  # vs 10 Mbps theoretical
            
            html_content += f"""
                <li><strong>Network Efficiency:</strong> {efficiency:.1f}% of theoretical 10BASE-T1S bandwidth</li>
                <li><strong>Average Latency:</strong> {avg_latency:.2f} ms (consistent PLCA performance)</li>
                <li><strong>Stability:</strong> Excellent (multiple zero-loss configurations)</li>
"""
        else:
            html_content += """
                <li><strong>Network Status:</strong> Under stress - no zero-loss configurations found</li>
                <li><strong>Recommendation:</strong> Consider lower data rates or network optimization</li>
"""

        html_content += """
            </ul>
        </div>

        <div class="phase-section">
            <h2>📋 Test Methodology</h2>
            <ol>
                <li><strong>Phase 1:</strong> Tested 18 packet sizes (64-1472 bytes) at fixed 100 pps rate</li>
                <li><strong>Phase 2:</strong> Tested up to 16 different rates using optimal packet size</li>
                <li><strong>Stop Condition:</strong> Testing stopped when packet loss exceeded 5%</li>
                <li><strong>Measurements:</strong> Each test used 50-100 ping packets for reliable statistics</li>
                <li><strong>Target:</strong> Find maximum sustainable bandwidth without packet loss</li>
            </ol>
        </div>

        <footer style="margin-top: 40px; padding-top: 20px; border-top: 1px solid #dee2e6; color: #6c757d;">
            <p><em>Generated by T1S Throughput Optimizer v1.0 • LAN8651 Bridge Performance Analysis</em></p>
        </footer>
    </div>
</body>
</html>
"""

        try:
            with open(filename, 'w', encoding='utf-8') as f:
                f.write(html_content)
            logger.info(f"📄 HTML report generated: {filename}")
        except Exception as e:
            logger.error(f"Failed to generate HTML report: {e}")
        
        return self.phase1_results, self.phase2_results
    
    def analyze_results(self) -> Dict:
        """Analyze test results and find optimal settings"""
        
        if not self.results:
            return {}
        
        # Filter results with zero packet loss
        zero_loss = [r for r in self.results if r.packet_loss == 0.0]
        low_loss = [r for r in self.results if r.packet_loss <= 1.0]
        
        logger.info(f"\n📊 ANALYSIS RESULTS:")
        logger.info(f"Total tests: {len(self.results)}")
        logger.info(f"Zero loss tests: {len(zero_loss)}")  
        logger.info(f"Low loss (≤1%) tests: {len(low_loss)}")
        
        if not zero_loss:
            logger.warning("⚠️  No tests achieved 0% packet loss!")
            analysis_set = low_loss if low_loss else self.results
            logger.info(f"Using low-loss results for analysis...")
        else:
            analysis_set = zero_loss
        
        if analysis_set:
            # Find best throughput
            best_throughput = max(analysis_set, key=lambda x: x.throughput_kbps)
            best_latency = min(analysis_set, key=lambda x: x.avg_rtt)
            best_stability = min(analysis_set, key=lambda x: x.mdev_rtt)
            
            # Calculate statistics
            throughputs = [r.throughput_bps for r in analysis_set]
            latencies = [r.avg_rtt for r in analysis_set]
            
            analysis = {
                'total_tests': len(self.results),
                'zero_loss_count': len(zero_loss),
                'best_throughput': best_throughput,
                'best_latency': best_latency, 
                'best_stability': best_stability,
                'avg_throughput': statistics.mean(throughputs),
                'max_throughput': max(throughputs),
                'avg_latency': statistics.mean(latencies),
                'min_latency': min(latencies),
                'recommended_config': best_throughput
            }
            
            # Print recommendations
            logger.info(f"\n🎯 OPTIMAL CONFIGURATION:")
            logger.info(f"Packet Size: {best_throughput.packet_size} bytes")
            logger.info(f"Interval: {best_throughput.interval:.3f} seconds") 
            logger.info(f"Throughput: {best_throughput.throughput_bps:.0f} bps ({best_throughput.throughput_mbps:.2f} Mbps)")
            logger.info(f"Latency: {best_throughput.avg_rtt:.2f} ms (±{best_throughput.mdev_rtt:.2f})")
            logger.info(f"Success Rate: {best_throughput.success_rate:.1f}%")
            
            logger.info(f"\n📈 PERFORMANCE SUMMARY:")
            logger.info(f"Max Throughput: {max(throughputs):.0f} bps ({max(throughputs)/1_000_000:.2f} Mbps)")
            logger.info(f"Avg Throughput: {statistics.mean(throughputs):.0f} bps ({statistics.mean(throughputs)/1_000_000:.2f} Mbps)")
            logger.info(f"Min Latency: {min(latencies):.2f} ms")
            logger.info(f"Avg Latency: {statistics.mean(latencies):.2f} ms")
            
            return analysis
        
        return {}
    
    def export_results(self, filename: str = "t1s_throughput_results.csv"):
        """Export results to CSV file"""
        try:
            import csv
            
            with open(filename, 'w', newline='', encoding='utf-8') as csvfile:
                fieldnames = [
                    'packet_size', 'interval', 'packets_per_second', 'count', 'transmitted', 'received',
                    'packet_loss', 'success_rate', 'min_rtt', 'avg_rtt', 'max_rtt', 
                    'mdev_rtt', 'total_time', 'throughput_bps', 'throughput_mbps', 'expected_throughput_bps'
                ]
                
                writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
                writer.writeheader()
                
                for result in self.results:
                    # Calculate expected throughput for comparison
                    expected_bps = TestConfig(result.packet_size, result.interval, result.count).expected_throughput_bps
                    pps = 1.0 / result.interval if result.interval > 0 else 0
                    
                    writer.writerow({
                        'packet_size': result.packet_size,
                        'interval': result.interval,
                        'packets_per_second': f"{pps:.1f}",
                        'count': result.count,
                        'transmitted': result.transmitted,
                        'received': result.received,
                        'packet_loss': result.packet_loss,
                        'success_rate': result.success_rate,
                        'min_rtt': result.min_rtt,
                        'avg_rtt': result.avg_rtt,
                        'max_rtt': result.max_rtt,
                        'mdev_rtt': result.mdev_rtt,
                        'total_time': result.total_time,
                        'throughput_bps': f"{result.throughput_bps:.0f}",
                        'throughput_mbps': f"{result.throughput_mbps:.3f}",
                        'expected_throughput_bps': f"{expected_bps:.0f}"
                    })
            
            logger.info(f"📄 Results exported to {filename}")
            
        except Exception as e:
            logger.error(f"Export failed: {e}")

def main():
    """Main optimization routine"""
    
    optimizer = T1SThroughputOptimizer(port='COM9', baudrate=115200, timeout=30.0)
    
    try:
        # Connect to Linux shell
        if not optimizer.connect():
            logger.error("Failed to connect to Linux shell")
            return
        
        # Run 2-phase optimization
        phase1_results, phase2_results = optimizer.run_optimization()
        
        # Analyze results
        analysis = optimizer.analyze_results()
        
        # Generate HTML report
        optimizer.generate_html_report()
        
        # Export CSV results
        optimizer.export_results()
        
        logger.info(f"\n🏁 2-Phase Optimization Complete!")
        
        if analysis and 'recommended_config' in analysis:
            rec = analysis['recommended_config']
            pps = 1.0 / rec.interval if rec.interval > 0 else 0
            
            logger.info(f"\n🚀 MAXIMUM BANDWIDTH PING COMMAND (Zero Loss):")
            logger.info(f"ping -s {rec.packet_size} -i {rec.interval:.4f} -c 1000 192.168.0.200")
            logger.info(f"")
            logger.info(f"📊 Performance: {rec.throughput_bps:.0f} bps ({rec.throughput_mbps:.2f} Mbps)")
            logger.info(f"⚡ Rate: {pps:.0f} packets/second")
            logger.info(f"🕐 Latency: {rec.avg_rtt:.2f} ms")
        
        logger.info(f"\n📄 Reports generated:")
        logger.info(f"   • HTML Report: t1s_optimization_report.html")
        logger.info(f"   • CSV Data: t1s_throughput_results.csv")
        
    except KeyboardInterrupt:
        logger.info("\n⏹️  Test interrupted by user")
        
    except Exception as e:
        logger.error(f"❌ Optimization failed: {e}")
        
    finally:
        optimizer.disconnect()

if __name__ == "__main__":
    main()