#include "PluginProcessor.h"
#include "PluginEditor.h"

MSProcessor::MSProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    loadGlobal();
}

static juce::File globalFile()
{
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support").getChildFile ("MINDSCOPE");
    d.createDirectory();
    return d.getChildFile ("settings.json");
}

void MSProcessor::loadGlobal()
{
    auto v = juce::JSON::parse (globalFile().loadFileAsString());
    settings = v.isObject() ? v : juce::var (new juce::DynamicObject());
    settingsChanged();
}

void MSProcessor::saveGlobal()
{
    globalFile().replaceWithText (juce::JSON::toString (settings, true));
}

void MSProcessor::prepareToPlay (double sampleRate, int block) { analyzer.prepare (sampleRate, block); }

bool MSProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
}

void MSProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    midi.clear();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch) buffer.clear (ch, 0, buffer.getNumSamples());

    // Reset automático del integrado al darle a play en Ableton
    bool isPlaying = false;
    if (! isStandalone())
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition()) isPlaying = pos->getIsPlaying();
    if (isPlaying && ! wasPlaying && autoReset.load()) analyzer.requestReset();
    wasPlaying = isPlaying;
    playing.store (isPlaying);

    // Solo se lee: el audio sale tal cual entra.
    const int n = buffer.getNumSamples();
    if (n <= 0 || buffer.getNumChannels() == 0) return;
    const float* l = buffer.getReadPointer (0);
    const float* r = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : nullptr;
    analyzer.process (l, r, n);
}

juce::AudioProcessorEditor* MSProcessor::createEditor() { return new MSEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MSProcessor(); }
