#include "EQVisualizer.h"

EQVisualizer::EQVisualizer(EQAudioProcessor& processor)
    : audioProcessor(processor)
{
    // Initialize filters array with nullptr
    for (auto& coef : filterCoefficients)
    {
        coef = nullptr;
    }
    
    // Initialize frequency response array
    std::fill(frequencyResponse.begin(), frequencyResponse.end(), 0.0f);
    
    // Initialize magnitude data array
    std::fill(magnitudeData.begin(), magnitudeData.end(), -100.0f);
    
    // Initialize spectrum data array
    std::fill(spectrumData.begin(), spectrumData.end(), -100.0f);
    
    // Set up timer for periodic updates
    startTimerHz(30); // Update 30 times per second
}

EQVisualizer::~EQVisualizer()
{
    // Stop the timer when the visualizer is destroyed
    stopTimer();
}

void EQVisualizer::paint(juce::Graphics& g)
{
    // Fill background
    g.fillAll(juce::Colours::black);
    
    // Draw the grid
    drawGrid(g);
    
    // Draw the frequency response curve
    {
        juce::ScopedLock scopedLock(lock);
        
        // Check if we have valid filter coefficients
        bool hasValidFilters = false;
        for (const auto& coef : filterCoefficients)
        {
            if (coef != nullptr)
            {
                hasValidFilters = true;
                break;
            }
        }
        
        if (hasValidFilters)
        {
            g.setColour(juce::Colours::white);
            
            juce::Path responseCurve;
            
            const double outputMin = getHeight();
            const double outputMax = 0.0;
            
            auto map = [outputMin, outputMax](double input)
            {
                return juce::jmap(input, -24.0, 24.0, outputMin, outputMax);
            };
            
            const int numPoints = frequencyResponse.size();
            responseCurve.startNewSubPath(0, map(getMagnitudeResponse(20.0f)));
            
            for (int i = 1; i < numPoints; ++i)
            {
                double freq = 20.0 * std::pow(2.0, i / (numPoints / 10.0));
                responseCurve.lineTo(getWidth() * getFrequencyPosition(freq), 
                                    map(getMagnitudeResponse(freq)));
            }
            
            g.strokePath(responseCurve, juce::PathStrokeType(2.0f));
        }
    }
    
    // Draw the spectrum analyzer data if available
    if (hasSpectrum)
    {
        g.setColour(juce::Colours::green.withAlpha(0.5f));
        
        juce::Path spectrumPath;
        const int numPoints = static_cast<int>(spectrumData.size());
        
        spectrumPath.startNewSubPath(0, getHeight());
        
        for (int i = 0; i < numPoints; ++i)
        {
            float freq = EQConstants::minFreq * std::pow(EQConstants::maxFreq / EQConstants::minFreq, i / (float)(numPoints - 1));
            float x = getWidth() * getFrequencyPosition(freq);
            
            // Map the spectrum data to the y-axis (dB scale)
            float y = juce::jmap(spectrumData[i], -100.0f, 0.0f, static_cast<float>(getHeight()), 0.0f);
            
            spectrumPath.lineTo(x, y);
        }
        
        spectrumPath.lineTo(getWidth(), getHeight());
        
        g.fillPath(spectrumPath);
    }
}

void EQVisualizer::resized()
{
    // Recalculate the response curve when the component is resized
    calculateResponseCurve();
}

void EQVisualizer::forceUpdate()
{
    // Force recalculation of the response curve
    calculateResponseCurve();
    
    // Trigger a repaint
    repaint();
}

float EQVisualizer::getMagnitudeResponse(float frequency)
{
    // Calculate the combined magnitude response of all filters at the given frequency
    float magnitude = 0.0f;
    
    // Get the sample rate from the processor
    double sampleRate = audioProcessor.getSampleRate();
    
    // Calculate the response for each filter
    for (int i = 0; i < EQConstants::numEQBands; ++i)
    {
        if (filterCoefficients[i] != nullptr)
        {
            // Calculate the phase angle
            double omega = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
            
            // Calculate the complex response manually
            // For a biquad filter with coefficients b0, b1, b2, a0, a1, a2
            // H(z) = (b0 + b1*z^-1 + b2*z^-2) / (a0 + a1*z^-1 + a2*z^-2)
            // where z = e^(jω)
            
            const float* coeffs = filterCoefficients[i]->getRawCoefficients();
            
            // For a second-order IIR filter, the coefficients are arranged as:
            // b0, b1, b2, a0, a1, a2 (where a0 is usually normalized to 1.0)
            double b0 = static_cast<double>(coeffs[0]);
            double b1 = static_cast<double>(coeffs[1]);
            double b2 = static_cast<double>(coeffs[2]);
            double a0 = 1.0; // Normalized
            double a1 = static_cast<double>(coeffs[3]);
            double a2 = static_cast<double>(coeffs[4]);
            
            // Calculate z = e^(jω)
            std::complex<double> z = std::exp(std::complex<double>(0.0, omega));
            std::complex<double> z_1 = 1.0 / z; // z^-1
            std::complex<double> z_2 = z_1 * z_1; // z^-2
            
            // Calculate H(z)
            std::complex<double> numerator = b0 + (b1 * z_1) + (b2 * z_2);
            std::complex<double> denominator = a0 + (a1 * z_1) + (a2 * z_2);
            std::complex<double> response = numerator / denominator;
            
            // Convert to magnitude in dB
            double magDB = 20.0 * std::log10(std::abs(response));
            
            // Add to the total magnitude
            magnitude += static_cast<float>(magDB);
        }
    }
    
    return magnitude;
}

bool EQVisualizer::coefficientsEqual(const juce::dsp::IIR::Coefficients<float>::Ptr& a, 
    const juce::dsp::IIR::Coefficients<float>::Ptr& b)
{
// If both are null, they're equal
if (a == nullptr && b == nullptr)
return true;

// If only one is null, they're not equal
if (a == nullptr || b == nullptr)
return false;

// Compare the actual coefficients
return a->getRawCoefficients() == b->getRawCoefficients();
}

void EQVisualizer::updateFilters(const std::array<juce::dsp::IIR::Coefficients<float>::Ptr, EQConstants::numEQBands>& newFilters)
{
    juce::ScopedLock scopedLock(lock);
    
    // Check if filters have changed
    bool filtersChanged = false;
    
    for (int i = 0; i < EQConstants::numEQBands; ++i)
    {
        if (!coefficientsEqual(filterCoefficients[i], newFilters[i]))
        {
            filterCoefficients[i] = newFilters[i];
            filtersChanged = true;
        }
    }
    
    // Recalculate the response curve if filters have changed
    if (filtersChanged)
    {
        calculateResponseCurve();
        repaint(); // Force a repaint to update the display
    }
}

void EQVisualizer::updateSpectrum(const std::array<float, EQConstants::fftSize / 2>& newSpectrumData)
{
    juce::ScopedLock scopedLock(lock);
    
    // Copy the new spectrum data
    std::copy(newSpectrumData.begin(), newSpectrumData.end(), spectrumData.begin());
    
    // Set the flag to indicate that spectrum data is available
    hasSpectrum = true;
}

void EQVisualizer::getFrequencyResponse(const juce::dsp::IIR::Coefficients<float>& coefficients, 
                                      double sampleRate, 
                                      std::vector<double>& frequencies, 
                                      std::vector<double>& magnitudes)
{
    // Implementation of frequency response calculation
    
    for (size_t i = 0; i < frequencies.size(); ++i)
    {
        double omega = 2.0 * juce::MathConstants<double>::pi * frequencies[i] / sampleRate;
        std::complex<double> z = std::exp(std::complex<double>(0.0, omega));
        
        // Calculate the frequency response
        // This is a simplified approximation
        magnitudes[i] = 1.0; // Placeholder
    }
}

void EQVisualizer::calculateResponseCurve()
{
    juce::ScopedLock scopedLock(lock);
    
    // Calculate the frequency response for visualization
    calculateFilterResponse();
    
    // Trigger a repaint to update the display
    repaint();
}

void EQVisualizer::calculateFilterResponse()
{
    // Clear the magnitude data
    std::fill(magnitudeData.begin(), magnitudeData.end(), 0.0f);
    
    // Calculate the frequency response for each point
    for (int i = 0; i < magnitudeData.size(); ++i)
    {
        // Calculate the frequency for this point (logarithmic scale)
        float freq = 20.0f * std::pow(2.0f, i / (magnitudeData.size() / 10.0f));
        
        // Calculate the magnitude response at this frequency
        magnitudeData[i] = this->getMagnitudeResponse(freq);
    }
    
    // Convert the magnitude data to the frequency response
    for (int i = 0; i < frequencyResponse.size(); ++i)
    {
        frequencyResponse[i] = juce::Decibels::gainToDecibels(magnitudeData[i], -100.0f);
    }
}

void EQVisualizer::timerCallback()
{
    // Update the visualizer periodically
    repaint();
}

void EQVisualizer::drawGrid(juce::Graphics& g)
{
    g.setColour(juce::Colours::darkgrey.withAlpha(0.6f));
    
    // Draw frequency grid lines
    for (auto freq : freqLines)
    {
        float x = getWidth() * getFrequencyPosition(freq);
        
        g.drawVerticalLine(static_cast<int>(x), 0.0f, static_cast<float>(getHeight()));
        
        // Draw frequency labels
        g.setColour(juce::Colours::lightgrey);
        g.setFont(12.0f);
        
        juce::String freqText;
        if (freq >= 1000.0f)
            freqText = juce::String(freq / 1000.0f) + "k";
        else
            freqText = juce::String(freq);
            
        g.drawText(freqText, static_cast<int>(x - 10), getHeight() - 20, 20, 20, juce::Justification::centred);
        
        g.setColour(juce::Colours::darkgrey.withAlpha(0.6f));
    }
    
    // Draw gain grid lines
    for (int gain = -24; gain <= 24; gain += 6)
    {
        float y = getHeight() * getGainPosition(gain);
        
        g.drawHorizontalLine(static_cast<int>(y), 0.0f, static_cast<float>(getWidth()));
        
        // Draw gain labels
        g.setColour(juce::Colours::lightgrey);
        g.setFont(12.0f);
        g.drawText(juce::String(gain) + " dB", 5, static_cast<int>(y - 10), 40, 20, juce::Justification::left);
        
        g.setColour(juce::Colours::darkgrey.withAlpha(0.6f));
    }
}

float EQVisualizer::getFrequencyPosition(float frequency) const
{
    // Map frequency to x-coordinate (logarithmic scale)
    const float minFreq = EQConstants::minFreq;
    const float maxFreq = EQConstants::maxFreq;
    
    return std::log10(frequency / minFreq) / std::log10(maxFreq / minFreq);
}

float EQVisualizer::getGainPosition(float gain) const
{
    // Convert gain to position (0-1)
    return 1.0f - (gain - EQConstants::minGain) / (EQConstants::maxGain - EQConstants::minGain);
}


