// Pruebas del motor de MINDSCOPE sin host ni interfaz.
// Loudness con las señales de prueba de EBU Tech 3341 / 3342, true peak, correlación, onda por bandas y espectro.
#include "../Source/Analyzer.h"
#include "../Source/Spectrum.h"
#include <cstdio>
#include <cmath>
#include <random>
#include <chrono>

static int fails = 0, oks = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++fails; std::printf ("  FALLO: " __VA_ARGS__); std::printf ("\n"); } else { ++oks; } } while (0)

static const double twoPi = 6.283185307179586;

// Pasa una señal por el analizador en bloques de 512. gen(n, &l, &r)
template <typename Gen>
static void run (ms::Analyzer& a, double fs, double seconds, Gen gen, juce::int64& n0)
{
    const int blk = 512;
    std::vector<float> l (blk), r (blk);
    const juce::int64 total = (juce::int64) std::llround (seconds * fs);
    for (juce::int64 done = 0; done < total; done += blk)
    {
        const int n = (int) std::min<juce::int64> (blk, total - done);
        for (int i = 0; i < n; ++i) gen (n0 + i, l[(size_t) i], r[(size_t) i]);
        a.process (l.data(), r.data(), n);
        n0 += n;
    }
}

static auto sine (double fs, double f, double dbfs)
{
    const double amp = std::pow (10.0, dbfs / 20.0);
    return [=] (juce::int64 n, float& l, float& r) { l = r = (float) (amp * std::sin (twoPi * f * (double) n / fs)); };
}

static void loudnessTests (double fs)
{
    std::printf ("Loudness a %.0f Hz\n", fs);
    {
        ms::Analyzer a; a.prepare (fs, 512); juce::int64 n = 0;
        run (a, fs, 20.0, sine (fs, 1000.0, -23.0), n);
        CHECK (std::abs (a.momentary.load() + 23.0f) <= 0.1f, "3341-1 M = %.2f (debe -23)", a.momentary.load());
        CHECK (std::abs (a.shortTerm.load() + 23.0f) <= 0.1f, "3341-1 S = %.2f (debe -23)", a.shortTerm.load());
        CHECK (std::abs (a.integrated.load() + 23.0f) <= 0.1f, "3341-1 I = %.2f (debe -23)", a.integrated.load());
        std::printf ("  seno 1 kHz -23 dBFS: M %.2f  S %.2f  I %.2f\n", a.momentary.load(), a.shortTerm.load(), a.integrated.load());
    }
    {
        ms::Analyzer a; a.prepare (fs, 512); juce::int64 n = 0;
        run (a, fs, 20.0, sine (fs, 1000.0, -33.0), n);
        CHECK (std::abs (a.integrated.load() + 33.0f) <= 0.1f, "3341-2 I = %.2f (debe -33)", a.integrated.load());
    }
    {   // 3341-3: -36 / -23 / -36 (10 / 60 / 10 s) -> I = -23
        ms::Analyzer a; a.prepare (fs, 512); juce::int64 n = 0;
        run (a, fs, 10.0, sine (fs, 1000.0, -36.0), n);
        run (a, fs, 60.0, sine (fs, 1000.0, -23.0), n);
        run (a, fs, 10.0, sine (fs, 1000.0, -36.0), n);
        CHECK (std::abs (a.integrated.load() + 23.0f) <= 0.1f, "3341-3 I = %.2f (debe -23)", a.integrated.load());
        std::printf ("  3341-3 (puerta relativa): I %.2f\n", a.integrated.load());
    }
    {   // 3341-4: -72 / -36 / -23 / -36 / -72 (10/10/60/10/10 s) -> I = -23
        ms::Analyzer a; a.prepare (fs, 512); juce::int64 n = 0;
        run (a, fs, 10.0, sine (fs, 1000.0, -72.0), n);
        run (a, fs, 10.0, sine (fs, 1000.0, -36.0), n);
        run (a, fs, 60.0, sine (fs, 1000.0, -23.0), n);
        run (a, fs, 10.0, sine (fs, 1000.0, -36.0), n);
        run (a, fs, 10.0, sine (fs, 1000.0, -72.0), n);
        CHECK (std::abs (a.integrated.load() + 23.0f) <= 0.1f, "3341-4 I = %.2f (debe -23)", a.integrated.load());
        std::printf ("  3341-4 (puertas absoluta y relativa): I %.2f\n", a.integrated.load());
    }
    {   // 3342-1: -20 / -30 dBFS (20 s cada uno) -> LRA 10 +-1
        ms::Analyzer a; a.prepare (fs, 512); juce::int64 n = 0;
        run (a, fs, 20.0, sine (fs, 1000.0, -20.0), n);
        run (a, fs, 20.0, sine (fs, 1000.0, -30.0), n);
        CHECK (std::abs (a.lra.load() - 10.0f) <= 1.0f, "3342-1 LRA = %.2f (debe 10)", a.lra.load());
        std::printf ("  3342-1: LRA %.2f\n", a.lra.load());
    }
    {   // 3342-2: -20 / -15 dBFS -> LRA 5 +-1
        ms::Analyzer a; a.prepare (fs, 512); juce::int64 n = 0;
        run (a, fs, 20.0, sine (fs, 1000.0, -20.0), n);
        run (a, fs, 20.0, sine (fs, 1000.0, -15.0), n);
        CHECK (std::abs (a.lra.load() - 5.0f) <= 1.0f, "3342-2 LRA = %.2f (debe 5)", a.lra.load());
    }
    {   // reset
        ms::Analyzer a; a.prepare (fs, 512); juce::int64 n = 0;
        run (a, fs, 5.0, sine (fs, 1000.0, -23.0), n);
        a.requestReset();
        run (a, fs, 0.01, sine (fs, 1000.0, -23.0), n);
        CHECK (a.integrated.load() < -150.0f, "RESET deja el integrado vacío (%.1f)", a.integrated.load());
        CHECK (a.truePeakMax.load() < 0.0f && a.truePeakMax.load() > -24.0f, "tras RESET el true peak vuelve a medir");
        run (a, fs, 5.0, sine (fs, 1000.0, -18.0), n);
        CHECK (std::abs (a.integrated.load() + 18.0f) <= 0.2f, "tras RESET mide solo lo nuevo (%.2f, debe -18)", a.integrated.load());
    }
}

static void truePeakTests (double fs)
{
    std::printf ("True peak a %.0f Hz\n", fs);
    {   // seno a fs/4 con 45 grados: las muestras se quedan en -3 dB, el pico real es 0 dBTP
        ms::Analyzer a; a.prepare (fs, 512); juce::int64 n = 0;
        run (a, fs, 1.0, [=] (juce::int64 k, float& l, float& r) { l = r = (float) std::sin (twoPi * 0.25 * (double) k + 0.7853981633974483); }, n);
        const float tp = a.truePeakMax.load();
        if (a.oversampling() > 1) CHECK (std::abs (tp) <= 0.5f, "fs/4 a 45 grados: TP = %.2f dBTP (debe ~0)", tp);
        std::printf ("  fs/4 a 45 grados (muestras a -3 dB): TP %.2f dBTP (x%d)\n", tp, a.oversampling());
    }
    {
        ms::Analyzer a; a.prepare (fs, 512); juce::int64 n = 0;
        run (a, fs, 1.0, sine (fs, 997.0, -6.0206), n);
        CHECK (std::abs (a.truePeakMax.load() + 6.02f) <= 0.2f, "997 Hz a -6 dBFS: TP = %.2f", a.truePeakMax.load());
    }
}

static void stereoTests()
{
    std::printf ("Estéreo\n");
    const double fs = 48000.0;
    {
        ms::Analyzer a; a.prepare (fs, 512); juce::int64 n = 0;
        run (a, fs, 1.0, sine (fs, 440.0, -12.0), n);
        CHECK (a.correlation.load() > 0.99f, "mono: correlación +1 (%.2f)", a.correlation.load());
        CHECK (a.correlationLow.load() > 0.99f, "mono: graves +1");
    }
    {
        ms::Analyzer a; a.prepare (fs, 512); juce::int64 n = 0;
        run (a, fs, 1.0, [=] (juce::int64 k, float& l, float& r) { l = (float) (0.3 * std::sin (twoPi * 440.0 * k / fs)); r = -l; }, n);
        CHECK (a.correlation.load() < -0.99f, "fase invertida: correlación -1 (%.2f)", a.correlation.load());
    }
    {
        ms::Analyzer a; a.prepare (fs, 512); juce::int64 n = 0;
        std::mt19937 g (7); std::uniform_real_distribution<float> u (-0.3f, 0.3f);
        run (a, fs, 2.0, [&] (juce::int64, float& l, float& r) { l = u (g); r = u (g); }, n);
        CHECK (std::abs (a.correlation.load()) < 0.15f, "ruido independiente: correlación ~0 (%.2f)", a.correlation.load());
    }
    {   // bajo abierto: 55 Hz con el lado R desfasado 90 grados -> aviso; agudos en estéreo no deben contar
        ms::Analyzer a; a.prepare (fs, 512); juce::int64 n = 0;
        run (a, fs, 2.0, [=] (juce::int64 k, float& l, float& r) {
            const double t = (double) k / fs;
            l = (float) (0.3 * std::sin (twoPi * 55.0 * t));
            r = (float) (0.3 * std::cos (twoPi * 55.0 * t)); }, n);
        CHECK (a.correlationLow.load() < 0.3f, "bajo en estéreo: correlación de graves baja (%.2f)", a.correlationLow.load());
        CHECK (a.lowSideDb.load() > -3.0f, "bajo en estéreo: mucho lado en graves (%.1f dB)", a.lowSideDb.load());
    }
    {   // bajo mono + pads muy abiertos arriba -> graves OK
        ms::Analyzer a; a.prepare (fs, 512); juce::int64 n = 0;
        std::mt19937 g (3); std::uniform_real_distribution<float> u (-0.2f, 0.2f);
        ms::Biquad hl = ms::Biquad::highPass (fs, 2000, 0.707), hr = ms::Biquad::highPass (fs, 2000, 0.707);
        run (a, fs, 2.0, [&] (juce::int64 k, float& l, float& r) {
            const float b = (float) (0.4 * std::sin (twoPi * 50.0 * k / fs));
            l = b + (float) hl.process (u (g)); r = b + (float) hr.process (u (g)); }, n);
        CHECK (a.correlationLow.load() > 0.95f, "bajo mono con agudos abiertos: graves +1 (%.2f)", a.correlationLow.load());
        CHECK (a.correlation.load() < 0.98f, "...y la correlación general sí baja (%.2f)", a.correlation.load());
        CHECK (a.lowSideDb.load() < -25.0f, "...y casi nada de lado en graves (%.1f dB)", a.lowSideDb.load());
    }
}

static void waveTests()
{
    std::printf ("Onda por bandas\n");
    const double fs = 44100.0;
    ms::WaveColumn buf[4096];
    {
        ms::Analyzer a; a.prepare (fs, 512); juce::int64 n = 0;
        run (a, fs, 1.0, sine (fs, 60.0, -6.0), n);
        const int c = a.readColumns (buf, 4096);
        CHECK (std::abs (c - 200) <= 2, "200 columnas por segundo (%d)", c);
        const auto& k = buf[c - 1];
        CHECK (k.low > 0.4f && k.high < 0.02f, "60 Hz va a graves (low %.3f, high %.3f)", k.low, k.high);
    }
    {
        ms::Analyzer a; a.prepare (fs, 512); juce::int64 n = 0;
        run (a, fs, 0.5, sine (fs, 8000.0, -6.0), n);
        const int c = a.readColumns (buf, 4096);
        const auto& k = buf[c - 1];
        CHECK (k.high > 0.4f && k.low < 0.02f, "8 kHz va a agudos (high %.3f, low %.3f)", k.high, k.low);
    }
    {
        ms::Analyzer a; a.prepare (fs, 512); juce::int64 n = 0;
        run (a, fs, 0.5, sine (fs, 700.0, -6.0), n);
        const int c = a.readColumns (buf, 4096);
        const auto& k = buf[c - 1];
        CHECK (k.mid > 0.4f && k.low < 0.1f && k.high < 0.1f, "700 Hz va a medios (mid %.3f)", k.mid);
    }
}

static void spectrumTests()
{
    std::printf ("Espectro\n");
    for (double f0 : { 60.0, 1000.0, 8000.0 })
    {
        const double fs = 48000.0;
        ms::Spectrum s; s.slopeDbPerOct = 0.0f; s.setup (fs);
        const int N = s.windowSize();
        std::vector<float> l ((size_t) N), r ((size_t) N);
        for (int i = 0; i < N; ++i) l[(size_t) i] = r[(size_t) i] = (float) (0.5 * std::sin (twoPi * f0 * i / fs));
        for (int k = 0; k < 10; ++k) s.compute (l.data(), r.data(), 1.0f / 60.0f);
        int best = 0;
        for (int i = 1; i < ms::Spectrum::points; ++i) if (s.live[(size_t) i] > s.live[(size_t) best]) best = i;
        const float fb = s.freq[(size_t) best], lv = s.live[(size_t) best];
        CHECK (std::abs (std::log2 (fb / f0)) < 0.1, "seno de %.0f Hz: máximo en %.0f Hz", f0, fb);
        // a 60 Hz la banda de 1/6 oct tiene 1-2 bins: se lee cerca de su nivel. Arriba el suavizado reparte la energía (como SPAN).
        if (f0 < 200) CHECK (lv > -10.0f && lv < -4.0f, "seno de 60 Hz a -6 dB se lee a %.1f dB", lv);
        std::printf ("  seno %.0f Hz a -6 dB: máximo en %.0f Hz, %.1f dB\n", f0, fb, lv);
    }
    {   // ruido rosa con 3 dB/oct: plano
        const double fs = 48000.0;
        ms::Spectrum s; s.slopeDbPerOct = 3.0f; s.speed = 0; s.setup (fs);
        const int N = s.windowSize();
        std::vector<float> l ((size_t) N);
        std::mt19937 g (1); std::normal_distribution<float> nd (0.0f, 0.1f);
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;   // filtro rosa de Paul Kellet
        std::array<double, ms::Spectrum::points> acc {};
        for (int frame = 0; frame < 40; ++frame)
        {
            for (int i = 0; i < N; ++i)
            {
                const double w = nd (g);
                b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
                b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
                l[(size_t) i] = (float) (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362); b6 = w * 0.115926;
            }
            s.compute (l.data(), l.data(), 1.0f / 60.0f);
            for (int i = 0; i < ms::Spectrum::points; ++i) acc[(size_t) i] += s.live[(size_t) i];
        }
        auto at = [&] (float f) { int b = 0; for (int i = 0; i < ms::Spectrum::points; ++i) if (std::abs (s.freq[(size_t) i] - f) < std::abs (s.freq[(size_t) b] - f)) b = i; return acc[(size_t) b] / 40.0; };
        const double a100 = at (100), a1k = at (1000), a10k = at (10000);
        CHECK (std::abs (a100 - a1k) < 3.0 && std::abs (a10k - a1k) < 3.0, "ruido rosa con 3 dB/oct sale plano (100 Hz %.1f, 1 kHz %.1f, 10 kHz %.1f)", a100, a1k, a10k);
        std::printf ("  ruido rosa con 3 dB/oct: 100 Hz %.1f, 1 kHz %.1f, 10 kHz %.1f dB\n", a100, a1k, a10k);
    }
}

int main()
{
    loudnessTests (48000.0);
    loudnessTests (44100.0);
    loudnessTests (96000.0);
    truePeakTests (48000.0);
    truePeakTests (44100.0);
    truePeakTests (96000.0);
    stereoTests();
    waveTests();
    spectrumTests();
    {   // coste en el hilo de audio
        ms::Analyzer a; a.prepare (48000.0, 512); juce::int64 n = 0; ms::WaveColumn wc[4096];
        const auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < 60; ++k) { run (a, 48000.0, 1.0, sine (48000.0, 440.0, -12.0), n); a.readColumns (wc, 4096); }
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("CPU: 60 s de audio estéreo en %.2f s (%.2f %% de un núcleo, incluida la señal de prueba)\n", sec, sec / 60.0 * 100.0);
        CHECK (sec < 3.0, "el análisis cuesta menos del 5 %% de un núcleo");
    }
    std::printf ("\n%d comprobaciones bien, %d fallos\n", oks, fails);
    return fails == 0 ? 0 : 1;
}
