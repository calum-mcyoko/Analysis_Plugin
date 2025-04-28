/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "EQConstants.h"
//==============================================================================
EQAudioProcessor::EQAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    ),
    parameters(*this, nullptr, "Parameters", createParameterLayout())
#endif
{
    // Initialize FFT objects
    fftObjects.clear();
    for (int i = 0; i < EQConstants::numEQBands; ++i)
    {
        fftObjects.push_back(std::make_unique<juce::dsp::FFT>(10)); // Order 10 = 1024 points
    }
    
    // Initialize FFT analyzer
    fftAnalyzer = std::make_unique<juce::dsp::FFT>(std::log2(EQConstants::fftSize));
    
    // Initialize FFT window (Hann window)
    for (int i = 0; i < EQConstants::fftSize; ++i)
        fftWindow[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * i / (EQConstants::fftSize - 1)));
        
    // Initialize DSP spec with default values (will be updated in prepareToPlay)
    spec.sampleRate = 44100.0;
    spec.maximumBlockSize = 512;
    spec.numChannels = 2;

    copyPythonScriptIfNeeded();
    
    // Properly initialize filters with default coefficients
    for (int i = 0; i < EQConstants::numEQBands; ++i)
    {
        // Create default coefficients based on band position
        juce::dsp::IIR::Coefficients<float>::Ptr defaultCoeffs;
        
        if (i == 0) // Lowest band - low shelf
        {
            defaultCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
                spec.sampleRate, 80.0f, 1.0f, 1.0f); // Default flat response
        }
        else if (i == EQConstants::numEQBands - 1) // Highest band - high shelf
        {
            defaultCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
                spec.sampleRate, 8000.0f, 1.0f, 1.0f); // Default flat response
        }
        else // Mid bands - peak filters
        {
            float freq = 100.0f * std::pow(10.0f, i * 0.5f); // Spread frequencies logarithmically
            defaultCoeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                spec.sampleRate, freq, 1.0f, 1.0f); // Default flat response
        }
        
        // Apply the default coefficients to the filter
        if (defaultCoeffs != nullptr) {
            *filters[i].state = *defaultCoeffs;
        }
        
        // Reset the filter
        filters[i].reset();
        filters[i].prepare(spec);
    }
    // Initialize oversampling (2x)
        // Initialize oversampling (2x)
        oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
            getTotalNumOutputChannels(), // Correct number of channels
            1, // Factor of 2x oversampling
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
        
        // Prepare oversampling
        oversampling->initProcessing(spec.maximumBlockSize);
    
    
    // Create parameter listener to update filters when parameters change
    paramListener = std::make_unique<ParameterListener>(*this);
    
    // Add listener to all parameters
    for (int i = 0; i < EQConstants::numEQBands; ++i)
    {
        auto* freqParam = parameters.getParameter("Frequency" + std::to_string(i));
        auto* gainParam = parameters.getParameter("Gain" + std::to_string(i));
        auto* qParam = parameters.getParameter("Q" + std::to_string(i));
        
        if (freqParam) freqParam->addListener(paramListener.get());
        if (gainParam) gainParam->addListener(paramListener.get());
        if (qParam) qParam->addListener(paramListener.get());
    }
    
    // Add listener to zero latency parameter
    auto* zeroLatencyParam = parameters.getParameter("ZeroLatency");
    if (zeroLatencyParam) zeroLatencyParam->addListener(paramListener.get());
    
    // Set default preset name
    currentPresetName = "Default";
    
    // Force an initial update of all filters
    updateFilters();
    
    // Initialize FFT analyzer for visualization
    fftAnalyzer = std::make_unique<juce::dsp::FFT>(fftOrder);
    fftData.resize(EQConstants::fftSize * 2, 0.0f);  // Complex data (real/imag pairs) 

    // Create Hann window for better FFT results
    fftWindow.resize(EQConstants::fftSize);
    for (int i = 0; i < EQConstants::fftSize; ++i)
        fftWindow[i] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * i / (EQConstants::fftSize - 1));
    
    // Initialize FIFO buffer
    audioFifo.resize(EQConstants::fftSize * 2, 0.0f);
    fifoIndex = 0;
}


EQAudioProcessor::~EQAudioProcessor()
{
    // First, stop any background processing
    // If you have any background threads, stop them first
    
    // Remove parameter listeners before destroying the listener
    for (int i = 0; i < EQConstants::numEQBands; ++i)
    {
        auto* freqParam = parameters.getParameter("Frequency" + juce::String(i));
        auto* gainParam = parameters.getParameter("Gain" + juce::String(i));
        auto* qParam = parameters.getParameter("Q" + juce::String(i));
        
        if (freqParam) freqParam->removeListener(paramListener.get());
        if (gainParam) gainParam->removeListener(paramListener.get());
        if (qParam) qParam->removeListener(paramListener.get());
    }
    
    auto* zeroLatencyParam = parameters.getParameter("ZeroLatency");
    if (zeroLatencyParam) zeroLatencyParam->removeListener(paramListener.get());
    
    // Clear the parameter listener before other resources
    paramListener = nullptr;
    
    // Reset all filters
    for (auto& filter : filters)
    {
        filter.reset();
    }
    
    // Clear FFT resources
    fftAnalyzer = nullptr;
    fftData.clear();
    fftWindow.clear();
    audioFifo.clear();
    
    // Finally reset oversampling
    oversampling = nullptr;
}
//==============================================================================
const juce::String EQAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool EQAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool EQAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool EQAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double EQAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int EQAudioProcessor::getNumPrograms()
{
    return 1;
}

int EQAudioProcessor::getCurrentProgram()
{
    return 0;
}

void EQAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String EQAudioProcessor::getProgramName (int index)
{
    return {};
}

void EQAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================


void EQAudioProcessor::releaseResources()
{
    // Free resources when playback stops
    oversampling->reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool EQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void EQAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Log preparation for debugging
    juce::Logger::writeToLog("prepareToPlay called: sampleRate=" + juce::String(sampleRate) +
                           ", samplesPerBlock=" + juce::String(samplesPerBlock));
    
    // Initialize DSP processing spec
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = getTotalNumOutputChannels();
    
    // Initialize parameter smoothing with MINIMAL smoothing time
    for (int i = 0; i < EQConstants::numEQBands; ++i)
{
    // Use 1ms smoothing instead of 50ms for near-instant response
    // This is just enough to prevent clicks but won't be audibly noticeable
    smoothedFrequency[i].reset(sampleRate, 0.001); // 1ms smoothing time (was 0.05)
    smoothedGain[i].reset(sampleRate, 0.001);      // 1ms smoothing time
    smoothedQ[i].reset(sampleRate, 0.001);         // 1ms smoothing time
    
    // Set initial values
    auto freqParam = parameters.getParameter("Frequency" + std::to_string(i));
    auto gainParam = parameters.getParameter("Gain" + std::to_string(i));
    auto qParam = parameters.getParameter("Q" + std::to_string(i));
    
    if (freqParam && gainParam && qParam)
    {
        smoothedFrequency[i].setTargetValue(freqParam->convertFrom0to1(freqParam->getValue()));
        smoothedGain[i].setTargetValue(gainParam->convertFrom0to1(gainParam->getValue()));
        smoothedQ[i].setTargetValue(qParam->convertFrom0to1(qParam->getValue()));
         
            // Log parameter values for debugging
            juce::Logger::writeToLog("Band " + juce::String(i) +
                                   ": Freq=" + juce::String(smoothedFrequency[i].getTargetValue()) +
                                   ", Gain=" + juce::String(smoothedGain[i].getTargetValue()) +
                                   ", Q=" + juce::String(smoothedQ[i].getTargetValue()));
        }
    }
    
    // Prepare all filters with the processing spec
    for (auto& filter : filters)
    {
        filter.prepare(spec);
        filter.reset(); // Ensure filters are properly reset
    }
    
    // Prepare oversampling
    oversampling->initProcessing(samplesPerBlock);
    oversampling->reset();

    // Update filter coefficients based on current parameter values
    updateFilters();
    
    // Initialize FFT analyzer if not already done
    if (fftAnalyzer == nullptr)
    {
        fftAnalyzer = std::make_unique<juce::dsp::FFT>(fftOrder);
        fftData.resize(EQConstants::fftSize * 2, 0.0f);  // Complex data (real/imag pairs)
        
        // Create Hann window for better FFT results
        fftWindow.resize(EQConstants::fftSize);
        for (int i = 0; i < EQConstants::fftSize; ++i)
            fftWindow[i] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * i / (EQConstants::fftSize - 1));
    }
    
    // Reset FFT analysis
    audioFifo.resize(EQConstants::fftSize * 2, 0.0f); // Ensure buffer is large enough
    std::fill(audioFifo.begin(), audioFifo.end(), 0.0f);
    fifoIndex = 0;
    std::fill(fftData.begin(), fftData.end(), 0.0f);
    std::fill(spectrumData.begin(), spectrumData.end(), -100.0f); // Initialize to -100dB for better visualization
    
    // Reset test signal parameters
    juce::ScopedLock lock(testSignalLock);
    testSignalPhase = 0.0f;
    pinkNoiseY1 = 0.0f;
    
    // Log that preparation is complete
    juce::Logger::writeToLog("prepareToPlay completed, filters updated");
}

void EQAudioProcessor::updateFilters()
{
    // Skip if not initialized yet
    if (spec.sampleRate <= 0)
    {
        juce::Logger::writeToLog("updateFilters: spec not initialized yet");
        return;
    }
    
    // Check if we're currently loading a preset to avoid redundant updates
    if (m_isLoadingPreset)
    {
        juce::Logger::writeToLog("updateFilters: skipping during preset loading");
        return;
    }
    
    // Get the current zero latency mode setting
    zeroLatencyMode = parameters.getParameter("ZeroLatency")->getValue() > 0.5f;
    
    // Create an array to store coefficients for the visualizer
    std::array<juce::dsp::IIR::Coefficients<float>::Ptr, EQConstants::numEQBands> filterCoefficients;
    
    for (int i = 0; i < EQConstants::numEQBands; ++i)
    {
        auto freqParam = parameters.getParameter("Frequency" + std::to_string(i));
        auto gainParam = parameters.getParameter("Gain" + std::to_string(i));
        auto qParam = parameters.getParameter("Q" + std::to_string(i));
        
        if (freqParam && gainParam && qParam)
        {
            // Get current parameter values DIRECTLY (no smoothing for visualization)
            float bandFreq = freqParam->convertFrom0to1(freqParam->getValue());
            float bandGain = gainParam->convertFrom0to1(gainParam->getValue());
            float bandQ = qParam->convertFrom0to1(qParam->getValue());
            
            // Update smoothed values if needed - but with VERY short smoothing time
            // This ensures audio processing has minimal smoothing but still prevents clicks
            if (smoothedFrequency[i].getTargetValue() != bandFreq)
            {
                smoothedFrequency[i].reset(spec.sampleRate, 0.001); // 1ms smoothing (was 50ms)
                smoothedFrequency[i].setTargetValue(bandFreq);
            }

            if (smoothedGain[i].getTargetValue() != bandGain)
            {
                smoothedGain[i].reset(spec.sampleRate, 0.001); // 1ms smoothing
                smoothedGain[i].setTargetValue(bandGain);
            }

            if (smoothedQ[i].getTargetValue() != bandQ)
            {
                smoothedQ[i].reset(spec.sampleRate, 0.001); // 1ms smoothing
                smoothedQ[i].setTargetValue(bandQ);
            }
            
            // Calculate filter coefficients based on band position and zero-latency mode
            // IMPORTANT: Use direct parameter values for visualization (no smoothing)
            juce::dsp::IIR::Coefficients<float>::Ptr coefficients;
            
            try {
                if (zeroLatencyMode)
                {
                    // Minimum phase filters (zero latency)
                    if (i == 0) // Lowest band - low shelf for more bass impact
                    {
                        coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
                            spec.sampleRate, bandFreq, bandQ, juce::Decibels::decibelsToGain(bandGain));
                    }
                    else if (i == EQConstants::numEQBands - 1) // Highest band - high shelf for air/brightness
                    {
                        coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
                            spec.sampleRate, bandFreq, bandQ, juce::Decibels::decibelsToGain(bandGain));
                    }
                    else // Mid bands - peak filters
                    {
                        coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                            spec.sampleRate, bandFreq, bandQ, juce::Decibels::decibelsToGain(bandGain));
                    }
                }
                else
                {
                    // Linear phase approximation (with latency)
                    if (i == 0) // Lowest band
                    {
                        coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
                            spec.sampleRate, bandFreq, bandQ * 0.7f, juce::Decibels::decibelsToGain(bandGain));
                    }
                    else if (i == EQConstants::numEQBands - 1) // Highest band
                    {
                        coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
                            spec.sampleRate, bandFreq, bandQ * 0.7f, juce::Decibels::decibelsToGain(bandGain));
                    }
                    else // Mid bands
                    {
                        coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                            spec.sampleRate, bandFreq, bandQ * 1.5f, juce::Decibels::decibelsToGain(bandGain));
                    }
                }
            }
            catch (const std::exception& e) {
                juce::Logger::writeToLog("Exception creating filter coefficients: " + juce::String(e.what()));
                continue; // Skip this filter if there's an exception
            }
            
            // Make sure the coefficients were created successfully
            if (coefficients != nullptr)
            {
                // Apply the coefficients to the filter
                *filters[i].state = *coefficients;
                
                // Store the coefficients for the visualizer
                filterCoefficients[i] = coefficients;
                
                // Only reset the filter when absolutely necessary (e.g., during preset loading)
                // Frequent resets can cause audible artifacts
                if (m_isLoadingPreset)
                {
                    filters[i].reset();
                }
            }
            else
            {
                juce::Logger::writeToLog("ERROR: Failed to create coefficients for filter " + juce::String(i));
                
                // Create a flat response filter for the visualizer
                filterCoefficients[i] = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                    spec.sampleRate, 1000.0f, 1.0f, 1.0f);
            }
        }
        else
        {
            juce::Logger::writeToLog("ERROR: Missing parameters for filter " + juce::String(i));
            
            // Create a flat response filter for the visualizer
            filterCoefficients[i] = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                spec.sampleRate, 1000.0f, 1.0f, 1.0f);
        }
    }
    
    // If editor exists, update the visualizer
    if (auto* editor = dynamic_cast<EQAudioProcessorEditor*>(getActiveEditor()))
    {
        editor->getVisualizer().updateFilters(filterCoefficients);
        editor->getVisualizer().repaint(); // Force immediate repaint
        juce::Logger::writeToLog("Updated visualizer with new filter coefficients");
    }
}


void EQAudioProcessor::updateFilterCoefficients()
{
    // Just call the consolidated method
    updateFilters();
}

std::array<juce::dsp::IIR::Coefficients<float>::Ptr, EQConstants::numEQBands> EQAudioProcessor::getFilterCoefficients() const
{
    std::array<juce::dsp::IIR::Coefficients<float>::Ptr, EQConstants::numEQBands> coefficients;
    
    for (int i = 0; i < EQConstants::numEQBands; ++i)
    {
        coefficients[i] = filters[i].state;
    }
    
    return coefficients;
}

void EQAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    
    try {
        auto totalNumInputChannels = getTotalNumInputChannels();
        auto totalNumOutputChannels = getTotalNumOutputChannels();
        
        // Clear any output channels that didn't contain input data
        for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
            buffer.clear(i, 0, buffer.getNumSamples());
        
        // Generate test signal if enabled
        if (testSignalEnabled)
        {
            // Clear buffer before generating test signal
            buffer.clear();
            
            // Log that we're using a test signal (occasionally)
            static int testSignalLogCounter = 0;
            if (++testSignalLogCounter % 1000 == 0)
            {
                juce::Logger::writeToLog("Generating test signal: Type=" + juce::String(testSignalType) +
                                       ", Freq=" + juce::String(testSignalFrequency) +
                                       ", Amp=" + juce::String(testSignalAmplitude));
            }
            
            // Thread-safe test signal generation
            juce::ScopedLock lock(testSignalLock);
            
            const float sampleRate = (float)getSampleRate();
            if (sampleRate <= 0.0f) {
                juce::Logger::writeToLog("Invalid sample rate in test signal generation");
                return;
            }
            
            const float amplitude = juce::jlimit(0.0f, 1.0f, testSignalAmplitude);
            const float frequency = juce::jlimit(20.0f, 20000.0f, testSignalFrequency);
            
            // Generate the test signal for all channels
            for (int channel = 0; channel < totalNumOutputChannels; ++channel)
            {
                auto* channelData = buffer.getWritePointer(channel);
                
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                {
                    float signalValue = 0.0f;
                    
                    switch (testSignalType)
                    {
                        case 0: // Sine wave
                            signalValue = std::sin(testSignalPhase);
                            testSignalPhase += 2.0f * juce::MathConstants<float>::pi * frequency / sampleRate;
                            
                            // Wrap phase to prevent floating point errors over time
                            if (testSignalPhase > 2.0f * juce::MathConstants<float>::pi)
                                testSignalPhase -= 2.0f * juce::MathConstants<float>::pi;
                            break;
                            
                        case 1: // White noise
                            // Use system random for thread safety
                            signalValue = juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f;
                            break;
                            
                        case 2: // Pink noise
                            // Generate white noise
                            float white = juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f;
                            
                            // Apply pink filter (first order IIR approximation)
                            float pink = 0.99765f * pinkNoiseY1 + white * 0.0990460f;
                            pinkNoiseY1 = pink;
                            
                            signalValue = pink;
                            break;
                    }
                    
                    // Apply amplitude
                    channelData[sample] = signalValue * amplitude;
                }
            }
        }
        
        // Log input signal occasionally for debugging
        static int logCounter = 0;
        if (++logCounter % 1000 == 0 && buffer.getNumSamples() > 0 && buffer.getNumChannels() > 0)
        {
            float inputSample = buffer.getSample(0, 0);
            juce::Logger::writeToLog("Input sample value: " + juce::String(inputSample));
        }
        
        // Check if any parameter is still smoothing
        bool needsFilterUpdate = false;
        for (int i = 0; i < EQConstants::numEQBands; ++i)
        {
            if (smoothedFrequency[i].isSmoothing() || 
                smoothedGain[i].isSmoothing() || 
                smoothedQ[i].isSmoothing())
            {
                needsFilterUpdate = true;
                break;
            }
        }
        
        // Only update filters if parameters are still smoothing
        if (needsFilterUpdate)
        {
            // Update filter coefficients with current smoothed values
            for (int i = 0; i < EQConstants::numEQBands; ++i)
            {
                // Get the current smoothed values
                float bandFreq = smoothedFrequency[i].getNextValue();
                float bandGain = smoothedGain[i].getNextValue();
                float bandQ = smoothedQ[i].getNextValue();
                
                // Calculate filter coefficients
                juce::dsp::IIR::Coefficients<float>::Ptr coefficients;
                
                try {
                    if (zeroLatencyMode)
                    {
                        // Minimum phase filters (zero latency)
                        if (i == 0) // Lowest band - low shelf
                        {
                            coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
                                spec.sampleRate, bandFreq, bandQ, juce::Decibels::decibelsToGain(bandGain));
                        }
                        else if (i == EQConstants::numEQBands - 1) // Highest band - high shelf
                        {
                            coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
                                spec.sampleRate, bandFreq, bandQ, juce::Decibels::decibelsToGain(bandGain));
                        }
                        else // Mid bands - peak filters
                        {
                            coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                                spec.sampleRate, bandFreq, bandQ, juce::Decibels::decibelsToGain(bandGain));
                        }
                    }
                    else
                    {
                        // Linear phase approximation (with latency)
                        if (i == 0) // Lowest band
                        {
                            coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
                                spec.sampleRate, bandFreq, bandQ * 0.7f, juce::Decibels::decibelsToGain(bandGain));
                        }
                        else if (i == EQConstants::numEQBands - 1) // Highest band
                        {
                            coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
                                spec.sampleRate, bandFreq, bandQ * 0.7f, juce::Decibels::decibelsToGain(bandGain));
                        }
                        else // Mid bands
                        {
                            coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                                spec.sampleRate, bandFreq, bandQ * 1.5f, juce::Decibels::decibelsToGain(bandGain));
                        }
                    }
                    
                    // Apply the coefficients to the filter
                    if (coefficients != nullptr)
                    {
                        *filters[i].state = *coefficients;
                    }
                }
                catch (const std::exception& e) {
                    // Just continue if there's an error
                    juce::Logger::writeToLog("Exception updating filter coefficients in processBlock: " + juce::String(e.what()));
                }
            }
        }
        
        // Create audio block from buffer
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        
        try {
            // Check if oversampling is properly initialized
            if (oversampling == nullptr) {
                juce::Logger::writeToLog("Oversampling not initialized");
                return;
            }
            
            // Process with oversampling if not in zero latency mode
            if (!zeroLatencyMode) {
                oversampling->processSamplesUp(block);
            }
            
            // Process all filters in series for all channels
            for (int i = 0; i < EQConstants::numEQBands; ++i)
            {
                // Process all filters regardless of gain
                // This ensures that even filters with zero gain still shape the frequency response
                filters[i].process(context);
                
                // Log filter usage occasionally
                if (logCounter % 1000 == 0 && i == 0) // Just log the first filter to avoid spam
                {
                    auto* gainParam = parameters.getParameter("Gain" + std::to_string(i));
                    auto* freqParam = parameters.getParameter("Frequency" + std::to_string(i));
                    auto* qParam = parameters.getParameter("Q" + std::to_string(i));
                    
                    juce::Logger::writeToLog("Applied filter " + juce::String(i) +
                                           " with gain " + juce::String(gainParam->convertFrom0to1(gainParam->getValue())) +
                                           ", freq " + juce::String(freqParam->convertFrom0to1(freqParam->getValue())) +
                                           ", Q " + juce::String(qParam->convertFrom0to1(qParam->getValue())));
                }
            }
            
            // Downsample back to original sample rate if not in zero latency mode
            if (!zeroLatencyMode) {
                oversampling->processSamplesDown(block);
            }
        }
        catch (const std::exception& e) {
            // Log any exceptions during processing
            juce::Logger::writeToLog("Exception during audio processing: " + juce::String(e.what()));
            
            // Clear the buffer to prevent noise in case of error
            buffer.clear();
        }
        
        // After processing, analyze the output for visualization
        // We'll just use the left channel (0) for simplicity
        if (buffer.getNumChannels() > 0)
        {
            const float* channelData = buffer.getReadPointer(0);
            
            // Calculate RMS level for metering (optional)
            float rmsLevel = 0.0f;
            
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                // Add the sample to the FFT buffer
                audioFifo[fifoIndex] = channelData[i];
                fifoIndex = (fifoIndex + 1) % audioFifo.size();
                
                // Accumulate squared samples for RMS calculation
                rmsLevel += channelData[i] * channelData[i];
            }
            
            // Finalize RMS calculation
            if (buffer.getNumSamples() > 0)
            {
                rmsLevel = std::sqrt(rmsLevel / buffer.getNumSamples());
                
                // Log RMS level occasionally
                if (logCounter % 1000 == 0)
                {
                    juce::Logger::writeToLog("Output RMS level: " + juce::String(rmsLevel));
                }
            }
            
            // Calculate FFT if enough samples have been collected
            if (++fftBlockCounter >= fftUpdateInterval)
            {
                fftBlockCounter = 0;
                calculateFFT();
            }
        }
        
        // Occasionally log a sample value for debugging
        if (logCounter % 1000 == 0 && buffer.getNumSamples() > 0 && buffer.getNumChannels() > 0)
        {
            float outputSample = buffer.getSample(0, 0);
            juce::Logger::writeToLog("Output sample value: " + juce::String(outputSample));
        }
    }
    catch (const std::exception& e) {
        // Log any exceptions during processing
        juce::Logger::writeToLog("Exception in processBlock: " + juce::String(e.what()));
        buffer.clear(); // Ensure clean output in case of error
    }
}

void EQAudioProcessor::updateSpectrum(const juce::AudioBuffer<float>& buffer)
{
    if (fftAnalyzer == nullptr || buffer.getNumChannels() == 0)
        return;
        
    const int numSamples = buffer.getNumSamples();
    const float* channelData = buffer.getReadPointer(0); // Use first channel for analysis
    
    // Add samples to the FIFO
    for (int i = 0; i < numSamples; ++i)
    {
        if (fifoIndex >= audioFifo.size())
            fifoIndex = 0;
            
        audioFifo[fifoIndex++] = channelData[i];
    }
    
    // If we have enough samples, perform FFT
    if (fifoIndex >= EQConstants::fftSize)
    {
        // Clear FFT data
        std::fill(fftData.begin(), fftData.end(), 0.0f);
        
        // Copy data from FIFO to FFT buffer with windowing
        for (int i = 0; i < EQConstants::fftSize; ++i)
        {
            // Calculate the index in the circular buffer, going backwards from current position
            int bufferIndex = (fifoIndex - EQConstants::fftSize + i + audioFifo.size()) % audioFifo.size();
            
            // Bounds check to prevent out-of-range access
            if (bufferIndex >= 0 && bufferIndex < audioFifo.size()) {
                fftData[i * 2] = audioFifo[bufferIndex] * fftWindow[i];
            }
        }
        
        // Perform FFT
        fftAnalyzer->performFrequencyOnlyForwardTransform(fftData.data());
        
        // Convert to dB scale and store in spectrum data
        juce::ScopedLock lock(spectrumLock);
        for (int i = 0; i < EQConstants::fftSize / 2; ++i)
        {
            // Convert magnitude to dB with proper scaling
            float magnitude = fftData[i];
            
            // Avoid log of zero or negative values
            if (magnitude <= 0.0f)
                magnitude = 1e-6f;
                
            // Convert to dB with proper scaling and range limiting
            float dbValue = 20.0f * std::log10(magnitude);
            spectrumData[i] = juce::jlimit(-100.0f, 0.0f, dbValue);
        }
    }
}

void EQAudioProcessor::enableTestSignal(bool shouldEnable) 
{
    juce::ScopedLock lock(testSignalLock);
    testSignalEnabled = shouldEnable;
    testSignalPhase = 0.0f; // Reset phase when enabling
}

void EQAudioProcessor::setTestSignalFrequency(float freq) 
{
    juce::ScopedLock lock(testSignalLock);
    testSignalFrequency = juce::jlimit(20.0f, 20000.0f, freq);
}

void EQAudioProcessor::setTestSignalAmplitude(float amp) 
{
    juce::ScopedLock lock(testSignalLock);
    testSignalAmplitude = juce::jlimit(0.0f, 1.0f, amp);
}

void EQAudioProcessor::setTestSignalType(int type) 
{
    juce::ScopedLock lock(testSignalLock);
    testSignalType = juce::jlimit(0, 2, type);
}

void EQAudioProcessor::initializeFFTAnalyzer() 
{
    if (fftAnalyzer == nullptr) {
        fftAnalyzer = std::make_unique<juce::dsp::FFT>(fftOrder);
        
        // Initialize window function (Hann window)
        for (int i = 0; i < EQConstants::fftSize; ++i) {
            fftWindow[i] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * i / (float)(EQConstants::fftSize - 1));
        }
        
        // Clear spectrum data
        std::fill(spectrumData.begin(), spectrumData.end(), -100.0f);
    }
}




void EQAudioProcessor::pushNextSampleIntoFifo(float sample)
{
    // If the fifo contains enough data, set a flag to say
    // that the next frame should be rendered
    if (fifoIndex == audioFifo.size())
    {
        if (!nextFFTBlockReady)
        {
            std::fill(fftData.begin(), fftData.end(), 0.0f);
            std::copy(audioFifo.begin(), audioFifo.end(), fftData.begin());
            nextFFTBlockReady = true;
        }
        
        fifoIndex = 0;
    }
    
    audioFifo[fifoIndex++] = sample;
}

void EQAudioProcessor::calculateFFT()
{
    // Clear the FFT data
    std::fill(fftData.begin(), fftData.end(), 0.0f);
    
    // Copy data from the audio fifo to the FFT data buffer
    for (int i = 0; i < EQConstants::fftSize; ++i)
    {
        // Calculate the index in the circular buffer, going backwards from current position
        int bufferIndex = (fifoIndex - EQConstants::fftSize + i + audioFifo.size()) % audioFifo.size();
        
        // Add bounds checking to prevent out-of-range access
        if (bufferIndex >= 0 && bufferIndex < audioFifo.size()) {
            fftData[i * 2] = audioFifo[bufferIndex] * fftWindow[i];
        }
    }
    
    // Perform FFT
    fftAnalyzer->performFrequencyOnlyForwardTransform(fftData.data());
    
    // Convert to dB scale and store in spectrum data
    juce::ScopedLock lock(spectrumLock);
    for (int i = 0; i < EQConstants::fftSize / 2; ++i)
    {
        // Convert magnitude to dB with proper scaling
        float magnitude = fftData[i];
        
        // Avoid log of zero or negative values
        if (magnitude <= 0.0f)
            magnitude = 1e-6f;
        
        // Convert to dB scale
        spectrumData[i] = juce::Decibels::gainToDecibels(magnitude);
    }
}

const std::array<float, EQConstants::fftSize / 2>& EQAudioProcessor::getSpectrumData() const
{
    juce::ScopedLock lock(spectrumLock);
    return spectrumData;
}
//==============================================================================
bool EQAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* EQAudioProcessor::createEditor()
{
    return new EQAudioProcessorEditor (*this);
}

//==============================================================================
void EQAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Create an XML element to store the state
    juce::XmlElement xml("EQPluginState");
    
    // Add the current preset name
    xml.setAttribute("presetName", currentPresetName);
    
    // Add test signal settings
    xml.setAttribute("testSignalEnabled", testSignalEnabled);
    xml.setAttribute("testSignalType", testSignalType);
    xml.setAttribute("testSignalFrequency", testSignalFrequency);
    xml.setAttribute("testSignalAmplitude", testSignalAmplitude);
    
    // Store the parameter state
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xmlState(state.createXml());
    
    if (xmlState.get() != nullptr)
        xml.addChildElement(xmlState.release());
    
    // Copy the XML data to the destination memory block
    copyXmlToBinary(xml, destData);
}

void EQAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Create an XML element from the binary data
    auto xmlState = std::unique_ptr<juce::XmlElement>(getXmlFromBinary(data, sizeInBytes));
    
    if (xmlState != nullptr)
    {
        // If it's the correct type of XML
        if (xmlState->hasTagName("EQPluginState"))
        {
            // Restore the current preset name
            currentPresetName = xmlState->getStringAttribute("presetName", "");
            
            // Restore test signal settings
            testSignalEnabled = xmlState->getBoolAttribute("testSignalEnabled", false);
            testSignalType = xmlState->getIntAttribute("testSignalType", 0);
            testSignalFrequency = xmlState->getDoubleAttribute("testSignalFrequency", 1000.0);
            testSignalAmplitude = (float)xmlState->getDoubleAttribute("testSignalAmplitude", 0.5);
            
            // Restore the parameter state
            auto params = xmlState->getChildByName(parameters.state.getType());
            
            if (params != nullptr)
                parameters.replaceState(juce::ValueTree::fromXml(*params));
            
            // Log the restored state
            juce::Logger::writeToLog("State restored: Preset=" + currentPresetName + 
                                   ", TestSignal=" + juce::String(testSignalEnabled ? "On" : "Off"));
            
            // Force update filters after state restoration
            updateFilters();
            
            // Force visualizer update after state restoration
            if (auto* editor = dynamic_cast<EQAudioProcessorEditor*>(getActiveEditor()))
            {
                // Get the latest filter coefficients
                auto filterCoeffs = getFilterCoefficients();
                
                // Update the visualizer with these coefficients
                editor->getVisualizer().updateFilters(filterCoeffs);
                
                // Force a complete update and repaint
                editor->getVisualizer().forceUpdate();
                
                juce::Logger::writeToLog("Forced visualizer update after state restoration");
            }
        }
    }
}


bool EQAudioProcessor::createPresetFromAudioFile(const juce::File& audioFile, const juce::String& presetName)
{
    juce::Logger::writeToLog("Starting preset creation from audio file: " + audioFile.getFullPathName());
    
    // Get the path to the analyzer executable using the new method
    juce::File exeFile = getAnalyzerExecutable();
    
    // Check if the executable exists
    if (!exeFile.existsAsFile()) {
        juce::Logger::writeToLog("ERROR: Analyzer executable not found at: " + exeFile.getFullPathName());
        return false;
    }
    
    juce::Logger::writeToLog("Analyzer executable found at: " + exeFile.getFullPathName());
    
    // Ensure the presets directory exists
    juce::File presetsDir = getPresetsDirectory();
    presetsDir.createDirectory();
    
    // Define the output file path
    juce::String outputFileName = (presetName.isNotEmpty() ? presetName : audioFile.getFileNameWithoutExtension()) + "_preset.json";
    juce::File outputFile = presetsDir.getChildFile(outputFileName);
    
    // Create a batch file to run the executable and keep the window open
    juce::File batchFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("run_preset_analyzer.bat");
    
    // Build the batch file content
    juce::String batchContent = 
        "@echo off\n"
        "echo Running audio analyzer...\n"
        "\"" + exeFile.getFullPathName() + "\" \"" + audioFile.getFullPathName() + "\"";
    
    if (presetName.isNotEmpty())
        batchContent += " \"" + presetName + "\"";
        
    batchContent += " \"" + outputFile.getFullPathName() + "\"\n"
                    "echo.\n"
                    "echo Execution completed with exit code %errorlevel%\n"
                    "echo.\n"
                    "if %errorlevel% neq 0 (\n"
                    "    echo ERROR: Analysis failed. Please check the output above for errors.\n"
                    ")\n"
                    "if exist \"" + outputFile.getFullPathName() + "\" (\n"
                    "    echo SUCCESS: Preset file created at: " + outputFile.getFullPathName() + "\n"
                    ")\n"
                    "echo.\n"
                    "echo Press any key to close this window and continue...\n"
                    "pause > nul\n";
    
    // Write the batch file
    batchFile.replaceWithText(batchContent);
    
    juce::Logger::writeToLog("Created batch file: " + batchFile.getFullPathName());
    
    // Execute the batch file
    int result = system(batchFile.getFullPathName().toRawUTF8());
    
    juce::Logger::writeToLog("Batch file execution result: " + juce::String(result));
    
    // Check if the output file was created
    if (!outputFile.existsAsFile()) {
        juce::Logger::writeToLog("WARNING: Output file not found at expected location: " + outputFile.getFullPathName());
        
        // Try looking in other common locations
        juce::File altOutputFile = exeFile.getParentDirectory().getChildFile(outputFileName);
        juce::Logger::writeToLog("Checking alternative location: " + altOutputFile.getFullPathName());
        
        if (altOutputFile.existsAsFile()) {
            juce::Logger::writeToLog("Found preset file in executable directory");
            outputFile = altOutputFile;
        } else {
            // Try the current working directory
            juce::File cwdOutputFile = juce::File::getCurrentWorkingDirectory().getChildFile(outputFileName);
            juce::Logger::writeToLog("Checking current working directory: " + cwdOutputFile.getFullPathName());
            
            if (cwdOutputFile.existsAsFile()) {
                juce::Logger::writeToLog("Found preset file in current working directory");
                outputFile = cwdOutputFile;
            } else {
                juce::Logger::writeToLog("ERROR: Could not find the generated preset file in any location");
                return false;
            }
        }
    }
    
    // Load the preset
    if (loadPresetFromJSON(outputFile))
    {
        juce::Logger::writeToLog("Preset loaded successfully from: " + outputFile.getFullPathName());
        setCurrentPresetName(presetName.isNotEmpty() ? presetName : audioFile.getFileNameWithoutExtension());
        
        return true;
    }
    else
    {
        juce::Logger::writeToLog("ERROR: Failed to load the created preset");
        return false;
    }
}

juce::File EQAudioProcessor::getAnalyzerExecutable()
{
    // First try next to the plugin
    juce::File pluginFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    juce::File pluginDir = pluginFile.getParentDirectory();
    
#if JUCE_WINDOWS
    juce::File exeFile = pluginDir.getChildFile("PresetAnalyzer.exe");
#else
    juce::File exeFile = pluginDir.getChildFile("PresetAnalyzer");
#endif

    if (exeFile.existsAsFile())
        return exeFile;
        
    // If not found next to the plugin, try the user documents location
    juce::File docsExe = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                         .getChildFile("EQPlugin")
#if JUCE_WINDOWS
                         .getChildFile("PresetAnalyzer.exe");
#else
                         .getChildFile("PresetAnalyzer");
#endif

    return docsExe;
}


bool EQAudioProcessor::extractPresetMetadata(const juce::var& jsonData)
{
    if (auto* object = jsonData.getDynamicObject())
    {
        if (object->hasProperty("Metadata"))
        {
            auto* metadata = object->getProperty("Metadata").getDynamicObject();
            if (metadata)
            {
                // Extract transient density
                if (metadata->hasProperty("TransientDensity"))
                    currentPresetMetadata.transientDensity = (float)metadata->getProperty("TransientDensity");
                
                // Extract frequency range
                if (metadata->hasProperty("FrequencyRange") && metadata->getProperty("FrequencyRange").isArray())
                {
                    auto* rangeArray = metadata->getProperty("FrequencyRange").getArray();
                    if (rangeArray && rangeArray->size() == 2)
                    {
                        currentPresetMetadata.frequencyRange[0] = (float)(*rangeArray)[0];
                        currentPresetMetadata.frequencyRange[1] = (float)(*rangeArray)[1];
                    }
                }
                
                // Extract source file
                if (metadata->hasProperty("SourceFile"))
                    currentPresetMetadata.sourceFile = metadata->getProperty("SourceFile").toString();
                
                // Extract creation date
                if (metadata->hasProperty("CreationDate"))
                    currentPresetMetadata.creationDate = metadata->getProperty("CreationDate").toString();
                
                // Extract spectral balance if available
                if (metadata->hasProperty("SpectralBalance") && metadata->getProperty("SpectralBalance").isArray())
                {
                    auto* balanceArray = metadata->getProperty("SpectralBalance").getArray();
                    for (int i = 0; i < juce::jmin(balanceArray->size(), (int)currentPresetMetadata.spectralBalance.size()); ++i)
                    {
                        currentPresetMetadata.spectralBalance[i] = (float)(*balanceArray)[i];
                    }
                }
                
                return true;
            }
        }
    }
    
    // Reset to defaults if no metadata found
    currentPresetMetadata = PresetMetadata();
    return false;
}

juce::String EQAudioProcessor::getCurrentPresetName() const
{
    return currentPresetName;
}

bool EQAudioProcessor::loadPresetFromJSON(const juce::File& jsonFile)
{
    // Check if the file exists
    if (!jsonFile.existsAsFile())
    {
        juce::Logger::writeToLog("Preset file not found: " + jsonFile.getFullPathName());
        return false;
    }
    
    // Read the file content
    juce::String jsonContent = jsonFile.loadFileAsString();
    juce::Logger::writeToLog("Loading preset content: " + jsonContent);
    
    // Parse JSON
    juce::var parsedJson;
    juce::Result result = juce::JSON::parse(jsonContent, parsedJson);
    
    if (result.failed())
    {
        juce::Logger::writeToLog("Failed to parse JSON: " + result.getErrorMessage());
        return false;
    }
    
    // Extract metadata from the preset (keep this if needed for other features)
    extractPresetMetadata(parsedJson);
    
    // Set loading flag to prevent multiple filter updates
    m_isLoadingPreset = true;
    bool anyParamChanged = false;
    
    if (auto* object = parsedJson.getDynamicObject())
    {
        // Check for the Metadata from our enhanced Python analyzer
        bool hasMetadata = false;
        juce::Array<float> frequencyRange = { 20.0f, 20000.0f }; // Default full range
        juce::String sourceFile = "";
        
        // Extract metadata if available (keep this for other features)
        if (object->hasProperty("Metadata"))
        {
            auto* metadata = object->getProperty("Metadata").getDynamicObject();
            if (metadata)
            {
                hasMetadata = true;
                juce::Logger::writeToLog("Found enhanced analysis metadata");
                
                // Extract frequency range (keep this for frequency adjustments)
                if (metadata->hasProperty("FrequencyRange") && metadata->getProperty("FrequencyRange").isArray())
                {
                    auto* rangeArray = metadata->getProperty("FrequencyRange").getArray();
                    if (rangeArray && rangeArray->size() == 2)
                    {
                        currentPresetMetadata.frequencyRange[0] = (float)(*rangeArray)[0];
                        currentPresetMetadata.frequencyRange[1] = (float)(*rangeArray)[1];
                        juce::Logger::writeToLog("Frequency range: " +
                                               juce::String(frequencyRange[0]) + "Hz - " +
                                               juce::String(frequencyRange[1]) + "Hz");
                    }
                }
                
                // Extract source file (keep this for informational purposes)
                if (metadata->hasProperty("SourceFile"))
                {
                    sourceFile = metadata->getProperty("SourceFile").toString();
                    juce::Logger::writeToLog("Source file: " + sourceFile);
                }
            }
        }
        
        // Process the EQ band parameters
        for (int i = 0; i < EQConstants::numEQBands; ++i)
        {
            juce::String freqId = "Frequency" + juce::String(i);
            juce::String gainId = "Gain" + juce::String(i);
            juce::String qId = "Q" + juce::String(i);
            
            // Get the parameters
            auto* freqParam = parameters.getParameter(freqId);
            auto* gainParam = parameters.getParameter(gainId);
            auto* qParam = parameters.getParameter(qId);
            
            // Handle frequency parameter
            if (object->hasProperty(freqId) && freqParam)
            {
                float normValue = (float)object->getProperty(freqId);
                
                // Keep frequency range adjustments if needed
                if (hasMetadata && i == 0 && frequencyRange[0] > 30.0f)
                {
                    // Adjust the lowest band to better match the detected low frequency
                    float actualFreq = freqParam->convertFrom0to1(normValue);
                    float adjustedFreq = juce::jmin(actualFreq, frequencyRange[0] * 0.8f);
                    normValue = freqParam->convertTo0to1(adjustedFreq);
                    juce::Logger::writeToLog("Adjusted low frequency band to: " + juce::String(adjustedFreq) + "Hz");
                }
                else if (hasMetadata && i == EQConstants::numEQBands - 1 && frequencyRange[1] < 18000.0f)
                {
                    // Adjust the highest band to better match the detected high frequency
                    float actualFreq = freqParam->convertFrom0to1(normValue);
                    float adjustedFreq = juce::jmax(actualFreq, frequencyRange[1] * 1.2f);
                    normValue = freqParam->convertTo0to1(adjustedFreq);
                    juce::Logger::writeToLog("Adjusted high frequency band to: " + juce::String(adjustedFreq) + "Hz");
                }
                
                freqParam->setValueNotifyingHost(normValue);
                anyParamChanged = true;
                juce::Logger::writeToLog("Set " + freqId + " to " + juce::String(normValue) + " (normalized)");
            }
            
            // Handle gain parameter
            if (object->hasProperty(gainId) && gainParam)
            {
                float normValue = (float)object->getProperty(gainId);
                gainParam->setValueNotifyingHost(normValue);
                anyParamChanged = true;
                juce::Logger::writeToLog("Set " + gainId + " to " + juce::String(normValue) + " (normalized)");
            }
            
            // Handle Q parameter - REINTRODUCING MUSICAL Q VALUES
            if (object->hasProperty(qId) && qParam)
            {
                float normValue = (float)object->getProperty(qId);
                float originalQ = qParam->convertFrom0to1(normValue);
                
                juce::Logger::writeToLog("Original Q value for band " + juce::String(i) + ": " +
                                       juce::String(originalQ) + " (normalized: " + juce::String(normValue) + ")");
                
                // Get the frequency for this band for frequency-dependent adjustments
                float freq = 1000.0f; // Default if we can't get actual frequency
                if (freqParam && object->hasProperty(freqId))
                {
                    float freqNormValue = (float)object->getProperty(freqId);
                    freq = freqParam->convertFrom0to1(freqNormValue);
                }
                
                // Apply more musical Q values based on frequency band
                float q = originalQ;
                
                // Apply band-specific Q caps for more musical results
                if (i == 0) // Sub bass (20-150Hz)
                {
                    // Sub bass needs wider Q for natural sound
                    q = juce::jmin(q, 0.8f);
                    juce::Logger::writeToLog("Capping sub bass Q to 0.8");
                }
                else if (i == 1) // Bass (150-400Hz)
                {
                    // Bass needs moderate Q
                    q = juce::jmin(q, 1.0f);
                    juce::Logger::writeToLog("Capping bass Q to 1.0");
                }
                else if (i == 2) // Low mids (400-800Hz)
                {
                    // Low mids can have slightly higher Q
                    q = juce::jmin(q, 1.2f);
                    juce::Logger::writeToLog("Capping low mids Q to 1.2");
                }
                else if (i == 3) // Mids (800-2500Hz)
                {
                    // Mids can have moderate Q
                    q = juce::jmin(q, 1.5f);
                    juce::Logger::writeToLog("Capping mids Q to 1.5");
                }
                else if (i == 4) // High mids (2500-5000Hz)
                {
                    // High mids can have higher Q for precision
                    q = juce::jmin(q, 1.8f);
                    juce::Logger::writeToLog("Capping high mids Q to 1.8");
                }
                else if (i == 5) // Presence (5000-10000Hz)
                {
                    // Presence can have higher Q
                    q = juce::jmin(q, 2.0f);
                    juce::Logger::writeToLog("Capping presence Q to 2.0");
                }
                else // Air (10000-20000Hz)
                {
                    // Air needs moderate Q for natural sound
                    q = juce::jmin(q, 1.5f);
                    juce::Logger::writeToLog("Capping air Q to 1.5");
                }
                
                // Apply gain-dependent Q adjustment
                // Higher gain settings need wider Q for musical results
                if (gainParam && object->hasProperty(gainId))
                {
                    float gainNormValue = (float)object->getProperty(gainId);
                    float gain = gainParam->convertFrom0to1(gainNormValue);
                    
                    // Reduce Q for high gain settings
                    if (std::abs(gain) > 10.0f)
                    {
                        q *= 0.7f; // Significantly reduce Q for very high gain
                        juce::Logger::writeToLog("Reducing Q due to high gain (>10dB): " + juce::String(q));
                    }
                    else if (std::abs(gain) > 6.0f)
                    {
                        q *= 0.85f; // Moderately reduce Q for high gain
                        juce::Logger::writeToLog("Reducing Q due to moderate-high gain (>6dB): " + juce::String(q));
                    }
                }
                
                // Final safety constraint to reasonable limits
                q = juce::jlimit(EQConstants::minQ, EQConstants::maxQ, q);
                
                // Convert back to normalized
                normValue = qParam->convertTo0to1(q);
                
                juce::Logger::writeToLog("Final adjusted Q for band " + juce::String(i) +
                                       ": " + juce::String(q) + " (normalized: " + juce::String(normValue) + ")");
                
                qParam->setValueNotifyingHost(normValue);
                anyParamChanged = true;
            }
        }
        
        // Also check for ZeroLatency parameter
        if (object->hasProperty("ZeroLatency"))
        {
            float normValue = (float)object->getProperty("ZeroLatency");
            auto* param = parameters.getParameter("ZeroLatency");
            if (param) {
                param->setValueNotifyingHost(normValue);
                anyParamChanged = true;
                juce::Logger::writeToLog("Set ZeroLatency to " + juce::String(normValue));
            }
        }
        
        // Set the current preset name to the file name without extension
        setCurrentPresetName(jsonFile.getFileNameWithoutExtension());
    }
    
    // Force update filters after loading preset
    if (anyParamChanged) {
        // First update the filters with the new parameter values
        updateFilters();
        juce::Logger::writeToLog("Updated filters after loading preset");
        
        // Notify the host that parameters have changed
        updateHostDisplay();
        
        // Use MessageManager to ensure UI updates happen on the message thread
        juce::MessageManager::callAsync([this]() {
            // Then force the visualizer to update immediately
            if (auto* editor = dynamic_cast<EQAudioProcessorEditor*>(getActiveEditor()))
            {
                // Get the latest filter coefficients
                auto filterCoeffs = getFilterCoefficients();
                
                // Update the visualizer with these coefficients
                editor->getVisualizer().updateFilters(filterCoeffs);
                
                // Force a repaint to show the changes immediately
                editor->getVisualizer().repaint();
                
                // Also repaint the entire editor to update all UI components
                editor->repaint();
                
                juce::Logger::writeToLog("Forced visualizer and editor update after loading preset");
            }
        });
        
        // Removed the sendChangeMessage() call that was causing the error
    }
    
    // Reset loading flag
    m_isLoadingPreset = false;
    
    return anyParamChanged;
}

bool EQAudioProcessor::loadPresetFromFile(const juce::File& presetFile)
{
    // Simply call the existing implementation
    return loadPresetFromJSON(presetFile);
}

juce::File EQAudioProcessor::getPresetsDirectory()
{
    // First try a directory relative to the plugin (most visible to users)
    juce::File pluginDir = juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                          .getParentDirectory();
    
    // Try a "Presets" directory next to the plugin
    juce::File presetsDir = pluginDir.getChildFile("Presets");
    
    // Check if we can create/write to this directory
    bool canUsePluginDir = false;
    
    if (!presetsDir.exists())
    {
        juce::Result result = presetsDir.createDirectory();
        canUsePluginDir = result.wasOk();
        
        if (!canUsePluginDir)
            juce::Logger::writeToLog("Cannot create presets directory in plugin folder: " + result.getErrorMessage());
    }
    else
    {
        // Directory exists, check if it's writable
        juce::File testFile = presetsDir.getChildFile("write_test.tmp");
        
        if (testFile.create())
        {
            testFile.deleteFile();
            canUsePluginDir = true;
        }
        else
        {
            juce::Logger::writeToLog("Plugin presets directory exists but is not writable");
        }
    }
    
    // If we can use the plugin directory, return it
    if (canUsePluginDir)
    {
        juce::Logger::writeToLog("Using presets directory in plugin folder: " + presetsDir.getFullPathName());
        return presetsDir;
    }
    
    // Try a "Presets" directory in the distribution package (one level up from plugin)
    juce::File distPresetsDir = pluginDir.getParentDirectory().getChildFile("Presets");
    bool canUseDistDir = false;
    
    if (distPresetsDir.exists() && distPresetsDir.isDirectory())
    {
        // Check if it's writable
        juce::File testFile = distPresetsDir.getChildFile("write_test.tmp");
        
        if (testFile.create())
        {
            testFile.deleteFile();
            canUseDistDir = true;
        }
    }
    
    // If we can use the distribution directory, return it
    if (canUseDistDir)
    {
        juce::Logger::writeToLog("Using presets directory from distribution package: " + distPresetsDir.getFullPathName());
        return distPresetsDir;
    }
    
    // If plugin directory is not writable, fall back to user documents
    juce::File docsPresetsDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                               .getChildFile("EQPlugin")
                               .getChildFile("Presets");
    
    // Create the directory if it doesn't exist
    if (!docsPresetsDir.exists())
    {
        juce::Result result = docsPresetsDir.createDirectory();
        if (result.failed())
        {
            juce::Logger::writeToLog("Failed to create presets directory in documents: " + result.getErrorMessage());
            
            // If that fails, try the temporary directory (almost always writable)
            juce::File tempPresetsDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                      .getChildFile("EQPlugin_Presets");
            
            if (!tempPresetsDir.exists())
                tempPresetsDir.createDirectory();
            
            juce::Logger::writeToLog("Using temporary presets directory: " + tempPresetsDir.getFullPathName());
            return tempPresetsDir;
        }
    }
    
    juce::Logger::writeToLog("Using presets directory in documents: " + docsPresetsDir.getFullPathName());
    return docsPresetsDir;
}


juce::Array<juce::File> EQAudioProcessor::getAvailablePresets()
{
    juce::Array<juce::File> presets;
    
    // Get the presets directory
    juce::File presetsDir = getPresetsDirectory();
    juce::Logger::writeToLog("Looking for presets in: " + presetsDir.getFullPathName());
    
    // Find all JSON files in the directory
    for (auto entry : juce::RangedDirectoryIterator(presetsDir, false, "*.json"))
    {
        presets.add(entry.getFile());
        juce::Logger::writeToLog("Found preset: " + entry.getFile().getFileName());
    }
    
    juce::Logger::writeToLog("Found " + juce::String(presets.size()) + " presets");
    return presets;
}

void EQAudioProcessor::copyPythonScriptIfNeeded()
{
    juce::Logger::writeToLog("Using external analyzer executable");
}

bool EQAudioProcessor::savePresetToJSON(const juce::File& jsonFile)
{
    // Create a JSON object to store the preset data
    juce::DynamicObject::Ptr rootObject = new juce::DynamicObject();
    
    // Add each parameter directly to the root object (for backward compatibility)
    for (int i = 0; i < EQConstants::numEQBands; ++i)
    {
        auto* freqParam = parameters.getParameter("Frequency" + juce::String(i));
        auto* gainParam = parameters.getParameter("Gain" + juce::String(i));
        auto* qParam = parameters.getParameter("Q" + juce::String(i));
        
        if (freqParam != nullptr && gainParam != nullptr && qParam != nullptr)
        {
            // Add normalized values to the root object
            rootObject->setProperty("Frequency" + juce::String(i), freqParam->getValue());
            rootObject->setProperty("Gain" + juce::String(i), gainParam->getValue());
            rootObject->setProperty("Q" + juce::String(i), qParam->getValue());
        }
    }
    
    // Add ZeroLatency parameter
    auto* zeroLatencyParam = parameters.getParameter("ZeroLatency");
    if (zeroLatencyParam != nullptr)
    {
        rootObject->setProperty("ZeroLatency", zeroLatencyParam->getValue());
    }
    
    // Add metadata if available
    if (hasEnhancedMetadata())
    {
        juce::DynamicObject::Ptr metadataObject = new juce::DynamicObject();
        
        // Add basic metadata
        metadataObject->setProperty("CreatedBy", "EQPlugin");
        metadataObject->setProperty("SourceFile", currentPresetMetadata.sourceFile);
        
        // Add creation date (use existing or create new)
        if (currentPresetMetadata.creationDate.isNotEmpty())
        {
            metadataObject->setProperty("CreationDate", currentPresetMetadata.creationDate);
        }
        else
        {
            juce::Time now = juce::Time::getCurrentTime();
            metadataObject->setProperty("CreationDate", now.formatted("%Y-%m-%d %H:%M:%S"));
        }
        
        // Add transient density
        metadataObject->setProperty("TransientDensity", currentPresetMetadata.transientDensity);
        
        // Add frequency range
        juce::Array<juce::var> rangeArray;
        rangeArray.add(currentPresetMetadata.frequencyRange[0]);
        rangeArray.add(currentPresetMetadata.frequencyRange[1]);
        metadataObject->setProperty("FrequencyRange", rangeArray);
        
        // Add spectral balance if available
        juce::Array<juce::var> balanceArray;
        for (float balance : currentPresetMetadata.spectralBalance)
        {
            balanceArray.add(balance);
        }
        if (balanceArray.size() > 0)
        {
            metadataObject->setProperty("SpectralBalance", balanceArray);
        }
        
        // Add the metadata object to the root
        rootObject->setProperty("Metadata", metadataObject.get());
    }
    
    // Convert the object to a JSON string
    juce::var jsonVar(rootObject.get());
    juce::String jsonString = juce::JSON::toString(jsonVar, true);
    
    // Write the JSON string to the file
    bool success = jsonFile.replaceWithText(jsonString);
    
    if (success)
    {
        // Update the current preset name
        currentPresetName = jsonFile.getFileNameWithoutExtension();
    }
    
    return success;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EQAudioProcessor();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout EQAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    // Define frequency ranges with crossover points
    const std::array<std::pair<float, float>, EQConstants::numEQBands> frequencyRanges = {
        std::make_pair(20.0f, 80.0f),    // Sub-Bass
        std::make_pair(70.0f, 300.0f),   // Bass
        std::make_pair(250.0f, 600.0f),  // Low Midrange
        std::make_pair(500.0f, 2500.0f), // Midrange
        std::make_pair(2000.0f, 5000.0f),// Upper Midrange
        std::make_pair(4000.0f, 7000.0f),// Presence
        std::make_pair(6000.0f, 20000.0f)// Brilliance
    };
    
    // Create parameters with wider gain range for more dramatic effect
    for (int i = 0; i < EQConstants::numEQBands; ++i)
    {
        auto [minFreq, maxFreq] = frequencyRanges[i];
        
        // Use skewed normalization for frequency (logarithmic)
        auto freqRange = juce::NormalisableRange<float>(minFreq, maxFreq, 0.1f, 0.5f);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "Frequency" + std::to_string(i), "Frequency " + std::to_string(i + 1),
            freqRange, (minFreq + maxFreq) / 2));
        
        // Wider gain range for more dramatic effect (-24dB to +24dB)
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "Gain" + std::to_string(i), "Gain " + std::to_string(i + 1),
            juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));
        
        // Q range from 0.1 (very wide) to 10.0 (very narrow)
        auto qRange = juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f, 0.5f);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "Q" + std::to_string(i), "Q " + std::to_string(i + 1),
            qRange, 1.0f));
    }
    
    // Add zero-latency mode parameter outside the loop
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "ZeroLatency", 
        "Zero Latency", 
        true, 
        "Processing Mode"));
    
    return { params.begin(), params.end() };
}


float EQAudioProcessor::generatePinkNoise()
{
    // White noise sample
    float white = random.nextFloat() * 2.0f - 1.0f;
    
    // Pink noise filter (Voss-McCartney algorithm)
    pinkNoiseBuffer[0] = 0.99886f * pinkNoiseBuffer[0] + white * 0.0555179f;
    pinkNoiseBuffer[1] = 0.99332f * pinkNoiseBuffer[1] + white * 0.0750759f;
    pinkNoiseBuffer[2] = 0.96900f * pinkNoiseBuffer[2] + white * 0.1538520f;
    pinkNoiseBuffer[3] = 0.86650f * pinkNoiseBuffer[3] + white * 0.3104856f;
    pinkNoiseBuffer[4] = 0.55000f * pinkNoiseBuffer[4] + white * 0.5329522f;
    pinkNoiseBuffer[5] = -0.7616f * pinkNoiseBuffer[5] - white * 0.0168980f;
    
    // Mix and scale
    float pink = pinkNoiseBuffer[0] + pinkNoiseBuffer[1] + pinkNoiseBuffer[2] + 
                 pinkNoiseBuffer[3] + pinkNoiseBuffer[4] + pinkNoiseBuffer[5] + 
                 pinkNoiseBuffer[6] + white * 0.5362f;
    
    pinkNoiseBuffer[6] = white * 0.115926f;
    
    // Normalize to roughly -1.0 to 1.0 range
    return pink * 0.11f;
}
