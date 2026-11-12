// ECHO effect module (id ECHO): master insert + project data + settings page
// (modules/module.h). The line comes from the host's SDRAM block
// (ModuleHost::sdram()); without it the module leaves itself out (the boot
// log names a refused module).
#include <cstdio>
#include <cstring>

#include "echo.h"
#include "modules/module.h"
#include "modules/module_ui.h"

namespace ot {
namespace mod {

namespace {

#ifndef OT_MODULE_PERF_ROWS
// A core without Performance FX columns (fork #159): nothing plays over the settings.
template <int N>
struct PerfRows {
    bool any() const { return false; }
    void over(int, uint8_t&) const {}
};
#endif

using audio::echo::Params;

const ModuleInfo kInfo = {"ECHO", kModuleApi, 100, "Echo", kCapMasterInsert | kCapSection | kCapPage, 0,
                          audio::echo::kLine * 2u, 0, 9000, 1};

audio::echo::Echo gEcho;
bool gDirty = false;
Params gSaved;       // the page's settings: gEcho plays them with the Performance rows over them
PerfRows<8> gPerf;   // [I] fork #159
constexpr uint8_t kHi[8] = {1, audio::echo::kTimes - 1, 100, 100, 100, 1, 100, 100};

uint8_t* field(Params& p, int i) {
    uint8_t* const f[8] = {&p.on, &p.time, &p.feedback, &p.lowcut, &p.tone, &p.pingpong, &p.duck, &p.mix};
    return f[i < 0 || i >= 8 ? 0 : i];
}

[[gnu::noinline]] void play() {
    Params p = gSaved;
    for (int i = 1; i < 8; ++i) gPerf.over(i, *field(p, i));
    if (gPerf.any()) p.on = 1;
    gEcho.setParams(p);
}

[[gnu::noinline]] void keep(const Params& p) {
    gEcho.setParams(p);   // setParams clamps
    gSaved = gEcho.params();
    if (gPerf.any()) play();
}

void set(const Params& p) {
    keep(p);
    gDirty = true;
}

// Project data v1: on, time, feedback, lowcut, tone, pingpong, duck, mix (8 B).
// Nothing is saved while the effect is Off at the default settings.
class Section final : public IProjectSection {
public:
    uint16_t tag() const override { return 0x4006; }
    uint16_t version() const override { return 1; }
    bool dirty() const override { return gDirty; }
    uint32_t encode(uint8_t* buf, uint32_t cap) override {
        gDirty = false;
        const Params p = gSaved;
        const uint8_t b[8] = {p.on, p.time, p.feedback, p.lowcut, p.tone, p.pingpong, p.duck, p.mix};
        const Params d;
        const uint8_t z[8] = {0, d.time, d.feedback, d.lowcut, d.tone, d.pingpong, d.duck, d.mix};
        if (std::memcmp(b, z, 8) == 0 || cap < 8) return 0;
        std::memcpy(buf, b, 8);
        return 8;
    }
    bool decode(const uint8_t* b, uint32_t len, uint16_t) override {
        Params p;
        uint8_t* const f[8] = {&p.on, &p.time, &p.feedback, &p.lowcut, &p.tone, &p.pingpong, &p.duck, &p.mix};
        for (uint32_t i = 0; i < 8 && i < len; ++i) *f[i] = b[i];
        keep(p);   // clamped
        gDirty = false;
        return true;
    }
    void reset() override {
        keep(Params{});
        gDirty = false;
    }
};

// Settings page: Echo, Time, Feedback, Low Cut, Tone, Ping-Pong, Duck, Mix.
class Page final : public IModulePage {
public:
    const char* title() const override { return "Echo"; }
    int rows() const override { return 8; }
    [[gnu::noinline]] void row(int i, ModuleRow& out) override {
        static const char* const kLabel[8] = {"Echo", "Time", "Feedback", "Low Cut", "Tone", "Ping-Pong", "Duck", "Mix"};
        i = i < 0 || i > 7 ? 0 : i;
        const Params p = gSaved;
        const uint8_t v[8] = {p.on, p.time, p.feedback, p.lowcut, p.tone, p.pingpong, p.duck, p.mix};
        std::snprintf(out.label, sizeof(out.label), "%s", kLabel[i]);
        if (i == 0 || i == 5) {
            std::snprintf(out.value, sizeof(out.value), "%s", v[i] ? "On" : "Off");
        } else if (i == 1) {
            bool clamped = false;
            gEcho.seconds(p, gEcho.tempo(), &clamped);
            std::snprintf(out.value, sizeof(out.value), "%s%s", audio::echo::kTimeName[p.time], clamped ? " (max)" : "");
        } else {
            std::snprintf(out.value, sizeof(out.value), "%d", v[i]);
        }
        out.frac = float(v[i]) / float(kHi[i]);
        out.bipolar = false;
    }
    void adjust(int i, int d) override {
        Params p = gSaved;
        uint8_t* const f[8] = {&p.on, &p.time, &p.feedback, &p.lowcut, &p.tone, &p.pingpong, &p.duck, &p.mix};
        if (i < 0 || i > 7) return;
        if (i == 0 || i == 5) {
            *f[i] = d > 0 ? 1 : (d < 0 ? 0 : *f[i]);
        } else {
            const int n = int(*f[i]) + d;
            *f[i] = uint8_t(n < 0 ? 0 : (n > 255 ? 255 : n));   // setParams clamps to the range
        }
        set(p);
    }
#ifdef OT_MODULE_PERF_ROWS
    // [I] fork #159: every row but the on row; the text as row() shows it.
    bool perfRange(int i, int& lo, int& hi) const override {
        const bool ok = i >= 1 && i < 8;
        lo = 0;
        hi = ok ? kHi[i] : 0;
        return ok;
    }
    void perfText(int i, int v, char* out, int cap) override {
        if (i == 1) {   // the division alone: a cell has no room for "(max)"
            std::snprintf(out, size_t(cap), "%s", audio::echo::kTimeName[v < audio::echo::kTimes ? v : 0]);
            return;
        }
        const Params was = gSaved;
        *field(gSaved, i) = uint8_t(v < 0 ? 0 : v);
        ModuleRow r;
        row(i, r);
        gSaved = was;
        std::snprintf(out, size_t(cap), "%s", r.value);
    }
    void perfPlay(int i, int v) override {
        const bool ok = i >= 1 && i < 8;
        if (gPerf.set(i, v, 0, ok ? kHi[i] : 0)) play();
    }
#endif
};

Section gSection;
Page gPage;

class Module final : public IModule {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost& host) override {
        int16_t* line = static_cast<int16_t*>(host.sdram(audio::echo::kLine * sizeof(int16_t)));
        gEcho.setLine(line);   // zeroed .sdram_bss (host builds: .bss)
        return line != nullptr;
    }
    audio::IMasterInsert* masterInsert() override { return &gEcho; }
    IProjectSection* section() override { return &gSection; }
    IModulePage* page() override { return &gPage; }
};

Module gModule;

}  // namespace

IModule& echoModule() { return gModule; }

// Settings API (tests, the module page).
void echoSet(const audio::echo::Params& p) { set(p); }
audio::echo::Params echoGet() { return gSaved; }

}  // namespace mod
}  // namespace ot
