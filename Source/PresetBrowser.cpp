#include "PresetBrowser.h"
#include <vector>
#include <algorithm> // For std::remove
#include <memory>    

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
    
    // Set up button callbacks
    saveButton.onClick = [this]() { handleSaveButton(); };
    deleteButton.onClick = [this]() { handleDeleteButton(); };
    
    // Initial refresh of presets
    refreshPresetList();
}

PresetBrowser::~PresetBrowser()
{
    saveButton.removeListener(this);
    deleteButton.removeListener(this);
}

void PresetBrowser::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    
    g.setColour(juce::Colours::white);
    g.setFont(15.0f);
    g.drawText("Presets", getLocalBounds().removeFromTop(20), juce::Justification::centred, true);
}

void PresetBrowser::resized()
{
    auto area = getLocalBounds();
    
    // Position the title area
    area.removeFromTop(30);
    
    // Position the buttons at the bottom
    auto buttonArea = area.removeFromBottom(30);
    saveButton.setBounds(buttonArea.removeFromLeft(buttonArea.getWidth() / 2).reduced(5));
    deleteButton.setBounds(buttonArea.reduced(5));
    
    // Position the preset list in the remaining area
    presetList.setBounds(area.reduced(5));
}

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

int PresetBrowser::getNumRows()
{
    return availablePresets.size();
}

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

void PresetBrowser::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    // Make sure the row stays selected
    presetList.selectRow(row, false, false);
    presetChanged = true;
}

void PresetBrowser::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
   
}

void PresetBrowser::handleRowSelection(int lastRowSelected)
{
    if (lastRowSelected >= 0 && lastRowSelected < availablePresets.size())
    {
        juce::File selectedPreset = availablePresets[lastRowSelected];
        
        // Load the preset
        if (audioProcessor.loadPresetFromJSON(selectedPreset))
        {
            // Force update of filters and UI
            audioProcessor.updateFilters();
            
            // Use MessageManager to ensure UI updates happen on the message thread
            juce::MessageManager::callAsync([this, selectedPreset]() {
                // FIX: Use juce::AudioProcessorEditor instead of EQAudioProcessorEditor
                if (auto* editor = audioProcessor.getActiveEditor())
                {
                    // Force a repaint of the editor
                    editor->repaint();
                }
                
                // Log success
                juce::Logger::writeToLog("Preset loaded and UI updated: " + selectedPreset.getFileNameWithoutExtension());
            });
        }
    }
}

bool PresetBrowser::hasPresetChanged() const
{
    return presetChanged;
}

void PresetBrowser::resetPresetChangedFlag()
{
    presetChanged = false;
}

void PresetBrowser::buttonClicked(juce::Button* button)
{
    if (button == &saveButton)
    {
        handleSaveButton();
    }
    else if (button == &deleteButton)
    {
        handleDeleteButton();
    }
}

void PresetBrowser::handleSaveButton()
{
    // Log that the save button was clicked
    juce::Logger::writeToLog("Save button clicked");
    
    // Create a dialog to get the preset name
    auto dialog = std::make_shared<juce::AlertWindow>("Save Preset",
                                                   "Enter a name for your preset:",
                                                   juce::AlertWindow::QuestionIcon);
    
    dialog->addTextEditor("presetName", "", "Preset Name:");
    dialog->addButton("Save", 1);
    dialog->addButton("Cancel", 0);
    
    // Keep a reference to the dialog to prevent it from being destroyed
    activeDialogs.push_back(dialog);
    
    // Show the dialog asynchronously
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, dialog](int result) {
            if (result == 1) // Save button was clicked
            {
                juce::String presetName = dialog->getTextEditorContents("presetName").trim();
                
                if (presetName.isNotEmpty())
                {
                    // Log the preset name
                    juce::Logger::writeToLog("Saving preset with name: " + presetName);
                    
                    // Create the preset file
                    juce::File presetFile = audioProcessor.getPresetsDirectory().getChildFile(presetName + "_preset.json");
                    
                    // Log the file path
                    juce::Logger::writeToLog("Saving to file: " + presetFile.getFullPathName());
                    
                    // Save the preset
                    if (audioProcessor.savePresetToJSON(presetFile))
                    {
                        juce::Logger::writeToLog("Preset saved successfully");
                        
                        // Refresh the list of presets
                        refreshPresetList();
                        
                        // Select the newly saved preset
                        selectPreset(presetName);
                    }
                    else
                    {
                        juce::Logger::writeToLog("Failed to save preset");
                        
                        // Show error dialog
                        auto errorDialog = std::make_shared<juce::AlertWindow>(
                            "Save Failed",
                            "Could not save the preset file. You may not have permission to write to the directory.",
                            juce::AlertWindow::WarningIcon
                        );
                        
                        errorDialog->addButton("OK", 0);
                        
                        // Keep a reference to the dialog
                        activeDialogs.push_back(errorDialog);
                        
                        // Show the dialog
                        errorDialog->enterModalState(true, juce::ModalCallbackFunction::create(
                            [this, errorDialog](int) {
                                // Remove the dialog from the list
                                activeDialogs.erase(std::remove_if(activeDialogs.begin(), activeDialogs.end(),
                                    [errorDialog](const std::shared_ptr<juce::AlertWindow>& d) { return d == errorDialog; }),
                                    activeDialogs.end());
                            }
                        ));
                    }
                }
            }
            
            // Remove the dialog from the list
            activeDialogs.erase(std::remove_if(activeDialogs.begin(), activeDialogs.end(),
                [dialog](const std::shared_ptr<juce::AlertWindow>& d) { return d == dialog; }),
                activeDialogs.end());
        }
    ));
}

void PresetBrowser::handleDeleteButton()
{
    int selectedRow = presetList.getSelectedRow();
    juce::Logger::writeToLog("Delete button clicked. Selected row: " + juce::String(selectedRow));
    
    if (selectedRow >= 0 && selectedRow < availablePresets.size())
    {
        juce::File selectedPreset = availablePresets[selectedRow];
        juce::String presetName = selectedPreset.getFileNameWithoutExtension();
        
        // Log the file we're trying to delete
        juce::Logger::writeToLog("Attempting to delete: " + selectedPreset.getFullPathName());
        
        // Create a confirmation dialog
        auto dialog = std::make_shared<juce::AlertWindow>(
            "Delete Preset",
            "Are you sure you want to delete the preset \"" + presetName + "\"?",
            juce::AlertWindow::WarningIcon
        );
        
        dialog->addButton("Delete", 1);
        dialog->addButton("Cancel", 0);
        
        // Keep a reference to the dialog to prevent it from being destroyed
        activeDialogs.push_back(dialog);
        
        // Show the dialog asynchronously
        dialog->enterModalState(true, juce::ModalCallbackFunction::create(
            [this, selectedPreset, dialog, selectedRow](int result) {
                if (result == 1)  // User clicked "Delete"
                {
                    // Actually delete the file
                    if (selectedPreset.existsAsFile())
                    {
                        juce::Logger::writeToLog("Deleting file: " + selectedPreset.getFullPathName());
                        
                        // Try to delete the file
                        bool deleted = selectedPreset.deleteFile();
                        
                        juce::Logger::writeToLog("Delete result: " + juce::String(deleted ? "success" : "failed"));
                        
                        if (deleted)
                        {
                            // Refresh the list to show the preset is gone
                            refreshPresetList();
                            
                            // Select a nearby row if possible
                            if (availablePresets.size() > 0)
                            {
                                int newSelection = juce::jmin(selectedRow, availablePresets.size() - 1);
                                presetList.selectRow(newSelection, false, false);
                            }
                        }
                        else
                        {
                            // Show error if deletion failed
                            auto errorDialog = std::make_shared<juce::AlertWindow>(
                                "Delete Failed",
                                "Could not delete the preset file. It may be in use or you don't have permission.",
                                juce::AlertWindow::WarningIcon
                            );
                            
                            errorDialog->addButton("OK", 0);
                            
                            // Keep a reference to the dialog
                            activeDialogs.push_back(errorDialog);
                            
                            // Show the dialog
                            errorDialog->enterModalState(true, juce::ModalCallbackFunction::create(
                                [this, errorDialog](int) {
                                    // Remove the dialog from the list
                                    activeDialogs.erase(std::remove_if(activeDialogs.begin(), activeDialogs.end(),
                                        [errorDialog](const std::shared_ptr<juce::AlertWindow>& d) { return d == errorDialog; }),
                                        activeDialogs.end());
                                }
                            ));
                        }
                    }
                }
                
                // Remove the dialog from the list
                activeDialogs.erase(std::remove_if(activeDialogs.begin(), activeDialogs.end(),
                    [dialog](const std::shared_ptr<juce::AlertWindow>& d) { return d == dialog; }),
                    activeDialogs.end());
            }
        ));
    }
}

void PresetBrowser::selectedRowsChanged(int lastRowSelected)
{
    if (lastRowSelected >= 0 && lastRowSelected < availablePresets.size())
    {
        juce::File selectedPreset = availablePresets[lastRowSelected];
        
        // Load the preset
        if (audioProcessor.loadPresetFromJSON(selectedPreset))
        {
            // Force update of filters and UI
            audioProcessor.updateFilters();
            
            // Log success
            juce::Logger::writeToLog("Preset loaded: " + selectedPreset.getFileNameWithoutExtension());
            
            // If the editor exists, force it to update
            if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*>(audioProcessor.getActiveEditor()))
            {
                // Force a repaint of the editor
                editor->repaint();
            }
        }
    }
}

void PresetBrowser::refreshPresetList()
{
    // Remember the currently selected preset name if any
    juce::String selectedPresetName;
    int selectedRow = presetList.getSelectedRow();
    if (selectedRow >= 0 && selectedRow < availablePresets.size())
    {
        selectedPresetName = availablePresets[selectedRow].getFileNameWithoutExtension();
    }
    
    // Clear the current list
    availablePresets.clear();
    
    // Get all preset files from the same directory the processor uses
    juce::File presetDir = audioProcessor.getPresetsDirectory();
    presetDir.findChildFiles(availablePresets, juce::File::findFiles, false, "*.json");
    
    // Update the list box
    presetList.updateContent();
    
    // Restore selection if possible
    if (selectedPresetName.isNotEmpty())
    {
        for (int i = 0; i < availablePresets.size(); ++i)
        {
            if (availablePresets[i].getFileNameWithoutExtension() == selectedPresetName)
            {
                presetList.selectRow(i, false, false);
                break;
            }
        }
    }
    
    repaint();
}

