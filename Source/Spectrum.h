#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

// Espectro para la interfaz (hilo de mensajes): FFT con ventana Hann, suavizado de 1/6 de octava,
// inclinación (dB por octava, pivote en 1 kHz), caída limitada y línea de picos con retención.
namespace ms
{
class Spectrum
{
public:
    static constexpr int points = 256;
    static constexpr float fMin = 20.0f;

    void setup (double sampleRate);
    int windowSize() const noexcept { return fftSize; }
    // l y r: las últimas windowSize() muestras. dt: segundos desde el anterior.
    void compute (const float* l, const float* r, float dt);

    float slopeDbPerOct = 4.5f;
    int speed = 1;                 // 0 lenta, 1 media, 2 rápida
    float smoothingOct = 1.0f / 6.0f;

    std::array<float, points> freq {}, live {}, peak {};
    void clear();

private:
    double fs = 48000.0;
    int order = 13, fftSize = 8192;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, work;
    double windowSum = 1.0;
    std::array<float, points> holdT {};
    std::array<float, points> slopeAdd {};
    std::array<int, points> binLo {}, binHi {};
    std::array<float, points> binPos {};
};
} // namespace ms
