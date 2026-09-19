#include "mod.h"

#include <windows.h>

#include "autocast.h"
#include "config.h"
#include "datatweaks.h"
#include "eye.h"
#include "log.h"
#include "production.h"
#include "spells.h"
#include "trees.h"
#include "tweaks.h"
#include "workers.h"
#include "world.h"

using namespace game;

namespace mod {

namespace {

wchar_t g_dllDir[MAX_PATH];
bool g_initialised = false;
bool g_configBroken = false;
bool g_firstTickLogged = false;
unsigned g_tick = 0;
bool g_toggleKeyWasDown = false;
bool g_netGameLogged = false;
ULONGLONG g_lastTickMs = 0;

bool g_productionKeyWasDown = false;

bool GameWindowFocused() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

bool RealKeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
KeyReader g_keyDown = &RealKeyDown;

// Ctrl + key, only while the game has the focus; true once per press.
bool Pressed(int vk, bool& wasDown) {
    if (!vk) return false;
    const bool down = g_keyDown(vk) && g_keyDown(VK_CONTROL) && (g_keyDown != &RealKeyDown || GameWindowFocused());
    const bool pressed = down && !wasDown;
    wasDown = down;
    return pressed;
}

void PollToggleKeys() {
    if (Pressed(config::g.toggleKey, g_toggleKeyWasDown)) {
        config::g.enabled = !config::g.enabled;
        logx::Write("toggle key: autocast %s", config::g.enabled ? "on" : "off");
        ShowMessage(config::g.enabled ? "Autocast ON" : "Autocast OFF");
    }
    if (Pressed(config::g.production.toggleKey, g_productionKeyWasDown)) {
        config::g.production.enabled = !config::g.production.enabled;
        logx::Write("toggle key: auto-production %s", config::g.production.enabled ? "on" : "off");
        ShowMessage(config::g.production.enabled ? "Auto-production ON" : "Auto-production OFF");
    }
}

// Real milliseconds since the previous simulation step. A long gap is a pause, a menu or a load, not game time.
unsigned ElapsedMs() {
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG delta = g_lastTickMs ? now - g_lastTickMs : 0;
    g_lastTickMs = now;
    return delta > 500 ? 0 : static_cast<unsigned>(delta);
}

}  // namespace

void SetModuleBase(uintptr_t exeBase, const wchar_t* dllDir) {
    g_base = exeBase;
    wcscpy_s(g_dllDir, dllDir);
}

void EnsureConfigLoaded() {
    if (g_initialised) return;
    g_initialised = true;
    g_configBroken = !config::Init(g_dllDir);
}

void __cdecl OnTick() {
    EnsureConfigLoaded();
    if (!g_firstTickLogged) {
        g_firstTickLogged = true;
        if (g_configBroken) ShowMessage("Gameplay Options: gameplay_options.toml has an error, see gameplay_options.log");
        logx::Write("first game tick, mod live");
    }
    ++g_tick;
    if (g_tick % 64 == 0) {
        const int reloaded = config::ReloadIfChanged();
        if (reloaded > 0) ShowMessage("Gameplay Options: settings reloaded");
        if (reloaded < 0) ShowMessage("Gameplay Options: gameplay_options.toml has an error, see gameplay_options.log");
    }
    PollToggleKeys();
    const unsigned elapsedMs = ElapsedMs();

    // A savegame can be loaded without a new map ever starting, so this cannot live in the map-load hook alone.
    datatweaks::SyncRangeBonus(*At<uint32_t>(kRvaNetGame) != 0);
    spells::Sync(*At<uint32_t>(kRvaNetGame) != 0);

    // Everything below changes game state locally. In a network game that desyncs the match, so nothing runs.
    if (*At<uint32_t>(kRvaNetGame) != 0) {
        if (!g_netGameLogged) {
            g_netGameLogged = true;
            logx::Write("multiplayer game detected, the mod stays off");
        }
        return;
    }
    g_netGameLogged = false;

    World w;
    if (!BuildWorld(w)) return;
    tweaks::OnTick(w, elapsedMs);
    workers::OnTick(w, elapsedMs);
    production::OnTick(w, elapsedMs);
    trees::OnTick(w, elapsedMs);
    if (g_tick % static_cast<unsigned>(config::g.intervalTicks) == 0) {
        if (config::g.enabled) {
            autocast::Pass(w);
            eye::Pass(w);
        } else {
            autocast::GuardChannels(w);  // a Blizzard the mod started must not keep burning friends after Ctrl+F9
        }
    }
}

void SetKeyReaderForTest(KeyReader reader) { g_keyDown = reader ? reader : &RealKeyDown; }

void RunAutocastPass() {
    World w;
    if (!BuildWorld(w)) return;
    autocast::Pass(w);
    eye::Pass(w);
}

}  // namespace mod
