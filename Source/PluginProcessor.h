/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/
#pragma once

#include <JuceHeader.h>
#include "EQConstants.h"

class EQAudioProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    EQAudioProcessor();
    ~EQAudioProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Update filters based on current parameter values
    void updateFilters();
    
    void updateFilterCoefficients();
    
    //==============================================================================
    // Helper methods for preset management
    bool createPresetFromAudioFile(const juce::File& audioFile, const juce::String& presetName = "");
    bool loadPresetFromJSON(const juce::File& jsonFile);
    bool loadPresetFromFile(const juce::File& presetFile);
    bool savePresetToJSON(const juce::File& jsonFile);
    juce::File getPresetsDirectory();
    juce::Array<juce::File> getAvailablePresets();

    //=============================================================================
    // Define the PresetMetadata struct properly
    struct PresetMetadata {
        float transientDensity = 0.5f;
        std::array<float, 2> frequencyRange = { 20.0f, 20000.0f };
        juce::String sourceFile;
        juce::String creationDate;
        std::array<float, EQConstants::numEQBands> spectralBalance = { 0.0f };
    };

    juce::String getPresetMetadataString() const
    {
        if (!hasEnhancedMetadata())
            return "No enhanced metadata available";
        
        juce::String result;
        
        if (currentPresetMetadata.sourceFile.isNotEmpty())
            result += "Source: " + currentPresetMetadata.sourceFile + "\n";
        
        if (currentPresetMetadata.creationDate.isNotEmpty())
            result += "Created: " + currentPresetMetadata.creationDate + "\n";
        
        result += "Frequency Range: " + 
            juce::String(int(currentPresetMetadata.frequencyRange[0])) + "Hz - " +
            juce::String(int(currentPresetMetadata.frequencyRange[1])) + "Hz\n";
            
        result += "Transient Density: " + 
            juce::String(int(currentPresetMetadata.transientDensity * 100)) + "%";
            
        return result;
    }

    //==============================================================================
    const PresetMetadata& getPresetMetadata() const { return currentPresetMetadata; }
    bool hasEnhancedMetadata() const { return currentPresetMetadata.sourceFile.isNotEmpty(); }
    
    // Access to parameters for the editor
    juce::AudioProcessorValueTreeState& getParameters() { return parameters; }
    
    // Use the constant from EQConstants
    std::array<juce::dsp::IIR::Coefficients<float>::Ptr, EQConstants::numEQBands> getFilterCoefficients() const;
    
    // Get current preset name
    juce::String getCurrentPresetName() const;

    void copyPythonScriptIfNeeded();
    
    // Report latency based on processing mode
    int getLatencySamples() const
    {
        if (zeroLatencyMode)
            return 0;
        else
            return 2048; // Example latency for linear phase mode - adjust based on your implementation
    }
    
    // Flag to indicate if a preset is currently being loaded
    bool m_isLoadingPreset = false;
    
    // Method to check if a preset is being loaded
    bool isLoadingPreset() const { return m_isLoadingPreset; }
    
    // Test signal methods
    void enableTestSignal(bool shouldEnable);
    void setTestSignalFrequency(float freq);
    void setTestSignalAmplitude(float amp);
    void setTestSignalType(int type);
    
    // Getters for UI
    bool isTestSignalEnabled() const { return testSignalEnabled; }
    float getTestSignalFrequency() const { return testSignalFrequency; }
    float getTestSignalAmplitude() const { return testSignalAmplitude; }
    int getTestSignalType() const { return testSignalType; }
    
    // Fixed: Use a single return type for getSpectrumData()
    const std::array<float, EQConstants::fftSize / 2>& getSpectrumData() const;

private:
    // Filter implementation using JUCE DSP module
    using Filter = juce::dsp::IIR::Filter<float>;
    using FilterDuplicator = juce::dsp::ProcessorDuplicator<Filter, juce::dsp::IIR::Coefficients<float>>;
    
    // Array of filters for each band
    std::array<FilterDuplicator, EQConstants::numEQBands> filters;
    
    // DSP processing spec
    juce::dsp::ProcessSpec spec;
    
    // EQ band parameters
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState parameters{*this, nullptr, "Parameters", createParameterLayout()};
    
    // Parameter smoothing
    std::array<juce::SmoothedValue<float>, EQConstants::numEQBands> smoothedFrequency;
    std::array<juce::SmoothedValue<float>, EQConstants::numEQBands> smoothedGain;
    std::array<juce::SmoothedValue<float>, EQConstants::numEQBands> smoothedQ;
    
    // Oversampling for better high-frequency response
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    
    // Current preset name
    juce::String currentPresetName;
    
    // Current preset metadata
    PresetMetadata currentPresetMetadata;
    
    // Linear phase processing
    bool useLinearPhase = false;
    std::vector<std::unique_ptr<juce::dsp::FFT>> fftObjects;
    std::array<std::vector<float>, EQConstants::numEQBands> fftBuffers;
    
    // Zero-latency mode flag
    bool zeroLatencyMode = true;
    
    // Parameter to control this from the UI
    std::atomic<bool> zeroLatencyModeParameter { true };
    
    // Parameter listener to update filters when parameters change
    struct ParameterListener : public juce::AudioProcessorParameter::Listener
    {
        ParameterListener(EQAudioProcessor& p) : processor(p) {}
        
        void parameterValueChanged(int parameterIndex, float newValue) override
        {
            juce::ignoreUnused(parameterIndex, newValue);
            
            // Skip individual updates when loading a preset (they'll be handled in batch)
            if (processor.m_isLoadingPreset)
                return;
            
            // Update filters when parameters change
            processor.updateFilters();
            
            // Check if the zero latency parameter changed
            auto* zeroLatencyParam = processor.parameters.getParameter("ZeroLatency");
            if (zeroLatencyParam != nullptr)
            {
                processor.zeroLatencyMode = zeroLatencyParam->getValue() > 0.5f;
            }
        }
        
        void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override
        {
            juce::ignoreUnused(parameterIndex, gestureIsStarting);
        }
        
        EQAudioProcessor& processor;
    };
    
    std::unique_ptr<ParameterListener> paramListener;
    
    // Helper method to set the current preset name
    void setCurrentPresetName(const juce::String& name) { currentPresetName = name; }
    
    std::unique_ptr<juce::dsp::FFT> fftAnalyzer;
    static constexpr int fftOrder = 11;
    // Use the constant from EQConstants instead of defining it here
    
    int fftBlockCounter = 0;
    int fftUpdateInterval = 4;
    
    std::vector<float> fftData = std::vector<float>(2 * EQConstants::fftSize, 0.0f);
    std::vector<float> fftWindow = std::vector<float>(EQConstants::fftSize, 0.0f);
    std::vector<float> audioFifo = std::vector<float>(EQConstants::fftSize * 2, 0.0f);
    int fifoIndex = 0;
    
    // Spectrum data with proper initialization
    std::array<float, EQConstants::fftSize / 2> spectrumData = {};
    juce::CriticalSection spectrumLock;
    
    // Method to safely initialize FFT analyzer
    void initializeFFTAnalyzer();
    
    // Test signal parameters with proper initialization
    bool testSignalEnabled = false;
    float testSignalFrequency = 440.0f;
    float testSignalAmplitude = 0.5f;
    int testSignalType = 0; // 0: Sine, 1: White Noise, 2: Pink Noise
    
    // Phase accumulator for sine wave generation
    float testSignalPhase = 0.0f;
    
    // Pink noise filter coefficients
    float pinkNoiseB0 = 0.0f, pinkNoiseB1 = 0.0f, pinkNoiseB2 = 0.0f;
    float pinkNoiseA1 = 0.0f, pinkNoiseA2 = 0.0f;
    float pinkNoiseX1 = 0.0f, pinkNoiseX2 = 0.0f;
    float pinkNoiseY1 = 0.0f, pinkNoiseY2 = 0.0f;
    
    // Thread safety for test signal
    juce::CriticalSection testSignalLock;
    
    // Pink noise generator state
    float pinkNoiseBuffer[7] = {0.0f};
    
    // Method to generate pink noise
    float generatePinkNoise();
    
    // Random number generator for noise
    juce::Random random;
    
    // Helper methods
    void generateTestSignal(juce::AudioBuffer<float>& buffer);
    void updateSpectrum(const juce::AudioBuffer<float>& buffer);
    void pushNextSampleIntoFifo(float sample);
    void calculateFFT();
    bool nextFFTBlockReady = false;
    
    bool extractPresetMetadata(const juce::var& jsonData);

    // Thread safety for filter updates
    juce::CriticalSection filterUpdateLock;

    juce::File getAnalyzerExecutable();

    
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQAudioProcessor)
};


