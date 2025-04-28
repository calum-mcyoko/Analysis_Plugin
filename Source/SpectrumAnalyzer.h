#pragma once

#include <JuceHeader.h>
#include "EQConstants.h"
#include <array>

class SpectrumAnalyzer : public juce::Component, public juce::Timer
{
public:
    SpectrumAnalyzer(juce::AudioProcessor& processor);
    ~SpectrumAnalyzer() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    
    // Update to use std::array
    void updateSpectrum(const std::array<float, EQConstants::fftSize / 2>& newData);

private:
    juce::AudioProcessor& audioProcessor;
    
    // Update to use std::array
    std::array<float, EQConstants::fftSize / 2> spectrumData;
    
    // Frequency grid lines (Hz)
    const std::vector<float> freqLines = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzer)
};