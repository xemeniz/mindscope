// MINDSCOPE solo mide: el audio tiene que salir idéntico, bit a bit, con cualquier tamaño de bloque y en mono o estéreo.
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdio>
#include <random>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    int fails = 0;
    for (int chans : { 2, 1 })
        for (int blk : { 32, 512, 1024, 4096 })
        {
            std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
            juce::AudioProcessor::BusesLayout lay;
            lay.inputBuses.add (chans == 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono());
            lay.outputBuses.add (lay.inputBuses[0]);
            if (! p->setBusesLayout (lay)) { std::printf ("  FALLO: no acepta %d canales\n", chans); ++fails; continue; }
            p->prepareToPlay (44100.0, blk);
            std::mt19937 g ((unsigned) blk); std::uniform_real_distribution<float> u (-1.2f, 1.2f);
            juce::AudioBuffer<float> b (chans, blk), ref (chans, blk);
            juce::MidiBuffer m;
            bool same = true;
            for (int k = 0; k < 200; ++k)
            {
                for (int c = 0; c < chans; ++c) for (int i = 0; i < blk; ++i) b.setSample (c, i, u (g));
                ref.makeCopyOf (b);
                p->processBlock (b, m);
                for (int c = 0; c < chans; ++c)
                    if (std::memcmp (b.getReadPointer (c), ref.getReadPointer (c), sizeof (float) * (size_t) blk) != 0) same = false;
            }
            std::printf ("  %s: %d canal(es), bloques de %d: el audio sale %s\n", same ? "ok" : "FALLO", chans, blk, same ? "idéntico" : "CAMBIADO");
            if (! same) ++fails;
        }
    std::printf (fails ? "\n%d fallos\n" : "\nTodo bien\n", fails);
    return fails ? 1 : 0;
}
