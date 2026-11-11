# MINDSCOPE · Dj Nané

Medidor (VST3/AU) para el master en Ableton, de la MIND SERIES. Solo mide: el audio sale idéntico, bit a bit (`tests/pass_test.cpp`).

- **WAVE**: onda que avanza, por bandas (graves < 150 Hz, medios, agudos > 2,5 kHz), 200 columnas por segundo; 2, 4 u 8 s.
- **SPECTRUM**: FFT con ventana Hann (8192 puntos a 44,1/48 kHz), suavizado de 1/6 de octava, inclinación 0 / 3 / 4,5 dB/oct, línea de picos, FREEZE y lectura de frecuencia, nota y dB con el ratón.
- **LOUDNESS** (EBU R128 / ITU-R BS.1770-4): momentáneo, corto plazo, integrado con puertas absoluta y relativa, LRA (EBU Tech 3342) y true peak con sobremuestreo 4x. Historial de 60 s, objetivo (-14 streaming / -8 club) y reset al darle a play.
- **STEREO**: vectorscopio, correlación general y de graves (< 120 Hz) y aviso LOW MONO OK / WIDE LOWS.
- Skin MIND SERIES v2 en turquesa laguna. Tema claro, Undo (Cmd+Z) y Settings.

Código:
- `Source/Analyzer.*`: todo lo que se mide en el hilo de audio (loudness, true peak, onda, correlación, anillo de muestras).
- `Source/Spectrum.*`: espectro, en el hilo de la interfaz.
- `ui/parts/`: la interfaz. `python3 tools/build_ui.py` monta `ui/index.html` (app + fuente Nunito + skin común de la serie).

Pruebas (Linux):
- `cmake -B build -DMS_TESTS=ON && cmake --build build --target MsTest MsPassTest`
- `./build/MsTest_artefacts/Release/MsTest`: señales de EBU Tech 3341/3342 (±0,1 LU), true peak, correlación, bandas y espectro.
- `./build/MsPassTest_artefacts/Release/MsPassTest`: el audio sale idéntico.
- `node tests/ui.test.mjs`: interfaz con un puente JUCE simulado.

Cada push a `main` compila en macOS (GitHub Actions) y publica un `.zip` con VST3, AU y Standalone en **Releases**.
