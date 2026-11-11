#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    const juce::Colour kBg { 0xff0b0a0e };
    const juce::Colour kLightBg { 0xffe9e7ed };

    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url)
    {
        const auto path = url.upToFirstOccurrenceOf ("?", false, false);
        if (path != "/" && path != "/index.html" && path.isNotEmpty())
            return std::nullopt;
        const auto* begin = reinterpret_cast<const std::byte*> (BinaryData::index_html);
        std::vector<std::byte> data (begin, begin + BinaryData::index_htmlSize);
        return juce::WebBrowserComponent::Resource { std::move (data), "text/html" };
    }

    juce::var ints (const float* v, int n, float scale)
    {
        juce::Array<juce::var> a;
        a.ensureStorageAllocated (n);
        for (int i = 0; i < n; ++i) a.add (juce::roundToInt (v[i] * scale));
        return a;
    }

    float db (float lin) { return lin > 1.0e-10f ? 20.0f * std::log10 (lin) : -200.0f; }
    float r1 (float v) { return std::round (v * 10.0f) / 10.0f; }
}

MSEditor::MSEditor (MSProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    auto options = juce::WebBrowserComponent::Options{}
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([] (const juce::String& url) { return getResource (url); })
        .withEventListener ("msReady", [this] (const juce::var&) { ready = true; lastReset = -1; sendSettings(); })
        .withEventListener ("msReset", [this] (const juce::var&) { proc.analyzer.requestReset(); })
        .withEventListener ("msSettings", [this] (const juce::var& v)
        {
            if (auto* o = v.getDynamicObject())
            {
                auto* s = proc.settings.getDynamicObject();
                if (s == nullptr) { proc.settings = juce::var (new juce::DynamicObject()); s = proc.settings.getDynamicObject(); }
                for (auto& kv : o->getProperties()) s->setProperty (kv.name, kv.value);
            }
            proc.settingsChanged();
            proc.saveGlobal();
            applySettings();
            repaint();
        });

    web = std::make_unique<juce::WebBrowserComponent> (options);
    addAndMakeVisible (*web);
    web->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    applySettings();
    setResizable (true, true);
    setResizeLimits (940, 600, 2600, 1500);
    setSize (1100, 700);
    startTimerHz (60);
}

MSEditor::~MSEditor() { stopTimer(); }

void MSEditor::paint (juce::Graphics& g)
{
    g.fillAll (proc.settings.getProperty ("theme", "dark").toString() == "light" ? kLightBg : kBg);
}

void MSEditor::resized()
{
    if (web != nullptr) web->setBounds (getLocalBounds());
}

void MSEditor::emit (const juce::Identifier& ev, const juce::var& v)
{
    if (web != nullptr) web->emitEventIfBrowserIsVisible (ev, v);
}

void MSEditor::applySettings()
{
    const auto& s = proc.settings;
    spectrum.slopeDbPerOct = (float) (double) s.getProperty ("slope", 4.5);
    spectrum.speed = juce::jlimit (0, 2, (int) s.getProperty ("speed", 1));
}

void MSEditor::sendSettings()
{
    auto* o = new juce::DynamicObject();
    if (auto* s = proc.settings.getDynamicObject())
        for (auto& kv : s->getProperties()) o->setProperty (kv.name, kv.value);
    o->setProperty ("standalone", proc.isStandalone());
    o->setProperty ("sr", proc.analyzer.getSampleRate());
    if (! juce::exactlyEqual (specRate, proc.analyzer.getSampleRate())) { specRate = proc.analyzer.getSampleRate(); spectrum.setup (specRate); }
    o->setProperty ("freqs", ints (spectrum.freq.data(), ms::Spectrum::points, 10.0f));   // Hz x10
    emit ("msSettings", juce::var (o));
}

void MSEditor::timerCallback()
{
    if (! ready) return;
    auto& an = proc.analyzer;

    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = lastMs > 0 ? (float) juce::jlimit (0.001, 0.25, (now - lastMs) / 1000.0) : 1.0f / 60.0f;
    lastMs = now;

    if (! juce::exactlyEqual (specRate, an.getSampleRate())) sendSettings();

    auto* o = new juce::DynamicObject();

    // ---- onda: columnas nuevas [full, low, mid, high] x1000 ----
    cols.resize (1024);
    const int nc = an.readColumns (cols.data(), (int) cols.size());
    {
        juce::Array<juce::var> w;
        w.ensureStorageAllocated (nc * 4);
        for (int i = 0; i < nc; ++i)
        {
            const auto& c = cols[(size_t) i];
            w.add (juce::roundToInt (juce::jmin (c.full, 2.0f) * 1000.0f)); w.add (juce::roundToInt (juce::jmin (c.low, 2.0f) * 1000.0f));
            w.add (juce::roundToInt (juce::jmin (c.mid, 2.0f) * 1000.0f));  w.add (juce::roundToInt (juce::jmin (c.high, 2.0f) * 1000.0f));
        }
        o->setProperty ("w", w);
    }

    // ---- espectro ----
    const int nfft = spectrum.windowSize();
    if ((int) bufL.size() < nfft) { bufL.resize ((size_t) nfft); bufR.resize ((size_t) nfft); }
    an.copyLatest (bufL.data(), bufR.data(), nfft);
    spectrum.compute (bufL.data(), bufR.data(), dt);
    o->setProperty ("sl", ints (spectrum.live.data(), ms::Spectrum::points, 10.0f));
    o->setProperty ("sp", ints (spectrum.peak.data(), ms::Spectrum::points, 10.0f));

    // ---- vectorscopio: lo que ha sonado desde el último cuadro, máx. 400 pares ----
    {
        const int span = juce::jlimit (64, nfft, (int) std::lround (an.getSampleRate() * dt));
        const int step = juce::jmax (1, span / 400);
        const float* l = bufL.data() + (nfft - span);
        const float* r = bufR.data() + (nfft - span);
        juce::Array<juce::var> sc;
        sc.ensureStorageAllocated (2 * span / step + 2);
        for (int i = 0; i < span; i += step) { sc.add (juce::roundToInt (l[i] * 1000.0f)); sc.add (juce::roundToInt (r[i] * 1000.0f)); }
        o->setProperty ("sc", sc);
    }

    // ---- loudness y niveles ----
    o->setProperty ("m", r1 (an.momentary.load()));
    o->setProperty ("s", r1 (an.shortTerm.load()));
    o->setProperty ("i", r1 (an.integrated.load()));
    o->setProperty ("lra", r1 (an.lra.load()));
    o->setProperty ("tp", r1 (an.truePeakMax.load()));
    o->setProperty ("mmax", r1 (an.momentaryMax.load()));
    o->setProperty ("pl", r1 (db (an.peakL.exchange (0.0f))));
    o->setProperty ("pr", r1 (db (an.peakR.exchange (0.0f))));
    o->setProperty ("tpw", r1 (db (an.tpWindow.exchange (0.0f))));
    o->setProperty ("rl", r1 (db (an.rmsL.load())));
    o->setProperty ("rr", r1 (db (an.rmsR.load())));
    o->setProperty ("c", std::round (an.correlation.load() * 100.0f) / 100.0f);
    o->setProperty ("cl", std::round (an.correlationLow.load() * 100.0f) / 100.0f);
    o->setProperty ("ll", r1 (an.lowLevelDb.load()));
    o->setProperty ("ls", r1 (an.lowSideDb.load()));
    o->setProperty ("play", proc.playing.load());
    const int rc = an.resetCount.load();
    if (rc != lastReset) { lastReset = rc; o->setProperty ("reset", rc); }

    emit ("msState", juce::var (o));
}
