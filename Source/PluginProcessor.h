#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Analyzer.h"

class MSProcessor : public juce::AudioProcessor
{
public:
    MSProcessor();
    ~MSProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    bool isStandalone() const { return wrapperType == wrapperType_Standalone; }

    ms::Analyzer analyzer;
    std::atomic<bool> playing { false };

    // Ajustes globales (los mismos en todos los proyectos): tema, inclinación del espectro, etc.
    juce::var settings;
    void loadGlobal();
    void saveGlobal();
    std::atomic<bool> autoReset { true };   // copia de settings.autoreset para el hilo de audio
    void settingsChanged() { autoReset.store ((bool) settings.getProperty ("autoreset", true)); }

private:
    bool wasPlaying = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MSProcessor)
};
