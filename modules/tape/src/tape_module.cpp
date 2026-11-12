// TAPE effect module (id TAPE): master insert + project data (modules/module.h).
#include <cstdio>
#include <cstring>

#include "modules/module.h"
#include "modules/module_ui.h"
#include "tape.h"

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

const ModuleInfo kInfo = {"TAPE", kModuleApi, 100, "Tape", kCapMasterInsert | kCapSection | kCapPage, 0, 0, 0, 15000, 1};

// Project data v1: on, drive, wow, flutter, mix, 3 reserved (8 B). Nothing is
// saved while the effect is Off at the default settings.
class Module final : public IModule, public IProjectSection, public IModulePage {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::IMasterInsert* masterInsert() override { return &tape_; }
    IProjectSection* section() override { return this; }
    IModulePage* page() override { return this; }

    // Settings page: On, Drive, Wow, Flutter, Mix (Master key cycle).
    const char* title() const override { return "Tape"; }
    int rows() const override { return 5; }
    [[gnu::noinline]] void row(int i, ModuleRow& out) override {
        static const char* const kLabel[5] = {"Tape", "Drive", "Wow", "Flutter", "Mix"};
        const audio::tape::Params p = saved_;
        const uint8_t v[5] = {p.on, p.drive, p.wow, p.flutter, p.mix};
        std::snprintf(out.label, sizeof(out.label), "%s", kLabel[i]);
        if (i == 0) std::snprintf(out.value, sizeof(out.value), "%s", p.on ? "On" : "Off");
        else std::snprintf(out.value, sizeof(out.value), "%d", v[i]);
        out.frac = i == 0 ? float(p.on) : float(v[i]) * 0.01f;
        out.bipolar = false;
    }
    void adjust(int i, int d) override {
        audio::tape::Params p = saved_;
        auto step = [d](uint8_t& x) {
            const int n = int(x) + d;
            x = uint8_t(n < 0 ? 0 : (n > 100 ? 100 : n));
        };
        switch (i) {
            case 0: p.on = d > 0 ? 1 : (d < 0 ? 0 : p.on); break;
            case 1: step(p.drive); break;
            case 2: step(p.wow); break;
            case 3: step(p.flutter); break;
            case 4: step(p.mix); break;
            default: return;
        }
        set(p);
    }

    uint16_t tag() const override { return 0x4001; }
    uint16_t version() const override { return 1; }
    bool dirty() const override { return dirty_; }
    uint32_t encode(uint8_t* buf, uint32_t cap) override {
        dirty_ = false;
        const audio::tape::Params p = saved_;
        const audio::tape::Params d;
        if (!p.on && p.drive == d.drive && p.wow == d.wow && p.flutter == d.flutter && p.mix == d.mix) return 0;
        if (cap < 8) return 0;
        const uint8_t b[8] = {p.on, p.drive, p.wow, p.flutter, p.mix, 0, 0, 0};
        std::memcpy(buf, b, 8);
        return 8;
    }
    bool decode(const uint8_t* b, uint32_t len, uint16_t) override {
        audio::tape::Params p;
        auto take = [&](uint32_t i, uint8_t& v, uint8_t max) {
            if (len > i) v = b[i] > max ? max : b[i];
        };
        take(0, p.on, 1);
        take(1, p.drive, 100);
        take(2, p.wow, 100);
        take(3, p.flutter, 100);
        take(4, p.mix, 100);
        keep(p);
        dirty_ = false;
        return true;
    }
    void reset() override {
        keep(audio::tape::Params{});
        dirty_ = false;
    }

    void set(const audio::tape::Params& p) {
        keep(p);
        dirty_ = true;
    }
    audio::tape::Params get() const { return saved_; }

#ifdef OT_MODULE_PERF_ROWS
    // [I] fork #159: every row but the on row; the text as row() shows it.
    bool perfRange(int i, int& lo, int& hi) const override {
        const bool ok = i >= 1 && i < 5;
        lo = 0;
        hi = ok ? kHi[i] : 0;
        return ok;
    }
    void perfText(int i, int v, char* out, int cap) override {
        const audio::tape::Params was = saved_;
        *field(saved_, i) = uint8_t(v < 0 ? 0 : v);
        ModuleRow r;
        row(i, r);
        saved_ = was;
        std::snprintf(out, size_t(cap), "%s", r.value);
    }
    void perfPlay(int i, int v) override {
        if (perf_.set(i, v, 0, i >= 1 && i < 5 ? kHi[i] : 0)) play();
    }
#endif

private:
    static constexpr uint8_t kHi[5] = {1, 100, 100, 100, 100};
    static uint8_t* field(audio::tape::Params& p, int i) {
        uint8_t* const f[5] = {&p.on, &p.drive, &p.wow, &p.flutter, &p.mix};
        return f[i < 0 || i >= 5 ? 0 : i];
    }
    // The page's settings in saved_; tape_ plays them with the Performance rows over them.
    [[gnu::noinline]] void play() {
        audio::tape::Params p = saved_;
        for (int i = 1; i < 5; ++i) perf_.over(i, *field(p, i));
        if (perf_.any()) p.on = 1;
        tape_.setParams(p);
    }
    [[gnu::noinline]] void keep(audio::tape::Params p) {
        for (int i = 0; i < 5; ++i)
            if (*field(p, i) > kHi[i]) *field(p, i) = kHi[i];
        saved_ = p;
        play();
    }
    audio::tape::Params saved_;
    PerfRows<5> perf_;   // [I] fork #159
    audio::tape::Tape tape_;
    bool dirty_ = false;
};

Module gModule;

}  // namespace

IModule& tapeModule() { return gModule; }

// Settings API (tests, the module page).
void tapeSet(const audio::tape::Params& p) { gModule.set(p); }
audio::tape::Params tapeGet() { return gModule.get(); }

}  // namespace mod
}  // namespace ot
