# Experimental game bridge for Steam Frame

`xr_bridge=1` sends OpenXR work from the 32 bit mod to an x64 Windows host.
`xr_bridge=0` keeps the local OpenXR path and is the default. Change this
setting with the game closed and restart. A live switch is not implemented
because the session and all shared resources belong to that backend.

The game runs on Steam Frame with Proton 11 ARM64 and SteamVR/OpenXR 2.18.2.
Stereo, controller input, shared GPU textures and session shutdown work.
The host submits hint cards, subtitles and cut scene layers. Full control
coverage, haptic feel, device loss and long sessions still need tests.
Performance can be below the headset refresh rate. This is experimental
support, not a promise that each Proton or SteamVR version works.

When the headset wants no image, the scene still closes its frame. The flat
image path waits until no scene frame is open before it starts another one.
Headset removal and return still need a runtime test with this guard.

## Install the experimental artifact

Use your own game with patch v1.42 and the normal mod installer. Install the
mod in the same Proton prefix as the game. Download `steam-frame-<build>.zip`
from the same release as the installer. For a pull request, open its Build
run and download the installer and `steam-frame-addon` artifacts. Extract
the add-on artifact once to get its files. Match the build IDs.
No Windows development machine is needed to download these builds.

The add-on needs a working Steam Frame game launcher with Proton 11 ARM64,
SteamVR and its OpenXR runtime. It does not install Proton, SteamVR or
FramePort. The tested launcher uses FramePort and its OpenXR compatibility
layer. Keep its prefix, display session and API layer settings. Other
launcher setups need testing.

With the game closed, copy the whole `bwvr-host` folder beside
`runblack.exe`. The `ddraw.dll` of the installer has the client of the
host, so the add-on has no `ddraw.dll`. Keep the other files of the mod,
including its 32 bit `openxr_loader.dll` and the dxwrapper folder. Do not
copy the x64 loader out of `bwvr-host`.

The folder layout is:

```text
game/
  runblack.exe
  ddraw.dll                 the mod, from the installer
  openxr_loader.dll         existing 32 bit loader
  bwvr.ini
  dxwrapper/                existing backend
  bwvr-host/
    bwvr-xr-host.exe        64 bit OpenXR process
    openxr_loader.dll       64 bit loader
    LICENSE
```

In the existing `[bwvr]` section of `bwvr.ini`, set:

```ini
xr_bridge=1
```

Do not replace the whole ini. Use the FramePort launcher script with:

```bash
export WINEDLLOVERRIDES="ddraw=n,b;openxr_loader=n"
export PROTON_LOG=1
```

Start through your existing launcher and select Play in VR. The mod starts
the host itself in the same Proton prefix. Do not run the host by hand.
The bridge does not start the game, install a runtime or change registry
settings. The launcher and the game retain their existing behavior.

Return `bwvr.log`, `bwvr-host/bwvr-host.log` and the Proton log. First check
startup, the logos and menus. Report whether both eyes show an image and
whether controller pointing and buttons work. Then check the scene, head
motion, panels, subtitles, hint cards, the ring menu and haptics. Do not
use an existing saved game as a development test. Automated game tests
must use `bwvr/tools/desk/desk_run.ps1` and its profile safeguards.

To return to the local runtime, set `xr_bridge=0` with the game closed.
The add-on does not change saves or profiles. The uninstaller of the mod
also removes the `bwvr-host` folder.

## Transport and limitations

The host owns the OpenXR instance, session, swapchains, input and layers.
The mod owns a local D3D11 device on the same adapter. Its existing GPU
bridge opens the D3D9Ex textures on that device. The host opens their legacy
shared handles and waits for its GPU copy before acknowledging each texture.
The mod keeps its existing waits for the producer GPU.

The named mapping has a version, size and request sequence. Events pass
ownership between processes. Full eye and hand state, display time, input
focus and recenter events return at BeginFrame. The game sends render poses,
projection options, panels, cards, cut scene windows and haptics. CPU image
fallback and card payloads have a 32 MiB limit per request. Card pixels
remain alive in the host until EndFrame.

Submission is synchronous in bridge mode, including when `frame_send=2`.
The native threaded submission remains available with `xr_bridge=0`.
There is no live backend switch and no automatic fallback to a local runtime
when the host fails. Failed or timed out requests stop VR submission. The
initialization timeout is 30 seconds, other requests have 5 seconds and
host GPU copies have 2 seconds. Shutdown signals the host. If it does not
exit in 3 seconds, the mod ends only the host process that it started.
The host also watches the parent process between requests. When process
jobs are available, closing the parent also ends its host. If Proton cannot
create that job, the mod logs the limitation.

CI builds the host and runs 120 synthetic x86 to x64 host exchanges without
OpenXR. This checks payloads, pose layout, 64 bit values, command
sequencing and rejection of an incompatible version. It does not test
rendering, layers, input behavior, game compatibility, device loss or
headset comfort.

## Graphics settings on Frame

Keep the render size near the runtime recommendation to limit GPU cost.
The launcher can produce a wider image than the selected square size.
Read `game renders at` in `bwvr.log` for the actual size.

To disable MSAA, change both existing entries in `dxwrapper/dxwrapper.ini`
with the game closed:

```ini
[Dd7to9]
DdrawAllowMultiSampling=0

[d3d9]
AntiAliasing=0
```

Keep the other settings. The next log must say `multisample type 0`.
`AntiAliasing=0` alone does not disable the Dd7to9 multisampling path.
These are optional player settings. The add-on does not overwrite them.

## Build and checks

The normal Build workflow calls `Steam Frame build` for pull
requests and main. The release workflow waits for those checks and attaches
the add-on ZIP beside the installer. Actions artifacts contain the files
directly. Only the release job creates a ZIP for the release asset. The standalone workflow also accepts
manual runs. It builds the x64 loader from OpenXR-SDK release-1.1.43 and
includes its license. The host and the installer come from the same commit.
The ZIP contains no game files, player ini, probe executables or run logs.

## Timing reports

Bridge mode writes `bridge time` summaries every 450 End calls in both
logs, and a partial summary at shutdown. Keep both logs from the same run.
Times are CPU elapsed time, not GPU timestamps. Request totals include the
copy API and GPU waits. Do not add overlapping categories. Use steady scene
windows to compare runs. The first window can include startup menus.

## Experimental Vulkan renderer on Steam Frame

This path needs `xr_bridge=1`, `xr_bridge_vulkan=1` and `backend=vulkan`
in the existing `[bwvr]` section. `xr_bridge_vulkan=0` is the default.
Select `backend=dxwrapper` and `xr_bridge_vulkan=0` to return to the tested
D3D9 bridge. Change these settings with the game closed. There is no live
switch because the renderer owns its device and images.

The game renders on its own Vulkan 1.3 device. The x64 host keeps its D3D11
OpenXR session. A local D3D11 texture exports a legacy shared handle that
Vulkan imports with `VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT`.
Vulkan blits each eye or panel into the relay, releases external ownership
and waits for its fence. The host copies the relay and waits before it
acknowledges the request. The game keeps the relay alive until shutdown.
Image handles and device pointers never cross the process boundary. The
relay converts RGBA to BGRA on the GPU. It has no CPU image readback.

The Vulkan device must expose `VK_KHR_external_memory_win32` and the same
adapter LUID as the host. Missing support or a failed import stops this
path with a log. It does not silently select another renderer. The native
renderer can draw a monitor image if VR startup fails.

The Vulkan KMT import and game rendering work on Frame. Full panel and
control coverage, device loss and long sessions need more tests. Keep foveation and
foveated resolution off for the first comparison:

```ini
[bwvr]
xr_bridge=1
xr_bridge_vulkan=1
backend=vulkan
foveation=0
fovea_res=0
```

Use the installer or add-on built from this integration branch, with both
its DLL and host. Keep the same render size as the D3D9 run. The existing
Proton launcher and DLL overrides stay applicable. Do not use a player save
as a development test. Collect both logs. `vk frame` reports adapter and
relay setup, and `bridge time` reports `Vulkan relay` elapsed CPU time,
including its GPU fence wait. No performance gain is established yet.
Eye gaze is not transported by the bridge. Foveation tests must use the
fixed center until that transport exists. The Vulkan startup runs on the
frame thread because the staged startup worker configures D3D11 only.

### A/B test of the Vulkan copies

`xr_bridge_vk_batch=0` is the default: each eye copy has its own Vulkan
submission and fence wait. With 1, both copies use one command buffer and
one fence wait. Source layouts are restored per layer. Both destination
images are released to the external owner before the wait ends. Host
requests and host waits stay separate with `xr_bridge_host_batch=0`. Single images and panels keep the
single-copy path. This setting has no effect without the Vulkan Frame relay.

With `test_keys=1`, Ctrl+Shift+F8 switches the setting while the game has
focus, at the frame boundary. The log records the value. Keep the same
view and compare 30 to 60 seconds per mode, then switch back. Do not compare
the first timing window after a switch: it can contain both modes.
Compare `Vulkan relay` in ms/End, not ms/call. A stereo batch is one call
instead of two. The host batch path still needs a headset run before adoption.

### A/B test of the host copies

Keep `xr_bridge_vk_batch=1`. Set `xr_bridge_host_batch=0` for separate host
requests and waits (the default), or 1 for one request with both handles.
Change the ini with the game closed. The host queues both eye copies and
waits once before it acknowledges the request. The producer must keep both
textures unchanged until that reply. Panels and single images use the
separate path. With `xr_bridge_vk_batch=0`, this option has no effect.

Use the same view for both runs. Compare `eye pair request` and `eye pair
GPU wait` in ms/End against `eye requests` and the sum of `left GPU wait`
and `right GPU wait`. These are overlapping CPU elapsed times, not GPU
timestamps. Do not add the request time to its GPU wait time.

This path uses protocol 2. Replace both the game DLL and the x64 host from
the same build, also for the baseline run. A protocol 1 host is incompatible.

### Fixed-center foveation test

The Vulkan renderer applies `VK_KHR_fragment_shading_rate` before the relay
copies the eye images. It does not need a foveation extension in the x64
OpenXR host. The GPU must support a shading rate attachment. The Vulkan
startup log reports that capability. Eye gaze is not transported, so use
`foveation_gaze=0` for this test.

Keep the same resolution and both bridge batch options enabled. With the
game closed, set these keys in the existing `[bwvr]` section:

```ini
xr_bridge_vk_batch=1
xr_bridge_host_batch=1
foveation=1
foveation_inner_deg=30
foveation_outer_deg=45
foveation_gaze=0
foveation_show=0
fovea_res=0
```

Level 1 keeps full shading within 30 degrees of the center, uses one shade
per 2 by 2 pixels out to 45 degrees, and 4 by 4 outside if supported. This
changes shading density, not the size of the relay images. It does not
remove the copies or their synchronization. `fovea_res` is a separate
technique and stays off for this comparison.

Compare two runs with `foveation=0` and `foveation=1`, changing only that key.
Use the same scene and view for about two minutes after loading. Save both
game and host logs for each mode. Exclude startup and partial timing windows.
Check the land, hands, text and edges while looking away from the center
with the eyes. The sharp region follows the head, not the eyes. A separate
short run with `foveation_show=1` marks the coarse regions. Turn those marks
off for timing. Steam Frame image quality and performance with foveation
still need a headset test.
