#!/usr/bin/env python3

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
from datetime import datetime
import os
import sys
import warnings
warnings.filterwarnings('ignore')

# Set up plotting style
plt.style.use('default')
sns.set_palette("husl")

class FlightDataAnalyzer:
    def __init__(self, file_path):
        self.file_path = "/home/nyongesamutua/Documents/Nakuja/Nakuja_Recovery_Tests/Recovery_tests/telemetry_logs/telemetry_20250807_144646.csv"
        self.df = None
        self.load_data()
        
    def load_data(self):
        """Load and preprocess the flight data"""
        print("Loading flight data...")
        
        if not os.path.exists(self.file_path):
            raise FileNotFoundError(f"Data file not found: {self.file_path}")
        
        try:
            self.df = pd.read_csv(self.file_path)
            print(f"✓ Successfully loaded {len(self.df)} records from {self.file_path}")
        except Exception as e:
            print(f"✗ Error loading CSV file: {e}")
            sys.exit(1)
        
        # Check required columns
        required_columns = ['record_number', 'agl_altitude', 'kalman_altitude', 'kalman_vertical_velocity', 'state']
        missing_columns = [col for col in required_columns if col not in self.df.columns]
        if missing_columns:
            print(f"✗ Missing required columns: {missing_columns}")
            print(f"Available columns: {list(self.df.columns)}")
            sys.exit(1)
        
        # Convert timestamp to datetime if available
        if 'iso_timestamp' in self.df.columns:
            self.df['datetime'] = pd.to_datetime(self.df['iso_timestamp'])
            start_time = self.df['datetime'].min()
            self.df['time_seconds'] = (self.df['datetime'] - start_time).dt.total_seconds()
        elif 'timestamp' in self.df.columns:
            # Use Unix timestamp if available
            self.df['time_seconds'] = self.df['timestamp'] - self.df['timestamp'].min()
        else:
            # Create time based on record number
            self.df['time_seconds'] = (self.df['record_number'] - self.df['record_number'].min()) * 0.1  # assuming 10Hz
        
        print(f"✓ Data preprocessing completed")
    
    def show_plot_interactive(self):
        """Show plot in interactive mode with navigation controls"""
        plt.show(block=False)
        print("Plot displayed. Use the navigation toolbar to interact with the plot.")
        print("You can:")
        print("  - Use arrow buttons to navigate between views")
        print("  - Use home button to reset the view") 
        print("  - Use zoom and pan tools")
        print("  - Close the plot window when ready to continue...")
        input("Press Enter in this terminal to continue to next analysis...")
        plt.close()
    
    def check_data_quality(self):
        """Check data quality and identify missing records"""
        print("\n" + "="*50)
        print("DATA QUALITY ANALYSIS")
        print("="*50)
        
        # Check for missing record numbers
        min_record = self.df['record_number'].min()
        max_record = self.df['record_number'].max()
        expected_records = max_record - min_record + 1
        actual_records = len(self.df)
        missing_records = expected_records - actual_records
        
        print(f"Record number range: {min_record} to {max_record}")
        print(f"Expected records: {expected_records}")
        print(f"Actual records: {actual_records}")
        print(f"Missing records: {missing_records}")
        print(f"Data completeness: {actual_records/expected_records*100:.2f}%")
        
        if missing_records > 0:
            # Identify missing record numbers
            all_records = set(range(min_record, max_record + 1))
            actual_record_set = set(self.df['record_number'])
            missing_record_numbers = sorted(list(all_records - actual_record_set))
            if len(missing_record_numbers) > 10:
                print(f"First 10 missing records: {missing_record_numbers[:10]}")
            else:
                print(f"Missing records: {missing_record_numbers}")
        
        # Check for NaN values
        nan_counts = self.df.isnull().sum()
        nan_columns = nan_counts[nan_counts > 0]
        if len(nan_columns) > 0:
            print(f"\nColumns with missing values:")
            for col, count in nan_columns.items():
                print(f"  {col}: {count} NaN values ({count/len(self.df)*100:.1f}%)")
        else:
            print("✓ No missing values found")
        
        return missing_records
    
    def analyze_chute_deployment(self):
        """Analyze main and drogue chute deployment status"""
        print("\n" + "="*50)
        print("CHUTE DEPLOYMENT ANALYSIS")
        print("="*50)
        
        # Determine chute state columns
        chute_columns = []
        if 'pyro1_state' in self.df.columns and 'pyro2_state' in self.df.columns:
            chute_columns = [('pyro1_state', 'Drogue'), ('pyro2_state', 'Main')]
        elif 'drogue_pin_state' in self.df.columns and 'main_chute_pin_state' in self.df.columns:
            chute_columns = [('drogue_pin_state', 'Drogue'), ('main_chute_pin_state', 'Main')]
        else:
            print("No chute deployment columns found")
            return False, False
        
        drogue_deployed = False
        main_deployed = False
        
        for col, name in chute_columns:
            deployed = self.df[col].max() > 0
            if name == 'Drogue':
                drogue_deployed = deployed
            else:
                main_deployed = deployed
            
            print(f"{name} chute deployed: {'YES' if deployed else 'NO'}")
            
            if deployed:
                first_deploy = self.df[self.df[col] > 0].iloc[0]
                print(f"First {name.lower()} deployment at: {first_deploy['time_seconds']:.1f}s")
                print(f"  Altitude: {first_deploy.get('agl_altitude', first_deploy.get('kalman_altitude', 'N/A')):.1f}m")
                print(f"  Record: {first_deploy['record_number']}")
        
        # Plot chute deployment timeline
        if chute_columns:
            fig, axes = plt.subplots(len(chute_columns), 1, figsize=(12, 4*len(chute_columns)))
            if len(chute_columns) == 1:
                axes = [axes]
            
            for i, (col, name) in enumerate(chute_columns):
                axes[i].plot(self.df['time_seconds'], self.df[col], 'r-' if name == 'Drogue' else 'b-', 
                            linewidth=2, label=f'{name} Chute State')
                axes[i].set_ylabel(f'{name} State')
                axes[i].set_title(f'{name} Chute Deployment Status')
                axes[i].grid(True, alpha=0.3)
                axes[i].legend()
            
            axes[-1].set_xlabel('Time (seconds)')
            plt.tight_layout()
            self.show_plot_interactive()
        
        return drogue_deployed, main_deployed
    
    def plot_altitude_comparison(self):
        """Plot raw vs filtered altitude data"""
        print("\n" + "="*50)
        print("ALTITUDE ANALYSIS")
        print("="*50)
        
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 10))
        
        # Raw vs Kalman filtered altitude
        ax1.plot(self.df['time_seconds'], self.df['agl_altitude'], 'b-', alpha=0.7, 
                label='Raw Altitude', linewidth=1)
        ax1.plot(self.df['time_seconds'], self.df['kalman_altitude'], 'r-', 
                label='Kalman Filtered', linewidth=2)
        ax1.set_ylabel('Altitude (m)')
        ax1.set_title('Raw vs Kalman Filtered Altitude')
        ax1.legend()
        ax1.grid(True, alpha=0.3)
        
        # Altitude difference
        altitude_diff = self.df['kalman_altitude'] - self.df['agl_altitude']
        ax2.plot(self.df['time_seconds'], altitude_diff, 'g-', label='Kalman - Raw')
        ax2.set_ylabel('Altitude Difference (m)')
        ax2.set_xlabel('Time (seconds)')
        ax2.set_title('Altitude Filtering Difference')
        ax2.legend()
        ax2.grid(True, alpha=0.3)
        
        plt.tight_layout()
        self.show_plot_interactive()
        
        # Statistics
        max_altitude_raw = self.df['agl_altitude'].max()
        max_altitude_kalman = self.df['kalman_altitude'].max()
        
        print(f"Maximum Raw Altitude: {max_altitude_raw:.2f}m")
        print(f"Maximum Kalman Altitude: {max_altitude_kalman:.2f}m")
        print(f"Mean altitude difference: {altitude_diff.mean():.3f}m")
        print(f"Std of altitude difference: {altitude_diff.std():.3f}m")
        
        # Find apogee (maximum altitude)
        apogee_idx = self.df['kalman_altitude'].idxmax()
        apogee_record = self.df.loc[apogee_idx]
        print(f"Apogee detected at: {apogee_record['time_seconds']:.1f}s")
        print(f"Apogee altitude: {apogee_record['kalman_altitude']:.1f}m")
        print(f"Apogee record number: {apogee_record['record_number']}")
    
    def plot_velocity_analysis(self):
        """Plot velocity data"""
        print("\n" + "="*50)
        print("VELOCITY ANALYSIS")
        print("="*50)
        
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 10))
        
        # Kalman vertical velocity
        ax1.plot(self.df['time_seconds'], self.df['kalman_vertical_velocity'], 
                'purple', linewidth=2, label='Kalman Vertical Velocity')
        ax1.set_ylabel('Velocity (m/s)')
        ax1.set_title('Kalman Filtered Vertical Velocity')
        ax1.legend()
        ax1.grid(True, alpha=0.3)
        
        # Velocity histogram
        ax2.hist(self.df['kalman_vertical_velocity'], bins=50, alpha=0.7, 
                color='orange', edgecolor='black')
        ax2.set_xlabel('Vertical Velocity (m/s)')
        ax2.set_ylabel('Frequency')
        ax2.set_title('Vertical Velocity Distribution')
        ax2.grid(True, alpha=0.3)
        
        plt.tight_layout()
        self.show_plot_interactive()
        
        # Velocity statistics
        max_velocity = self.df['kalman_vertical_velocity'].max()
        min_velocity = self.df['kalman_vertical_velocity'].min()
        mean_velocity = self.df['kalman_vertical_velocity'].mean()
        
        print(f"Maximum velocity: {max_velocity:.2f} m/s")
        print(f"Minimum velocity: {min_velocity:.2f} m/s")
        print(f"Mean velocity: {mean_velocity:.2f} m/s")
        
        # Find velocity zero-crossing (apogee approximation)
        positive_velocity = self.df[self.df['kalman_vertical_velocity'] > 0]
        if len(positive_velocity) > 0:
            last_positive = positive_velocity.iloc[-1]
            print(f"Last positive velocity at: {last_positive['time_seconds']:.1f}s")
            print(f"  Altitude: {last_positive['kalman_altitude']:.1f}m")
    
    def analyze_flight_states(self):
        """Analyze flight state transitions"""
        print("\n" + "="*50)
        print("FLIGHT STATE ANALYSIS")
        print("="*50)
        
        # Flight states mapping (from your C++ code)
        state_mapping = {
            0: "PRE_FLIGHT_GROUND",
            1: "POWERED_FLIGHT", 
            2: "COASTING",
            3: "APOGEE",
            4: "DROGUE_DEPLOY",
            5: "DROGUE_DESCENT",
            6: "MAIN_DEPLOY",
            7: "MAIN_DESCENT",
            8: "POST_FLIGHT_GROUND"
        }
        
        # Count occurrences of each state
        state_counts = self.df['state'].value_counts().sort_index()
        
        print("Flight State Distribution:")
        for state, count in state_counts.items():
            state_name = state_mapping.get(state, f"UNKNOWN_{state}")
            percentage = count/len(self.df)*100
            print(f"  {state_name}: {count} records ({percentage:.1f}%)")
        
        # Plot state transitions
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 10))
        
        # State over time
        ax1.plot(self.df['time_seconds'], self.df['state'], 'o-', markersize=3, linewidth=1, alpha=0.7)
        ax1.set_ylabel('Flight State')
        ax1.set_xlabel('Time (seconds)')
        ax1.set_title('Flight State Transitions')
        
        # Set y-axis labels for states
        unique_states = sorted(self.df['state'].unique())
        ax1.set_yticks(unique_states)
        ax1.set_yticklabels([state_mapping.get(s, f"UNK_{s}") for s in unique_states])
        ax1.grid(True, alpha=0.3)
        
        # State with altitude
        scatter = ax2.scatter(self.df['time_seconds'], self.df['kalman_altitude'], 
                             c=self.df['state'], cmap='viridis', s=20, alpha=0.6)
        ax2.set_ylabel('Altitude (m)')
        ax2.set_xlabel('Time (seconds)')
        ax2.set_title('Flight States vs Altitude')
        ax2.grid(True, alpha=0.3)
        plt.colorbar(scatter, ax=ax2, label='Flight State')
        
        plt.tight_layout()
        self.show_plot_interactive()
        
        # Check state transitions
        visited_states = set(self.df['state'].unique())
        print(f"\nStates visited: {[state_mapping.get(s, f'UNK_{s}') for s in visited_states]}")
        
        # Check for expected state progression
        expected_order = [0, 1, 2, 3, 4, 5, 6, 7, 8]  # Expected state progression
        actual_states = self.df['state'].unique()
        
        print("✓ Flight state analysis completed")
    
    def plot_rssi_analysis(self):
        """Plot WiFi RSSI signal strength"""
        print("\n" + "="*50)
        print("RSSI ANALYSIS")
        print("="*50)
        
        if 'wifi_rssi' not in self.df.columns:
            print("No RSSI data available")
            return
        
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 10))
        
        # RSSI over time
        ax1.plot(self.df['time_seconds'], self.df['wifi_rssi'], 'g-', linewidth=2, label='WiFi RSSI')
        ax1.set_ylabel('RSSI (dBm)')
        ax1.set_title('WiFi Signal Strength Over Time')
        ax1.legend()
        ax1.grid(True, alpha=0.3)
        
        # RSSI vs Altitude
        scatter = ax2.scatter(self.df['wifi_rssi'], self.df['kalman_altitude'], 
                             c=self.df['time_seconds'], cmap='viridis', alpha=0.6)
        ax2.set_xlabel('RSSI (dBm)')
        ax2.set_ylabel('Altitude (m)')
        ax2.set_title('WiFi Signal Strength vs Altitude')
        ax2.grid(True, alpha=0.3)
        plt.colorbar(scatter, ax=ax2, label='Time (seconds)')
        
        plt.tight_layout()
        self.show_plot_interactive()
        
        # RSSI statistics
        avg_rssi = self.df['wifi_rssi'].mean()
        min_rssi = self.df['wifi_rssi'].min()
        max_rssi = self.df['wifi_rssi'].max()
        
        print(f"Average RSSI: {avg_rssi:.1f} dBm")
        print(f"Minimum RSSI: {min_rssi:.1f} dBm")
        print(f"Maximum RSSI: {max_rssi:.1f} dBm")
        
        # Signal quality assessment
        if avg_rssi > -60:
            quality = "Excellent"
        elif avg_rssi > -70:
            quality = "Good" 
        elif avg_rssi > -80:
            quality = "Fair"
        else:
            quality = "Poor"
        
        print(f"Signal Quality: {quality}")
    
    def generate_summary_report(self):
        """Generate a comprehensive summary report"""
        print("\n" + "="*60)
        print("FLIGHT DATA ANALYSIS SUMMARY REPORT")
        print("="*60)
        
        # Basic statistics
        duration = self.df['time_seconds'].max() - self.df['time_seconds'].min()
        avg_sample_rate = len(self.df) / duration if duration > 0 else 0
        
        print(f"Flight Duration: {duration:.1f} seconds")
        print(f"Total Records: {len(self.df)}")
        print(f"Average Sample Rate: {avg_sample_rate:.1f} Hz")
        print(f"Maximum Altitude: {self.df['kalman_altitude'].max():.1f} m")
        print(f"Maximum Velocity: {self.df['kalman_vertical_velocity'].max():.1f} m/s")
        
        # Operation mode summary
        if 'operation_mode' in self.df.columns:
            mode_counts = self.df['operation_mode'].value_counts()
            print(f"\nOperation Modes:")
            for mode, count in mode_counts.items():
                mode_name = "SAFE" if mode == 0 else "ARMED"
                print(f"  {mode_name}: {count} records")
        
        # Communication mode summary
        if 'communication_mode' in self.df.columns:
            comm_counts = self.df['communication_mode'].value_counts()
            print(f"\nCommunication Modes:")
            for mode, count in comm_counts.items():
                print(f"  {mode}: {count} records")
        
        # Battery status
        if 'battery_voltage' in self.df.columns:
            avg_battery = self.df['battery_voltage'].mean()
            min_battery = self.df['battery_voltage'].min()
            print(f"Average Battery Voltage: {avg_battery:.2f}V")
            print(f"Minimum Battery Voltage: {min_battery:.2f}V")
        
        print("\n" + "="*60)

    def run_complete_analysis(self):
        """Run all analysis methods"""
        try:
            print("🚀 Starting Flight Data Analysis")
            print("You will see interactive plots with navigation controls.")
            print("Use the toolbar at the bottom of each plot to:")
            print("  - Navigate between views (arrow buttons)")
            print("  - Zoom and pan")
            print("  - Reset view (home button)")
            print("  - Close plot window when ready to continue")
            print("-" * 60)
            
            self.check_data_quality()
            input("Press Enter to see chute deployment analysis...")
            
            self.analyze_chute_deployment()
            input("Press Enter to see altitude analysis...")
            
            self.plot_altitude_comparison()
            input("Press Enter to see velocity analysis...")
            
            self.plot_velocity_analysis()
            input("Press Enter to see flight state analysis...")
            
            self.analyze_flight_states()
            input("Press Enter to see RSSI analysis...")
            
            self.plot_rssi_analysis()
            self.generate_summary_report()
            
            print("✓ Analysis completed successfully!")
            print("📊 All plots have been displayed. Check the terminal for detailed analysis results.")
            
        except Exception as e:
            print(f"✗ Error during analysis: {e}")
            import traceback
            traceback.print_exc()

def main():
    """Main function to run the analysis"""
    # USE YOUR SPECIFIC FILE PATH HERE
    file_path = "/home/nyongesamutua/Documents/Nakuja/Nakuja_Recovery_Tests/Recovery_tests/telemetry_logs/telemetry_20250807_141824.csv"
    
    print(f"Looking for data file: {file_path}")
    
    if not os.path.exists(file_path):
        print(f"❌ File not found: {file_path}")
        print("Please check the file path and try again.")
        print(f"Current working directory: {os.getcwd()}")
        return
    
    try:
        analyzer = FlightDataAnalyzer(file_path)
        analyzer.run_complete_analysis()
    except Exception as e:
        print(f"Failed to analyze data: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()