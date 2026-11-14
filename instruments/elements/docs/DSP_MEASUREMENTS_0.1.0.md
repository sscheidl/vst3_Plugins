# DSP measurements: Mendelejew 0.1.0 (project Elements)

The output of `test_elements_dsp` (Release, Visual Studio 2026) in the final
run of 2026-11-14, as written to `dsp-measurements-Release.md` in the build
directory. The [build report](BUILD_REPORT_0.1.0.md) summarises it. To
reproduce: `ctest --preset windows-vs2026-x64-release -R elements.dsp`.

Reading the band tables: a number is the port's level minus the original's in
that band, in dB; "." means the original has less than -70 dB there; "(!)"
marks a difference beyond the limit the test sets for that case. "+/-" after
a level is the scatter of the original between runs with different random
start values, for the cases that are driven by the random generator. -300.0
stands for "nothing": at 48 kHz there is no band from 24 to 48 kHz.

One row carries "(!)": the exciter signal of the bright mallet at 96 kHz. It
is the known deviation described in the build report (section 6, "96 kHz in
detail"); the test reports it without failing.

## The port against the original

Written by test_elements_dsp. "Original" is the unchanged code of
pichenettes/eurorack at its 32 kHz; "port" is core/ at the rate named.


### Pitch of the fundamental (struck, cents from equal temperament)

| MIDI note | Hz | original 32 kHz | port 48 kHz | port 96 kHz |
| --- | --- | --- | --- | --- |
| 36 | 65.41 | -0.14 | -0.10 | -0.10 |
| 48 | 130.81 | -0.10 | -0.07 | -0.07 |
| 60 | 261.63 | -0.07 | -0.03 | -0.03 |
| 69 | 440.00 | -0.07 | -0.07 | -0.07 |
| 72 | 523.25 | -0.03 | -0.03 | -0.03 |
| 84 | 1046.50 | -0.14 | -0.14 | -0.14 |
| 96 | 2093.00 | -0.38 | -0.38 | -0.38 |
| 108 | 4186.01 | -0.17 | -0.17 | -0.17 |

### Decay time of a struck note (seconds per 60 dB)

Of the whole sound, of its fundamental, and of one high partial: the strongest
one the original has between 5 and 7 kHz. "Cents" is how far that partial of
the port lies from the original's. The patch is a bright one (Brightness and
strike Timbre 0.8), so that the high partial can be followed.

| MIDI note | Damping | Rate | Whole sound | ratio | Fundamental | ratio | High partial | Hz | cents | ratio |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 57 | 0.25 | original 32 kHz | 0.150 | | 0.501 | | too short | 5041.0 | | |
| | | port 48 kHz | 0.151 | 1.005 | 0.502 | 1.001 | too short | 5041.0 | +0.0 | . |
| | | port 96 kHz | 0.150 | 0.998 | 0.501 | 1.000 | too short | 5041.0 | +0.0 | . |
| 57 | 0.50 | original 32 kHz | 0.456 | | 0.656 | | 0.393 | 5041.1 | | |
| | | port 48 kHz | 0.457 | 1.002 | 0.658 | 1.004 | 0.393 | 5041.1 | +0.0 | 1.000 |
| | | port 96 kHz | 0.457 | 1.002 | 0.657 | 1.002 | 0.393 | 5041.1 | +0.0 | 0.999 |
| 57 | 0.75 | original 32 kHz | 2.882 | | 4.337 | | 2.478 | 5041.1 | | |
| | | port 48 kHz | 2.878 | 0.999 | 4.338 | 1.000 | 2.478 | 5041.1 | +0.0 | 1.000 |
| | | port 96 kHz | 2.881 | 0.999 | 4.334 | 0.999 | 2.476 | 5041.1 | +0.0 | 0.999 |
| 69 | 0.25 | original 32 kHz | 0.142 | | 0.367 | | 0.090 | 5490.3 | | |
| | | port 48 kHz | 0.143 | 1.011 | 0.367 | 0.999 | 0.090 | 5490.3 | +0.0 | 1.000 |
| | | port 96 kHz | 0.142 | 0.998 | 0.367 | 1.000 | 0.090 | 5490.3 | +0.0 | 1.000 |
| 69 | 0.50 | original 32 kHz | 0.583 | | 0.654 | | 0.535 | 5490.3 | | |
| | | port 48 kHz | 0.583 | 1.001 | 0.656 | 1.002 | 0.535 | 5490.3 | +0.0 | 1.000 |
| | | port 96 kHz | 0.583 | 1.000 | 0.657 | 1.004 | 0.535 | 5490.3 | +0.0 | 1.000 |
| 69 | 0.75 | original 32 kHz | 3.676 | | 4.347 | | 3.376 | 5490.3 | | |
| | | port 48 kHz | 3.674 | 0.999 | 4.347 | 1.000 | 3.376 | 5490.3 | +0.0 | 1.000 |
| | | port 96 kHz | 3.677 | 1.000 | 4.346 | 1.000 | 3.376 | 5490.3 | +0.0 | 1.000 |
| 84 | 0.25 | original 32 kHz | 0.144 | | 0.230 | | 0.118 | 6865.9 | | |
| | | port 48 kHz | 0.144 | 1.000 | 0.226 | 0.983 | 0.118 | 6865.9 | +0.0 | 1.003 |
| | | port 96 kHz | 0.144 | 1.003 | 0.222 | 0.968 | 0.118 | 6865.9 | +0.0 | 1.006 |
| 84 | 0.50 | original 32 kHz | 0.764 | | 0.683 | | 0.755 | 6866.2 | | |
| | | port 48 kHz | 0.762 | 0.997 | 0.679 | 0.995 | 0.753 | 6866.2 | +0.0 | 0.996 |
| | | port 96 kHz | 0.760 | 0.995 | 0.676 | 0.991 | 0.750 | 6866.2 | +0.0 | 0.993 |
| 84 | 0.75 | original 32 kHz | 4.768 | | 4.398 | | 4.763 | 6866.2 | | |
| | | port 48 kHz | 4.769 | 1.000 | 4.382 | 0.996 | 4.745 | 6866.2 | +0.0 | 0.996 |
| | | port 96 kHz | 4.775 | 1.002 | 4.366 | 0.993 | 4.728 | 6866.2 | +0.0 | 0.993 |

### Exciter signal: level by frequency band (port minus original, dB)

The summed exciters (the hardware's left output at SPACE 0) without reverb. "Level" is the original's level over the
analysed stretch in dB re full scale of the DSP. A band is listed where the
original has more than -70 dB in it; the two bands above 15.5 kHz, which the
original cannot produce, are given as absolute levels of the port.

Port at 48 kHz:

| Case | Level | Total | 20-125 | 125-250 | 250-500 | 0.5-1k | 1-2k | 2-4k | 4-8k | 8-12k | 12-15.5k | abs. 15.5-24k | abs. 24-48k |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| strike: mallet (default patch) | -37.4 | -0.01 | +0.0 | +0.0 | -0.0 | -0.0 | -0.0 | +0.0 | +0.2 | . | . | -93.8 | -300.0 |
| strike: mallet, bright | -47.3 | +0.01 | -0.0 | -0.0 | -0.0 | -0.0 | -0.0 | -0.0 | +0.1 | +0.1 | . | -63.5 | -300.0 |
| strike: mallet, dark, low note | -30.2 | -0.01 | -0.0 | -0.0 | -0.0 | -0.0 | +0.0 | . | . | . | . | -109.4 | -300.0 |
| strike: sample | -35.7 | +0.05 | -0.1 | -0.2 | +0.0 | -0.0 | -0.0 | -0.0 | -0.0 | +0.3 | +0.8 | -58.8 | -300.0 |
| strike: sample, bright, with bleed | -41.1 | -1.64 | -2.9 | -0.7 | -0.7 | -0.6 | -4.1 | -1.3 | -1.5 | -2.2 | -1.1 | -45.4 | -300.0 |
| strike: plectrum | -37.4 | -0.01 | +0.0 | +0.0 | -0.0 | -0.0 | -0.0 | +0.0 | +0.2 | . | . | -93.8 | -300.0 |
| strike: particles | -37.2 +/-1.7 | -1.02 | -1.0 | -1.0 | -1.0 | -1.1 | -1.0 | -1.0 | -0.7 | . | . | -94.9 | -300.0 |
| bow | -42.7 +/-0.1 | -0.05 | -0.1 | +0.0 | +0.0 | +0.0 | . | . | . | . | . | -138.7 | -300.0 |
| bow, bright | -39.2 +/-0.0 | -0.00 | -0.1 | +0.0 | +0.1 | -0.0 | -0.1 | -0.2 | . | . | . | -112.5 | -300.0 |
| blow | -33.4 +/-0.0 | -0.01 | -0.4 | +0.0 | +0.0 | +0.0 | +0.0 | -0.0 | -0.5 | -1.4 | -2.6 | -62.5 | -300.0 |
| blow, with the tube | -31.9 +/-0.2 | +0.11 | -0.4 | -0.2 | +0.1 | +0.2 | +0.0 | +0.1 | +0.1 | +0.6 | +0.7 | -57.4 | -300.0 |
| blow, with the tube, low timbre | -23.9 +/-0.1 | +0.04 | +0.1 | +0.0 | +0.1 | -0.0 | +0.0 | +0.0 | -0.3 | -1.0 | -1.8 | -53.8 | -300.0 |

Port at 96 kHz:

| Case | Level | Total | 20-125 | 125-250 | 250-500 | 0.5-1k | 1-2k | 2-4k | 4-8k | 8-12k | 12-15.5k | abs. 15.5-24k | abs. 24-48k |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| strike: mallet (default patch) | -37.4 | -0.01 | +0.0 | +0.0 | -0.0 | -0.0 | -0.0 | +0.0 | +0.1 | . | . | -84.4 | -59.4 |
| strike: mallet, bright | -47.3 | -3.36(!) | -3.5(!) | -3.5(!) | -3.5(!) | -3.5(!) | -3.5(!) | -3.5(!) | -3.5(!) | -2.9 | . | -62.1 | -45.3 |
| strike: mallet, dark, low note | -30.2 | -0.01 | -0.0 | -0.0 | -0.0 | -0.0 | -0.0 | . | . | . | . | -100.5 | -75.3 |
| strike: sample | -35.7 | -0.03 | -0.1 | -0.2 | +0.0 | +0.0 | -0.0 | -0.0 | -0.0 | -0.0 | -0.1 | -60.2 | -61.5 |
| strike: sample, bright, with bleed | -41.1 | -3.01 | -1.4 | +0.5 | -1.0 | -5.5 | -8.7 | -1.7 | -2.6 | -3.3 | -4.3 | -47.5 | -46.6 |
| strike: plectrum | -37.4 | -0.01 | +0.0 | +0.0 | -0.0 | -0.0 | -0.0 | +0.0 | +0.1 | . | . | -84.4 | -59.4 |
| strike: particles | -37.2 +/-1.7 | -0.31 | -0.3 | -0.3 | -0.3 | -0.3 | -0.3 | -0.3 | -0.3 | . | . | -85.0 | -59.9 |
| bow | -42.7 +/-0.1 | -0.06 | -0.1 | +0.1 | -0.1 | -0.0 | . | . | . | . | . | -130.4 | -105.6 |
| bow, bright | -39.2 +/-0.0 | -0.02 | -0.2 | +0.1 | -0.0 | -0.0 | -0.0 | -0.2 | . | . | . | -103.8 | -79.1 |
| blow | -33.4 +/-0.0 | +0.00 | -0.0 | -0.0 | -0.0 | +0.1 | +0.1 | -0.1 | -0.7 | -2.2 | -4.1 | -65.3 | -65.1 |
| blow, with the tube | -31.9 +/-0.2 | +0.16 | +0.1 | -0.1 | +0.1 | +0.1 | +0.2 | +0.1 | +0.2 | +0.7 | +0.7 | -58.9 | -61.1 |
| blow, with the tube, low timbre | -23.9 +/-0.1 | -0.02 | +0.4 | -0.1 | +0.1 | -0.2 | -0.1 | -0.1 | -0.6 | -1.7 | -3.0 | -56.0 | -55.2 |

### Resonator output: level by frequency band (port minus original, dB)

The resonator (the hardware's right output at SPACE 0) without reverb. "Level" is the original's level over the
analysed stretch in dB re full scale of the DSP. A band is listed where the
original has more than -70 dB in it; the two bands above 15.5 kHz, which the
original cannot produce, are given as absolute levels of the port.

Port at 48 kHz:

| Case | Level | Total | 20-125 | 125-250 | 250-500 | 0.5-1k | 1-2k | 2-4k | 4-8k | 8-12k | 12-15.5k | abs. 15.5-24k | abs. 24-48k |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| strike: mallet (default patch) | -35.1 | -0.05 | -0.0 | -0.0 | -0.1 | -0.1 | -0.1 | -0.1 | +0.2 | . | . | -102.2 | -300.0 |
| strike: mallet, bright | -32.2 | +0.07 | . | . | +0.0 | -0.0 | -0.0 | +0.1 | +0.1 | +0.1 | -0.2 | -72.1 | -300.0 |
| strike: mallet, dark, low note | -28.8 | -0.01 | -0.0 | +0.1 | -0.2 | -0.0 | -0.0 | +0.1 | . | . | . | -131.4 | -300.0 |
| strike: sample | -29.5 | +0.13 | -0.3 | -1.4 | +0.3 | -0.1 | -0.1 | -0.2 | -0.1 | +0.9 | +5.4 | -63.6 | -300.0 |
| strike: sample, bright, with bleed | -33.7 | -2.12 | -2.7 | -1.0 | -0.8 | -3.7 | -5.6 | -1.8 | -2.5 | -1.8 | -0.8 | -48.0 | -300.0 |
| strike: plectrum | -34.6 | -0.05 | -0.0 | -0.0 | -0.1 | -0.1 | -0.1 | -0.1 | +0.2 | . | . | -102.5 | -300.0 |
| strike: particles | -31.4 +/-2.1 | -0.83 | -1.0 | -0.5 | -0.9 | -1.0 | -1.0 | -1.0 | -0.8 | . | . | -105.5 | -300.0 |
| bow | -15.9 +/-3.0 | -0.23 | -0.1 | +0.9 | +2.4 | -6.9 | -5.6 | . | . | . | . | -144.2 | -300.0 |
| bow, bright | -13.8 +/-3.6 | -2.22 | +0.2 | +1.0 | -3.8 | +2.2 | +6.2 | +1.2 | . | . | . | -126.8 | -300.0 |
| blow | -27.6 +/-0.1 | -0.12 | -0.2 | +0.0 | +0.1 | +0.2 | -0.1 | -0.2 | -0.5 | -0.4 | +3.1 | -72.1 | -300.0 |
| blow, with the tube | -25.8 +/-0.2 | +0.02 | -0.5 | -0.4 | -0.1 | -0.0 | -0.1 | -0.0 | +0.0 | +1.2 | +5.5 | -67.1 | -300.0 |
| blow, with the tube, low timbre | -19.7 +/-0.2 | -0.03 | -0.0 | +0.2 | -0.2 | +0.1 | -0.0 | +0.0 | -0.1 | +0.8 | +4.5 | -68.5 | -300.0 |

Port at 96 kHz:

| Case | Level | Total | 20-125 | 125-250 | 250-500 | 0.5-1k | 1-2k | 2-4k | 4-8k | 8-12k | 12-15.5k | abs. 15.5-24k | abs. 24-48k |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| strike: mallet (default patch) | -35.1 | -0.05 | -0.1 | -0.0 | -0.1 | -0.1 | -0.1 | -0.1 | +0.1 | . | . | -90.3 | -77.2 |
| strike: mallet, bright | -32.2 | -0.01 | . | . | -0.0 | -0.1 | -0.0 | -0.0 | -0.0 | +0.0 | +0.0 | -65.3 | -57.6 |
| strike: mallet, dark, low note | -28.8 | -0.01 | -0.0 | +0.1 | -0.2 | -0.0 | -0.0 | +0.1 | . | . | . | -115.7 | -101.6 |
| strike: sample | -29.5 | +0.16 | -0.3 | -1.4 | +0.3 | -0.1 | -0.2 | -0.2 | -0.0 | +1.0 | +6.1 | -62.3 | -73.0 |
| strike: sample, bright, with bleed | -33.7 | -3.13 | -1.3 | -0.0 | +0.5 | -9.2 | -6.0 | -2.2 | -3.8 | -2.7 | -2.5 | -48.3 | -49.7 |
| strike: plectrum | -34.6 | -0.05 | -0.1 | -0.0 | -0.1 | -0.1 | -0.1 | -0.2 | +0.0 | . | . | -90.1 | -77.2 |
| strike: particles | -31.4 +/-2.1 | -0.35 | -0.5 | -0.4 | -0.4 | -0.2 | -0.4 | -0.4 | -0.2 | . | . | -91.3 | -77.6 |
| bow | -15.9 +/-3.0 | -1.67 | +0.5 | -4.2 | -0.2 | +1.0 | +0.8 | . | . | . | . | -135.2 | -126.5 |
| bow, bright | -13.8 +/-3.6 | +0.15 | -0.5 | +0.4 | +0.2 | +0.1 | -2.2 | -0.5 | . | . | . | -109.9 | -96.5 |
| blow | -27.6 +/-0.1 | -0.05 | +0.2 | -0.1 | +0.2 | +0.1 | +0.1 | -0.3 | -0.8 | -0.7 | +3.4 | -69.3 | -77.4 |
| blow, with the tube | -25.8 +/-0.2 | +0.06 | -0.0 | -0.2 | -0.1 | -0.1 | +0.1 | -0.0 | +0.1 | +1.5 | +7.1 | -63.4 | -73.0 |
| blow, with the tube, low timbre | -19.7 +/-0.2 | -0.14 | +0.5 | +0.3 | -0.2 | -0.1 | -0.1 | -0.2 | -0.2 | +0.9 | +5.3 | -65.0 | -72.1 |
