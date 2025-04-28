# Parametric EQ Plugin: Technical Documentation

## Table of Contents

1. [Code Architecture Overview](#code-architecture-overview)
2. [Core Components](#core-components)
   - [Audio Processing (`EQAudioProcessor` class)](#audio-processing-eqaudioprocessor-class)
     - [Filter Implementation](#filter-implementation)
     - [Parameter Management](#parameter-management)
     - [Signal Processing Pipeline](#signal-processing-pipeline)
     - [Test Signal Generation](#test-signal-generation)
   - [Visualization Components](#visualization-components)
     - [Spectrum Analyzer (`SpectrumAnalyzer` class)](#spectrum-analyzer-spectrumanalyzer-class)
     - [EQ Visualizer (`EQVisualizer` class)](#eq-visualizer-eqvisualizer-class)
   - [Preset Management (`PresetBrowser` class)](#preset-management-presetbrowser-class)
     - [Preset Browser Initialization](#preset-browser-initialization)
     - [Loading Presets](#loading-presets)
     - [Preset Selection](#preset-selection)
     - [Preset List Display](#preset-list-display)
     - [Handling Save Button](#handling-save-button)
3. [Key Algorithms](#key-algorithms)
   - [Parameter Smoothing](#parameter-smoothing)
   - [Error Handling and Logging](#error-handling-and-logging)
4. [Design Patterns](#design-patterns)
   - [Observer Pattern](#observer-pattern)
   - [Model-View-Controller (MVC)](#model-view-controller-mvc)
5. [Performance Considerations](#performance-considerations)
   - [CPU Optimization](#cpu-optimization)
6. [Conclusion](#conclusion)

## Code Architecture Overview

This document provides a technical overview of the parametric EQ plugin implementation, suitable for academic dissertation reference, with actual code snippets from the implementation.

## Core Components

### Audio Processing (`EQAudioProcessor` class)

#### Filter Implementation
The plugin uses JUCE's DSP module for efficient filter implementation:

```cpp
// Filter coefficient calculation based on processing mode
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
}
catch (const std::exception& e) {
    // Error handling
}
```

#### Parameter Management
The plugin implements parameter smoothing to prevent audio artifacts:

```cpp
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
        
        // Update filter coefficients...
    }
}
```

#### Signal Processing Pipeline
The main audio processing occurs in the `processBlock` method:

```cpp
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
            // Test signal generation code...
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
}
```

#### Test Signal Generation
The plugin includes a test signal generator for calibration:

```cpp
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
                break;
                
            // Other signal types...
        }
        
        // Apply amplitude
        channelData[sample] = signalValue * amplitude;
    }
}
```

### Visualization Components

#### Spectrum Analyzer (`SpectrumAnalyzer` class)
The spectrum analyzer provides real-time frequency visualization:

```cpp
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
    
    // Draw spectrum visualization...
}
```

#### EQ Visualizer (`EQVisualizer` class)
The EQ visualizer shows the frequency response curve:

```cpp
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
    
    // Additional initialization...
}
```

### Preset Management (`PresetBrowser` class)

#### Preset Browser Initialization
The preset browser manages saving, loading, and deleting presets:

```cpp
PresetBrowser::PresetBrowser(EQAudioProcessor& processor)
    : audioProcessor(processor)
{
    // Set up the list box
    presetList.setModel(this);
    presetList.setRowHeight(24);
    presetList.setMultipleSelectionEnabled(false);
    
    addAndMakeVisible(presetList);
    
    // Set up buttons
    saveButton.setButtonText("Save");
    addAndMakeVisible(saveButton);
    
    deleteButton.setButtonText("Delete");
    addAndMakeVisible(deleteButton);
    
    // Additional setup...
}
```

#### Loading Presets
The plugin loads presets from JSON files:

```cpp
void PresetBrowser::loadSelectedPreset()
{
    int selectedRow = presetList.getSelectedRow();
    
    if (selectedRow >= 0 && selectedRow < availablePresets.size())
    {
        juce::File selectedPreset = availablePresets[selectedRow];
        
        // Load the preset using the processor
        audioProcessor.loadPresetFromJSON(selectedPreset);
        
        // Reset the preset changed flag
        resetPresetChangedFlag();
    }
}
```

#### Preset Selection
The plugin allows users to select presets from a list:

```cpp
void PresetBrowser::selectPreset(const juce::String& presetName)
{
    // Find the preset in the list and select it
    for (int i = 0; i < availablePresets.size(); ++i)
    {
        if (availablePresets[i].getFileNameWithoutExtension() == presetName)
        {
            presetList.selectRow(i);
            break;
        }
    }
}
```

#### Preset List Display
The plugin displays presets in a customized list:

```cpp
void PresetBrowser::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowNumber >= 0 && rowNumber < availablePresets.size())
    {
        if (rowIsSelected)
            g.fillAll(juce::Colours::lightblue);
        
        g.setColour(juce::Colours::black);
        g.setFont(14.0f);
        
        juce::String presetName = availablePresets[rowNumber].getFileNameWithoutExtension();
        g.drawText(presetName, 2, 0, width - 4, height, juce::Justification::centredLeft, true);
    }
}
```

#### Handling Save Button
The plugin provides a dialog for saving presets:

```cpp
void PresetBrowser::handleSaveButton()
{
    // Log that the save button was clicked
    juce::Logger::writeToLog("Save button clicked");
    
    // Create a dialog to get the preset name
    auto dialog = std::make_shared<juce::AlertWindow>("Save Preset",
                                                   "Enter a name for your preset:",
                                                   juce::AlertWindow::QuestionIcon);
    
    dialog->addTextEditor("presetName", "", "Preset Name:");
    dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    
    // Show dialog and handle result...
}
```

## Key Algorithms

### Parameter Smoothing
The plugin implements parameter smoothing to prevent audio artifacts:

```cpp
// Initialize smoothed values
for (int i = 0; i < EQConstants::numEQBands; ++i)
{
    smoothedFrequency[i].reset(sampleRate, 0.05);
    smoothedGain[i].reset(sampleRate, 0.05);
    smoothedQ[i].reset(sampleRate, 0.05);
    
    // Set initial values
    smoothedFrequency[i].setTargetValue(*parameters.getRawParameterValue("Frequency" + std::to_string(i)));
    smoothedGain[i].setTargetValue(*parameters.getRawParameterValue("Gain" + std::to_string(i)));
    smoothedQ[i].setTargetValue(*parameters.getRawParameterValue("Q" + std::to_string(i)));
}
```

### Error Handling and Logging
The plugin implements robust error handling:

```cpp
try {
    // Processing code...
}
catch (const std::exception& e) {
    // Log any exceptions during processing
    juce::Logger::writeToLog("Exception during audio processing: " + juce::String(e.what()));
    
    // Clear the buffer to prevent noise in case of error
    buffer.clear();
}

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
```

## Design Patterns

### Observer Pattern
The plugin uses the observer pattern for UI updates:

```cpp
// In the processor, notify UI components of parameter changes
if (editor != nullptr)
{
    // Update the visualizer with new filter coefficients
    dynamic_cast<EQAudioProcessorEditor*>(editor)->updateFilterCoefficients(filterCoefficients);
}
```

### Model-View-Controller (MVC)
The plugin follows the MVC pattern:
- Model: `EQAudioProcessor` (audio processing and parameter state)
- View: `PluginEditor` and visualization components
- Controller: Parameter handling and preset management

## Performance Considerations

### CPU Optimization
The plugin uses several techniques to optimize CPU usage:

```cpp
// Prevent denormals for better CPU performance
juce::ScopedNoDenormals noDenormals;

// Only update filters if parameters are still smoothing
if (needsFilterUpdate)
{
    // Update filter coefficients with current smoothed values
    // ...
}

// Process with oversampling only when needed
if (!zeroLatencyMode) {
    oversampling->processSamplesUp(block);
}
```

## Conclusion

This EQ plugin demonstrates a comprehensive implementation of digital audio processing techniques, combining efficient DSP algorithms with an intuitive user interface. The architecture balances performance requirements with sound quality considerations, making it suitable for both educational purposes and practical audio production use.

The code snippets provided illustrate the key components and algorithms used in the implementation, showing how modern C++ and the JUCE framework can be used to create professional audio plugins.