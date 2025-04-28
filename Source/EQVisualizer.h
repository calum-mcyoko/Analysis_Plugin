#pragma once

#include <JuceHeader.h>
#include "EQConstants.h"
#include "PluginProcessor.h"

class EQVisualizer : public juce::Component,
                     public juce::Timer
{
public:
    EQVisualizer(EQAudioProcessor& processor);
    ~EQVisualizer() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    
    // Timer callback
    void timerCallback() override;
    
    // Update methods
    void updateFilters(const std::array<juce::dsp::IIR::Coefficients<float>::Ptr, EQConstants::numEQBands>& newFilters);
    void updateSpectrum(const std::array<float, EQConstants::fftSize / 2>& newSpectrumData);
    
    // Force a complete update of the visualizer
    void forceUpdate();

private:
    // Helper methods for drawing
    void drawGrid(juce::Graphics& g);
    
    // Calculate the frequency response curve for visualization
    void calculateResponseCurve();
    void calculateFilterResponse();
    
    // Helper methods for frequency response calculation
    float getMagnitudeResponse(float frequency);
    float getMagnitudeForFrequency(float frequency);
    
    // Helper method to get frequency response across the spectrum
    void getFrequencyResponse(const juce::dsp::IIR::Coefficients<float>& coefficients, 
                             double sampleRate, 
                             std::vector<double>& frequencies, 
                             std::vector<double>& magnitudes);
    
    // Helper method to map frequency to x-coordinate
    float getFrequencyPosition(float frequency) const;
    
    // Helper method to map gain to y-coordinate
    float getGainPosition(float gain) const;
    
    // Helper method to check if two coefficient sets are equal
    bool coefficientsEqual(const juce::dsp::IIR::Coefficients<float>::Ptr& a, 
                          const juce::dsp::IIR::Coefficients<float>::Ptr& b);
    
    // Store filter coefficients for visualization
    std::array<juce::dsp::IIR::Coefficients<float>::Ptr, EQConstants::numEQBands> filterCoefficients;
    
    // Store the calculated frequency response
    std::array<float, 512> frequencyResponse;
    
    // Store the magnitude data for visualization
    std::array<float, 512> magnitudeData;
    
    // Store the spectrum data for visualization
    std::array<float, EQConstants::fftSize / 2> spectrumData;
    
    // Flag to indicate if spectrum data is available
    bool hasSpectrum = false;
    
    // Reference to the processor
    EQAudioProcessor& audioProcessor;
    
    // Mutex for thread safety
    juce::CriticalSection lock;
    
    // Frequency lines for grid
    std::vector<float> freqLines = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQVisualizer)
};
