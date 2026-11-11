#include "Spectrum.h"
#include <cmath>

namespace ms
{
void Spectrum::setup (double sampleRate)
{
    fs = sampleRate > 0 ? sampleRate : 48000.0;
    order = fs <= 50000.0 ? 13 : (fs <= 100000.0 ? 14 : 15);
    fftSize = 1 << order;
    fft = std::make_unique<juce::dsp::FFT> (order);
    window.assign ((size_t) fftSize, 0.0f);
    work.assign ((size_t) fftSize * 2, 0.0f);
    windowSum = 0;
    for (int i = 0; i < fftSize; ++i)
    {
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) fftSize);
        windowSum += window[(size_t) i];
    }

    const float fMax = (float) std::min (20000.0, fs * 0.45);
    const float df = (float) (fs / fftSize);
    const float half = std::pow (2.0f, smoothingOct * 0.5f);
    for (int i = 0; i < points; ++i)
    {
        const float f = fMin * std::pow (fMax / fMin, (float) i / (points - 1));
        freq[(size_t) i] = f;
        binLo[(size_t) i] = (int) std::ceil (f / half / df);
        binHi[(size_t) i] = (int) std::floor (f * half / df);
        binPos[(size_t) i] = f / df;
    }
    clear();
}

void Spectrum::clear()
{
    live.fill (-120.0f); peak.fill (-120.0f); holdT.fill (0.0f);
}

void Spectrum::compute (const float* l, const float* r, float dt)
{
    if (fft == nullptr) setup (fs);
    std::fill (work.begin(), work.end(), 0.0f);
    for (int i = 0; i < fftSize; ++i)
        work[(size_t) i] = 0.5f * (l[i] + r[i]) * window[(size_t) i];
    fft->performFrequencyOnlyForwardTransform (work.data(), true);

    // amplitud: un seno de amplitud 1 da |X| = windowSum / 2
    const double norm = 2.0 / windowSum;
    const int nyq = fftSize / 2;
    auto pw = [&] (int k) { k = juce::jlimit (0, nyq, k); const double a = work[(size_t) k] * norm; return a * a; };

    const float rel = speed == 0 ? 12.0f : (speed == 2 ? 60.0f : 28.0f);   // dB por segundo de caída
    for (int i = 0; i < points; ++i)
    {
        double p;
        const int a = binLo[(size_t) i], b = binHi[(size_t) i];
        if (b >= a)
        {
            double s = 0;
            for (int k = a; k <= b; ++k) s += pw (k);
            p = s / (b - a + 1);   // media de potencia: el ruido rosa con 3 dB/oct sale plano
        }
        else
        {
            const float x = binPos[(size_t) i];
            const int k = (int) std::floor (x);
            const float fr = x - (float) k;
            p = pw (k) * (1.0 - fr) + pw (k + 1) * fr;
        }
        const float slope = slopeDbPerOct * std::log2 (freq[(size_t) i] / 1000.0f);
        const float db = (float) (10.0 * std::log10 (p + 1.0e-14)) + slope;

        float& v = live[(size_t) i];
        if (db > v) v = v + (db - v) * 0.75f;
        else v = std::max (db, v - rel * dt);

        float& pk = peak[(size_t) i];
        float& ht = holdT[(size_t) i];
        if (v >= pk) { pk = v; ht = 1.2f; }
        else if (ht > 0) ht -= dt;
        else pk = std::max (v, pk - 6.0f * dt);
    }
}
} // namespace ms
