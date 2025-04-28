#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <vector>
#include <memory>

// Forward declaration
class EQAudioProcessorEditor;

class PresetBrowser : public juce::Component,
                      public juce::ListBoxModel,
                      public juce::Button::Listener
{
public:
    PresetBrowser(EQAudioProcessor& processor);
    ~PresetBrowser() override;
    
    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    // ListBoxModel overrides
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    void selectedRowsChanged(int lastRowSelected) override;
    
    // Button::Listener override
    void buttonClicked(juce::Button* button) override;
    
    // ListBox callback (not part of an interface)
    void listBoxItemClicked(int row, const juce::MouseEvent&);
    
    // Method to refresh the preset list
    void refreshPresetList();
    
    // Method to select a specific preset
    void selectPreset(const juce::String& presetName);
    
    // Method to load the currently selected preset
    void loadSelectedPreset();
    
    // Methods to handle preset change tracking
    bool hasPresetChanged() const;
    void resetPresetChangedFlag();
    
    // Methods to handle button actions
    void handleRowSelection(int lastRowSelected);
    void handleSaveButton();
    void handleDeleteButton();

private:
    EQAudioProcessor& audioProcessor;
    
    juce::ListBox presetList;
    juce::Array<juce::File> availablePresets;
    
    juce::TextButton saveButton;
    juce::TextButton deleteButton;
    
    bool presetChanged = false;
    
    // Vector to keep track of active dialog windows
    std::vector<std::shared_ptr<juce::AlertWindow>> activeDialogs;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBrowser)
};
