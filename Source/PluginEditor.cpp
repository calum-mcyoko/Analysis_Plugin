/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/
/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "EQVisualizer.h"
#include "EQConstants.h" // Include the header for EQVisualizer

//==============================================================================
EQAudioProcessorEditor::EQAudioProcessorEditor(EQAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), visualizer(p), presetBrowser(p)
{
    setSize(1000, 800);
    
    loadJsonButton.setButtonText("Load JSON Preset");
    loadJsonButton.addListener(this);
    addAndMakeVisible(loadJsonButton);
    
    analyzeAudioButton.setButtonText("Create Preset from Audio");
    analyzeAudioButton.addListener(this);
    addAndMakeVisible(analyzeAudioButton);
    
    addAndMakeVisible(presetBrowser);
    addAndMakeVisible(visualizer);  // Add visualizer to the editor
    
    // Set up zero latency button
    zeroLatencyButton.setButtonText("Zero Latency Mode");
    zeroLatencyButton.setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(zeroLatencyButton);
    
    // Create attachment to parameter
    zeroLatencyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getParameters(), "ZeroLatency", zeroLatencyButton);
    
    testSignalLabel.setText("Test Signal", juce::dontSendNotification);
    testSignalLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    addAndMakeVisible(testSignalLabel);
    
    testSignalToggle.setButtonText("Enable");
    testSignalToggle.setToggleState(audioProcessor.isTestSignalEnabled(), juce::dontSendNotification);
    testSignalToggle.onClick = [this]() {
        audioProcessor.enableTestSignal(testSignalToggle.getToggleState());
    };
    addAndMakeVisible(testSignalToggle);
    
    testSignalTypeCombo.addItem("Sine Wave", 1);
    testSignalTypeCombo.addItem("White Noise", 2);
    testSignalTypeCombo.addItem("Pink Noise", 3);
    testSignalTypeCombo.setSelectedId(audioProcessor.getTestSignalType() + 1, juce::dontSendNotification);
    testSignalTypeCombo.onChange = [this]() {
        audioProcessor.setTestSignalType(testSignalTypeCombo.getSelectedId() - 1);
    };
    addAndMakeVisible(testSignalTypeCombo);
    
    testSignalFreqLabel.setText("Frequency", juce::dontSendNotification);
    addAndMakeVisible(testSignalFreqLabel);
    
    testSignalFreqSlider.setRange(20.0, 20000.0, 1.0);
    testSignalFreqSlider.setSkewFactorFromMidPoint(1000.0); // Logarithmic scaling
    testSignalFreqSlider.setValue(audioProcessor.getTestSignalFrequency(), juce::dontSendNotification);
    testSignalFreqSlider.setTextValueSuffix(" Hz");
    testSignalFreqSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    testSignalFreqSlider.onValueChange = [this]() {
        audioProcessor.setTestSignalFrequency((float)testSignalFreqSlider.getValue());
    };
    addAndMakeVisible(testSignalFreqSlider);
    
    testSignalAmpLabel.setText("Amplitude", juce::dontSendNotification);
    addAndMakeVisible(testSignalAmpLabel);
    
    testSignalAmpSlider.setRange(0.0, 1.0, 0.01);
    testSignalAmpSlider.setValue(audioProcessor.getTestSignalAmplitude(), juce::dontSendNotification);
    testSignalAmpSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    testSignalAmpSlider.onValueChange = [this]() {
        audioProcessor.setTestSignalAmplitude((float)testSignalAmpSlider.getValue());
    };
    addAndMakeVisible(testSignalAmpSlider);
    
    for (int i = 0; i < EQConstants::numEQBands; ++i)
    {
        // Initialize frequency sliders
        frequencySliders[i].setSliderStyle(juce::Slider::Rotary);
        frequencySliders[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
        addAndMakeVisible(frequencySliders[i]);
        
        // Initialize gain sliders
        gainSliders[i].setSliderStyle(juce::Slider::Rotary);
        gainSliders[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
        addAndMakeVisible(gainSliders[i]);
        
        // Initialize Q sliders
        qSliders[i].setSliderStyle(juce::Slider::Rotary);
        qSliders[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
        addAndMakeVisible(qSliders[i]);
        
        // Initialize band labels
        bandLabels[i] = std::make_unique<juce::Label>("BandLabel" + std::to_string(i),
                                                     "Band " + std::to_string(i + 1));
        bandLabels[i]->setJustificationType(juce::Justification::centred);
        bandLabels[i]->attachToComponent(&frequencySliders[i], false);
        addAndMakeVisible(*bandLabels[i]);
        
        // Attach sliders to parameters
        frequencyAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameters(), "Frequency" + std::to_string(i), frequencySliders[i]);
        gainAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameters(), "Gain" + std::to_string(i), gainSliders[i]);
        qAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameters(), "Q" + std::to_string(i), qSliders[i]);
    }
    startTimerHz(30);
}

EQAudioProcessorEditor::~EQAudioProcessorEditor()
{
    stopTimer();
    // Ensure all attachments are destroyed before the components
    // Clear all attachments first
    for (int i = 0; i < EQConstants::numEQBands; ++i)
    {
        frequencyAttachments[i] = nullptr;
        gainAttachments[i] = nullptr;
        qAttachments[i] = nullptr;
    }
    
    zeroLatencyAttachment = nullptr;
    loadJsonButton.removeListener(this);
    analyzeAudioButton.removeListener(this);
}


//==============================================================================
void EQAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(juce::Colours::white);
    g.setFont(15.0f);
    g.drawFittedText("7-Band EQ", getLocalBounds(), juce::Justification::centred, 1);
}

void EQAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    
    // Create a top area for the title and buttons
    auto topArea = area.removeFromTop(40);
    
    // Position the load JSON button
    loadJsonButton.setBounds(topArea.removeFromLeft(150).reduced(5));
    
    // Position the zero latency button
    zeroLatencyButton.setBounds(topArea.removeFromLeft(150).reduced(5));
    
    // Reserve space at the bottom for test signal controls
    auto bottomArea = area.removeFromBottom(120);
    
    // Position the analyze audio button in the bottom area
    analyzeAudioButton.setBounds(bottomArea.removeFromTop(30).removeFromLeft(200).reduced(5));
    
    // Position test signal controls in the remaining bottom area
    auto testSignalArea = bottomArea.reduced(5);
    
    // First row: Label and Toggle
    auto testRow1 = testSignalArea.removeFromTop(25);
    testSignalLabel.setBounds(testRow1.removeFromLeft(100).reduced(2));
    testSignalToggle.setBounds(testRow1.removeFromLeft(100).reduced(2));
    testSignalTypeCombo.setBounds(testRow1.removeFromLeft(150).reduced(2));
    
    // Second row: Frequency controls
    auto testRow2 = testSignalArea.removeFromTop(25);
    testSignalFreqLabel.setBounds(testRow2.removeFromLeft(80).reduced(2));
    testSignalFreqSlider.setBounds(testRow2.removeFromLeft(300).reduced(2));
    
    // Third row: Amplitude controls
    auto testRow3 = testSignalArea.removeFromTop(25);
    testSignalAmpLabel.setBounds(testRow3.removeFromLeft(80).reduced(2));
    testSignalAmpSlider.setBounds(testRow3.removeFromLeft(300).reduced(2));
    
    // Position the preset browser on the right
    presetBrowser.setBounds(area.removeFromRight(200).reduced(5));
    
    // IMPROVED: Give more vertical space to the visualizer (increased from 250 to 350)
    // This will make the EQ curve more pronounced
    visualizer.setBounds(area.removeFromTop(350).reduced(5));
    
    // Log the visualizer size for debugging
    juce::Logger::writeToLog("Visualizer size: " + juce::String(visualizer.getWidth()) + 
                           " x " + juce::String(visualizer.getHeight()));
    
    // Remaining area for EQ band controls
    auto controlsArea = area.reduced(5);
    
    // IMPROVED: Better calculation of band width to ensure proper spacing
    // Calculate width for each band, ensuring minimum width but keeping them compact
    float bandWidth = juce::jmax(100.0f, controlsArea.getWidth() / (float)EQConstants::numEQBands);
    
    // Position the EQ band controls
    for (int i = 0; i < EQConstants::numEQBands; ++i)
    {
        auto bandArea = controlsArea.removeFromLeft(static_cast<int>(bandWidth)).reduced(5);
        
        // IMPROVED: Better vertical spacing for controls
        // Calculate heights to use available space more effectively
        int sliderHeight = juce::jmin(85, bandArea.getHeight() / 3 - 10);
        
        // Position frequency slider at top with more space
        frequencySliders[i].setBounds(bandArea.removeFromTop(sliderHeight));
        
        // Add spacing
        bandArea.removeFromTop(10);
        
        // Position gain slider in middle
        gainSliders[i].setBounds(bandArea.removeFromTop(sliderHeight));
        
        // Add spacing
        bandArea.removeFromTop(10);
        
        // Position Q slider at bottom
        qSliders[i].setBounds(bandArea.removeFromTop(sliderHeight));
    }
}

void EQAudioProcessorEditor::buttonClicked(juce::Button* button)
{
    if (button == &loadJsonButton)
    {
        // Create a file chooser that won't go out of scope
        fileChooser = std::make_unique<juce::FileChooser>("Select a JSON preset file",
                                                        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                                                        "*.json");
        
        // Show the dialog asynchronously
        fileChooser->launchAsync(juce::FileBrowserComponent::openMode |
                               juce::FileBrowserComponent::canSelectFiles,
                               [this](const juce::FileChooser& fc)
        {
            // Check if a file was selected
            if (fc.getResults().size() > 0)
            {
                juce::File selectedFile = fc.getResults().getReference(0);
                
                // Load the JSON preset
                if (audioProcessor.loadPresetFromJSON(selectedFile))
                {
                    // Success
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                                         "Success",
                                                         "Preset loaded successfully.");
                }
                else
                {
                    // Failed to load
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                         "Error Loading Preset",
                                                         "Failed to load the selected JSON preset file.");
                }
            }
        });
    }
    else if (button == &analyzeAudioButton)
    {
        // Create a file chooser to select an audio file
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select an audio file to analyze...",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            "*.wav;*.mp3;*.aiff;*.flac");
        
        auto folderChooserFlags = juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles;
        
        fileChooser->launchAsync(folderChooserFlags, [this](const juce::FileChooser& fc)
        {
            // Check if a file was selected
            if (fc.getResults().size() > 0)
            {
                juce::File selectedFile = fc.getResults().getReference(0);
                
                // Show a dialog to get the preset name
                auto dialog = std::make_shared<juce::AlertWindow>("Create Preset from Audio",
                                                               "Enter a name for the preset:",
                                                               juce::AlertWindow::QuestionIcon);
                
                dialog->addTextEditor("presetName", selectedFile.getFileNameWithoutExtension(), "Preset Name:");
                dialog->addButton("Create", 1);
                dialog->addButton("Cancel", 0);
                
                // Show the dialog asynchronously
                dialog->enterModalState(true, juce::ModalCallbackFunction::create(
                    [this, selectedFile, dialog](int result)
                    {
                        if (result == 1) // "Create" button
                        {
                            juce::String presetName = dialog->getTextEditorContents("presetName");
                            
                            if (presetName.isNotEmpty())
                            {
                                // Show a progress indicator
                                juce::AlertWindow::showMessageBoxAsync(
                                    juce::AlertWindow::InfoIcon,
                                    "Processing",
                                    "Analyzing audio file. This may take a moment...",
                                    "OK");
                                
                                // Process the audio file in a background thread
                                juce::Thread::launch([this, selectedFile, presetName]()
                                {
                                    bool success = audioProcessor.createPresetFromAudioFile(selectedFile, presetName);
                                    
                                    // Show a message based on the result
                                    if (success)
                                    {
                                        juce::MessageManager::callAsync([this]()
                                        {
                                            juce::AlertWindow::showMessageBoxAsync(
                                                juce::AlertWindow::InfoIcon,
                                                "Success",
                                                "Preset created and loaded successfully!");
                                            
                                            // Refresh the preset browser if you have one
                                            presetBrowser.refreshPresetList();
                                            
                                            // The preset is already loaded by createPresetFromAudioFile
                                        });
                                    }
                                    else
                                    {
                                        juce::MessageManager::callAsync([]()
                                        {
                                            juce::AlertWindow::showMessageBoxAsync(
                                                juce::AlertWindow::WarningIcon,
                                                "Error",
                                                "Failed to create preset from audio file. Make sure Python and Librosa are installed.");
                                        });
                                    }
                                });
                            }
                        }
                    }
                ));
            }
        });
    }
}
void EQAudioProcessorEditor::timerCallback()
{
    // Update the visualizer with the current spectrum data
    visualizer.updateSpectrum(audioProcessor.getSpectrumData());
    
    // Force the visualizer to update its display
    visualizer.repaint();
    
    // Update test signal controls to reflect current processor state
    testSignalToggle.setToggleState(audioProcessor.isTestSignalEnabled(), juce::dontSendNotification);
    testSignalTypeCombo.setSelectedId(audioProcessor.getTestSignalType() + 1, juce::dontSendNotification);
    testSignalFreqSlider.setValue(audioProcessor.getTestSignalFrequency(), juce::dontSendNotification);
    testSignalAmpSlider.setValue(audioProcessor.getTestSignalAmplitude(), juce::dontSendNotification);
    
    // Update zero latency button if the processor state changes externally
    zeroLatencyButton.setToggleState(
        audioProcessor.getParameters().getParameter("ZeroLatency")->getValue() > 0.5f,
        juce::dontSendNotification);
    
    // Update preset browser
    presetBrowser.refreshPresetList();
}