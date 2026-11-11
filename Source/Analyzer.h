#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>
#include <vector>

// Todo lo que mide MINDSCOPE en el hilo de audio. No toca nunca el audio que pasa por el plugin.
//  - Loudness EBU R128 / ITU-R BS.1770-4: momentáneo (400 ms), corto plazo (3 s), integrado con doble puerta y LRA.
//  - True peak con sobremuestreo (4x hasta 96 kHz, 2x hasta 192 kHz).
//  - Columnas de la onda por bandas (graves, medios, agudos), 200 por segundo.
//  - Correlación estéreo general y de graves (< 120 Hz) para el aviso LOW MONO.
//  - Un anillo con las últimas muestras para el espectro y el vectorscopio (los lee el hilo de mensajes).
namespace ms
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0; }
    static Biquad lowPass (double fs, double f, double q);
    static Biquad highPass (double fs, double f, double q);
};

struct WaveColumn { float full = 0, low = 0, mid = 0, high = 0; };

class Analyzer
{
public:
    Analyzer();

    void prepare (double sampleRate, int maxBlock);
    // Solo lee. Si mono, R = L.
    void process (const float* left, const float* right, int numSamples) noexcept;

    // ---- loudness (en LUFS; -inf si no hay señal) ----
    std::atomic<float> momentary { -200.0f }, shortTerm { -200.0f }, integrated { -200.0f }, lra { 0.0f };
    std::atomic<float> truePeakMax { -200.0f };      // dBTP desde el último reset
    std::atomic<float> momentaryMax { -200.0f };     // máximo momentáneo desde el reset
    void requestReset() noexcept { resetRequested.store (true); }
    std::atomic<bool> resetRequested { false };
    std::atomic<int> resetCount { 0 };               // la interfaz se entera de que se ha reseteado

    // ---- niveles por canal: pico (se vacía al leer) y RMS de 300 ms (lineal) ----
    std::atomic<float> peakL { 0 }, peakR { 0 }, tpWindow { 0 };   // tpWindow: pico real desde la última lectura
    std::atomic<float> rmsL { 0 }, rmsR { 0 };

    // ---- estéreo ----
    std::atomic<float> correlation { 1.0f }, correlationLow { 1.0f };
    std::atomic<float> lowLevelDb { -200.0f };       // nivel de los graves (< 120 Hz) en dBFS RMS
    std::atomic<float> lowSideDb { -200.0f };        // cuánto lado hay en los graves respecto al centro (dB)

    // ---- onda: cola de columnas (un productor, un consumidor) ----
    static constexpr int columnsPerSecond = 200;
    int readColumns (WaveColumn* dest, int maxCount) noexcept;

    // ---- últimas muestras (para espectro y vectorscopio) ----
    static constexpr int ringSize = 1 << 15;
    // Copia las últimas n muestras (n <= ringSize). Devuelve la posición total escrita.
    juce::int64 copyLatest (float* l, float* r, int n) const noexcept;
    juce::int64 totalWritten() const noexcept { return written.load (std::memory_order_acquire); }

    double getSampleRate() const noexcept { return fs; }
    int oversampling() const noexcept { return osFactor; }

    // Cálculo de los valores a partir de la energía (para las pruebas)
    static float energyToLufs (double z) noexcept { return z > 0 ? (float) (-0.691 + 10.0 * std::log10 (z)) : -200.0f; }

private:
    void resetLoudness() noexcept;
    void finishSubBlock() noexcept;
    void updateIntegrated() noexcept;
    float truePeakSample (int ch, float x) noexcept;

    double fs = 48000.0;

    // K-weighting por canal (2 etapas)
    std::array<Biquad, 2> kShelf, kHigh;

    // sub-bloques de 100 ms
    int subLen = 4800, subPos = 0;
    double subEnergy = 0;                 // suma de cuadrados (L + R) del sub-bloque en curso
    static constexpr int ringBlocks = 30; // 3 s
    std::array<double, ringBlocks> blockEnergy {};
    std::array<int, ringBlocks> blockLen {};
    int blockIdx = 0, blocksFilled = 0;

    // histogramas para el integrado y el LRA: bins de 0,01 LU de -70 a +10 LUFS
    static constexpr int histBins = 8000;
    std::vector<double> histMomE, histShortE;
    std::vector<juce::uint32> histMomN, histShortN;
    juce::uint64 shortCount = 0;

    // true peak
    int osFactor = 4, tapsPerPhase = 16;
    std::vector<float> osCoeffs;                       // [fase][tap]
    std::array<std::vector<float>, 2> osHist;          // historial circular por canal
    int osPos = 0;
    float tpMaxLin = 0, tpWinLin = 0;

    // onda por bandas
    std::array<Biquad, 2> wLow, wMidH, wMidL, wHigh;   // 2 etapas cada uno (Linkwitz-Riley 4)
    int colLen = 240, colPos = 0;
    WaveColumn colNow;
    juce::AbstractFifo colFifo { 4096 };
    std::vector<WaveColumn> colBuf;

    // estéreo
    std::array<Biquad, 2> lpL, lpR;                    // graves < 120 Hz (LR4) por canal
    double cLL = 0, cRR = 0, cLR = 0, lLL = 0, lRR = 0, lLR = 0, lMM = 0, lSS = 0;
    double corrCoef = 0.0, rmsCoef = 0.0, msL = 0, msR = 0;

    // anillo de muestras
    std::vector<float> ringL, ringR;
    std::atomic<juce::int64> written { 0 };

    // picos
    float pkL = 0, pkR = 0;
};
} // namespace ms
