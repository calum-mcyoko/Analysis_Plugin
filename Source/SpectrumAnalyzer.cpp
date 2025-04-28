#include "SpectrumAnalyzer.h"
#include "PluginProcessor.h"
#include "EQConstants.h" // Add this include

SpectrumAnalyzer::SpectrumAnalyzer(juce::AudioProcessor& p)
    : audioProcessor(p)
{
    // Start timer to update spectrum at 30fps
    startTimerHz(30);
}

SpectrumAnalyzer::~SpectrumAnalyzer()
{
    stopTimer();
}

void SpectrumAnalyzer::paint(juce::Graphics& g)
{
    // Fill background
    g.fillAll(juce::Colours::black);
    
    // Draw grid lines
    g.setColour(juce::Colours::darkgrey.withAlpha(0.5f));
    
    // Draw frequency grid lines
    auto sampleRate = audioProcessor.getSampleRate();
    if (sampleRate > 0)
    {
        for (auto freq : freqLines)
        {
            // Convert frequency to x position (logarithmic scale)
            float normX = std::log10(freq / 20.0f) / std::log10(20000.0f / 20.0f);
            float x = normX * getWidth();
            
            g.drawVerticalLine(static_cast<int>(x), 0.0f, static_cast<float>(getHeight()));
            
            // Draw frequency labels
            g.setColour(juce::Colours::grey);
            juce::String freqText;
            if (freq >= 1000)
                freqText = juce::String(freq / 1000) + "k";
            else
                freqText = juce::String(freq);
            
            g.drawText(freqText, static_cast<int>(x) - 10, getHeight() - 20, 20, 20,
                      juce::Justification::centred, false);
        }
    }
    
    // Draw dB grid lines
    g.setColour(juce::Colours::darkgrey.withAlpha(0.5f));
    for (int dB = -100; dB <= 0; dB += 10)
    {
        float y = juce::jmap(static_cast<float>(dB), -100.0f, 0.0f,
                           static_cast<float>(getHeight()), 0.0f);
        g.drawHorizontalLine(static_cast<int>(y), 0.0f, static_cast<float>(getWidth()));
        
        // Draw dB labels
        if (dB % 20 == 0)
        {
            g.setColour(juce::Colours::grey);
            g.drawText(juce::String(dB), 5, static_cast<int>(y) - 10, 30, 20,
                      juce::Justification::centredLeft, false);
        }
    }
    
    // Draw spectrum
    if (spectrumData.size() > 0) // Changed from !spectrumData.empty()
    {
        g.setColour(juce::Colours::cyan);
        
        juce::Path spectrumPath;
        const int numBins = static_cast<int>(spectrumData.size());
        
        spectrumPath.startNewSubPath(0, getHeight());
        
                for (int i = 0; i < numBins; ++i)
        {
            // Convert bin index to frequency (Hz)
            float binFreq = i * sampleRate / (2.0f * numBins);
            if (binFreq < 20.0f) binFreq = 20.0f; // Clamp to audible range
            
            // Map frequency to x position (logarithmic scale)
            float normX = std::log10(binFreq / 20.0f) / std::log10(20000.0f / 20.0f);
            float x = normX * getWidth();
            
            // Map magnitude to y position (dB scale)
            float y = juce::jmap(spectrumData[i], 0.0f, 1.0f, 
                                static_cast<float>(getHeight()), 0.0f);
            
            spectrumPath.lineTo(x, y);
        }   
        
        spectrumPath.lineTo(getWidth(), getHeight());
        spectrumPath.closeSubPath();
        
        // Fill spectrum with gradient
        juce::ColourGradient gradient(juce::Colours::cyan.withAlpha(0.8f), 0, 0,
                                    juce::Colours::blue.withAlpha(0.2f), 0, getHeight(),
                                    false);
        g.setGradientFill(gradient);
        g.fillPath(spectrumPath);
        
        // Draw outline
        g.setColour(juce::Colours::white);
        g.strokePath(spectrumPath, juce::PathStrokeType(1.0f));
    }
}

void SpectrumAnalyzer::resized()
{
    // Nothing to do here
}

// Update this method to use std::array instead of std::vector
void SpectrumAnalyzer::updateSpectrum(const std::array<float, EQConstants::fftSize / 2>& newData)
{
    spectrumData = newData;
    repaint();
}

void SpectrumAnalyzer::timerCallback()
{
    // Get spectrum data from processor
    auto* eqProcessor = dynamic_cast<EQAudioProcessor*>(&audioProcessor);
    if (eqProcessor != nullptr)
    {
        // Update to directly use the array returned by getSpectrumData
        const auto& newData = eqProcessor->getSpectrumData();
        updateSpectrum(newData);
    }
}
