// PHSR effect module: the Phaser as a per-instance effect (modules/module.h).
#include "modules/module.h"
#include "phaser.h"

namespace ot {
namespace mod {

namespace {

const ModuleInfo kInfo = {"PHSR", kModuleApi, 100, "Phaser", kCapEffect, 0, 0, 0, 12500, 3};

class Module final : public IModule {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::IEffect* effect(int i) override { return i == 0 ? &audio::phaser::gPhaserEffect : nullptr; }
};

Module gModule;

}  // namespace

IModule& phaserModule() { return gModule; }

}  // namespace mod
}  // namespace ot
