import librosa
import librosa.display
import numpy as np
import json
import sys
import os
import platform
import traceback
import datetime
from scipy import signal

def get_presets_directory():
    """
    Get the specific preset directory for the plugin
    """
    # Check if an output path was provided as a command line argument
    if len(sys.argv) > 3:
        output_dir = os.path.dirname(sys.argv[3])
        print(f"Using output directory from command line: {output_dir}")
        try:
            os.makedirs(output_dir, exist_ok=True)
            return output_dir
        except Exception as e:
            print(f"Cannot use specified output directory: {e}")
    
    # Use a Presets folder in the same directory as the script
    script_dir = os.path.dirname(os.path.abspath(__file__))
    presets_dir = os.path.join(script_dir, "Presets")
    
    # Create the directory if it doesn't exist
    try:
        os.makedirs(presets_dir, exist_ok=True)
        print(f"Using presets directory: {presets_dir}")
        
        # Test if we can write to this directory
        test_file = os.path.join(presets_dir, "test_write.tmp")
        try:
            with open(test_file, 'w') as f:
                f.write("test")
            os.remove(test_file)  # Clean up
            print("Write test successful")
        except Exception as e:
            print(f"Cannot write to directory: {e}")
            return os.getcwd()  # Fall back to current directory
            
        return presets_dir
    except Exception as e:
        print(f"Error creating presets directory: {e}")
        # If that fails, use current directory
        print("Using current directory for presets")
        return os.getcwd()
    
def apply_perceptual_weighting(spectrum, freqs):
    """Apply improved A-weighting to better match human hearing perception"""
    weighted_spectrum = np.copy(spectrum)
    
    # Use a more accurate A-weighting formula
    for i, f in enumerate(freqs):
        if f > 0:
            # More accurate A-weighting formula
            f2 = f * f
            A = 12200.0**2 * f2**2 / ((f2 + 20.6**2) * (f2 + 12200.0**2) * np.sqrt((f2 + 107.7**2) * (f2 + 737.9**2)))
            # Convert to dB
            A_db = 2.0 + 20.0 * np.log10(A)
            weighted_spectrum[i] += A_db
    
    # Apply additional psychoacoustic corrections
    # Boost low-mids slightly as they're often important for instrument body
    low_mid_mask = (freqs > 250) & (freqs < 800)
    weighted_spectrum[low_mid_mask] += 1.5
    
    # Slightly reduce harsh high frequencies
    high_freq_mask = (freqs > 6000) & (freqs < 10000)
    weighted_spectrum[high_freq_mask] -= 1.0
    
    return weighted_spectrum

def calculate_q_from_spectrum(spectrum, peak_idx, freqs):
    """Calculate Q based on the actual width of the peak/dip with improved accuracy"""
    if peak_idx <= 0 or peak_idx >= len(spectrum) - 1:
        return 1.0  # Default Q if peak is at the edge
    
    peak_val = spectrum[peak_idx]
    
    # Use a more musical -3dB point for bandwidth calculation
    half_power = peak_val - 3.0  # -3dB point
    
    # Find the lower -3dB point with interpolation for better accuracy
    lower_idx = peak_idx
    while lower_idx > 0 and spectrum[lower_idx] > half_power:
        lower_idx -= 1
    
    # Find the upper -3dB point with interpolation
    upper_idx = peak_idx
    while upper_idx < len(spectrum) - 1 and spectrum[upper_idx] > half_power:
        upper_idx += 1
    
    # If we couldn't find proper bandwidth points, use a more conservative Q
    if upper_idx <= lower_idx or upper_idx >= len(freqs) or lower_idx < 0:
        # Return a musically useful default based on frequency
        center_freq = freqs[peak_idx]
        if center_freq < 100:
            return 0.7  # Wider Q for sub-bass
        elif center_freq < 250:
            return 1.0  # Wide Q for bass
        elif center_freq < 2000:
            return 1.2  # Medium Q for mids
        else:
            return 1.5  # Narrower Q for highs
    
    center_freq = freqs[peak_idx]
    bandwidth = freqs[upper_idx] - freqs[lower_idx]
    
    if bandwidth <= 0:
        return 1.0  # Default Q for very narrow peaks
    
    # Calculate Q as center frequency divided by bandwidth
    q = center_freq / bandwidth
    
    # Apply frequency-dependent scaling to make Q more musical
    if center_freq < 100:
        q = q * 0.5  # Much wider Q for sub-bass
    elif center_freq < 250:
        q = q * 0.6  # Wider Q for bass
    elif center_freq < 800:
        q = q * 0.7  # Moderately wide Q for low-mids
    elif center_freq < 2500:
        q = q * 0.8  # Medium Q for mids
    elif center_freq < 5000:
        q = q * 0.9  # Slightly narrower Q for high-mids
    
    # Constrain to more musically useful values
    return min(max(q, 0.5), 4.0)  # Cap at 4.0 instead of 10.0 for more musical results

def interpolate_frequency(freq1, freq2, amp1, amp2, target_amp):
    """Interpolate to find the exact frequency at a specific amplitude"""
    if amp1 == amp2:
        return freq1
    
    # Linear interpolation
    t = (target_amp - amp1) / (amp2 - amp1)
    
    # Use logarithmic interpolation for frequencies
    log_freq1 = np.log10(freq1)
    log_freq2 = np.log10(freq2)
    log_freq = log_freq1 + t * (log_freq2 - log_freq1)
    
    return 10 ** log_freq

def calculate_reference_level(spectrum, freqs):
    """Calculate a better reference level using multiple frequency bands"""
    # Use multiple reference points for a more balanced reference
    ref_points = [
        (400, 600),     # Low mids reference
        (800, 1200),    # 1kHz region
        (2000, 3000)    # High mids reference
    ]
    
    ref_levels = []
    
    # Add reference points
    for low, high in ref_points:
        low_idx = np.argmin(np.abs(freqs - low))
        high_idx = np.argmin(np.abs(freqs - high))
        
        if low_idx < high_idx:
            band_spectrum = spectrum[low_idx:high_idx]
            if len(band_spectrum) > 0:
                # Use the median to avoid outliers
                ref_levels.append(np.median(band_spectrum))
    
    if ref_levels:
        # Weight the 1kHz region more heavily
        weights = [0.25, 0.5, 0.25]  # More weight to the middle band (1kHz region)
        return np.average(ref_levels, weights=weights)
    else:
        # Fallback to 0 if no valid references
        return 0
    
def get_musical_q(frequency, gain, is_peak=True):
    """
    Return a musically appropriate Q value based on frequency and gain
    """
    # Base Q values by frequency range - these are conservative defaults
    if frequency < 100:
        base_q = 0.7  # Sub bass - wide
    elif frequency < 250:
        base_q = 0.9  # Bass - moderately wide
    elif frequency < 800:
        base_q = 1.1  # Low mids - medium
    elif frequency < 2500:
        base_q = 1.3  # Mids - medium
    elif frequency < 5000:
        base_q = 1.5  # High mids - medium-narrow
    elif frequency < 10000:
        base_q = 1.8  # Presence - narrower
    else:
        base_q = 1.2  # Air - medium
    
    # Adjust based on gain amount (higher gain = lower Q for more natural sound)
    gain_factor = 1.0 - (min(abs(gain), 12.0) / 12.0) * 0.4  # Reduce Q by up to 40% for high gain
    
    # Adjust based on whether it's a peak or dip
    type_factor = 1.0 if is_peak else 0.8  # Wider Q for dips
    
    # Calculate final Q
    final_q = base_q * gain_factor * type_factor
    
    # Ensure Q is within reasonable bounds
    return min(max(final_q, 0.5), 3.0)   


def analyze_segments(y, sr, n_fft, n_segments=8):
    """Analyze different segments of the audio and combine results"""
    # IMPROVEMENT: Add overlap between segments for better analysis
    segment_length = len(y) // n_segments
    hop_length = segment_length // 2  # 50% overlap
    segment_spectra = []
    
    for i in range(n_segments * 2 - 1):  # More segments with overlap
        start = i * hop_length
        end = min(start + segment_length, len(y))
        segment = y[start:end]
        
        # Skip silent segments
        if np.mean(np.abs(segment)) < 0.01:
            continue
            
        # Apply a better window function before FFT
        windowed_segment = segment * np.hanning(len(segment))
        
        # Analyze this segment
        S = np.abs(librosa.stft(windowed_segment, n_fft=n_fft, hop_length=n_fft//4))
        S_db = librosa.amplitude_to_db(S, ref=np.max)
        segment_spectrum = np.mean(S_db, axis=1)
        segment_spectra.append(segment_spectrum)
    
    # Combine the segments (using median to reduce impact of outliers)
    if segment_spectra:
        return np.median(np.array(segment_spectra), axis=0)
    else:
        return np.zeros(n_fft // 2 + 1)

def detect_frequency_range(spectrum, freqs, threshold_db=-60):
    """Detect the effective frequency range of the audio"""
    # Find where the spectrum is above the threshold
    valid_indices = np.where(spectrum > threshold_db)[0]
    
    if len(valid_indices) > 0:
        min_idx = valid_indices[0]
        max_idx = valid_indices[-1]
        
        # Get the corresponding frequencies
        min_freq = max(20, freqs[min_idx])
        max_freq = min(20000, freqs[max_idx])
        
        return min_freq, max_freq
    else:
        # Default to full range if detection fails
        return 20, 20000

def analyze_spectral_balance(spectrum, freqs):
    """Analyze the spectral balance of the audio"""
    # Define frequency bands for analysis
    bands = [
        (20, 150, "Sub Bass"),
        (150, 400, "Bass"),
        (400, 800, "Low Mids"),
        (800, 2500, "Mids"),
        (2500, 5000, "High Mids"),
        (5000, 10000, "Presence"),
        (10000, 20000, "Air")
    ]
    
    band_levels = []
    
    for low, high, name in bands:
        low_idx = np.argmin(np.abs(freqs - low))
        high_idx = np.argmin(np.abs(freqs - high))
        
        if low_idx < high_idx:
            band_spectrum = spectrum[low_idx:high_idx]
            if len(band_spectrum) > 0:
                band_level = np.mean(band_spectrum)
                band_levels.append((name, band_level))
    
    # Find the average level across all bands
    if band_levels:
        avg_level = np.mean([level for _, level in band_levels])
        
        # Calculate deviation from average for each band
        balance_info = []
        for name, level in band_levels:
            deviation = level - avg_level
            balance_info.append((name, deviation))
        
        return balance_info
    else:
        return []
 
def analyze_transients(y, sr):
    """Analyze transient content to inform Q settings"""
    # Calculate the envelope
    envelope = np.abs(y)
    
    # Smooth the envelope
    window_size = int(sr * 0.01)  # 10ms window
    if window_size % 2 == 0:
        window_size += 1
    
    smoothed = signal.savgol_filter(envelope, window_size, 3)
    
    # Calculate the derivative of the envelope
    derivative = np.diff(smoothed)
    
    # Find positive peaks in the derivative (attack transients)
    peaks = signal.find_peaks(derivative, height=np.std(derivative) * 2)[0]
    
    # Calculate transient density
    if len(y) > 0:
        transient_density = len(peaks) / (len(y) / sr)  # transients per second
    else:
        transient_density = 0
    
    # Calculate a Q factor based on transient density
    # More transients = higher Q for better definition
    if transient_density > 10:  # High transient content
        transient_q_factor = 1.3
    elif transient_density > 5:  # Medium transient content
        transient_q_factor = 1.1
    else:  # Low transient content
        transient_q_factor = 0.9
        
    return transient_density, transient_q_factor

def analyze_audio(file_path, preset_name=None, output_path=None):
    """
    Analyze an audio file and extract frequency information for EQ settings
    """
    try:
        # Load the audio file
        print(f"Loading audio file: {file_path}")
        y, sr = librosa.load(file_path, sr=None)
        print(f"Audio loaded successfully: {len(y)} samples, {sr}Hz sample rate")
        
        # If no preset name is provided, use the filename without extension
        if preset_name is None:
            preset_name = os.path.splitext(os.path.basename(file_path))[0]
        
        print(f"Creating preset: {preset_name}")
        
        # Use very high resolution FFT for the entire spectrum
        n_fft = 32768  # Very high resolution for all frequencies
        
        # Check if we have enough samples for this FFT size
        if len(y) < n_fft:
            # Fall back to a smaller FFT size if needed
            n_fft = min(16384, len(y))
            print(f"Audio file too short for maximum resolution. Using n_fft={n_fft}")
        
        # Analyze transients to inform Q settings
        transient_density, transient_q_factor = analyze_transients(y, sr)
        print(f"Transient density: {transient_density:.2f} transients/sec, Q factor: {transient_q_factor:.2f}")
        
        # Analyze segments instead of the whole file at once for better representation
        print(f"Analyzing audio with FFT size: {n_fft}")
        avg_spectrum = analyze_segments(y, sr, n_fft, n_segments=8)
        
        # Get the frequencies
        freqs = librosa.fft_frequencies(sr=sr, n_fft=n_fft)
        
        # Smooth the spectrum to reduce noise
        # Use a window size proportional to the FFT size
        window_size = min(101, n_fft // 160)
        # Ensure window size is odd
        window_size = window_size if window_size % 2 == 1 else window_size + 1
        print(f"Using smoothing window size: {window_size}")
        
        avg_spectrum_smooth = signal.savgol_filter(avg_spectrum, window_size, 3)
        
        # Apply perceptual weighting to better match human hearing
        avg_spectrum_smooth = apply_perceptual_weighting(avg_spectrum_smooth, freqs)
        
        # Detect the effective frequency range of the audio
        min_freq, max_freq = detect_frequency_range(avg_spectrum_smooth, freqs)
        print(f"Detected frequency range: {min_freq:.1f}Hz - {max_freq:.1f}Hz")
        
        # Analyze spectral balance
        balance_info = analyze_spectral_balance(avg_spectrum_smooth, freqs)
        print("Spectral balance analysis:")
        for name, deviation in balance_info:
            print(f"  {name}: {deviation:.1f}dB")
        
        # Calculate reference level
        reference_level = calculate_reference_level(avg_spectrum_smooth, freqs)
        print(f"Reference level: {reference_level:.2f}dB")
        
        # Create a flat dictionary to match your plugin's parameter structure
        preset_data = {}
        
        # Define frequency ranges for each band (in Hz)
        freq_ranges = [
            (20, 150),      # Sub bass
            (150, 400),     # Bass
            (400, 800),     # Low mids
            (800, 2500),    # Mids
            (2500, 5000),   # High mids
            (5000, 10000),  # Presence
            (10000, 20000)  # Air
        ]
        
        # Add metadata to the preset
        preset_data["Metadata"] = {
            "CreatedBy": "PresetAnalyzer",
            "SourceFile": os.path.basename(file_path),
            "CreationDate": datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "TransientDensity": float(transient_density),
            "FrequencyRange": [float(min_freq), float(max_freq)]
        }
        
        for i, (low_freq, high_freq) in enumerate(freq_ranges):
            # Find indices corresponding to this frequency range
            low_idx = np.argmin(np.abs(freqs - low_freq))
            high_idx = np.argmin(np.abs(freqs - high_freq))
            
            # Get the spectrum for this range
            range_spectrum = avg_spectrum_smooth[low_idx:high_idx]
            freq_array = freqs[low_idx:high_idx]
            
            if len(range_spectrum) > 0:
                # Calculate the average level in this band
                band_avg = np.mean(range_spectrum)
                
                # Different analysis strategies based on frequency range
                if i <= 2:  # Bass bands (Sub bass, Bass, Low mids) - focus on peaks
                    # Find local maxima
                    peak_idx = np.argmax(range_spectrum)
                    freq = freq_array[peak_idx]
                    
                    # Calculate gain relative to reference level
                    gain = band_avg - reference_level
                    
                    # Adjust gain based on frequency range
                    if gain < 0:
                        gain = gain * 0.7  # Reduce cuts in bass
                    else:
                        gain = gain * 1.2  # Enhance boosts in bass
                    
                    # Add frequency-specific adjustments
                    if i == 0:  # Sub bass
                        # Make sub-bass adjustments more conservative to avoid muddiness
                        gain = gain * 0.9
                        # Use wider Q for sub-bass
                        q = calculate_q_from_spectrum(range_spectrum, peak_idx, freq_array) * 0.7
                        # Cap sub-bass Q at 0.8 for musical results
                        q = min(q, 0.8)
                    elif i == 1:  # Bass
                        # Keep bass adjustments as is
                        q = calculate_q_from_spectrum(range_spectrum, peak_idx, freq_array) * 0.8
                        # Cap bass Q at 1.2 for musical results
                        q = min(q, 1.2)
                    else:  # Low mids
                        # Be slightly more aggressive with low mids as they're important for clarity
                        if gain > 0:
                            gain = gain * 1.1
                        q = calculate_q_from_spectrum(range_spectrum, peak_idx, freq_array) * 0.9
                        # Cap low mids Q at 1.5 for musical results
                        q = min(q, 1.5)
                    
                else:  # Mid and high bands - look for both peaks and dips
                    # Find both maxima and minima
                    peak_idx = np.argmax(range_spectrum)
                    dip_idx = np.argmin(range_spectrum)
                    
                    peak_val = range_spectrum[peak_idx]
                    dip_val = range_spectrum[dip_idx]
                    
                    # Enhanced significance calculation
                    peak_significance = peak_val - band_avg
                    dip_significance = band_avg - dip_val
                    
                    # Add band-specific weighting
                    if i == 3:  # Mids (800-2500Hz) - critical for vocals and instruments
                        # Slightly favor fixing dips in the midrange for better clarity
                        dip_significance *= 1.15
                    elif i == 4:  # High mids (2500-5000Hz) - can be harsh
                        # Slightly favor fixing peaks in high mids to reduce harshness
                        peak_significance *= 1.1
                    elif i >= 5:  # Presence and Air
                        # For high frequencies, we're more concerned about peaks (harshness)
                        peak_significance *= 1.2
                    
                    if peak_significance > dip_significance:
                        # Peak is more significant
                        freq = freq_array[peak_idx]
                        gain = peak_val - reference_level
                        q = calculate_q_from_spectrum(range_spectrum, peak_idx, freq_array)
                        
                        # For peaks, use appropriate Q based on frequency band
                        if i == 3:  # Mids
                            q = min(q * 0.9, 1.8)  # Cap mids Q at 1.8
                        elif i == 4:  # High mids
                            q = min(q * 0.95, 2.0)  # Cap high mids Q at 2.0
                        elif i == 5:  # Presence
                            q = min(q, 2.5)  # Cap presence Q at 2.5
                        else:  # Air
                            q = min(q * 0.8, 1.5)  # Cap air Q at 1.5, use wider Q
                    else:
                        # Dip is more significant
                        freq = freq_array[dip_idx]
                        gain = dip_val - reference_level
                        q = calculate_q_from_spectrum(range_spectrum, dip_idx, freq_array)
                        
                        # For dips, use wider Q for more natural sound
                        q = q * 0.8  # Wider Q for dips sounds more natural
                        
                        # Apply band-specific Q caps for dips
                        if i == 3:  # Mids
                            q = min(q, 1.5)  # Wider Q for mid dips
                        elif i == 4:  # High mids
                            q = min(q, 1.7)  # Wider Q for high mid dips
                        elif i == 5:  # Presence
                            q = min(q, 2.0)  # Wider Q for presence dips
                        else:  # Air
                            q = min(q, 1.2)  # Wider Q for air dips
                    
                    # Preserve existing gain adjustment for high frequencies
                    if i >= 5:  # High frequencies
                        # High frequencies often need gentle treatment
                        gain = gain * 0.8
                    
                    # Additional band-specific gain adjustments
                    if i == 3:  # Mids
                        # Be more conservative with mid adjustments
                        gain = gain * 0.9
                    elif i == 4:  # High mids
                        # Be more conservative with high mid adjustments
                        gain = gain * 0.85
                
                # Adjust Q based on gain amount - higher gain needs wider Q
                if abs(gain) > 10:
                    q = q * 0.6  # Significantly reduce Q for very high gain
                elif abs(gain) > 6:
                    q = q * 0.8  # Moderately reduce Q for high gain
                
                # Apply the transient-based Q adjustment
                q = q * transient_q_factor
                
                # Ensure gain is within reasonable limits
                gain = max(min(gain, 6), -12)
                
                # Ensure Q is within reasonable limits
                q = max(min(q, 2.0), 0.5)

                print(f"Band {i}: Freq={freq:.1f}Hz, Gain={gain:.1f}dB, Q={q:.2f}")

                # Store parameters using the same IDs as your plugin
                preset_data[f"Frequency{i}"] = normalize_frequency(freq)
                preset_data[f"Gain{i}"] = normalize_gain(gain)
                preset_data[f"Q{i}"] = normalize_q(q)
            else:
                # Fallback to center frequency if range is empty
                freq = np.sqrt(low_freq * high_freq)
                gain = 0.0
                q = 1.0
                
                preset_data[f"Frequency{i}"] = normalize_frequency(freq)
                preset_data[f"Gain{i}"] = normalize_gain(gain)
                preset_data[f"Q{i}"] = normalize_q(q)
        
        # Add ZeroLatency parameter (default to true)
        preset_data["ZeroLatency"] = 1.0
        
        # Determine the output path
        if output_path is None:
            # Get the presets directory
            presets_dir = get_presets_directory()
            
            # Define the output file path with _preset suffix to match what the plugin expects
            output_path = os.path.join(presets_dir, f"{preset_name}_preset.json")
        
        # Save to file with better error handling
        print(f"Saving preset to: {output_path}")
        try:
            # First check if directory exists and is writable
            output_dir = os.path.dirname(output_path)
            print(f"Output directory exists: {os.path.exists(output_dir)}")
            print(f"Output directory is writable: {os.access(output_dir, os.W_OK)}")
            
            # MUSICAL IMPROVEMENT: Final pass to ensure Q values are musically balanced
            actual_q_values = []
            for i in range(7):  # 7 bands
                normalized_q = preset_data[f"Q{i}"]
                # Convert from normalized 0-1 to actual Q value (0.1 to 10)
                actual_q = 0.1 * pow(10, normalized_q * (np.log10(10) - np.log10(0.1)))
                actual_q_values.append(actual_q)

            print("Original Q values:", [f"{q:.2f}" for q in actual_q_values])

            # Check if any Q values are too high
            if max(actual_q_values) > 2.0:  # Lower the threshold from 3.0 to 2.0
                # Scale down all Q values proportionally
                scale_factor = 2.0 / max(actual_q_values)
                for i in range(7):
                    # Scale the actual Q value
                    new_actual_q = actual_q_values[i] * scale_factor
                    # Convert back to normalized 0-1 value
                    new_normalized_q = (np.log10(new_actual_q) - np.log10(0.1)) / (np.log10(10) - np.log10(0.1))
                    preset_data[f"Q{i}"] = float(max(0.0, min(1.0, new_normalized_q)))
                
                # Print the adjusted Q values
                adjusted_q_values = [0.1 * pow(10, preset_data[f"Q{i}"] * (np.log10(10) - np.log10(0.1))) for i in range(7)]
                print("Adjusted Q values:", [f"{q:.2f}" for q in adjusted_q_values])
            # Test JSON serialization before opening file
            json_string = json.dumps(preset_data, indent=2)
            
            # Save the file
            with open(output_path, 'w') as f:
                f.write(json_string)
                f.flush()  # Ensure data is written to disk
            
            print(f"Preset successfully saved to {output_path}")
            return output_path
        except PermissionError:
            # Try saving to a location we definitely have access to
            fallback_path = os.path.join(os.getcwd(), f"{preset_name}_preset.json")
            print(f"Permission error. Trying fallback location: {fallback_path}")
            with open(fallback_path, 'w') as f:
                json.dump(preset_data, f, indent=2)
            print(f"Preset saved to fallback location: {fallback_path}")
            return fallback_path
        except Exception as e:
            print(f"Error saving preset: {str(e)}")
            traceback.print_exc()
            return None
    
    except Exception as e:
        print(f"Error analyzing audio: {e}")
        traceback.print_exc()
        return None


def normalize_frequency(freq):
    """
    Convert frequency to 0-1 range for the plugin
    Assuming frequency range of 20Hz to 20kHz
    """
    min_freq = 20.0
    max_freq = 20000.0
    # Use logarithmic scaling
    normalized = (np.log10(freq) - np.log10(min_freq)) / (np.log10(max_freq) - np.log10(min_freq))
    return float(max(0.0, min(1.0, normalized)))


def normalize_gain(gain):
    """
    Convert gain to 0-1 range for the plugin
    Assuming gain range of -24dB to +24dB
    """
    min_gain = -24.0
    max_gain = 24.0
    normalized = (gain - min_gain) / (max_gain - min_gain)
    return float(max(0.0, min(1.0, normalized)))


def normalize_q(q):
    min_q = 0.1
    max_q = 10.0
    normalized = (np.log10(q) - np.log10(min_q)) / (np.log10(max_q) - np.log10(min_q))
    normalized = float(max(0.0, min(1.0, normalized)))
    print(f"Converting Q: {q:.2f} → normalized: {normalized:.4f}")
    return normalized


if __name__ == "__main__":
    try:
        # Print system information for debugging
        print(f"===== SCRIPT STARTED =====")
        print(f"Python version: {sys.version}")
        print(f"Current working directory: {os.getcwd()}")
        print(f"Script location: {os.path.abspath(__file__)}")
        
        if len(sys.argv) < 2:
            print("Usage: python PresetAnalyzer.py <audio_file> [preset_name] [output_path]")
            sys.exit(1)
        
        print(f"Command line arguments: {sys.argv}")
        
        audio_file = sys.argv[1]
        preset_name = sys.argv[2] if len(sys.argv) > 2 else None
        output_path = sys.argv[3] if len(sys.argv) > 3 else None
        
        # Check if audio file exists and is readable
        if not os.path.exists(audio_file):
            print(f"Error: Audio file not found: {audio_file}")
            sys.exit(1)
        
        if not os.access(audio_file, os.R_OK):
            print(f"Error: Audio file is not readable: {audio_file}")
            sys.exit(1)
            
        print(f"Audio file exists and is readable")
        
        # Process the audio file
        output_path = analyze_audio(audio_file, preset_name, output_path)
        
        # Check the result
        if output_path:
            print(f"Success! Preset created at: {output_path}")
            # Verify the file was actually created
            if os.path.exists(output_path):
                print(f"Verified: File exists at {output_path}")
                sys.exit(0)
            else:
                print(f"Error: File was reported as created but doesn't exist at {output_path}")
                sys.exit(1)
        else:
            print("Failed to create preset.")
            sys.exit(1)
    
    except Exception as e:
        print(f"Unexpected error: {e}")
        traceback.print_exc()
        sys.exit(1)