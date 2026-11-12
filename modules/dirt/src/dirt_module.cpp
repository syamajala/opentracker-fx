// DIRT effect module (id DIRT): master insert + project data + settings page
// (modules/module.h).
#include <cstdio>
#include <cstring>

#include "dirt.h"
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

using audio::dirt::Params;

const ModuleInfo kInfo = {"DIRT", kModuleApi, 100, "Dirt", kCapMasterInsert | kCapSection | kCapPage | kCapEffect, 0, 0, 0, 9000, 1};

audio::dirt::Dirt gDirt;
bool gDirty = false;
Params gSaved;       // the page's settings: gDirt plays them with the Performance rows over them
PerfRows<7> gPerf;   // [I] fork #159
constexpr uint8_t kLo[7] = {0, 0, 0, 4, 1, 0, 0}, kHi[7] = {1, 3, 100, 16, 32, 100, 100};

uint8_t* field(Params& p, int i) {
    uint8_t* const f[7] = {&p.on, &p.type, &p.drive, &p.bits, &p.rate, &p.tone, &p.mix};
    return f[i < 0 || i >= 7 ? 0 : i];
}

[[gnu::noinline]] void play() {
    Params p = gSaved;
    for (int i = 1; i < 7; ++i) gPerf.over(i, *field(p, i));
    if (gPerf.any()) p.on = 1;
    gDirt.setParams(p);
}

[[gnu::noinline]] void keep(const Params& p) {
    gDirt.setParams(p);   // setParams clamps
    gSaved = gDirt.params();
    if (gPerf.any()) play();
}

void set(const Params& p) {
    keep(p);
    gDirty = true;
}

// Project data v1: on, type, drive, bits, rate, tone, mix, 1 reserved (8 B).
// Nothing is saved while the effect is Off at the default settings.
class Section final : public IProjectSection {
public:
    uint16_t tag() const override { return 0x4004; }
    uint16_t version() const override { return 1; }
    bool dirty() const override { return gDirty; }
    uint32_t encode(uint8_t* buf, uint32_t cap) override {
        gDirty = false;
        const Params p = gSaved;
        const uint8_t b[8] = {p.on, p.type, p.drive, p.bits, p.rate, p.tone, p.mix, 0};
        const Params d;
        const uint8_t z[8] = {0, d.type, d.drive, d.bits, d.rate, d.tone, d.mix, 0};
        if (std::memcmp(b, z, 8) == 0 || cap < 8) return 0;
        std::memcpy(buf, b, 8);
        return 8;
    }
    bool decode(const uint8_t* b, uint32_t len, uint16_t) override {
        Params p;
        uint8_t* const f[7] = {&p.on, &p.type, &p.drive, &p.bits, &p.rate, &p.tone, &p.mix};
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

// Settings page: Dirt, Type, Drive, Bits, Rate, Tone, Mix (Master key cycle).
class Page final : public IModulePage {
public:
    const char* title() const override { return "Dirt"; }
    int rows() const override { return 7; }
    [[gnu::noinline]] void row(int i, ModuleRow& out) override {
        static const char* const kLabel[7] = {"Dirt", "Type", "Drive", "Bits", "Rate", "Tone", "Mix"};
        i = i < 0 || i > 6 ? 0 : i;
        const Params p = gSaved;
        const uint8_t v[7] = {p.on, p.type, p.drive, p.bits, p.rate, p.tone, p.mix};
        std::snprintf(out.label, sizeof(out.label), "%s", kLabel[i]);
        if (i == 0) std::snprintf(out.value, sizeof(out.value), "%s", p.on ? "On" : "Off");
        else if (i == 1) std::snprintf(out.value, sizeof(out.value), "%s", audio::dirt::kTypeName[p.type]);
        else std::snprintf(out.value, sizeof(out.value), "%d", v[i]);
        out.frac = float(v[i] - kLo[i]) / float(kHi[i] - kLo[i]);
        out.bipolar = false;
    }
    void adjust(int i, int d) override {
        Params p = gSaved;
        uint8_t* const f[7] = {&p.on, &p.type, &p.drive, &p.bits, &p.rate, &p.tone, &p.mix};
        if (i < 0 || i > 6) return;
        if (i == 0) {
            p.on = d > 0 ? 1 : (d < 0 ? 0 : p.on);
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
    audio::IMasterInsert* masterInsert() override { return &gDirt; }
    audio::IEffect* effect(int i) override { return i == 0 ? &audio::dirt::gDriveEffect : nullptr; }
    IProjectSection* section() override { return &gSection; }
    IModulePage* page() override { return &gPage; }
};

Module gModule;

}  // namespace

IModule& dirtModule() { return gModule; }

// Settings API (tests, the module page).
void dirtSet(const audio::dirt::Params& p) { set(p); }
audio::dirt::Params dirtGet() { return gSaved; }

}  // namespace mod
}  // namespace ot
