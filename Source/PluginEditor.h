/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "EQVisualizer.h"
#include "PresetBrowser.h"
#include "EQConstants.h"  
#include "SpectrumAnalyzer.h"

class EQAudioProcessorEditor : public juce::AudioProcessorEditor,
                               private juce::Button::Listener,
                               public juce::Timer
{
public:
    EQAudioProcessorEditor (EQAudioProcessor&);
    ~EQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool loadPresetFromFile(const juce::String& filePath);
    
    // Get the visualizer for external updates
    EQVisualizer& getVisualizer() { return visualizer; }
    
    // Force an immediate update of the filter visualization
    void forceFilterUpdate() 
    { 
        visualizer.updateFilters(audioProcessor.getFilterCoefficients());
        visualizer.repaint();
    }

private:
    EQAudioProcessor& audioProcessor;
    
    void timerCallback() override;
    
    // Button listener callback
    void buttonClicked(juce::Button* button) override;
    
    // Buttons
    juce::TextButton loadJsonButton;
    juce::TextButton analyzeAudioButton;
    
    // Preset browser
    PresetBrowser presetBrowser;
    
    // File chooser (used for both loading presets and analyzing audio)
    std::unique_ptr<juce::FileChooser> fileChooser;

    // EQ bands
    // Use the constant from the shared header
    std::array<juce::Slider, EQConstants::numEQBands> frequencySliders;
    std::array<juce::Slider, EQConstants::numEQBands> gainSliders;
    std::array<juce::Slider, EQConstants::numEQBands> qSliders;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, EQConstants::numEQBands> frequencyAttachments;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, EQConstants::numEQBands> gainAttachments;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, EQConstants::numEQBands> qAttachments;
    std::array<std::unique_ptr<juce::Label>, EQConstants::numEQBands> bandLabels;

    // Visualizer
    EQVisualizer visualizer;
    
    // Zero latency mode controls
    juce::ToggleButton zeroLatencyButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> zeroLatencyAttachment;

    std::unique_ptr<SpectrumAnalyzer> spectrumAnalyzer;

    juce::ToggleButton testSignalToggle;
    juce::ComboBox testSignalTypeCombo;
    juce::Slider testSignalFreqSlider;
    juce::Slider testSignalAmpSlider;
    
    // Labels
    juce::Label testSignalLabel;
    juce::Label testSignalFreqLabel;
    juce::Label testSignalAmpLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQAudioProcessorEditor)
};
