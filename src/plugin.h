#pragma once

#include <atomic>
#include <memory>

#include "config.h"
#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/tracking/head_tracking_session.h"

namespace headtracking {

class CameraHook;
class HotkeyHandler;

class Plugin {
public:
    Plugin();
    ~Plugin();

    // Loads config, starts the receiver and the hotkey poller, selects a build
    // profile and installs the camera hook. There is no matching Shutdown: the
    // module is pinned at load (see dllmain.cpp), so nothing is ever torn down
    // and the OS reclaims it at process exit.
    void Initialize();

    bool IsEnabled() const { return m_enabled.load(); }
    void ToggleEnabled() { m_enabled.store(!m_enabled.load()); }

    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw.load(); }
    void ToggleYawMode();

    void CycleTrackingMode();
    const char* TrackingModeName() const;

    // What head tracking does while the sights are up. Read once per frame by
    // the render thread, written by the hotkey thread - an atomic rather than a
    // deferred request because nothing downstream of it is stateful, so the very
    // next frame recomputes its whole verdict from the new value. That is what
    // makes a mode cycled mid-aim take effect on THAT aim rather than the next
    // one: there is no cached verdict for it to ride.
    AdsMode GetAdsMode() const { return m_adsMode.load(std::memory_order_acquire); }
    void CycleAdsMode();


    // Pulls the latest UDP packet, runs it through the session and updates the
    // cached rotation (radians) + position offset (Source world units). Called
    // once per render frame from the camera hook. Returns false when tracking
    // is disabled or no fresh data has arrived.
    bool Update();

    // Thread-safe read of the most recent processed rotation, in radians.
    // Returns false when tracking is disabled or no fresh sample exists.
    bool GetRotationRadians(float& yaw, float& pitch, float& roll) const;

    // Camera-local head displacement in Source world units, in view axes
    // (x=right, y=up, z=forward). Returns false when positional tracking is
    // off or no fresh sample exists.
    bool GetPositionOffset(float& x, float& y, float& z) const;

    const Config& GetConfig() const { return m_config; }

private:
    void Invalidate();
    void ConsumeHotkeyRequests();
    bool HoldThroughLoss(float deltaTime);

    // Exponential decay applied to the held pose once the tracker goes quiet.
    // Matches Core.Unity's TrackingLossHandler, so the ease-out feels the same
    // as it does on the Unity mods. The same constant runs the blend back in
    // when the tracker returns, so losing and regaining a connection is
    // symmetric.
    static constexpr float kLossFadeSpeed = 2.0f;
    // Seconds after which the resume blend is treated as finished. exp(-2 * 5)
    // is under 1e-4, so this is "indistinguishable from 1" rather than a cutoff
    // the eye can see; it also seeds the blend as already-complete for a session
    // that never lost its tracker.
    static constexpr float kResumeBlendComplete = 5.0f;

    Config m_config;
    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_worldSpaceYaw{true};
    std::atomic<AdsMode> m_adsMode{kDefaultAdsMode};

    // Hotkeys run on their own poller thread, and CycleTrackingMode() reaches
    // deep into the session: it resets position smoothing. Doing that while the
    // render thread is mid-Process() is a torn read - one frame of camera flick
    // on a keypress, impossible to reproduce on demand. So the hotkey thread
    // only raises a flag; Update() consumes it on the render thread, between
    // frames.
    std::atomic<bool> m_modeCycleRequested{false};

    // Source world units per metre of head movement. The Invert* preferences
    // are NOT folded in here - they go to the position processor, which applies
    // them before its asymmetric Z clamp (see ApplyPositionConfig).
    float m_worldScale = 1.0f;

    using Session = cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver>;
    // The session picks between LocalSmoothing and RemoteSmoothing from the
    // receiver's source-address check. That wiring is compile-time detected, so
    // a receiver without IsRemoteConnection() would silently pin every session
    // to the local value instead of failing to build.
    static_assert(Session::kHasRemoteConnection,
                  "receiver must expose IsRemoteConnection() for per-connection smoothing");

    cameraunlock::UdpReceiver m_receiver;
    Session m_session{m_receiver};
    cameraunlock::time::FrameClock m_frameClock;

    std::unique_ptr<CameraHook>    m_cameraHook;
    std::unique_ptr<HotkeyHandler> m_hotkeys;

    std::atomic<float> m_cachedYaw{0.0f};
    std::atomic<float> m_cachedPitch{0.0f};
    std::atomic<float> m_cachedRoll{0.0f};
    std::atomic<bool>  m_cachedValid{false};

    std::atomic<float> m_cachedPosX{0.0f};
    std::atomic<float> m_cachedPosY{0.0f};
    std::atomic<float> m_cachedPosZ{0.0f};
    std::atomic<bool>  m_cachedPosValid{false};

    // Pose captured when the tracker went quiet, and how long ago that was.
    // Only touched from the render thread inside Update().
    //
    // `m_holding` is a separate flag rather than `m_lossSeconds == 0.0f`:
    // FrameClock::Tick() legitimately returns exactly 0 (two samples inside one
    // timer tick), so a float sentinel would recapture the hold pose from the
    // already-faded output every frame and the fade would never advance - the
    // view would freeze instead of easing out.
    bool m_holding = false;
    float m_lossSeconds = 0.0f;
    // Seconds of continuous data since the fade-out completed. Starts complete
    // so a normal session never blends in.
    float m_resumeSeconds = kResumeBlendComplete;
    float m_holdYaw = 0.0f, m_holdPitch = 0.0f, m_holdRoll = 0.0f;
    float m_holdPosX = 0.0f, m_holdPosY = 0.0f, m_holdPosZ = 0.0f;
};

Plugin& GetPlugin();

}  // namespace headtracking
