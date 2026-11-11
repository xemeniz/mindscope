#include "Analyzer.h"
#include <cmath>
#include <algorithm>

namespace ms
{
static constexpr double kPi = 3.14159265358979323846;

Biquad Biquad::lowPass (double fs, double f, double q)
{
    const double k = std::tan (kPi * f / fs), n = 1.0 / (1.0 + k / q + k * k);
    Biquad b;
    b.b0 = k * k * n; b.b1 = 2.0 * b.b0; b.b2 = b.b0;
    b.a1 = 2.0 * (k * k - 1.0) * n; b.a2 = (1.0 - k / q + k * k) * n;
    return b;
}

Biquad Biquad::highPass (double fs, double f, double q)
{
    const double k = std::tan (kPi * f / fs), n = 1.0 / (1.0 + k / q + k * k);
    Biquad b;
    b.b0 = n; b.b1 = -2.0 * n; b.b2 = n;
    b.a1 = 2.0 * (k * k - 1.0) * n; b.a2 = (1.0 - k / q + k * k) * n;
    return b;
}

// Filtro K de la ITU-R BS.1770-4 calculado para cualquier frecuencia de muestreo
// (mismas constantes que la referencia: estante de +4 dB y paso alto de 38 Hz).
static Biquad kStage1 (double fs)
{
    const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
    const double K = std::tan (kPi * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
    const double a0 = 1.0 + K / Q + K * K;
    Biquad b;
    b.b0 = (Vh + Vb * K / Q + K * K) / a0;
    b.b1 = 2.0 * (K * K - Vh) / a0;
    b.b2 = (Vh - Vb * K / Q + K * K) / a0;
    b.a1 = 2.0 * (K * K - 1.0) / a0;
    b.a2 = (1.0 - K / Q + K * K) / a0;
    return b;
}

static Biquad kStage2 (double fs)
{
    const double f0 = 38.13547087602444, Q = 0.5003270373238773;
    const double K = std::tan (kPi * f0 / fs), a0 = 1.0 + K / Q + K * K;
    Biquad b;
    b.b0 = 1.0; b.b1 = -2.0; b.b2 = 1.0;
    b.a1 = 2.0 * (K * K - 1.0) / a0;
    b.a2 = (1.0 - K / Q + K * K) / a0;
    return b;
}

static double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 40; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; }
    return sum;
}

Analyzer::Analyzer()
    : histMomE (histBins, 0.0), histShortE (histBins, 0.0), histMomN (histBins, 0u), histShortN (histBins, 0u),
      colBuf (4096), ringL (ringSize, 0.0f), ringR (ringSize, 0.0f)
{
    prepare (48000.0, 512);
}

void Analyzer::prepare (double sampleRate, int)
{
    fs = sampleRate > 0 ? sampleRate : 48000.0;

    for (int c = 0; c < 2; ++c)
    {
        kShelf[(size_t) c] = kStage1 (fs);
        kHigh[(size_t) c] = kStage2 (fs);
    }
    subLen = std::max (1, (int) std::lround (fs / 10.0));
    subPos = 0; subEnergy = 0;
    blockEnergy.fill (0); blockLen.fill (0); blockIdx = 0; blocksFilled = 0;

    // true peak: 4x hasta 96 kHz (la norma pide 4x a 48 kHz), 2x hasta 192 kHz
    osFactor = fs <= 97000.0 ? 4 : (fs <= 194000.0 ? 2 : 1);
    tapsPerPhase = 16;
    const int N = osFactor * tapsPerPhase;
    osCoeffs.assign ((size_t) N, 0.0f);
    if (osFactor > 1)
    {
        const double beta = 6.0, centre = (N - 1) / 2.0, i0b = besselI0 (beta);
        std::vector<double> h ((size_t) N);
        for (int n = 0; n < N; ++n)
        {
            const double x = (n - centre) / osFactor;
            const double sinc = std::abs (x) < 1e-12 ? 1.0 : std::sin (kPi * x) / (kPi * x);
            const double r = (n - centre) / centre;
            const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
            h[(size_t) n] = sinc * w;
        }
        // cada fase con ganancia 1 en continua
        for (int p = 0; p < osFactor; ++p)
        {
            double s = 0;
            for (int k = 0; k < tapsPerPhase; ++k) s += h[(size_t) (p + k * osFactor)];
            for (int k = 0; k < tapsPerPhase; ++k)
                osCoeffs[(size_t) (p * tapsPerPhase + k)] = (float) (h[(size_t) (p + k * osFactor)] / s);
        }
    }
    for (auto& v : osHist) v.assign ((size_t) tapsPerPhase, 0.0f);
    osPos = 0;

    // onda por bandas: graves < 150 Hz, medios 150 Hz - 2,5 kHz, agudos > 2,5 kHz
    const double q = 0.7071067811865476;
    for (int s = 0; s < 2; ++s)
    {
        wLow[(size_t) s] = Biquad::lowPass (fs, 150.0, q);
        wMidH[(size_t) s] = Biquad::highPass (fs, 150.0, q);
        wMidL[(size_t) s] = Biquad::lowPass (fs, 2500.0, q);
        wHigh[(size_t) s] = Biquad::highPass (fs, 2500.0, q);
        lpL[(size_t) s] = Biquad::lowPass (fs, 120.0, q);
        lpR[(size_t) s] = Biquad::lowPass (fs, 120.0, q);
    }
    colLen = std::max (1, (int) std::lround (fs / columnsPerSecond));
    colPos = 0; colNow = {};
    colFifo.reset();

    corrCoef = std::exp (-1.0 / (0.3 * fs));
    rmsCoef = corrCoef;
    cLL = cRR = cLR = lLL = lRR = lLR = lMM = lSS = msL = msR = 0;

    std::fill (ringL.begin(), ringL.end(), 0.0f);
    std::fill (ringR.begin(), ringR.end(), 0.0f);
    written.store (0);
    pkL = pkR = 0;

    momentary = -200.0f; shortTerm = -200.0f;
    resetLoudness();
}

void Analyzer::resetLoudness() noexcept
{
    std::fill (histMomE.begin(), histMomE.end(), 0.0);
    std::fill (histShortE.begin(), histShortE.end(), 0.0);
    std::fill (histMomN.begin(), histMomN.end(), 0u);
    std::fill (histShortN.begin(), histShortN.end(), 0u);
    shortCount = 0;
    tpMaxLin = 0;
    integrated = -200.0f; lra = 0.0f; truePeakMax = -200.0f; momentaryMax = -200.0f;
    resetCount.fetch_add (1);
}

float Analyzer::truePeakSample (int ch, float x) noexcept
{
    if (osFactor == 1) return std::abs (x);
    auto& h = osHist[(size_t) ch];
    h[(size_t) osPos] = x;   // osPos lo avanza process() después de los dos canales
    float best = std::abs (x);
    for (int p = 0; p < osFactor; ++p)
    {
        const float* c = osCoeffs.data() + p * tapsPerPhase;
        float y = 0;
        int idx = osPos;
        for (int k = 0; k < tapsPerPhase; ++k)
        {
            y += c[k] * h[(size_t) idx];
            idx = idx == 0 ? tapsPerPhase - 1 : idx - 1;
        }
        best = std::max (best, std::abs (y));
    }
    return best;
}

static inline int binOf (float l) noexcept
{
    return std::clamp ((int) std::floor ((l + 70.0f) * 100.0f), 0, 7999);
}

static inline float lufsOfBin (int i) noexcept { return -70.0f + (i + 0.5f) / 100.0f; }

void Analyzer::finishSubBlock() noexcept
{
    blockEnergy[(size_t) blockIdx] = subEnergy;
    blockLen[(size_t) blockIdx] = subPos;
    blockIdx = (blockIdx + 1) % ringBlocks;
    blocksFilled = std::min (blocksFilled + 1, ringBlocks);
    subEnergy = 0; subPos = 0;

    auto sumLast = [this] (int n)
    {
        double e = 0; int len = 0;
        for (int i = 1; i <= n; ++i)
        {
            const int k = (blockIdx - i + ringBlocks) % ringBlocks;
            e += blockEnergy[(size_t) k]; len += blockLen[(size_t) k];
        }
        return len > 0 ? e / len : 0.0;
    };

    if (blocksFilled >= 4)
    {
        const float m = energyToLufs (sumLast (4));
        momentary = m;
        if (m > momentaryMax.load()) momentaryMax = m;
        if (m > -70.0f)
        {
            const int b = binOf (m);
            histMomE[(size_t) b] += sumLast (4);
            histMomN[(size_t) b] += 1;
        }
        const float s = energyToLufs (sumLast (std::min (blocksFilled, ringBlocks)));
        shortTerm = s;
        if (blocksFilled >= ringBlocks && s > -70.0f)
        {
            const int b = binOf (s);
            histShortE[(size_t) b] += sumLast (ringBlocks);
            histShortN[(size_t) b] += 1;
            ++shortCount;
        }
        updateIntegrated();
    }
}

void Analyzer::updateIntegrated() noexcept
{
    // integrado: puerta absoluta -70 LUFS y relativa -10 LU
    double e = 0; double n = 0;
    for (int i = 0; i < histBins; ++i) { e += histMomE[(size_t) i]; n += histMomN[(size_t) i]; }
    if (n > 0)
    {
        const float rel = energyToLufs (e / n) - 10.0f;
        double e2 = 0, n2 = 0;
        for (int i = std::max (0, (int) std::ceil ((rel + 70.0f) * 100.0f)); i < histBins; ++i) { e2 += histMomE[(size_t) i]; n2 += histMomN[(size_t) i]; }
        integrated = n2 > 0 ? energyToLufs (e2 / n2) : -200.0f;
    }

    // LRA (EBU Tech 3342): corto plazo, puerta relativa -20 LU, percentiles 10 y 95
    if (shortCount > 0)
    {
        double se = 0, sn = 0;
        for (int i = 0; i < histBins; ++i) { se += histShortE[(size_t) i]; sn += histShortN[(size_t) i]; }
        const float rel = energyToLufs (se / sn) - 20.0f;
        const int from = std::max (0, (int) std::ceil ((rel + 70.0f) * 100.0f));
        double total = 0;
        for (int i = from; i < histBins; ++i) total += histShortN[(size_t) i];
        if (total > 0)
        {
            const double t10 = total * 0.10, t95 = total * 0.95;
            double acc = 0; float l10 = 0, l95 = 0; bool got10 = false;
            for (int i = from; i < histBins; ++i)
            {
                acc += histShortN[(size_t) i];
                if (! got10 && acc > t10) { l10 = lufsOfBin (i); got10 = true; }
                if (acc >= t95) { l95 = lufsOfBin (i); break; }
            }
            lra = std::max (0.0f, l95 - l10);
        }
    }
}

void Analyzer::process (const float* left, const float* right, int numSamples) noexcept
{
    if (resetRequested.exchange (false)) resetLoudness();
    if (right == nullptr) right = left;

    float bpL = 0, bpR = 0, tpBlk = 0;
    juce::int64 w = written.load (std::memory_order_relaxed);
    const int mask = ringSize - 1;

    for (int i = 0; i < numSamples; ++i)
    {
        const float l = left[i], r = right[i];

        // anillo
        const int idx = (int) ((w + i) & mask);
        ringL[(size_t) idx] = l; ringR[(size_t) idx] = r;

        // loudness
        const double kl = kHigh[0].process (kShelf[0].process (l));
        const double kr = kHigh[1].process (kShelf[1].process (r));
        subEnergy += kl * kl + kr * kr;
        if (++subPos >= subLen) finishSubBlock();

        // picos
        bpL = std::max (bpL, std::abs (l)); bpR = std::max (bpR, std::abs (r));
        const float t = std::max (truePeakSample (0, l), truePeakSample (1, r));
        osPos = osPos + 1 >= tapsPerPhase ? 0 : osPos + 1;
        tpBlk = std::max (tpBlk, t);

        // RMS y correlación (300 ms)
        const double a = corrCoef, b = 1.0 - a;
        msL = a * msL + b * (double) l * l; msR = a * msR + b * (double) r * r;
        cLL = a * cLL + b * (double) l * l; cRR = a * cRR + b * (double) r * r; cLR = a * cLR + b * (double) l * r;
        const double l1 = lpL[1].process (lpL[0].process (l)), r1 = lpR[1].process (lpR[0].process (r));
        lLL = a * lLL + b * l1 * l1; lRR = a * lRR + b * r1 * r1; lLR = a * lLR + b * l1 * r1;
        const double mm = 0.5 * (l1 + r1), ss = 0.5 * (l1 - r1);
        lMM = a * lMM + b * mm * mm; lSS = a * lSS + b * ss * ss;

        // onda por bandas (mono)
        const double m = 0.5 * ((double) l + r);
        const double lo = wLow[1].process (wLow[0].process (m));
        const double mi = wMidL[1].process (wMidL[0].process (wMidH[1].process (wMidH[0].process (m))));
        const double hi = wHigh[1].process (wHigh[0].process (m));
        colNow.full = std::max (colNow.full, (float) std::abs (m));
        colNow.low = std::max (colNow.low, (float) std::abs (lo));
        colNow.mid = std::max (colNow.mid, (float) std::abs (mi));
        colNow.high = std::max (colNow.high, (float) std::abs (hi));
        if (++colPos >= colLen)
        {
            int s1, n1, s2, n2;
            colFifo.prepareToWrite (1, s1, n1, s2, n2);
            if (n1 > 0) colBuf[(size_t) s1] = colNow;
            else if (n2 > 0) colBuf[(size_t) s2] = colNow;
            colFifo.finishedWrite (n1 + n2);
            colNow = {}; colPos = 0;
        }
    }
    written.store (w + numSamples, std::memory_order_release);

    tpMaxLin = std::max (tpMaxLin, tpBlk);
    truePeakMax = tpMaxLin > 0 ? 20.0f * std::log10 (tpMaxLin) : -200.0f;
    if (bpL > peakL.load()) peakL = bpL;
    if (bpR > peakR.load()) peakR = bpR;
    if (tpBlk > tpWindow.load()) tpWindow = tpBlk;
    rmsL = (float) std::sqrt (msL); rmsR = (float) std::sqrt (msR);

    const double den = std::sqrt (cLL * cRR);
    correlation = den > 1.0e-10 ? (float) std::clamp (cLR / den, -1.0, 1.0) : 1.0f;
    const double denL = std::sqrt (lLL * lRR);
    correlationLow = denL > 1.0e-10 ? (float) std::clamp (lLR / denL, -1.0, 1.0) : 1.0f;
    const double lowE = 0.5 * (lLL + lRR);
    lowLevelDb = lowE > 1.0e-20 ? (float) (10.0 * std::log10 (lowE)) : -200.0f;
    lowSideDb = (lMM > 1.0e-20 && lSS > 1.0e-20) ? (float) (10.0 * std::log10 (lSS / lMM)) : -200.0f;
}

int Analyzer::readColumns (WaveColumn* dest, int maxCount) noexcept
{
    int s1, n1, s2, n2;
    colFifo.prepareToRead (std::min (maxCount, colFifo.getNumReady()), s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) dest[i] = colBuf[(size_t) (s1 + i)];
    for (int i = 0; i < n2; ++i) dest[n1 + i] = colBuf[(size_t) (s2 + i)];
    colFifo.finishedRead (n1 + n2);
    return n1 + n2;
}

juce::int64 Analyzer::copyLatest (float* l, float* r, int n) const noexcept
{
    const juce::int64 w = written.load (std::memory_order_acquire);
    const int mask = ringSize - 1;
    for (int i = 0; i < n; ++i)
    {
        const juce::int64 pos = w - n + i;
        if (pos < 0) { l[i] = r[i] = 0.0f; continue; }
        const int idx = (int) (pos & mask);
        l[i] = ringL[(size_t) idx]; r[i] = ringR[(size_t) idx];
    }
    return w;
}
} // namespace ms
