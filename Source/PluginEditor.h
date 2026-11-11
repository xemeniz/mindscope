#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "Spectrum.h"

class MSEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MSEditor (MSProcessor&);
    ~MSEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void emit (const juce::Identifier& ev, const juce::var& v);
    void sendSettings();
    void applySettings();

    MSProcessor& proc;
    std::unique_ptr<juce::WebBrowserComponent> web;
    bool ready = false;

    ms::Spectrum spectrum;
    double specRate = 0;
    std::vector<float> bufL, bufR;
    std::vector<ms::WaveColumn> cols;
    double lastMs = 0;
    int lastReset = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MSEditor)
};
