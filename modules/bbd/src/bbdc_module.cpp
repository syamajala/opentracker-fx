// BBDC effect module (id BBDC): master insert, project data and a settings
// page (modules/module.h, modules/module_ui.h).
#include <cstdio>
#include <cstring>

#include "bbdc.h"
#include "modules/module.h"
#include "modules/module_ui.h"
#if __has_include("ot_profile.h")
#include "ot_profile.h"
#endif
#ifndef OT_SLOT_INSERTS
#define OT_SLOT_INSERTS 0
#endif

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

const ModuleInfo kInfo = {"BBDC", kModuleApi, 100, "BBD chorus", kCapMasterInsert | kCapSection | kCapPage | kCapEffect, 512, 0, 0, 12000, 1};

#if OT_SLOT_INSERTS
// Images with synth slot inserts have little stack headroom left: the master
// line lives in the SDRAM (attach()), moved into the effect window per block.
int16_t* const gLine = nullptr;
#else
int16_t gLine[audio::bbdc::kLine];   // the delay line (.bss)
#endif

// Project data v1: mode, mix, width, tone, 4 reserved (8 B). Nothing is saved
// while the effect is Off at the default settings.
class Module final : public IModule, public IProjectSection, public IModulePage {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost& host) override {
#if OT_SLOT_INSERTS
        void* line = host.sdram(uint32_t(audio::bbdc::kLine) * sizeof(int16_t));
        if (!line) return false;
        fx_.setSlowWork(line);
#else
        (void)host;
#endif
        return true;
    }
    audio::IMasterInsert* masterInsert() override { return &fx_; }
    audio::IEffect* effect(int i) override { return i == 0 ? &audio::bbdc::gChorusEffect : nullptr; }
    IProjectSection* section() override { return this; }
    IModulePage* page() override { return this; }

    uint16_t tag() const override { return 0x4003; }
    uint16_t version() const override { return 1; }
    bool dirty() const override { return dirty_; }
    uint32_t encode(uint8_t* buf, uint32_t cap) override {
        dirty_ = false;
        const audio::bbdc::Params p = saved_;
        const audio::bbdc::Params d;
        if (!p.mode && p.mix == d.mix && p.width == d.width && p.tone == d.tone) return 0;
        if (cap < 8) return 0;
        const uint8_t b[8] = {p.mode, p.mix, p.width, p.tone, 0, 0, 0, 0};
        std::memcpy(buf, b, 8);
        return 8;
    }
    bool decode(const uint8_t* b, uint32_t len, uint16_t) override {
        audio::bbdc::Params p;
        auto take = [&](uint32_t i, uint8_t& v, uint8_t max) {
            if (len > i) v = b[i] > max ? max : b[i];
        };
        take(0, p.mode, 2);
        take(1, p.mix, 100);
        take(2, p.width, 100);
        take(3, p.tone, 100);
        keep(p);
        dirty_ = false;
        return true;
    }
    void reset() override {
        keep(audio::bbdc::Params{});
        dirty_ = false;
    }

    // Settings page: Chorus Off / I / II, Mix, Width, Tone (Shift + Master).
    const char* title() const override { return "BBD"; }
    int rows() const override { return 4; }
    [[gnu::noinline]] void row(int i, ModuleRow& out) override {
        static const char* const kLabel[4] = {"Chorus", "Mix", "Width", "Tone"};
        static const char* const kMode[3] = {"Off", "I", "II"};
        const audio::bbdc::Params p = saved_;
        const uint8_t v[4] = {p.mode, p.mix, p.width, p.tone};
        std::snprintf(out.label, sizeof(out.label), "%s", kLabel[i]);
        if (i == 0) std::snprintf(out.value, sizeof(out.value), "%s", kMode[p.mode]);
        else if (i == 3) std::snprintf(out.value, sizeof(out.value), "%.1f kHz", double(audio::bbdc::toneHz(p.tone)) * 0.001);
        else std::snprintf(out.value, sizeof(out.value), "%d", v[i]);
        out.frac = i == 0 ? 0.5f * float(p.mode) : float(v[i]) * 0.01f;
        out.bipolar = false;
    }
    void adjust(int i, int d) override {
        audio::bbdc::Params p = saved_;
        auto step = [d](uint8_t& x, int max) {
            const int n = int(x) + d;
            x = uint8_t(n < 0 ? 0 : (n > max ? max : n));
        };
        switch (i) {
            case 0: step(p.mode, 2); break;
            case 1: step(p.mix, 100); break;
            case 2: step(p.width, 100); break;
            case 3: step(p.tone, 100); break;
            default: return;
        }
        set(p);
    }

    void set(const audio::bbdc::Params& p) {
        keep(p);
        dirty_ = true;
    }
    audio::bbdc::Params get() const { return saved_; }

#ifdef OT_MODULE_PERF_ROWS
    // [I] fork #159: every row but the on row; the text as row() shows it.
    bool perfRange(int i, int& lo, int& hi) const override {
        const bool ok = i >= 1 && i < 4;
        lo = 0;
        hi = ok ? kHi[i] : 0;
        return ok;
    }
    void perfText(int i, int v, char* out, int cap) override {
        const audio::bbdc::Params was = saved_;
        *field(saved_, i) = uint8_t(v < 0 ? 0 : v);
        ModuleRow r;
        row(i, r);
        saved_ = was;
        std::snprintf(out, size_t(cap), "%s", r.value);
    }
    void perfPlay(int i, int v) override {
        if (perf_.set(i, v, 0, i >= 1 && i < 4 ? kHi[i] : 0)) play();
    }
#endif

private:
    static constexpr uint8_t kHi[4] = {2, 100, 100, 100};
    static uint8_t* field(audio::bbdc::Params& p, int i) {
        uint8_t* const f[4] = {&p.mode, &p.mix, &p.width, &p.tone};
        return f[i < 0 || i >= 4 ? 0 : i];
    }
    // The page's settings in saved_; fx_ plays them with the Performance rows over them.
    [[gnu::noinline]] void play() {
        audio::bbdc::Params p = saved_;
        for (int i = 1; i < 4; ++i) perf_.over(i, *field(p, i));
        if (perf_.any()) p.mode = p.mode ? p.mode : 1;
        fx_.setParams(p);
    }
    [[gnu::noinline]] void keep(audio::bbdc::Params p) {
        for (int i = 0; i < 4; ++i)
            if (*field(p, i) > kHi[i]) *field(p, i) = kHi[i];
        saved_ = p;
        play();
    }
    audio::bbdc::Params saved_;
    PerfRows<4> perf_;   // [I] fork #159
    audio::bbdc::BbdChorus fx_{gLine};
    bool dirty_ = false;
};

Module gModule;

}  // namespace

IModule& bbdcModule() { return gModule; }

void bbdcSet(const audio::bbdc::Params& p) { gModule.set(p); }
audio::bbdc::Params bbdcGet() { return gModule.get(); }

}  // namespace mod
}  // namespace ot
