// LOFI effect module (id LOFI): master insert + project data + settings page
// (modules/module.h).
#include <cstdio>
#include <cstring>

#include "lofi.h"
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

using audio::lofi::Params;

const ModuleInfo kInfo = {"LOFI", kModuleApi, 100, "LoFi", kCapMasterInsert | kCapSection | kCapPage, 0, 0, 0, 12500, 1};

audio::lofi::Lofi gLofi;
bool gDirty = false;
Params gSaved;       // the page's settings: gLofi plays them with the Performance rows over them
PerfRows<7> gPerf;   // [I] fork #159
constexpr uint8_t kLo[7] = {0, 4, 0, 0, 0, 0, 0}, kHi[7] = {1, 16, 7, 100, 3, 1, 100};

uint8_t* field(Params& p, int i) {
    uint8_t* const f[7] = {&p.on, &p.bits, &p.rate, &p.ladder, &p.speaker, &p.mono, &p.mix};
    return f[i < 0 || i >= 7 ? 0 : i];
}

[[gnu::noinline]] void play() {
    Params p = gSaved;
    for (int i = 1; i < 7; ++i) gPerf.over(i, *field(p, i));
    if (gPerf.any()) p.on = 1;
    gLofi.setParams(p);
}

[[gnu::noinline]] void keep(const Params& p) {
    gLofi.setParams(p);   // setParams clamps
    gSaved = gLofi.params();
    if (gPerf.any()) play();
}

void set(const Params& p) {
    keep(p);
    gDirty = true;
}

// Project data v1: on, bits, rate, ladder, speaker, mono, mix, 1 reserved (8 B).
// Nothing is saved while the effect is Off at the default settings.
class Section final : public IProjectSection {
public:
    uint16_t tag() const override { return 0x4007; }
    uint16_t version() const override { return 1; }
    bool dirty() const override { return gDirty; }
    uint32_t encode(uint8_t* buf, uint32_t cap) override {
        gDirty = false;
        const Params p = gSaved;
        const uint8_t b[8] = {p.on, p.bits, p.rate, p.ladder, p.speaker, p.mono, p.mix, 0};
        const Params d;
        const uint8_t z[8] = {0, d.bits, d.rate, d.ladder, d.speaker, d.mono, d.mix, 0};
        if (std::memcmp(b, z, 8) == 0 || cap < 8) return 0;
        std::memcpy(buf, b, 8);
        return 8;
    }
    bool decode(const uint8_t* b, uint32_t len, uint16_t) override {
        Params p;
        uint8_t* const f[7] = {&p.on, &p.bits, &p.rate, &p.ladder, &p.speaker, &p.mono, &p.mix};
        for (uint32_t i = 0; i < 7 && i < len; ++i) *f[i] = b[i];
        keep(p);   // clamped
        gDirty = false;
        return true;
    }
    void reset() override {
        keep(Params{});
        gDirty = false;
    }
};

// Settings page: LoFi, Bits, Rate, Ladder, Speaker, Mono, Mix (Master key cycle).
class Page final : public IModulePage {
public:
    const char* title() const override { return "LoFi"; }
    int rows() const override { return 7; }
    [[gnu::noinline]] void row(int i, ModuleRow& out) override {
        static const char* const kLabel[7] = {"LoFi", "Bits", "Rate", "Ladder", "Speaker", "Mono", "Mix"};
        i = i < 0 || i > 6 ? 0 : i;
        const Params p = gSaved;
        const uint8_t v[7] = {p.on, p.bits, p.rate, p.ladder, p.speaker, p.mono, p.mix};
        std::snprintf(out.label, sizeof(out.label), "%s", kLabel[i]);
        if (i == 0 || i == 5) std::snprintf(out.value, sizeof(out.value), "%s", v[i] ? "On" : "Off");
        else if (i == 1 && p.bits >= 16) std::snprintf(out.value, sizeof(out.value), "%s", "Off");
        else if (i == 2) std::snprintf(out.value, sizeof(out.value), "%s", audio::lofi::kRateName[p.rate]);
        else if (i == 4) std::snprintf(out.value, sizeof(out.value), "%s", audio::lofi::kSpeakerName[p.speaker]);
        else std::snprintf(out.value, sizeof(out.value), "%d", v[i]);
        out.frac = float(v[i] - kLo[i]) / float(kHi[i] - kLo[i]);
        out.bipolar = false;
    }
    void adjust(int i, int d) override {
        Params p = gSaved;
        uint8_t* const f[7] = {&p.on, &p.bits, &p.rate, &p.ladder, &p.speaker, &p.mono, &p.mix};
        if (i < 0 || i > 6) return;
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
        const bool ok = i >= 1 && i < 7;
        lo = ok ? kLo[i] : 0;
        hi = ok ? kHi[i] : 0;
        return ok;
    }
    void perfText(int i, int v, char* out, int cap) override {
        const Params was = gSaved;
        *field(gSaved, i) = uint8_t(v < 0 ? 0 : v);
        ModuleRow r;
        row(i, r);
        gSaved = was;
        std::snprintf(out, size_t(cap), "%s", r.value);
    }
    void perfPlay(int i, int v) override {
        const bool ok = i >= 1 && i < 7;
        if (gPerf.set(i, v, ok ? kLo[i] : 0, ok ? kHi[i] : 0)) play();
    }
#endif
};

Section gSection;
Page gPage;

class Module final : public IModule {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::IMasterInsert* masterInsert() override { return &gLofi; }
    IProjectSection* section() override { return &gSection; }
    IModulePage* page() override { return &gPage; }
};

Module gModule;

}  // namespace

IModule& lofiModule() { return gModule; }

// Settings API (tests, the module page).
void lofiSet(const audio::lofi::Params& p) { set(p); }
audio::lofi::Params lofiGet() { return gSaved; }

}  // namespace mod
}  // namespace ot
