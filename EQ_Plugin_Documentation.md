# Parametric EQ Plugin

## Overview

This parametric equalizer plugin provides precise frequency control with real-time spectrum analysis and comprehensive preset management. It's designed for both zero-latency operation and high-quality linear phase processing.

## Key Features

### Equalizer Processing

- **Multiple EQ Bands**: Includes several parametric bands with the following controls:
  - Low shelf filter (lowest band)
  - Parametric peak filters (middle bands)
  - High shelf filter (highest band)

- **Per-Band Controls**:
  - Frequency: Adjust the center/cutoff frequency
  - Gain: Boost or cut by adjustable amount in dB
  - Q: Control the bandwidth/resonance of each filter

- **Processing Modes**:
  - Zero Latency Mode: Minimum phase filters for real-time monitoring
  - Linear Phase Mode: Higher quality processing with latency compensation

- **Oversampling**: Improves audio quality by processing at higher sample rates internally

### Visual Feedback

- **EQ Visualizer**: 
  - Real-time display of the combined frequency response
  - Visual representation of each filter band's contribution
  - Interactive interface for adjusting filter parameters

- **Spectrum Analyzer**:
  - 30fps update rate for smooth visualization
  - FFT-based analysis of the audio signal
  - Overlay of spectrum and EQ curve for informed adjustments

### Preset Management

- **Preset Browser**:
  - Save custom EQ settings with personalized names
  - Load presets with single or double-click
  - Delete unwanted presets
  - Automatic change detection to prevent accidental data loss

- **Preset Handling**:
  - JSON-based preset format for compatibility and readability
  - Automatic preset directory management
  - Change tracking to prompt for saving modified settings

### Additional Tools

- **Test Signal Generator**:
  - Sine wave generation for testing and calibration
  - Adjustable frequency and amplitude
  - Useful for measuring and tuning system response

- **Metering**:
  - RMS level calculation for monitoring signal levels
  - Visual feedback on signal presence and processing

## Technical Details

### Signal Processing

- **Filter Implementation**:
  - IIR (Infinite Impulse Response) filters for efficient processing
  - Customized coefficients for different filter types
  - Parameter smoothing to prevent clicks and pops during adjustment

- **Optimization**:
  - Denormal prevention for CPU efficiency
  - Efficient processing even with multiple filter bands
  - Careful exception handling for stability

### User Interface

- **Interactive Controls**:
  - Responsive knobs and sliders for parameter adjustment
  - Visual feedback on parameter changes
  - Intuitive preset management interface

- **Visual Displays**:
  - Real-time spectrum analysis
  - Combined frequency response visualization
  - Clear indication of active filters and their effect

## Usage Tips

- Use the spectrum analyzer to identify problem frequencies in your audio
- Save different presets for different instruments or mix scenarios
- Switch between zero latency mode for tracking and linear phase mode for mixing
- Use the test signal generator to calibrate your monitoring system
- Make subtle adjustments rather than dramatic ones for more natural sound

## System Requirements

- Compatible with VST3 and AU plugin formats
- Works with all major DAWs
- Minimal CPU usage for efficient performance
- Windows and macOS compatible