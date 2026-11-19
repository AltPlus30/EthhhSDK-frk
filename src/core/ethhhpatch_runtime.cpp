#include "ethhhpatch_runtime.h"
#include "ethhhpatch.h"
#include "ethhhpatch_display_startup.h"
#include "ethhhpatch_display_reset.h"
#include "hooks.h"
#include "ethhhpatch_runtime_policy.h"
#include "ethhhpatch_interpolation.h"
#include "ethhhpatch_entity_reader.h"
#include "ethhhpatch_inventory.h"
#include "ethhhpatch_inventory_thunk.h"
#include "ethhhpatch_movie.h"
#include "camera_relative_heading.h"
#include "camera_relative_heading_thunk.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "util.h"
#include "input.h"

#include <MinHook.h>
#include <intrin.h>
#include <type_traits>

namespace eth::patch::runtime {
void UninstallHooks();
namespace {
constexpr uintptr_t kZeroTickWaitBranch = 0x43D96C;
constexpr uint8_t kWaitBytes[2] = { 0x7C, 0xF2 };
constexpr uint8_t kZeroTickBytes[2] = { 0x90, 0x90 };
constexpr uintptr_t kSphereCull = 0x449420;
constexpr uintptr_t kHudPulse = 0x4483B0;
constexpr uintptr_t kInventoryMove = 0x41DD10;
constexpr uintptr_t kInventoryMoveReturn = 0x426063;
constexpr uintptr_t kUiHandles = 0x519A60;
constexpr uintptr_t kSpriteObjectVtable = 0x4C8D94;
constexpr uintptr_t kUiObjectDraw = 0x42BD40;
constexpr uintptr_t kGlyphClip = 0x4484B0;
constexpr uintptr_t kGlyphClipReturn = 0x448BE9;
constexpr uintptr_t kMovieDraw = 0x497D40;
constexpr uintptr_t kViewRender = 0x43F290;
constexpr uintptr_t kPrimaryViewReturn = 0x43E230;
constexpr uintptr_t kAppRender = 0x43E170;
constexpr uintptr_t kEntitySubmit = 0x44A490;
constexpr uintptr_t kNodeSubmit = 0x449EB0;
constexpr uintptr_t kBuiltinFieldGetter = 0x421680;
constexpr uintptr_t kBuiltinFieldReturn = 0x4252EA;
constexpr uintptr_t kNativeCameraOwner = 0x5119B8;
constexpr float kOriginalAspect = 4.0f / 3.0f;

using SphereCullFn = bool(__stdcall*)(const float*, const float*);
using HudPulseFn = void(__thiscall*)(void*);
using InventoryMoveFn = void(__cdecl*)(void*, const inventory::Position*);
using UiObjectDrawFn = void(__thiscall*)(void*);
using GlyphClipFn = uint32_t(__cdecl*)(const inventory::FontClip*);
using ViewRenderFn = void(__thiscall*)(void*);
using EntitySubmitFn = void(__thiscall*)(void*, void*, void*, const entity::Matrix*);
using NodeSubmitFn = void(__thiscall*)(void*, void*, const entity::Matrix*, const entity::Matrix*);
using BuiltinFieldFn = uint32_t(__thiscall*)(void*, void*, void*, uint32_t);
SphereCullFn g_sphereCullOrig = nullptr;
HudPulseFn g_hudPulseOrig = nullptr;
InventoryMoveFn g_inventoryMoveOrig = nullptr;
inventory::Roots g_inventoryRoots;
bool g_inventoryAnchored = false;
unsigned g_hudAnchoredKinds = 0;
UiObjectDrawFn g_uiObjectDrawOrig = nullptr;
GlyphClipFn g_glyphClipOrig = nullptr;
inventory::QueuedText g_inventoryText;
bool g_inventoryClipMatched = false;
bool g_inventoryNativeTick = false;
entity::ReadScope g_inventoryTickReads, g_inventoryRenderReads;
inventory::ClipBounds g_inventoryClipBounds;
bool g_inventoryClipLogPending = false;
struct InventoryProfile {
    struct Path { uint64_t calls=0, samples=0; int64_t sdk=0, native=0; } ui, glyph;
    bool active=false;
    unsigned frames=0;
    size_t tickQueries=0, renderQueries=0;
    bool Sample(Path& path, unsigned mask) {
        if (!active) return false;
        // Rotate the sampled position each render; fixed 64-glyph strings or
        // eight-object groups must not repeatedly select the same element.
        return ((uint32_t(++path.calls)+frames*17u) & mask)==1;
    }
} g_inventoryProfile;
MovieDrawFn g_movieDrawOrig = nullptr;
ViewRenderFn g_viewRenderOrig = nullptr;
ViewRenderFn g_appRenderOrig = nullptr;
EntitySubmitFn g_entitySubmitOrig = nullptr;
NodeSubmitFn g_nodeSubmitOrig = nullptr;
BuiltinFieldFn g_builtinFieldOrig = nullptr;
movement::RoutingPolicy g_headingPolicy;
movement::Owner g_headingCameraOwner;
uint64_t g_headingRouted = 0, g_headingRefused = 0;
entity::Cache g_entityPoses;
entity::OwnerList g_entityOwners;
entity::ReadScope g_entitySampleReads, g_entityRenderReads;
entity::WorkBudget g_entityRenderBudget;
constexpr int kEntityWorkBudgetMilliseconds = 2;
EntityTelemetry g_entityFrameTelemetry;
int64_t g_entityRenderWorkTicks = 0;
bool g_entityRenderBudgetStopped = false;
thread_local entity::Pose* g_submittedPose = nullptr;
thread_local bool g_entitySubmission = false;
std::atomic<bool> g_entityInterpolationEnabled{true};
std::mutex g_entityTelemetryMutex;
EntityTelemetry g_entityTelemetry;
float g_entityFrameAlpha = 1;
uint32_t g_entityFrameTick = 0;
std::atomic<bool> g_hooksReady{false};
std::atomic<bool> g_cameraHooksReady{false};
std::atomic<bool> g_active{false};
bool g_startupDisplayAttempted = false;
DisplayStartupGate g_startupDisplayGate;
DisplayStartupRecovery g_startupDisplayRecovery;
DisplayResetRecovery g_displayResetRecovery;
DisplayStartupGate g_displayResetGate;
DisplayPlan g_startupRecoveryPlan;
bool g_startupApplyInFlight = false, g_startupApplyIsRecovery = false;
std::atomic<float> g_aspect{kOriginalAspect};
std::atomic<unsigned> g_targetWidth{0}, g_targetHeight{0};
std::atomic<int> g_targetHz{0};
int64_t g_nextPresentCounter = 0;
std::atomic<bool> g_externalCameraOwned{false};
bool g_mouseLookEnabled = false, g_mouseCaptureOwned = false, g_inGameRender = false;
OrbitPolicy g_orbit;
RenderCameraBackup g_cameraBackup;
RenderLensBackup g_lensBackup;
PrimaryViewPass g_primaryViewPass;
std::atomic<bool> g_normalFovEnabled{false};
std::atomic<float> g_normalFovDegrees{60};
uintptr_t g_orbitPlayer = 0, g_orbitClass = 0, g_orbitCamera = 0, g_orbitView = 0;
uint32_t g_orbitHandle = 0;
int64_t g_lastCameraTraceCounter = 0, g_counterFrequency = 0;
int64_t g_lastOrbitLogCounter = 0;
uint64_t g_orbitSuppressedTransitions = 0;
uint32_t g_orbitRenderGuardFailures = 0, g_orbitRenderGuardMask = 0;
bool g_cameraRestoreFailed = false;
std::atomic<int64_t> g_movieUntilCounter{0};
std::atomic<bool> g_interpolationLogged{false};
PulseClock g_pulseClock;
std::mutex g_applyMutex;
ApplyStatus g_applyStatus;
std::mutex g_orbitTelemetryMutex;
OrbitTelemetry g_orbitTelemetry;
std::atomic<float> g_orbitSensitivity{1.0f};
std::atomic<bool> g_orbitInvertY{false};
D3DPRESENT_PARAMETERS g_previousParameters{};
RECT g_previousWindowRect{};
bool g_havePreviousParameters = false, g_havePreviousWindowRect = false;

using ConfigureDisplayFn = BOOL(__thiscall*)(void*, bool, int, int, int);

bool ReadWrapper(uintptr_t& wrapper)
{
    uintptr_t renderer = 0;
    return SafeRead(game::A(game::off::pRenderer), renderer) && renderer &&
        SafeRead(renderer + 0x7B0, wrapper) && wrapper;
}

bool ConfigureDisplay(bool windowed, int width, int height, int hz)
{
    uintptr_t wrapper = 0;
    if (!ReadWrapper(wrapper)) return false;
    BOOL posted = FALSE;
    __try {
        posted = ((ConfigureDisplayFn)game::A(0x452300))((void*)wrapper, !windowed, width, height,
            windowed ? 0 : hz);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return posted != FALSE;
}

bool CapturePresentParameters(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS& out)
{
    IDirect3DSwapChain9* swap = nullptr;
    const HRESULT chainResult = device ? device->GetSwapChain(0, &swap) : D3DERR_INVALIDCALL;
    if (FAILED(chainResult) || !swap) return false;
    const HRESULT result = swap->GetPresentParameters(&out);
    swap->Release();
    return SUCCEEDED(result);
}

bool ExactFullscreenModeSupported(IDirect3DDevice9* device, int width, int height, int hz)
{
    D3DDEVICE_CREATION_PARAMETERS creation{};
    IDirect3D9* d3d = nullptr;
    if (!device || FAILED(device->GetCreationParameters(&creation)) || FAILED(device->GetDirect3D(&d3d)) || !d3d)
        return false;
    D3DDISPLAYMODE desktop{};
    bool found = false;
    if (SUCCEEDED(d3d->GetAdapterDisplayMode(creation.AdapterOrdinal, &desktop))) {
        const UINT count = d3d->GetAdapterModeCount(creation.AdapterOrdinal, desktop.Format);
        for (UINT i = 0; i < count; ++i) {
            D3DDISPLAYMODE mode{};
            if (SUCCEEDED(d3d->EnumAdapterModes(creation.AdapterOrdinal, desktop.Format, i, &mode)) &&
                mode.Width == (UINT)width && mode.Height == (UINT)height && mode.RefreshRate == (UINT)hz) {
                found = true; break;
            }
        }
    }
    d3d->Release();
    return found;
}

bool ResizeClient(HWND window, int width, int height)
{
    if (!window || width <= 0 || height <= 0) return false;
    const DWORD style = (DWORD)GetWindowLongPtrW(window, GWL_STYLE);
    const DWORD exStyle = (DWORD)GetWindowLongPtrW(window, GWL_EXSTYLE);
    RECT outer{0, 0, width, height};
    if (!AdjustWindowRectEx(&outer, style, GetMenu(window) != nullptr, exStyle)) return false;
    if (!SetWindowPos(window, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) return false;
    RECT client{};
    if (!GetClientRect(window, &client)) return false;
    const int actualWidth = client.right - client.left, actualHeight = client.bottom - client.top;
    if (actualWidth == width && actualHeight == height) return true;
    // DPI virtualization/theme borders can make the first adjustment imperfect.
    RECT current{};
    if (!GetWindowRect(window, &current)) return false;
    return SetWindowPos(window, nullptr, 0, 0, current.right - current.left + width - actualWidth,
        current.bottom - current.top + height - actualHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) &&
        GetClientRect(window, &client) && client.right - client.left == width && client.bottom - client.top == height;
}

bool StartRollback(const char* reason)
{
    g_displayResetRecovery.Cancel();
    g_displayResetGate.Reset();
    g_startupApplyInFlight = false;
    g_startupDisplayRecovery.Cancel();
    ETH_WARN("EthhhPatch display rollback: %s", reason ? reason : "unspecified");
    if (!DisableStartupPlan())
        ETH_ERROR("EthhhPatch could not persist disabled startup display during rollback");
    D3DPRESENT_PARAMETERS previous{}; RECT windowRect{};
    bool haveParameters = false, haveRect = false;
    {
        std::lock_guard<std::mutex> lock(g_applyMutex);
        previous = g_previousParameters; windowRect = g_previousWindowRect;
        haveParameters = g_havePreviousParameters; haveRect = g_havePreviousWindowRect;
        g_applyStatus.state = ApplyState::RollingBack;
        g_applyStatus.detail = reason ? reason : "display application failed";
    }
    DeactivateRenderSplit();
    HWND window = (HWND)game::GameHwnd();
    if (!haveParameters || !ConfigureDisplay(previous.Windowed != FALSE,
        (int)previous.BackBufferWidth, (int)previous.BackBufferHeight,
        (int)previous.FullScreen_RefreshRateInHz)) {
        {
            std::lock_guard<std::mutex> lock(g_applyMutex);
            g_applyStatus.state = ApplyState::Failed;
            g_applyStatus.detail += "; rollback request failed";
        }
        UninstallHooks();
        return false;
    }
    // The same ordering matters when restoring a windowed baseline.
    if (haveRect && previous.Windowed && window &&
        !SetWindowPos(window, nullptr, windowRect.left, windowRect.top,
            windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
            SWP_NOZORDER | SWP_NOACTIVATE))
        ETH_WARN("EthhhPatch rollback could not restore the previous window rectangle");
    return true;
}

bool ExactCodeWrite(uintptr_t address, const uint8_t (&expected)[2], const uint8_t (&replacement)[2])
{
    uint8_t current[2] = {};
    if (!SafeReadBytes(address, current, sizeof(current)) || memcmp(current, expected, sizeof(current))) return false;
    DWORD oldProtect = 0;
    if (!VirtualProtect((void*)address, sizeof(current), PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    memcpy((void*)address, replacement, sizeof(current));
    FlushInstructionCache(GetCurrentProcess(), (void*)address, sizeof(current));
    DWORD ignored = 0;
    VirtualProtect((void*)address, sizeof(current), oldProtect, &ignored);
    return true;
}

bool __stdcall SphereCullDetour(const float* center, const float* radius)
{
    if (!g_active.load(std::memory_order_relaxed))
        return g_sphereCullOrig ? g_sphereCullOrig(center, radius) : false;
    float p[3] = {}, r = 0, matrix[16] = {}, viewValues[5] = {};
    __try {
        if (!center || !radius) return false;
        memcpy(p, center, sizeof(p)); r = *radius;
        const uintptr_t camera = *(uintptr_t*)game::A(game::off::pCamera);
        const uintptr_t view = *(uintptr_t*)game::A(game::off::pView);
        if (!camera || !view) return g_sphereCullOrig ? g_sphereCullOrig(center, radius) : false;
        memcpy(matrix, (const void*)(camera + game::off::CamViewMat), sizeof(matrix));
        memcpy(viewValues, (const void*)view, sizeof(viewValues));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return g_sphereCullOrig ? g_sphereCullOrig(center, radius) : false;
    }
    const float x = matrix[0] * p[0] + matrix[4] * p[1] + matrix[8] * p[2] + matrix[12];
    const float y = matrix[1] * p[0] + matrix[5] * p[1] + matrix[9] * p[2] + matrix[13];
    const float z = matrix[2] * p[0] + matrix[6] * p[1] + matrix[10] * p[2] + matrix[14];
    return PerspectiveSphereVisible(x, y, z, r, g_aspect.load(std::memory_order_relaxed),
        viewValues[4], viewValues[1]); // View+0x10 tan(FOV/2), View+0x04 far
}

int64_t CameraCounter();
bool ReadInventoryTick(uintptr_t address, void* out, size_t bytes)
{
    return entity::ReadProtected(g_inventoryTickReads,address,out,bytes);
}
bool ReadInventoryRender(uintptr_t address, void* out, size_t bytes)
{
    return entity::ReadProtected(g_inventoryRenderReads,address,out,bytes);
}

void FinishInventoryFrame()
{
    if (g_inventoryClipLogPending) {
        g_inventoryClipLogPending=false;
        ETH_INFO("EthhhPatch inventory text clipping matched queued inventory label");
    }
    auto& p=g_inventoryProfile;
    if (!p.active) return;
    p.renderQueries+=g_inventoryRenderReads.Queries();
    if (++p.frames<300) return;
    p.active=false;
    const double tickMs=g_counterFrequency ? 1000.0/double(g_counterFrequency) : 0;
    const double uiMs=p.ui.samples ? tickMs*p.ui.sdk/p.ui.samples*p.ui.calls/p.frames : 0;
    const double glyphMs=p.glyph.samples ? tickMs*p.glyph.sdk/p.glyph.samples*p.glyph.calls/p.frames : 0;
    const double nativeUs=p.glyph.samples ? tickMs*1000*p.glyph.native/p.glyph.samples : 0;
    ETH_INFO("EthhhPatch inventory profile (300 renders; sampled, excludes native UI draw): "
        "SDK UI %.3f ms/render, glyph %.3f ms/render; native clip %.3f us/call; "
        "UI %llu calls, glyph %llu calls; memory queries tick %zu/render %zu",
        uiMs,glyphMs,nativeUs,p.ui.calls,p.glyph.calls,p.tickQueries,p.renderQueries);
}

void __cdecl InventoryMoveScoped(void* self, const inventory::Position* args, uintptr_t caller, uintptr_t nextPC)
{
    inventory::Position native{};
    inventory::Root root{};
    const float aspect = g_aspect.load(std::memory_order_relaxed);
    inventory::Root* slot = nullptr;
    inventory::Kind kind = inventory::Kind::Unrelated;
    float localAnchor = 0;
    if (g_active.load(std::memory_order_relaxed) && g_inventoryNativeTick && aspect > kOriginalAspect &&
        caller == game::A(kInventoryMoveReturn) &&
        (kind=inventory::PositionCall(nextPC,game::A(kInventoryMove),ReadInventoryTick))!=inventory::Kind::Unrelated &&
        ReadInventoryTick(uintptr_t(args), &native, sizeof(native)) &&
        std::isfinite(native.x) && std::isfinite(native.y) &&
        inventory::Identity(uint32_t(uintptr_t(self)), native.handle, game::A(kUiHandles),
            game::A(kSpriteObjectVtable), root, ReadInventoryTick)) {
        root.kind=kind;
        if (kind!=inventory::Kind::Health || inventory::HealthLeftAnchor(root,localAnchor,ReadInventoryTick))
            slot = g_inventoryRoots.Slot(root, game::A(kUiHandles), game::A(kSpriteObjectVtable), ReadInventoryTick);
    }
    if (slot) {
        root.native = native;
        root.installed = native;
        root.installed.x = inventory::AnchorGroupX(native.x,localAnchor,aspect);
        // Copy arguments only: never edit Panel's animation registers/bytecode.
        if (g_inventoryMoveOrig) g_inventoryMoveOrig(self, &root.installed);
        *slot = root;
        if (!g_inventoryAnchored) {
            g_inventoryAnchored = true;
            g_inventoryProfile={}; g_inventoryProfile.active=true;
        }
        const unsigned bit=1u<<unsigned(kind);
        if (!(g_hudAnchoredKinds&bit)) {
            g_hudAnchoredKinds|=bit;
            const char* name=kind==inventory::Kind::Health ? "HP left-edge" :
                kind==inventory::Kind::Rage ? "rage" : "inventory";
            ETH_INFO("EthhhPatch %s percentage anchoring matched Panel (aspect %.5f)",name,aspect);
        }
    } else if (g_inventoryMoveOrig) g_inventoryMoveOrig(self, args);
}

ETH_INVENTORY_THUNK(InventoryMoveDetour, InventoryMoveScoped)

void __fastcall UiObjectDrawDetour(void* self, void*)
{
    const bool sample=g_inventoryProfile.Sample(g_inventoryProfile.ui,7);
    const int64_t start=sample ? CameraCounter() : 0;
    inventory::QueueSnapshot queues{};
    if (g_active.load(std::memory_order_relaxed) && g_inventoryNativeTick &&
        uintptr_t(_ReturnAddress()) == game::A(0x42B597) &&
        g_inventoryRoots.Contains(uintptr_t(self), game::A(kUiHandles),
            game::A(kSpriteObjectVtable), ReadInventoryTick))
        queues.Capture(game::A(0x502D9C), ReadInventoryTick);
    const int64_t before=sample ? CameraCounter() : 0;
    if (g_uiObjectDrawOrig) g_uiObjectDrawOrig(self);
    const int64_t after=sample ? CameraCounter() : 0;
    // Font queueing itself does not clip. Track only the entries appended by
    // this verified HUD object's draw; shared fonts remain untouched.
    g_inventoryText.Collect(queues, ReadInventoryTick);
    if (sample) {
        g_inventoryProfile.ui.sdk+=before-start+CameraCounter()-after;
        ++g_inventoryProfile.ui.samples;
    }
}

const inventory::FontClip* PrepareInventoryClip(const inventory::FontClip* font, uintptr_t caller,
    uintptr_t string, inventory::FontClip& copy)
{
    if (g_active.load(std::memory_order_relaxed) && g_inGameRender &&
        caller == game::A(kGlyphClipReturn) &&
        g_inventoryText.Matches(uintptr_t(font), string, ReadInventoryRender) &&
        ReadInventoryRender(uintptr_t(font), &copy, sizeof(copy)) &&
        g_inventoryClipBounds.Apply(copy)) {
        if (!g_inventoryClipMatched) {
            g_inventoryClipMatched = true;
            g_inventoryClipLogPending = true;
        }
        return &copy;
    }
    return font;
}

uint32_t __cdecl GlyphClipScoped(const inventory::FontClip* font, uintptr_t caller, uintptr_t string)
{
    const bool sample=g_inventoryProfile.Sample(g_inventoryProfile.glyph,63);
    const int64_t start=sample ? CameraCounter() : 0;
    inventory::FontClip copy{};
    const auto* submitted=PrepareInventoryClip(font,caller,string,copy);
    const int64_t before=sample ? CameraCounter() : 0;
    const uint32_t result=g_glyphClipOrig ? g_glyphClipOrig(submitted) : 0;
    if (sample) {
        const int64_t end=CameraCounter();
        auto& p=g_inventoryProfile.glyph;
        p.sdk+=before-start; p.native+=end-before; ++p.samples;
    }
    return result;
}

ETH_INVENTORY_CLIP_THUNK(GlyphClipDetour, GlyphClipScoped)

void __fastcall HudPulseDetour(void* self, void*)
{
    if (g_active.load(std::memory_order_relaxed) && self) {
        uint32_t tick = 0;
        float phase = 0;
        __try {
            tick = *(uint32_t*)game::A(game::off::TickCount);
            phase = *(float*)((uint8_t*)self + 0xE8);
            const float adjustment = PulsePreAdjustment(g_pulseClock.Advance(tick));
            if (std::isfinite(phase) && std::isfinite(adjustment))
                *(float*)((uint8_t*)self + 0xE8) = phase + adjustment;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (g_hudPulseOrig) g_hudPulseOrig(self);
}

void __cdecl MovieDrawDetour(void* surface, float x, float y,
    float scaleX, float scaleY, float alpha)
{
    MovieDrawArgs args{surface,x,y,scaleX,scaleY,alpha};
    if (g_active.load(std::memory_order_relaxed) && surface &&
        uintptr_t(_ReturnAddress())==game::A(0x49A40A)) {
        int sourceWidth = 0, sourceHeight = 0;
        __try {
            sourceWidth = *(int*)((uint8_t*)surface + 0x14);
            sourceHeight = *(int*)((uint8_t*)surface + 0x18);
        } __except (EXCEPTION_EXECUTE_HANDLER) { sourceWidth = sourceHeight = 0; }
        const unsigned targetWidth = g_targetWidth.load(std::memory_order_relaxed);
        const unsigned targetHeight = g_targetHeight.load(std::memory_order_relaxed);
        FitMovie(args,sourceWidth,sourceHeight,int(targetWidth),int(targetHeight));
    }
    SubmitMovie(g_movieDrawOrig,args);
}
static_assert(std::is_same_v<decltype(&MovieDrawDetour), MovieDrawFn>, "six-argument native movie ABI");

int64_t CameraCounter()
{
    LARGE_INTEGER counter{}, frequency{};
    if (!g_counterFrequency && QueryPerformanceFrequency(&frequency)) g_counterFrequency = frequency.QuadPart;
    return g_counterFrequency && QueryPerformanceCounter(&counter) ? counter.QuadPart : 0;
}

void PublishOrbitTelemetry()
{
    OrbitTelemetry next;
    next.enabled = g_mouseLookEnabled;
    next.state = g_orbit.state;
    next.following = g_mouseLookEnabled && g_orbit.state == OrbitState::Following && g_orbit.initialized;
    next.capture = g_mouseCaptureOwned && input::CaptureActive();
    next.nativeDistance = g_orbit.nativeDistance;
    next.orbitDistance = g_orbit.distance;
    next.yawDegrees = g_orbit.yaw * 57.29577951308232f;
    next.pitchDegrees = g_orbit.pitch * 57.29577951308232f;
    next.renderGuardFailures = g_orbitRenderGuardFailures;
    next.renderGuardMask = g_orbitRenderGuardMask;
    next.headingRouted = g_headingRouted;
    next.headingRefused = g_headingRefused;
    OrbitState previous = OrbitState::Waiting;
    uint64_t transitions = 0;
    {
        std::lock_guard<std::mutex> lock(g_orbitTelemetryMutex);
        previous = g_orbitTelemetry.state;
        transitions = g_orbitTelemetry.transitions + (previous != next.state ? 1 : 0);
        next.transitions = transitions;
        g_orbitTelemetry = next;
    }
    if (previous != next.state) {
        const int64_t counter = CameraCounter();
        // File logging flushes synchronously. Preserve every transition in UI
        // counters, but never turn an ownership/capture failure into log spam.
        if (counter && (!g_lastOrbitLogCounter || counter-g_lastOrbitLogCounter >= g_counterFrequency)) {
            g_lastOrbitLogCounter = counter;
            ETH_INFO("EthhhPatch orbit: %s -> %s (native %.2f u, orbit %.2f u, capture %s; guard mask %02X, failures %u; %llu transitions coalesced)",
                OrbitStateName(previous), OrbitStateName(next.state), next.nativeDistance,
                next.orbitDistance, next.capture ? "on" : "off", g_orbitRenderGuardMask,
                g_orbitRenderGuardFailures, (unsigned long long)g_orbitSuppressedTransitions);
            g_orbitSuppressedTransitions = 0;
        } else ++g_orbitSuppressedTransitions;
    }
}

void LoadOrbitTuning()
{
    auto& cfg = Config::Get();
    const float sensitivity = cfg.GetFloat("ethhhpatch", "orbit_sensitivity", 1.0f);
    g_orbitSensitivity.store(std::isfinite(sensitivity) ? std::clamp(sensitivity, 0.05f, 5.0f) : 1.0f);
    g_orbitInvertY.store(cfg.GetBool("ethhhpatch", "orbit_invert_y", false));
    g_normalFovEnabled.store(cfg.GetBool("ethhhpatch", "normal_fov_enabled", false));
    g_normalFovDegrees.store(NormalFovDegrees(cfg.GetFloat("ethhhpatch", "normal_fov_degrees", 60)));
}

void ReleaseOrbitCapture()
{
    g_headingPolicy.Invalidate();
    if (g_mouseCaptureOwned && !g_externalCameraOwned.load()) input::SetCapture(false);
    g_mouseCaptureOwned = false;
}

void RestoreRenderCamera()
{
    if (!g_cameraBackup.pending && !g_lensBackup.pending) return;
    uintptr_t camera = 0, view = 0;
    SafeRead(game::A(game::off::pCamera), camera);
    SafeRead(game::A(game::off::pView), view);
    const bool cameraRestored = g_cameraBackup.Restore(camera, view, SafeReadBytes, SafeWriteBytes);
    const bool lensRestored = g_lensBackup.Restore(view, SafeReadBytes, SafeWriteBytes);
    if (!cameraRestored || !lensRestored) {
        g_cameraRestoreFailed = true;
        g_orbit.Suspend(OrbitState::Unavailable);
        ReleaseOrbitCapture();
        ETH_ERROR("EthhhPatch camera restore failed/ownership changed; foreign writes preserved and orbit disabled");
    }
}

void SampleOrbitCamera()
{
    if ((!g_mouseLookEnabled && !g_normalFovEnabled.load()) || g_cameraRestoreFailed) { PublishOrbitTelemetry(); return; }
    if (g_externalCameraOwned.load()) {
        g_orbit.Suspend(OrbitState::External); PublishOrbitTelemetry(); return;
    }
    const int64_t counter = CameraCounter();
    if (!counter) {
        g_orbit.Suspend(OrbitState::Unavailable); ReleaseOrbitCapture(); PublishOrbitTelemetry(); return;
    }
    if (counter < g_movieUntilCounter.load()) {
        g_orbit.Suspend(OrbitState::Movie); ReleaseOrbitCapture(); PublishOrbitTelemetry(); return;
    }
    if (input::OverlayOpen() || !input::Window() || GetForegroundWindow() != input::Window() ||
        !std::isfinite(game::TimeStep()) || game::TimeStep() <= 0) {
        g_orbit.Suspend(OrbitState::Paused); ReleaseOrbitCapture(); PublishOrbitTelemetry(); return;
    }
    const uintptr_t player = game::PlayerTracker::Get().Addr();
    game::StratIdentity identity;
    game::CameraView camera;
    uintptr_t view = 0;
    float position[3]{};
    if (!player || !game::ReadStratIdentity(player, identity) || identity.role != game::StratRole::Ninja ||
        !SafeReadBytes(player + game::off::StratPos, position, sizeof(position)) || !game::ReadCamera(camera) ||
        !SafeRead(game::A(game::off::pView), view) || !view) {
        g_orbit.Suspend(OrbitState::Unavailable); ReleaseOrbitCapture(); PublishOrbitTelemetry(); return;
    }
    const bool changed = player != g_orbitPlayer || identity.selfHandle != g_orbitHandle ||
        identity.pClass != g_orbitClass || camera.addr != g_orbitCamera || view != g_orbitView;
    if (changed) { g_orbit.OwnerChanged(); ReleaseOrbitCapture(); g_lastCameraTraceCounter = 0; }
    g_orbitPlayer = player; g_orbitClass = identity.pClass; g_orbitHandle = identity.selfHandle;
    g_orbitCamera = camera.addr; g_orbitView = view;
    // Resolve the script Camera strat, NOT the separate render Camera object.
    // Full class/live-list checks stay on the completed-tick sample, never on
    // the shared field getter's many unrelated VM reads.
    uintptr_t owner = 0;
    if (!g_orbit.Sample(game::TickCount(), double(counter) / g_counterFrequency,
        {position[0], position[1] + 1.3f, position[2]}, camera.world, camera.fov, false)) {
        ReleaseOrbitCapture();
    } else if (g_mouseLookEnabled && SafeRead(game::A(kNativeCameraOwner), owner) && owner) {
        // A fully identified owner can be reused under unchanged class/handle
        // generation. Do not add a live-list walk to every 60-Hz sample.
        if (g_headingCameraOwner.addr != owner ||
            !movement::LiveOwner(g_headingCameraOwner, game::A(game::off::StratMgrObject), SafeReadBytes)) {
            g_headingCameraOwner = {};
            game::StratIdentity ownerIdentity;
            char ownerName[game::kClassNameCapacity]{};
            if (game::ReadStratIdentity(owner, ownerIdentity) &&
                game::ReadClassName(ownerIdentity.pClass, ownerName, SafeReadBytes) && !strcmp(ownerName, "Camera"))
                g_headingCameraOwner = {owner, ownerIdentity.pClass, ownerIdentity.selfHandle};
        }
    } else g_headingCameraOwner = {};
    PublishOrbitTelemetry();
}

void ApplyRenderMouseLook()
{
    if (!g_inGameRender || !g_cameraHooksReady.load(std::memory_order_relaxed) || !g_mouseLookEnabled ||
        g_cameraRestoreFailed || g_externalCameraOwned.load()) return;
    const int64_t counter = CameraCounter();
    if (!counter || counter < g_movieUntilCounter.load() || g_orbit.state != OrbitState::Following) return;
    if (input::OverlayOpen() || !input::Window() || GetForegroundWindow() != input::Window()) {
        ReleaseOrbitCapture(); return;
    }
    uintptr_t camera = 0, view = 0, pClass = 0;
    uint32_t handle = 0;
    // No class-name/live-list traversal per rendered frame. The full exact-Ninja
    // check is sampled once per completed tick, followed by these reuse guards.
    uint32_t guardMask = 0;
    if (game::PlayerTracker::Get().Addr() != g_orbitPlayer) guardMask |= 1;
    if (!SafeRead(g_orbitPlayer + game::off::StratClass, pClass) || pClass != g_orbitClass) guardMask |= 2;
    if (!SafeRead(g_orbitPlayer + game::off::StratSelfHandle, handle) || handle != g_orbitHandle) guardMask |= 4;
    if (!SafeRead(game::A(game::off::pCamera), camera) || camera != g_orbitCamera) guardMask |= 8;
    if (!SafeRead(game::A(game::off::pView), view) || view != g_orbitView) guardMask |= 16;
    if (guardMask) {
        g_orbitRenderGuardMask = guardMask; ++g_orbitRenderGuardFailures;
        g_orbit.Suspend(OrbitState::Unavailable); ReleaseOrbitCapture(); PublishOrbitTelemetry(); return;
    }
    // SetCapture can fail while the window is losing/gaining focus. Retry each
    // rendered frame instead of latching a permanently dead capture flag.
    if (!g_mouseCaptureOwned) {
        input::ConsumeMouseDelta();
        input::ConsumeMouseWheel();
        g_mouseCaptureOwned = input::SetCapture(true);
        if (!g_mouseCaptureOwned) { PublishOrbitTelemetry(); return; }
    }
    if (!input::CaptureActive()) {
        g_mouseCaptureOwned = false;
        PublishOrbitTelemetry();
        return;
    }
    float wheel = 0;
    input::GetMouseWheel(wheel);
    if (wheel != 0.0f) {
        g_orbit.Zoom(wheel);
        input::ConsumeMouseWheel();
    }
    float dx = 0, dy = 0;
    input::GetMouseDelta(dx, dy);
    const float sensitivity = g_orbitSensitivity.load(std::memory_order_relaxed);
    const float direction = g_orbitInvertY.load(std::memory_order_relaxed) ? -1.0f : 1.0f;
    g_orbit.Mouse(dx, dy * direction,
        input::SensitivityScale() * sensitivity * 0.12f * 0.01745329252f);
    input::ConsumeMouseDelta();
    const double now = double(counter) / g_counterFrequency;
    const CameraPoint target = g_orbit.Target(now), forward = g_orbit.Forward();
    // A small ray bundle provides padding around the camera, not a claimed hull.
    // At most one batch per 60-Hz interval, never per sphere/object/UI widget.
    if (!g_lastCameraTraceCounter || counter - g_lastCameraTraceCounter >= g_counterFrequency / 60) {
        const double traceDt = g_lastCameraTraceCounter ? double(counter - g_lastCameraTraceCounter) / g_counterFrequency : 1.0 / 60.0;
        g_lastCameraTraceCounter = counter;
        const CameraPoint end = target - forward * g_orbit.distance;
        const CameraPoint right{std::cos(g_orbit.yaw), 0, -std::sin(g_orbit.yaw)};
        const CameraPoint offsets[5] = {{}, right * 0.15f, right * -0.15f, {0, 0.15f, 0}, {0, -0.15f, 0}};
        game::TraceSegment segments[5]{};
        game::TraceHit hits[5]{};
        for (int i = 0; i < 5; ++i) {
            const CameraPoint a = target + offsets[i], b = end + offsets[i];
            segments[i] = {{a.x, a.y, a.z}, {b.x, b.y, b.z}};
        }
        if (!game::TraceWorldSegments(g_orbitPlayer, segments, 5, hits)) {
            g_orbit.Suspend(OrbitState::CollisionFailed); ReleaseOrbitCapture(); PublishOrbitTelemetry(); return;
        }
        float allowed = g_orbit.distance;
        for (int i = 0; i < 5; ++i) if (hits[i].hit) {
            const CameraPoint hit{hits[i].point[0], hits[i].point[1], hits[i].point[2]};
            const CameraPoint start{segments[i].start[0], segments[i].start[1], segments[i].start[2]};
            const float along = (hit - start).Dot(forward * -1.0f);
            if (!hit.Finite() || !std::isfinite(along) || along < -0.01f || along > g_orbit.distance + 0.01f) {
                g_orbit.Suspend(OrbitState::CollisionFailed); ReleaseOrbitCapture(); PublishOrbitTelemetry(); return;
            }
            allowed = (std::min)(allowed, (std::max)(0.0f, along - 0.2f));
        }
        g_orbit.Obstruction(allowed, traceDt);
    }
    const CameraPoint eye = target - forward * (std::min)(g_orbit.distance, g_orbit.collisionDistance);
    float adjusted[16]{}, pose[16]{};
    game::MakeCameraWorld(eye.x, eye.y, eye.z, g_orbit.yaw, g_orbit.pitch, 0, adjusted);
    pose[0] = eye.x; pose[1] = eye.y; pose[2] = eye.z; pose[3] = 1;
    OrbitQuaternion(g_orbit.yaw, g_orbit.pitch, pose + 4);
    memcpy(pose + 8, pose, sizeof(float) * 8);
    if (!g_cameraBackup.Capture(camera, view, SafeReadBytes)) {
        g_orbit.Suspend(OrbitState::Unavailable); ReleaseOrbitCapture(); PublishOrbitTelemetry(); return;
    }
    // View+0x20/+0x40 are the actual render position/quaternion endpoints;
    // camera+0x80/+0xC0 keep CPU culling coherent for this frame only.
    if (!game::SetCameraWorld(adjusted) || !SafeWriteBytes(view + 0x20, pose, sizeof(pose))) {
        RestoreRenderCamera(); g_orbit.Suspend(OrbitState::Unavailable); ReleaseOrbitCapture();
    } else {
        g_cameraBackup.MarkInstalled(adjusted, pose);
        g_headingPolicy.Rendered({{g_orbitPlayer, g_orbitClass, g_orbitHandle}, g_headingCameraOwner,
            camera, view, game::TickCount(), now, g_orbit.yaw});
    }
    PublishOrbitTelemetry();
}

bool WriteHeadingScratch(uintptr_t address, const void* value, size_t bytes)
{
    if (!address || !value || bytes != sizeof(float)) return false;
    // The original getter has just written this verified VM register. Never
    // change page protection or use SafeWrite's executable-memory fallback.
    __try { memcpy((void*)address, value, bytes); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool NativeOrbitStillPlayable(void* self)
{
    float world[16]{}, position[3]{}, fov = 0;
    const auto& visible = g_headingPolicy.Visible();
    if (!SafeReadBytes(visible.camera + game::off::CamMat, world, sizeof(world)) ||
        !SafeReadBytes(uintptr_t(self) + game::off::StratPos, position, sizeof(position)) ||
        !SafeRead(visible.camera + game::off::CamFov, fov)) return false;
    // Observe a newly changed native shot in the getter too, before the next
    // completed-tick publication. A copy cannot advance the real orbit state.
    OrbitPolicy probe = g_orbit;
    const int64_t counter = CameraCounter();
    return counter && probe.Sample(game::TickCount() + 1, double(counter) / g_counterFrequency,
        {position[0], position[1] + 1.3f, position[2]}, world, fov, false);
}

uint32_t __fastcall BuiltinFieldScoped(void* self, void*, void* output, void* other,
    uint32_t field, uintptr_t caller, uintptr_t nextInstruction)
{
    // Preserve native behavior/return value for EVERY field and caller, including
    // this one. Only its single scratch result may be compensated afterwards.
    const uint32_t result = g_builtinFieldOrig ? g_builtinFieldOrig(self, output, other, field) : 0;
    if (field != 0x6A || caller != game::A(kBuiltinFieldReturn) || uintptr_t(self) != g_orbitPlayer ||
        other != self || !g_headingPolicy.Eligible()) return result;
    const bool focused = input::Window() && GetForegroundWindow() == input::Window();
    if (!g_cameraHooksReady.load() || !g_mouseLookEnabled || g_orbit.state != OrbitState::Following ||
        !g_orbit.initialized || game::PlayerTracker::Get().Addr() != g_orbitPlayer ||
        !focused || input::OverlayOpen() || input::KeyDown(VK_F9) ||
        input::KeyDown(VK_CONTROL) || g_externalCameraOwned.load() || input::NativeMovementKeysOwned() ||
        g_cameraRestoreFailed || !NativeOrbitStillPlayable(self)) {
        g_headingPolicy.Invalidate(); ++g_headingRefused; return result;
    }
    movement::Context context{true, g_headingPolicy.Visible(), game::A(game::off::StratMgrObject),
        game::A(kNativeCameraOwner), game::A(game::off::pCamera), game::A(game::off::pView), game::A(kBuiltinFieldReturn)};
    const movement::Call call{uintptr_t(self), uintptr_t(other), uintptr_t(output), caller, nextInstruction, field};
    if (movement::Route(context, call, SafeReadBytes, WriteHeadingScratch)) ++g_headingRouted;
    else { ++g_headingRefused; g_headingPolicy.Invalidate(); }
    return result;
}

ETH_HEADING_THUNK(BuiltinFieldDetour, BuiltinFieldScoped)

float CurrentRenderFraction()
{
    const float step = game::TimeStep();
    if (!std::isfinite(step) || step <= 0 || input::OverlayOpen()) return 1;
    uintptr_t app = 0;
    float tickMilliseconds = 0, ticksPerMillisecond = 0, remainderMilliseconds = 0;
    if (!SafeRead(game::A(game::off::pApp), app) || !app) return 1;
    const uintptr_t timer = app + game::off::AppTimer;
    if (!SafeRead(timer + game::off::TimerMsPerTick, tickMilliseconds) ||
        !SafeRead(timer + game::off::TimerTicksPerMs, ticksPerMillisecond) ||
        !SafeRead(timer + game::off::TimerRemainderMs, remainderMilliseconds)) return 1;
    return RenderInterpolationFraction(tickMilliseconds, ticksPerMillisecond, remainderMilliseconds);
}

void ApplyNormalRenderFov(void* self)
{
    if (!g_active.load() || !g_normalFovEnabled.load() || g_cameraRestoreFailed ||
        g_externalCameraOwned.load() || g_orbit.state != OrbitState::Following ||
        input::OverlayOpen() || !input::Window() || GetForegroundWindow() != input::Window()) return;
    const int64_t counter = CameraCounter();
    if (!counter || counter < g_movieUntilCounter.load()) return;
    uintptr_t camera = 0, view = 0, cls = 0;
    uint32_t handle = 0;
    if (game::PlayerTracker::Get().Addr() != g_orbitPlayer ||
        !SafeRead(g_orbitPlayer + game::off::StratClass, cls) || cls != g_orbitClass ||
        !SafeRead(g_orbitPlayer + game::off::StratSelfHandle, handle) || handle != g_orbitHandle ||
        !SafeRead(game::A(game::off::pCamera), camera) || camera != g_orbitCamera ||
        !SafeRead(game::A(game::off::pView), view) || view != g_orbitView || uintptr_t(self) != view) return;
    if (!g_lensBackup.Apply(view, g_normalFovDegrees.load(), SafeReadBytes, SafeWriteBytes)) RestoreRenderCamera();
}

void SampleEntityPoses()
{
    if (!g_entityInterpolationEnabled.load(std::memory_order_relaxed)) return;
    const float step = game::TimeStep();
    if (!std::isfinite(step) || step <= 0 || input::OverlayOpen()) {
        g_entityPoses.Reset();
        std::lock_guard<std::mutex> lock(g_entityTelemetryMutex);
        g_entityTelemetry.sampled = g_entityTelemetry.paired = 0;
        return;
    }
    const uint32_t tick = game::TickCount();
    if (!g_entityPoses.BeginTick(tick)) return;
    const int64_t begin = CameraCounter();
    const int64_t limit = g_counterFrequency * kEntityWorkBudgetMilliseconds / 1000;
    bool budgetStopped = false;
    const auto withinBudget = [&]() {
        const int64_t now = CameraCounter();
        const bool available = begin && now >= begin && limit > 0 && now-begin < limit;
        budgetStopped |= !available;
        return available;
    };
    g_entitySampleReads.Reset();
    const auto read = [](uintptr_t address, void* output, size_t bytes) {
        return entity::ReadProtected(g_entitySampleReads, address, output, bytes);
    };
    // Only links/owners, no inspector snapshots, strings or quadratic scans.
    // Do not sample ANY pose unless this bounded live list completes unchanged.
    const bool complete = g_entityOwners.Collect(game::A(game::off::StratListHead), read, withinBudget);
    if (!complete) {
        g_entityPoses.Reset();
    } else {
        const uintptr_t manager = game::A(game::off::StratMgrObject);
        for (size_t i=0; i<g_entityOwners.Size(); ++i) {
            if (!withinBudget()) break; // unprocessed owners use native poses
            entity::Key key;
            if (entity::ReadKey(g_entityOwners[i], manager, key, read))
                g_entityPoses.Sample(key, manager, read);
        }
        g_entityPoses.EndTick();
        withinBudget();
    }
    const int64_t end = CameraCounter();
    std::lock_guard<std::mutex> lock(g_entityTelemetryMutex);
    g_entityTelemetry.sampled = g_entityPoses.Size();
    g_entityTelemetry.paired = g_entityPoses.Paired();
    g_entityTelemetry.owners = g_entityOwners.Size();
    g_entityTelemetry.listComplete = complete;
    g_entityTelemetry.sampleQueries = g_entitySampleReads.Queries();
    g_entityTelemetry.sampleMs = begin && end >= begin && g_counterFrequency ?
        1000.0 * double(end-begin) / g_counterFrequency : 0;
    if (budgetStopped) ++g_entityTelemetry.sampleBudgetStops;
    g_entitySampleReads.Reset();
}

bool ReadEntityRender(uintptr_t address, void* output, size_t bytes)
{
    return entity::ReadProtected(g_entityRenderReads, address, output, bytes);
}

void ChargeEntityRenderWork(int64_t begin)
{
    const int64_t end = CameraCounter();
    g_entityRenderBudget.Charge(begin, end);
    if (begin && end >= begin) g_entityRenderWorkTicks += end-begin;
    if (!g_entityRenderBudget.Available() && !g_entityRenderBudgetStopped) {
        g_entityRenderBudgetStopped = true;
        ++g_entityFrameTelemetry.renderBudgetStops;
    }
}

void BeginEntityFrame()
{
    g_entityRenderReads.Reset();
    CameraCounter(); // initialize the cached QPC frequency, not the sim clock
    g_entityRenderBudget.Reset(g_counterFrequency * kEntityWorkBudgetMilliseconds / 1000);
    g_entityFrameTelemetry = {};
    g_entityRenderWorkTicks = 0;
    g_entityRenderBudgetStopped = false;
}

void FinishEntityFrame()
{
    // One telemetry publication per frame; NEVER a mutex/file flush per entity.
    std::lock_guard<std::mutex> lock(g_entityTelemetryMutex);
    g_entityTelemetry.interpolatedSubmissions += g_entityFrameTelemetry.interpolatedSubmissions;
    g_entityTelemetry.nativeSubmissions += g_entityFrameTelemetry.nativeSubmissions;
    g_entityTelemetry.renderBudgetStops += g_entityFrameTelemetry.renderBudgetStops;
    // F9 suspends interpolation; retain the last eligible gameplay measurement
    // instead of replacing it with zero just as the user opens Diagnostics.
    if (g_entityFrameAlpha < 1) {
        g_entityTelemetry.renderQueries = g_entityRenderReads.Queries();
        g_entityTelemetry.renderMs = g_counterFrequency ? 1000.0 * double(g_entityRenderWorkTicks) / g_counterFrequency : 0;
    }
    g_entityRenderReads.Reset();
}

void __fastcall NodeSubmitDetour(void* self, void*, void* mesh,
    const entity::Matrix* world, const entity::Matrix* nodes)
{
    entity::Pose* pose = g_submittedPose;
    const entity::Matrix* supplied = nodes;
    const entity::Matrix* suppliedWorld = world;
    if (pose && g_inGameRender && world == &pose->renderWorld) {
        const int64_t begin = CameraCounter();
        if (uintptr_t(nodes) == pose->key.nodes && entity::MeshFits(uintptr_t(mesh), pose->key.count, ReadEntityRender))
            supplied = pose->renderNodes.data();
        else suppliedWorld = &pose->nativeWorld; // never mix a blended root with native bones
        ChargeEntityRenderWork(begin); // finish an accepted pose even at the budget boundary
    }
    if (g_nodeSubmitOrig) g_nodeSubmitOrig(self, mesh, suppliedWorld, supplied);
}

void SubmitEntityScoped(void* self, void* owner, void* instance, const entity::Matrix* world, entity::Pose* pose)
{
    entity::Pose* previousPose = g_submittedPose;
    const bool previousSubmission = g_entitySubmission;
    g_entitySubmission = true; g_submittedPose = pose;
    __try {
        if (g_entitySubmitOrig) g_entitySubmitOrig(self, owner, instance, pose ? &pose->renderWorld : world);
    } __finally { g_submittedPose = previousPose; g_entitySubmission = previousSubmission; }
}

void __fastcall EntitySubmitDetour(void* self, void*, void* owner, void* instance,
    const entity::Matrix* world)
{
    if (g_entitySubmission || !g_inGameRender || !g_active.load(std::memory_order_relaxed) ||
        !g_entityInterpolationEnabled.load(std::memory_order_relaxed) ||
        g_entityFrameAlpha >= 1 || input::OverlayOpen()) {
        SubmitEntityScoped(self, owner, instance, world, nullptr);
        return;
    }
    if (!g_entityRenderBudget.Available()) {
        ++g_entityFrameTelemetry.nativeSubmissions;
        SubmitEntityScoped(self, owner, instance, world, nullptr);
        return;
    }
    const int64_t begin = CameraCounter();
    entity::Matrix copied;
    entity::Pose* pose = ReadEntityRender(uintptr_t(world), &copied, sizeof(copied)) ?
        g_entityPoses.Render(uintptr_t(owner), uintptr_t(instance), copied,
            game::A(game::off::StratMgrObject), g_entityFrameTick, g_entityFrameAlpha, ReadEntityRender) : nullptr;
    // Validate BOTH native passes before changing even the root input. A failed
    // mesh check must not combine an interpolated root with native bones.
    uint32_t meshes[2]{};
    if (pose && (!ReadEntityRender(uintptr_t(instance), meshes, sizeof(meshes)) ||
        !entity::MeshFits(meshes[0], pose->key.count, ReadEntityRender) ||
        (meshes[1] && !entity::MeshFits(meshes[1], pose->key.count, ReadEntityRender)))) pose = nullptr;
    ChargeEntityRenderWork(begin); // do NOT charge the native renderer/Present
    if (pose) ++g_entityFrameTelemetry.interpolatedSubmissions;
    else ++g_entityFrameTelemetry.nativeSubmissions;
    SubmitEntityScoped(self, owner, instance, world, pose);
}

void __fastcall ViewRenderDetour(void* self, void*)
{
    // Secondary calls rebuild transforms after effect passes. They must not
    // capture/install again: the primary endpoints remain valid for this frame.
    const uintptr_t caller = uintptr_t(_ReturnAddress());
    uintptr_t liveView = 0;
    if (g_inGameRender) SafeRead(game::A(game::off::pView), liveView);
    const bool primary = g_primaryViewPass.Acquire(g_inGameRender, caller,
        game::A(kPrimaryViewReturn), uintptr_t(self), liveView);
    if (primary) { ApplyRenderMouseLook(); ApplyNormalRenderFov(self); }
    // Fn_RenderStep forces App+0x28 to 1.0 (0x43E1DC), so the engine normally
    // renders the newest simulated pose. While the render split is active we
    // substitute the timer's sub-tick remainder here, immediately around the
    // engine's own View_Render blend (0x43F2DE..0x43F31F). No simulation tick
    // is run or repeated. The fraction is restored afterwards so no other
    // render path observes it.
    uintptr_t app = 0;
    float savedFraction = 1.0f;
    bool interpolated = false;
    if (g_inGameRender && g_active.load(std::memory_order_relaxed) &&
        !(g_cameraBackup.pending && g_cameraBackup.installed) &&
        SafeRead(game::A(game::off::pApp), app) && app) {
        {
            const float fraction = CurrentRenderFraction();
            if (fraction < 1.0f &&
                SafeRead(app + game::off::AppInterpolation, savedFraction) &&
                SafeWrite(app + game::off::AppInterpolation, fraction)) {
                interpolated = true;
                if (!g_interpolationLogged.exchange(true))
                    ETH_INFO("EthhhPatch render interpolation active (60-Hz simulation, %.4f alpha)", fraction);
            }
        }
    }
    __try { if (g_viewRenderOrig) g_viewRenderOrig(self); }
    __finally { if (interpolated) SafeWrite(app + game::off::AppInterpolation, savedFraction); }
}

void __fastcall AppRenderDetour(void* self, void*)
{
    if (g_inGameRender) { if (g_appRenderOrig) g_appRenderOrig(self); return; }
    g_inGameRender = true;
    g_inventoryRenderReads.Reset();
    g_inventoryClipBounds.Configure(g_aspect.load(std::memory_order_relaxed));
    g_primaryViewPass.Reset();
    g_entityFrameAlpha = g_active.load(std::memory_order_relaxed) ? CurrentRenderFraction() : 1;
    g_entityFrameTick = game::TickCount();
    BeginEntityFrame();
    // Includes all queue building, scene drawing, EndScene and Present paths,
    // including early returns and exception unwinding. Never leak into Fn_Tick.
    __try { if (g_appRenderOrig) g_appRenderOrig(self); }
    __finally { RestoreRenderCamera(); FinishEntityFrame(); FinishInventoryFrame();
        g_entityFrameAlpha = 1; g_inGameRender = false; }
}
} // namespace

bool InstallHooks(bool cameraOnly)
{
    if (cameraOnly ? g_cameraHooksReady.load() : g_hooksReady.load()) return true;
    char exe[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (!length || length >= MAX_PATH || Sha256FileHex(exe) !=
        "49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0") {
        ETH_ERROR("EthhhPatch refused hooks: unsupported executable fingerprint"); return false;
    }
    const uint8_t submitBytes[] = {0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x83,0xEC,0x24};
    const uint8_t nodeBytes[] = {0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x81,0xEC,0x94,0,0,0};
    uint8_t current[12]{};
    const uint8_t primaryCall[] = {0xE8,0xD0,0xBB,0,0}; // 0x43E22B -> 0x449E00
    const uint8_t primaryTail[] = {0xE9,0x61,0x54,0xFF,0xFF}; // 0x449E2A -> 0x43F290
    const uint8_t fieldEntry[] = {0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x83,0xEC,0x48};
    const uint8_t fieldCall[] = {0xE8,0x96,0xC3,0xFF,0xFF}; // selector 0x10, thiscall ret 0xC
    if (cameraOnly && (!SafeReadBytes(game::A(kBuiltinFieldGetter), current, sizeof(fieldEntry)) ||
        memcmp(current, fieldEntry, sizeof(fieldEntry)) ||
        !SafeReadBytes(game::A(kBuiltinFieldReturn - 5), current, sizeof(fieldCall)) ||
        memcmp(current, fieldCall, sizeof(fieldCall)))) {
        ETH_ERROR("EthhhPatch refused heading hook: built-in field contract changed"); return false;
    }
    if (!SafeReadBytes(game::A(kPrimaryViewReturn - 5), current, sizeof(primaryCall)) ||
        memcmp(current, primaryCall, sizeof(primaryCall)) ||
        !SafeReadBytes(game::A(0x449E2A), current, sizeof(primaryTail)) ||
        memcmp(current, primaryTail, sizeof(primaryTail))) {
        ETH_ERROR("EthhhPatch refused camera hooks: primary render path changed"); return false;
    }
    if (!cameraOnly && (!SafeReadBytes(game::A(kEntitySubmit),current,sizeof(submitBytes)) || memcmp(current,submitBytes,sizeof(submitBytes)) ||
        !SafeReadBytes(game::A(kNodeSubmit),current,sizeof(nodeBytes)) || memcmp(current,nodeBytes,sizeof(nodeBytes)))) {
        ETH_ERROR("EthhhPatch refused entity hooks: render entry bytes changed"); return false;
    }
    const uint8_t inventoryEntry[] = {0x8B,0x54,0x24,0x08,0x8B,0x0A,0x8B,0xC1};
    const uint8_t nativeCall[] = {0x83,0xC6,0x04,0xFF,0xD0}; // advance ESI, cdecl call eax
    const uint8_t uiDrawEntry[] = {0x83,0xEC,0x1C,0xA1};
    const uint8_t clipEntry[] = {0x83,0xEC,0x0C,0x8B,0x44,0x24,0x10};
    const uint8_t clipCall[] = {0xE8,0xC7,0xF8,0xFF,0xFF};
    const uint8_t movieEntry[] = {0x8B,0x44,0x24,0x04,0x85,0xC0,0x74,0x15};
    const uint8_t movieCall[] = {0xE8,0x36,0xD9,0xFF,0xFF};
    const uint8_t movieCleanup[] = {0x83,0xC4,0x18}; // six cdecl arguments
    const uint8_t overlayProjection[] = {0x50,0x6A,0x03,0x53,0xFF,0x92,0xB0,0,0,0};
    const uint8_t overlayBuilder[] = {0xE8,0x13,0x03,0x01,0};
    const uint8_t overlayDepth[] = {0x68,0,0,0xC8,0x44,0x68,0xCD,0xCC,0xCC,0x3D};
    if (!cameraOnly && (!SafeReadBytes(game::A(kInventoryMove), current, sizeof(inventoryEntry)) ||
        memcmp(current, inventoryEntry, sizeof(inventoryEntry)) ||
        !SafeReadBytes(game::A(kInventoryMoveReturn - 5), current, sizeof(nativeCall)) ||
        memcmp(current, nativeCall, sizeof(nativeCall)))) {
        ETH_ERROR("EthhhPatch refused inventory hook: native position contract changed"); return false;
    }
    if (!cameraOnly && (!SafeReadBytes(game::A(kUiObjectDraw), current, sizeof(uiDrawEntry)) ||
        memcmp(current, uiDrawEntry, sizeof(uiDrawEntry)) ||
        !SafeReadBytes(game::A(kGlyphClip), current, sizeof(clipEntry)) ||
        memcmp(current, clipEntry, sizeof(clipEntry)) ||
        !SafeReadBytes(game::A(kGlyphClipReturn-5), current, sizeof(clipCall)) ||
        memcmp(current, clipCall, sizeof(clipCall)))) {
        ETH_ERROR("EthhhPatch refused inventory text hook: draw/clip contract changed"); return false;
    }
    if (!cameraOnly && (!SafeReadBytes(game::A(kMovieDraw),current,sizeof(movieEntry)) ||
        memcmp(current,movieEntry,sizeof(movieEntry)) ||
        !SafeReadBytes(game::A(0x49A405),current,sizeof(movieCall)) ||
        memcmp(current,movieCall,sizeof(movieCall)) ||
        !SafeReadBytes(game::A(0x49A410),current,sizeof(movieCleanup)) ||
        memcmp(current,movieCleanup,sizeof(movieCleanup)))) {
        ETH_ERROR("EthhhPatch refused movie hook: six-argument draw contract changed"); return false;
    }
    if (!cameraOnly && (!SafeReadBytes(game::A(0x44EA96),current,sizeof(overlayProjection)) ||
        memcmp(current,overlayProjection,sizeof(overlayProjection)) ||
        !SafeReadBytes(game::A(0x44EA8B),current,sizeof(overlayBuilder)) ||
        memcmp(current,overlayBuilder,sizeof(overlayBuilder)) ||
        !SafeReadBytes(game::A(0x44EA6C),current,sizeof(overlayDepth)) ||
        memcmp(current,overlayDepth,sizeof(overlayDepth)))) {
        ETH_ERROR("EthhhPatch refused HUD model projection: authored overlay contract changed"); return false;
    }
    struct Hook { uintptr_t va; void* detour; void** original; };
    const Hook hooks[] = {
        {kSphereCull,(void*)&SphereCullDetour,(void**)&g_sphereCullOrig},
        {kHudPulse,(void*)&HudPulseDetour,(void**)&g_hudPulseOrig},
        {kInventoryMove,(void*)&InventoryMoveDetour,(void**)&g_inventoryMoveOrig},
        {kUiObjectDraw,(void*)&UiObjectDrawDetour,(void**)&g_uiObjectDrawOrig},
        {kGlyphClip,(void*)&GlyphClipDetour,(void**)&g_glyphClipOrig},
        {kMovieDraw,(void*)&MovieDrawDetour,(void**)&g_movieDrawOrig},
        {kViewRender,(void*)&ViewRenderDetour,(void**)&g_viewRenderOrig},
        {kAppRender,(void*)&AppRenderDetour,(void**)&g_appRenderOrig},
        {kEntitySubmit,(void*)&EntitySubmitDetour,(void**)&g_entitySubmitOrig},
        {kNodeSubmit,(void*)&NodeSubmitDetour,(void**)&g_nodeSubmitOrig},
        {kBuiltinFieldGetter,(void*)&BuiltinFieldDetour,(void**)&g_builtinFieldOrig}};
    const Hook* created[11]{};
    size_t createdCount = 0;
    bool enabled = true;
    for (const auto& hook : hooks) {
        const bool shared = hook.va == kViewRender || hook.va == kAppRender;
        const bool camera = hook.va == kBuiltinFieldGetter;
        if ((!shared && camera != cameraOnly) || *hook.original) continue;
        const MH_STATUS status = MH_CreateHook((void*)game::A(hook.va),hook.detour,hook.original);
        if (status != MH_OK) {
            ETH_ERROR("EthhhPatch hook %08X failed: %s",unsigned(hook.va),MH_StatusToString(status));
            enabled = false; break;
        }
        created[createdCount++] = &hook;
    }
    if (enabled) for (size_t i = 0; i < createdCount; ++i)
        if (MH_QueueEnableHook((void*)game::A(created[i]->va)) != MH_OK) { enabled = false; break; }
    if (enabled) enabled = MH_ApplyQueued() == MH_OK;
    if (!enabled) {
        for (size_t i=0;i<createdCount;++i) {
            MH_DisableHook((void*)game::A(created[i]->va)); MH_RemoveHook((void*)game::A(created[i]->va));
            *created[i]->original = nullptr;
        }
        return false;
    }
    if (cameraOnly) g_cameraHooksReady.store(true);
    else g_hooksReady.store(true);
    ETH_INFO("EthhhPatch %s hooks ready; shared render scope retained independently", cameraOnly ? "camera/heading" : "display");
    return true;
}

void UninstallHooks()
{
    if (!g_hooksReady.exchange(false)) return;
    DeactivateRenderSplit();
    void* targets[] = { (void*)game::A(kSphereCull), (void*)game::A(kHudPulse), (void*)game::A(kMovieDraw),
        (void*)game::A(kEntitySubmit), (void*)game::A(kNodeSubmit), (void*)game::A(kInventoryMove),
        (void*)game::A(kUiObjectDraw), (void*)game::A(kGlyphClip) };
    for (void* target : targets) {
        MH_DisableHook(target);
        MH_RemoveHook(target);
    }
    g_sphereCullOrig = nullptr; g_hudPulseOrig = nullptr; g_movieDrawOrig = nullptr;
    g_inventoryMoveOrig = nullptr;
    g_uiObjectDrawOrig = nullptr; g_glyphClipOrig = nullptr;
    g_entitySubmitOrig = nullptr; g_nodeSubmitOrig = nullptr;
    ETH_INFO("EthhhPatch compatibility hooks removed");
}

bool ActivateRenderSplit(float aspect, unsigned width, unsigned height, int targetHz)
{
    if (!g_hooksReady.load() || !std::isfinite(aspect) || aspect < 0.5f || aspect > 8.0f ||
        targetHz < 30 || targetHz > 360) return false;
    if (g_active.load()) return std::abs(g_aspect.load() - aspect) < 1e-6f;
    g_aspect.store(aspect);
    g_targetWidth.store(width);
    g_targetHeight.store(height);
    g_targetHz.store(targetHz);
    g_nextPresentCounter = 0;
    g_pulseClock = {};
    g_headingPolicy.Reset(); g_headingCameraOwner = {}; g_headingRouted = g_headingRefused = 0;
    LoadOrbitTuning();
    g_entityPoses.Reset();
    g_orbitRenderGuardFailures = g_orbitRenderGuardMask = 0;
    g_lastOrbitLogCounter = 0; g_orbitSuppressedTransitions = 0;
    g_entityInterpolationEnabled.store(Config::Get().GetBool("ethhhpatch", "entity_interpolation", true));
    { std::lock_guard<std::mutex> lock(g_entityTelemetryMutex); g_entityTelemetry = {}; }
    { std::lock_guard<std::mutex> lock(g_orbitTelemetryMutex); g_orbitTelemetry = {}; }
    if (!ExactCodeWrite(game::A(kZeroTickWaitBranch), kWaitBytes, kZeroTickBytes)) {
        ETH_ERROR("EthhhPatch refused zero-tick branch patch: executable bytes differ");
        g_aspect.store(kOriginalAspect); g_targetWidth.store(0); g_targetHeight.store(0);
        g_targetHz.store(0); return false;
    }
    g_active.store(true);
    OnTickCompleted();
    if (!g_active.load()) {
        ETH_ERROR("EthhhPatch activation failed: aspect policy could not stay active");
        return false;
    }
    ETH_INFO("EthhhPatch render split activated internally (aspect %.6f); display application still gated", aspect);
    return true;
}

void DeactivateRenderSplit()
{
    if (!g_active.exchange(false)) return;
    g_inventoryRoots.Restore(game::A(kUiHandles), game::A(kSpriteObjectVtable), SafeReadBytes,
        [](uint32_t self, const inventory::Position& position) {
            if (g_inventoryMoveOrig) g_inventoryMoveOrig(reinterpret_cast<void*>(self), &position);
        });
    g_inventoryAnchored = false;
    g_hudAnchoredKinds = 0;
    g_inventoryClipMatched = false; g_inventoryNativeTick = false; g_inventoryText.Clear();
    g_inventoryProfile={}; g_inventoryClipLogPending=false;
    g_inventoryTickReads.Reset(); g_inventoryRenderReads.Reset();
    if (!ExactCodeWrite(game::A(kZeroTickWaitBranch), kZeroTickBytes, kWaitBytes))
        ETH_ERROR("EthhhPatch could not restore original wait branch bytes");
    g_aspect.store(kOriginalAspect);
    g_targetWidth.store(0); g_targetHeight.store(0);
    g_targetHz.store(0); g_nextPresentCounter = 0;
    RestoreRenderCamera(); ReleaseOrbitCapture();
    g_headingPolicy.Reset(); g_headingCameraOwner = {};
    g_pulseClock = {};
    g_entityPoses.Reset(); g_submittedPose = nullptr;
    { std::lock_guard<std::mutex> lock(g_entityTelemetryMutex); g_entityTelemetry = {}; }
    { std::lock_guard<std::mutex> lock(g_orbitTelemetryMutex); g_orbitTelemetry = {}; }
}

void OnTickCompleted()
{
    // Resume only on the game/window thread after successful presentation has
    // settled. Keep the original transaction/baseline: RequestDisplayPlan here
    // would accidentally replace Restore's baseline with the transient mode.
    if (g_displayResetRecovery.Pending()) {
        HWND window = (HWND)game::GameHwnd();
        const int64_t counter = CameraCounter();
        DWORD process = 0;
        if (!window || IsIconic(window) || GetForegroundWindow() != window || input::OverlayOpen()) {
            g_displayResetGate.Reset();
        } else if (counter && counter >= g_movieUntilCounter.load() &&
            GetWindowThreadProcessId(window, &process) == GetCurrentThreadId() &&
            g_displayResetGate.Ready(double(counter) / g_counterFrequency)) {
            const auto status = ReadApplyStatus();
            g_displayResetRecovery.Take(); // one request, even if it fails
            if (status.state == ApplyState::Active) {
                ETH_INFO("EthhhPatch transient window Reset: requesting original verified client size once");
                if (!ConfigureDisplay(status.requested.windowed, status.requested.width,
                    status.requested.height, status.targetHz) ||
                    !ResizeClient(window, status.requested.width, status.requested.height)) {
                    StartRollback("transient window Reset recovery request failed");
                }
            }
        }
    }
    // Tick runs on the game/window thread. Wait for Reset observation and input
    // hooks, not merely a device pointer discovered by the watcher thread.
    if (!g_startupDisplayAttempted && hooks::D3DHooked() && hooks::WndProcHooked()) {
        HWND window = (HWND)game::GameHwnd();
        const int64_t counter = CameraCounter();
        DWORD process = 0;
        uintptr_t view = 0;
        float nativeAspect = 0;
        if (window && game::Device() && IsWindowVisible(window) && !IsIconic(window) &&
            GetForegroundWindow() == window && !input::OverlayOpen() &&
            GetWindowThreadProcessId(window, &process) == GetCurrentThreadId() &&
            counter && counter >= g_movieUntilCounter.load() &&
            SafeRead(game::A(game::off::pView), view) && view &&
            SafeRead(view, nativeAspect) && std::isfinite(nativeAspect) && nativeAspect > 0 &&
            g_startupDisplayGate.Ready(double(counter) / g_counterFrequency)) {
            g_startupDisplayAttempted = true;
            DisplayPlan saved;
            const bool recovering = g_startupDisplayRecovery.Pending();
            const auto state = ReadApplyStatus().state;
            const bool eligible = state == ApplyState::Idle || (recovering && state == ApplyState::Failed);
            if (recovering) saved = g_startupRecoveryPlan;
            if (eligible && (recovering || LoadStartupPlan(saved))) {
                std::string error;
                g_startupDisplayRecovery.Take();
                g_startupApplyIsRecovery = recovering;
                // Disarm before trying: a crash, refusal or failed Reset must
                // not cause an automatic retry loop on every subsequent boot.
                if (!DisableStartupPlan()) {
                    ETH_ERROR("EthhhPatch startup display refused: could not persist recovery state");
                } else {
                    g_startupApplyInFlight = true;
                    if (!RequestDisplayPlan(saved, ReadMonitor(window).hz, error)) {
                        g_startupApplyInFlight = false;
                        g_startupDisplayRecovery.Cancel();
                        {
                            std::lock_guard<std::mutex> lock(g_applyMutex);
                            // RequestDisplayPlan may already have started rollback.
                            if (g_applyStatus.state == ApplyState::Idle || g_applyStatus.state == ApplyState::Failed) {
                                g_applyStatus.state = ApplyState::Failed;
                                g_applyStatus.detail = "saved startup display refused: " + error;
                            }
                        }
                        ETH_ERROR("EthhhPatch startup display refused: %s", error.c_str());
                    }
                }
            }
        } else if (!window || GetForegroundWindow() != window || input::OverlayOpen() ||
            (window && IsIconic(window))) {
            g_startupDisplayGate.Reset();
        }
    }
    if (g_cameraHooksReady.load(std::memory_order_relaxed)) SampleOrbitCamera();
    if (!g_active.load(std::memory_order_relaxed)) return;
    uintptr_t view = 0;
    const float aspect = g_aspect.load(std::memory_order_relaxed);
    if (!SafeRead(game::A(game::off::pView), view) || !view || !SafeWrite(view, aspect)) {
        ETH_ERROR("EthhhPatch aspect write failed; disabling render split");
        DeactivateRenderSplit();
        return;
    }
    if (!g_cameraHooksReady.load(std::memory_order_relaxed)) SampleOrbitCamera();
    SampleEntityPoses();
}

void BeginNativeMovementTick()
{
    // Native 0x448450 clears font queues at the start of each tick; UI-object
    // text is queued later at 0x43E119, not inside App_render_step. Keep its
    // provenance through all zero-tick renders until the NEXT native tick.
    g_inventoryText.Clear();
    g_inventoryTickReads.Reset();
    g_inventoryNativeTick = true;
    if (!g_cameraHooksReady.load(std::memory_order_relaxed)) { g_headingPolicy.Invalidate(); return; }
    const int64_t counter = CameraCounter();
    movement::Gates gates;
    gates.active = g_mouseLookEnabled;
    gates.following = g_orbit.initialized && g_orbit.state == OrbitState::Following;
    gates.focused = input::Window() && GetForegroundWindow() == input::Window();
    gates.overlay = input::OverlayOpen() || input::KeyDown(VK_F9);
    gates.externalCamera = g_externalCameraOwned.load();
    gates.leasedKeys = input::NativeMovementKeysOwned();
    gates.movie = !counter || counter < g_movieUntilCounter.load();
    gates.restoreFailed = g_cameraRestoreFailed || g_cameraBackup.pending || g_lensBackup.pending;
    gates.control = input::KeyDown(VK_CONTROL);
    gates.timeStep = game::TimeStep();
    g_headingPolicy.Begin(game::TickCount(), counter ? double(counter) / g_counterFrequency : 0, gates);
}

void EndNativeMovementTick() {
    if (g_inventoryProfile.active) g_inventoryProfile.tickQueries+=g_inventoryTickReads.Queries();
    g_inventoryNativeTick = false; g_headingPolicy.End();
}

void OnGamePresentResult(bool success)
{
    if (g_displayResetRecovery.Pending()) {
        const int64_t counter = CameraCounter();
        g_displayResetGate.Present(counter ? double(counter) / g_counterFrequency : 0, success);
    }
    if (g_startupDisplayAttempted) return;
    const int64_t counter = CameraCounter();
    g_startupDisplayGate.Present(counter ? double(counter) / g_counterFrequency : 0, success);
}

void BeforeGamePresent()
{
    if (!g_active.load(std::memory_order_relaxed)) return;
    const int hz = g_targetHz.load(std::memory_order_relaxed);
    LARGE_INTEGER frequency{}, now{};
    if (hz < 30 || !QueryPerformanceFrequency(&frequency) || !frequency.QuadPart || !QueryPerformanceCounter(&now)) return;
    const int64_t period = std::max<int64_t>(1, frequency.QuadPart / hz);
    if (!g_nextPresentCounter || now.QuadPart > g_nextPresentCounter + period * 4)
        g_nextPresentCounter = now.QuadPart;
    while (now.QuadPart < g_nextPresentCounter) {
        const double milliseconds = 1000.0 * double(g_nextPresentCounter - now.QuadPart) / frequency.QuadPart;
        if (milliseconds > 2.0) Sleep((DWORD)(milliseconds - 1.0));
        else SwitchToThread();
        if (!QueryPerformanceCounter(&now)) break;
    }
    g_nextPresentCounter += period;
}

void Shutdown()
{
    g_displayResetRecovery.Cancel();
    DeactivateRenderSplit(); UninstallHooks();
    RestoreRenderCamera(); ReleaseOrbitCapture();
    g_mouseLookEnabled = false;
    g_cameraHooksReady.store(false);
    void* targets[] = { (void*)game::A(kViewRender), (void*)game::A(kAppRender),
        (void*)game::A(kBuiltinFieldGetter) };
    for (void* target : targets) { MH_DisableHook(target); MH_RemoveHook(target); }
    g_viewRenderOrig = nullptr; g_appRenderOrig = nullptr; g_builtinFieldOrig = nullptr;
}
bool HooksReady() { return g_hooksReady.load(); }
bool Active() { return g_active.load(); }

bool AdjustHudProjection(uintptr_t returnAddress, const D3DMATRIX* input, D3DMATRIX& output)
{
    if (!g_active.load(std::memory_order_relaxed) || !input) return false;
    AuthoredHudSpace space=AuthoredHudSpace::Flat;
    if (returnAddress==game::A(0x44EAA0)) space=AuthoredHudSpace::ModelOverlay;
    else if (returnAddress != game::A(0x44726B) && returnAddress != game::A(0x4474D6) &&
        returnAddress != game::A(0x44884A)) return false;
    __try { memcpy(&output, input, sizeof(output)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    // Grade models and belt/pip sprites now use the same authored-space scale.
    // Only the submission copy changes: native animation/position/depth remain.
    return ContainAuthoredHudProjection(&output._11,space,g_aspect.load(std::memory_order_relaxed));
}

bool RequestDisplayPlan(const DisplayPlan& plan, int monitorHz, std::string& error)
{
    g_displayResetRecovery.Cancel();
    g_displayResetGate.Reset();
    // A manual request supersedes a queued startup recovery.
    if (!g_startupApplyInFlight) g_startupDisplayRecovery.Cancel();
    error.clear();
    const int targetHz = TargetRenderHz(plan, monitorHz);
    if (!ValidPlan(plan) || !targetHz) { error = "invalid target or unknown monitor refresh"; return false; }
    {
        std::lock_guard<std::mutex> lock(g_applyMutex);
        if (g_applyStatus.state == ApplyState::Applying || g_applyStatus.state == ApplyState::RollingBack) {
            error = "another display transaction is pending"; return false;
        }
    }
    IDirect3DDevice9* device = (IDirect3DDevice9*)game::Device();
    HWND window = (HWND)game::GameHwnd();
    DWORD process = 0;
    if (!device || !window || GetWindowThreadProcessId(window, &process) != GetCurrentThreadId()) {
        error = "display request must run on the game window thread"; return false;
    }
    D3DPRESENT_PARAMETERS previous{};
    RECT previousRect{};
    if (!CapturePresentParameters(device, previous) || !GetWindowRect(window, &previousRect)) {
        error = "could not capture rollback state"; return false;
    }
    if (plan.windowed) {
        if (targetHz != monitorHz) { error = "windowed D3D9 follows monitor refresh; explicit differing Hz refused"; return false; }
        if (!InstallHooks()) { error = "compatibility hooks could not be installed"; return false; }
    } else if (!ExactFullscreenModeSupported(device, plan.width, plan.height, targetHz)) {
        error = "exact fullscreen mode is not reported by this adapter"; return false;
    } else if (!InstallHooks()) { error = "compatibility hooks could not be installed"; return false; }
    DeactivateRenderSplit();
    {
        std::lock_guard<std::mutex> lock(g_applyMutex);
        g_previousParameters = previous; g_previousWindowRect = previousRect;
        g_havePreviousParameters = g_havePreviousWindowRect = true;
        g_applyStatus = {};
        g_applyStatus.state = ApplyState::Applying;
        g_applyStatus.requested = plan;
        g_applyStatus.targetHz = targetHz;
        g_applyStatus.detail = "waiting for the game Reset lifecycle";
    }
    ETH_INFO("EthhhPatch display request: %dx%d %s @ %d Hz; previous %ux%u %s @ %u Hz",
        plan.width, plan.height, plan.windowed ? "windowed" : "fullscreen", targetHz,
        previous.BackBufferWidth, previous.BackBufferHeight, previous.Windowed ? "windowed" : "fullscreen",
        previous.FullScreen_RefreshRateInHz);
    if (!ConfigureDisplay(plan.windowed, plan.width, plan.height, targetHz)) {
        error = "engine mode request/PostMessage failed";
        StartRollback(error.c_str());
        return false;
    }
    // ConfigureDisplay first sets the wrapper's mode flag. SetWindowPos sends
    // WM_SIZE synchronously: resizing while that flag still says FULLSCREEN
    // makes the engine's WM_SIZE helper copy the old fullscreen dimensions back.
    if (plan.windowed && !ResizeClient(window, plan.width, plan.height)) {
        RECT actual{};
        GetClientRect(window, &actual);
        ETH_ERROR("EthhhPatch window size mismatch: requested %dx%d, client %ldx%ld",
            plan.width, plan.height, actual.right - actual.left, actual.bottom - actual.top);
        error = "could not establish exact window client size after setting windowed mode";
        StartRollback(error.c_str());
        return false;
    }
    return true;
}

bool RequestRestore(std::string& error)
{
    g_displayResetRecovery.Cancel();
    g_displayResetGate.Reset();
    g_startupDisplayRecovery.Cancel();
    error.clear();
    HWND window = (HWND)game::GameHwnd();
    DWORD process = 0;
    if (!window || GetWindowThreadProcessId(window, &process) != GetCurrentThreadId()) {
        error = "restore must run on the game window thread"; return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_applyMutex);
        if (g_applyStatus.state != ApplyState::Active) { error = "no active EthhhPatch display transaction"; return false; }
    }
    if (!DisableStartupPlan()) { error = "could not disable saved startup display"; return false; }
    if (!StartRollback("user requested restore")) { error = "rollback request failed"; return false; }
    return true;
}

bool ForgetStartupDisplay()
{
    g_startupDisplayRecovery.Cancel();
    g_startupDisplayAttempted = true;
    return DisableStartupPlan();
}

void OnResetResult(const D3DPRESENT_PARAMETERS* parameters, HRESULT result)
{
    g_startupDisplayGate.Reset();
    g_displayResetGate.Reset();
    D3DPRESENT_PARAMETERS observed{};
    // Verify the swap chain that actually exists, not only the pre-Reset request.
    if (SUCCEEDED(result)) {
        parameters = CapturePresentParameters((IDirect3DDevice9*)game::Device(), observed) ? &observed : nullptr;
    }
    ApplyStatus status;
    {
        std::lock_guard<std::mutex> lock(g_applyMutex);
        status = g_applyStatus;
    }
    if (parameters) {
        ETH_INFO("EthhhPatch Reset: state %d, hr %08lX, %ux%u %s @ %u Hz, interval %u",
            int(status.state), (unsigned long)result, parameters->BackBufferWidth, parameters->BackBufferHeight,
            parameters->Windowed ? "windowed" : "fullscreen", parameters->FullScreen_RefreshRateInHz,
            parameters->PresentationInterval);
    } else {
        ETH_WARN("EthhhPatch Reset: state %d, hr %08lX, resulting parameters unavailable",
            int(status.state), (unsigned long)result);
    }
    if (status.state == ApplyState::Applying) {
        if (SUCCEEDED(result) && parameters && AppliedModeMatches(status.requested, status.targetHz,
            parameters->BackBufferWidth, parameters->BackBufferHeight, parameters->Windowed != FALSE,
            parameters->FullScreen_RefreshRateInHz) &&
            ActivateRenderSplit((float)Aspect(status.requested.width, status.requested.height),
                parameters->BackBufferWidth, parameters->BackBufferHeight, status.targetHz)) {
            const bool remember = RememberDisplay();
            const bool remembered = remember && SaveStartupPlan(status.requested);
            if (g_startupApplyInFlight && !g_startupApplyIsRecovery) {
                const int64_t counter = CameraCounter();
                g_startupDisplayRecovery.Arm(counter ? double(counter) / g_counterFrequency : 0);
            }
            g_startupApplyInFlight = false;
            std::lock_guard<std::mutex> lock(g_applyMutex);
            g_applyStatus.state = ApplyState::Active;
            g_applyStatus.detail = "display Reset matched; render split/culling/HUD timing active";
            g_applyStatus.detail += !remember ? "; this session only" :
                remembered ? "; remembered for next launch" : "; WARNING: could not save startup display";
            ETH_INFO("EthhhPatch display verified: %dx%d %s; aspect %.6f; startup %s",
                status.requested.width, status.requested.height, status.requested.windowed ? "windowed" : "fullscreen",
                g_aspect.load(), !remember ? "session only" : remembered ? "remembered" : "save failed");
            return;
        }
        StartRollback(FAILED(result) ? "display Reset failed" : "Reset result did not exactly match the request");
    } else if (status.state == ApplyState::RollingBack) {
        bool matches = SUCCEEDED(result) && parameters;
        if (matches) {
            std::lock_guard<std::mutex> lock(g_applyMutex);
            matches = g_havePreviousParameters &&
                parameters->BackBufferWidth == g_previousParameters.BackBufferWidth &&
                parameters->BackBufferHeight == g_previousParameters.BackBufferHeight &&
                parameters->Windowed == g_previousParameters.Windowed &&
                parameters->FullScreen_RefreshRateInHz == g_previousParameters.FullScreen_RefreshRateInHz;
        }
        {
            std::lock_guard<std::mutex> lock(g_applyMutex);
            g_applyStatus.state = matches ? ApplyState::RolledBack : ApplyState::Failed;
            g_applyStatus.detail += matches ? "; previous display restored exactly" : "; rollback Reset failed or mismatched";
        }
        UninstallHooks();
    } else if (status.state == ApplyState::Active) {
        const bool matches = SUCCEEDED(result) && parameters &&
            AppliedModeMatches(status.requested, status.targetHz, parameters->BackBufferWidth,
            parameters->BackBufferHeight, parameters->Windowed != FALSE,
            parameters->FullScreen_RefreshRateInHz);
        HWND window = (HWND)game::GameHwnd();
        RECT outer{}, client{};
        const bool haveFrame = window && GetWindowRect(window, &outer) && GetClientRect(window, &client);
        const bool frameMismatch = parameters && haveFrame && WindowFrameResetMismatch(status.requested,
            parameters->BackBufferWidth, parameters->BackBufferHeight, parameters->Windowed != FALSE,
            parameters->FullScreen_RefreshRateInHz,
            (outer.right - outer.left) - (client.right - client.left),
            (outer.bottom - outer.top) - (client.bottom - client.top));
        const auto action = g_displayResetRecovery.Observe(SUCCEEDED(result), parameters != nullptr,
            matches, window && IsIconic(window),
            FAILED(result) && parameters && parameters->Windowed &&
                !parameters->BackBufferWidth && !parameters->BackBufferHeight,
            frameMismatch);
        if (action == DisplayResetRecovery::Action::Suspend ||
            action == DisplayResetRecovery::Action::Recover) {
            if (g_active.load())
                ETH_WARN("EthhhPatch Reset interrupted verified display; suspending compatibility without forgetting plan");
            DeactivateRenderSplit();
            std::lock_guard<std::mutex> lock(g_applyMutex);
            g_applyStatus.detail = action == DisplayResetRecovery::Action::Recover ?
                "transient window-frame Reset; waiting to recover the exact verified mode" :
                "device Reset interrupted; compatibility suspended until exact mode verification";
            return;
        }
        const bool resuming = !g_active.load();
        if (action == DisplayResetRecovery::Action::Reject ||
            (!g_active.load() && !ActivateRenderSplit(
                (float)Aspect(status.requested.width, status.requested.height),
                parameters->BackBufferWidth, parameters->BackBufferHeight, status.targetHz))) {
            ETH_WARN("EthhhPatch active display replaced by a later game Reset; disabling compatibility policies");
            const int64_t counter = CameraCounter();
            if (g_startupDisplayRecovery.Queue(counter ? double(counter) / g_counterFrequency : 0)) {
                g_startupRecoveryPlan = status.requested;
                g_startupDisplayAttempted = false;
                ETH_WARN("EthhhPatch late boot mode override: queued one recovery after game presents settle");
            }
            if (!DisableStartupPlan())
                ETH_ERROR("EthhhPatch could not forget startup display after mode mismatch");
            DeactivateRenderSplit();
            {
                std::lock_guard<std::mutex> lock(g_applyMutex);
                g_applyStatus.state = ApplyState::Failed;
                g_applyStatus.detail = "a later game/device Reset changed the verified mode; compatibility patch disabled";
            }
            UninstallHooks();
        } else {
            if (resuming)
                ETH_INFO("EthhhPatch exact display restored; compatibility resumed with original Restore baseline");
            g_targetWidth.store(parameters->BackBufferWidth);
            g_targetHeight.store(parameters->BackBufferHeight);
            g_nextPresentCounter = 0;
            std::lock_guard<std::mutex> lock(g_applyMutex);
            g_applyStatus.detail = "display Reset matched; render split/culling/HUD timing active";
        }
    }
}

ApplyStatus ReadApplyStatus()
{
    std::lock_guard<std::mutex> lock(g_applyMutex);
    return g_applyStatus;
}

void SetExternalCameraOwned(bool owned)
{
    g_externalCameraOwned.store(owned);
    if (owned) { RestoreRenderCamera(); g_orbit.Suspend(OrbitState::External); g_mouseCaptureOwned = false; }
}

void NotifyMoviePresent()
{
    g_startupDisplayGate.Reset();
    g_displayResetGate.Reset();
    const int64_t counter = CameraCounter();
    if (counter) g_movieUntilCounter.store(counter + g_counterFrequency / 2);
    if (g_cameraHooksReady.load() && !g_externalCameraOwned.load()) {
        RestoreRenderCamera(); g_orbit.Suspend(OrbitState::Movie); ReleaseOrbitCapture();
    }
}

void SetOrbitEnabled(bool enabled)
{
    if (enabled && !InstallHooks(true)) {
        ETH_ERROR("EthhhPatch orbit refused: camera hooks unavailable");
        enabled = false;
    }
    g_mouseLookEnabled = enabled;
    RestoreRenderCamera(); ReleaseOrbitCapture();
    g_orbit.Reset(); g_lastCameraTraceCounter = 0;
    if (enabled) LoadOrbitTuning();
    PublishOrbitTelemetry();
}

bool ExternalCameraOwned() { return g_externalCameraOwned.load(); }

void InitializeCamera()
{
    LoadOrbitTuning();
    SetOrbitEnabled(Config::Get().GetBool("ethhhpatch", "mouse_look", true));
}

NormalFovTuning ReadNormalFovTuning() { return {g_normalFovEnabled.load(), g_normalFovDegrees.load()}; }
void SetNormalFovTuning(bool enabled, float verticalDegrees)
{
    RestoreRenderCamera();
    g_normalFovDegrees.store(NormalFovDegrees(verticalDegrees));
    g_normalFovEnabled.store(enabled);
}

void ResumeOrbit() { g_orbit.RequestResume(); PublishOrbitTelemetry(); }
OrbitState ReadOrbitState() { return g_orbit.state; }

OrbitTuning ReadOrbitTuning()
{
    return { g_orbitSensitivity.load(std::memory_order_relaxed),
        g_orbitInvertY.load(std::memory_order_relaxed) };
}

void SetOrbitTuning(float sensitivity, bool invertY)
{
    if (!std::isfinite(sensitivity)) sensitivity = 1.0f;
    g_orbitSensitivity.store(std::clamp(sensitivity, 0.05f, 5.0f), std::memory_order_relaxed);
    g_orbitInvertY.store(invertY, std::memory_order_relaxed);
}

OrbitTelemetry ReadOrbitTelemetry()
{
    std::lock_guard<std::mutex> lock(g_orbitTelemetryMutex);
    return g_orbitTelemetry;
}

void SetOrbitDistance(float distance)
{
    if (g_orbit.state == OrbitState::Following && g_orbit.initialized) g_orbit.SetDistance(distance);
    PublishOrbitTelemetry();
}

void ZoomOrbit(float notches)
{
    if (g_orbit.state == OrbitState::Following && g_orbit.initialized) g_orbit.Zoom(notches);
    PublishOrbitTelemetry();
}

EntityTelemetry ReadEntityTelemetry()
{
    std::lock_guard<std::mutex> lock(g_entityTelemetryMutex);
    EntityTelemetry result = g_entityTelemetry;
    result.enabled = g_entityInterpolationEnabled.load(std::memory_order_relaxed);
    return result;
}

void SetEntityInterpolationEnabled(bool enabled)
{
    g_entityInterpolationEnabled.store(enabled);
    g_entityPoses.Reset();
    std::lock_guard<std::mutex> lock(g_entityTelemetryMutex);
    g_entityTelemetry = {};
}

} // namespace eth::patch::runtime
