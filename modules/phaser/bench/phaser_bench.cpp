// PHSR bench (core tests/bench_api.h): one stereo block of the costliest
// setting (80s, all eight stages, Depth / Feedback / Spread 100, Mix 50).
#include <cstdio>

#include "audio/audio_tables.h"
#include "bench_api.h"
#include "phaser.h"

void phaserBench() {
    using namespace ot::audio;
    alignas(8) static uint8_t state[kEffectStateMax], work[kEffectWorkMax];
    static float l[kBlock], r[kBlock];
    const EffectSettings s = {{phaser::k80s, 60, 100, 100, 100, 50}};
    uint32_t x = 1;
    const int n = 400 / otbench::scale();
    double sum = 0.0;
    for (int b = 0; b < n; ++b) {
        for (uint32_t i = 0; i < kBlock; ++i) {
            x = x * 1664525u + 1013904223u;
            l[i] = float(int32_t(x)) * (0.5f / 2147483648.0f);
            r[i] = -l[i];
        }
        const double t0 = otbench::nowUnits();
        phaser::gPhaserEffect.process(state, work, s, true, l, r, kBlock);
        sum += otbench::nowUnits() - t0;
    }
    std::printf("    phas.unit %8.1f  (one stereo block, 80s, Depth / Feedback / Spread 100, limit 12500 M4 insn)\n",
                sum / n);
}
