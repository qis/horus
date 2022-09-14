# Horus
1. Download [OBS-Studio-27.2.4-Full-x64.zip][obs] and install it as `C:\OBS`.
2. Download the source code and install it as `C:\OBS\src`.
3. Start and configure OBS.

```
Settings
+ General
  ☐ Automatically check for updates on startup
+ Output
  Output Mode: Advanced
  + Recording
    Container Format: mkv
    Encoder: NVIDIA NVENC H.264
    Rate Control: VBR
    Bitrate: 16000
    Max Bitrate: 48000
    Preset: Quality
    Profile: high
    ☐ Look-ahead
    ☑ Psycho Visual Tuniung
    Max B-frames: 2
+ Video
  Base (Canvas) Resolution: 1920x1080
  Output (Scaled) Resolution: 1920x1080
  Downscale Filter: Bilinear (Fastest, but blurry if scaling)
  Integer FPS Value: 75

Sources
+ Overwatch
  Mode: Capture specific window
  Window: [Overwatch.exe]: Overwatch
  Window Match Priority: Match title, otherwise find window of the same executable
  ☐ SLI/Crossfire Capture Mode (Slow)
  ☐ Allow Transparency
  ☐ Limit capture framerate
  ☑ Capture Cursor
  ☑ Use anti-cheat compatibility hook
  ☐ Capture third-party overlays (such as steam)
  Hook Rate: Normal (recommended)
```

4. Install [Python 3][py3] to `C:\Python`.
5. Install dependencies in this directory (`C:\OBS\horus`).

```cmd
pip install conan
conan install . -if third_party -pr conan.profile
```

6. Configure Overwatch.

```
Options
+ Video
  Display Mode: Fullscreen
  Resolution: 2560 X 1080 (75)(*)
  Field of View: 103
  Aspect Ratio: 16:9
  VSync: Off
  Tripple Buffering: Off
  Reduce Buffering: Off
  Display Performance Stats: On
  + Advanced Performance Stats
    Show Framerate: On
    Show Network Latency: On
    Show Network Interpolation Delay: On
    *: Off
  NVIDIA Reflex: Disabled
  Limit FPS: Custom
  Frame Rate Cap: 151
  Graphics Quality: Low
  + Advanced
    Render Scale: 100%
    Texture Quality: Low
    Texture Filtering: Low - 1X
    Local Fog Detail: Low
    Dynamic Reflections: Off
    Shadow Detail: Off
    Model Detail: Low
    Effects Detail: Low
    Lighting Quality: Low
    Antialias Quality: Low
    Refraction Quality: Off
    Screenshot Quality: 1X Resolution
    Local Reflections: Off
    Ambient Occlusion: Off
    Damage FX: Low
  Gamma Correction: 50%
  Contrast: 50%
  Brightness: 50%
  + Color Blind Options
    Filter: Off
    Enemy UI Color: Magenta
    Friendly UI Color: Blue (Friendly Default)
+ Controls
  + Reticle
    + Type: Circle
      Show Accuracy: Off
      Color: [Bright Green]
      Thickness: 1
      Center Gap: 30
      Opacity: 80
      Outline Opacity: 50
      Dot Size: 6
      Dot Opacity: 100
      Scale with Resolution: Off
  + Hero
    Allied Health Bars: Always
  + Movement
    Jump: SPACE | `horus M 3`
  + Weapos & Abilities
    Ability 1: LSHIFT | `horus D 3`
    Ability 2: E | `horus U 3`
    Quick Melee: C
    Next Weapon: EMPTY
    Previous Weapon: EMPTY
  + Communication
    Communication Menu: V
+ Controls (Ana)
  + Reticle
    + Type: Dot
  + Hero
    Relative Aim Sensitivity While Zoomed: 46.00
+ Controls (Ashe)
  + Reticle
    + Type: Dot
  + Hero
    Relative Aim Sensitivity While Zoomed: 61.00
+ Controls (Mercy)
  + Weapos & Abilities
    Equip Weapon 1: 1 | SCROLL WHEEL UP
    Equip Weapon 2: 2 | SCROLL WHEEL DOWN
+ Controls (Pharah)
  + Weapos & Abilities
    Ability 1: LSHIFT | EMPTY
    Ability 2: E | EMPTY
    Secondary Fire: EMPTY
+ Controls (Reaper)
  + Reticle
    + Type: Circle
      Center Gap: 100
+ Controls (Zenyatta)
  + Weapos & Abilities
    Ability 1: SCROLL WHEEL UP
    Ability 2: SCROLL WHEEL DOWN
+ Gameplay
  Waypoint Opacity: 30%
  Respawn Icon Opacity: 30%
  Ability Timer Ring Opacity: 30%
  Player Outline Strength: 100%

Hero Gallery
+ Ana > Skins: Horus
+ Ashe > Skins: Sunflower (Change to Safari and update res/heroes/ashe.png)
+ Pharah > Skins: Security Chief
+ Reaper > Skins: Blackwatch Reyes
```

The code to inject mouse buttons relies on [anubis](https://github.com/qis/anubis), which is a private project.

## TODO
- Get a high refreshrate monitor.
- Move mouse interpolation to plugin.
- Calculate when to shoot between frames.
- Instead of using the close kernel a second time, connect contours?
- Replace low level mouse hook with DirectInput.
- Blink and 180 turn as Tracer.
- Infinite flight as Pharah.

[obs]: https://github.com/obsproject/obs-studio/releases/tag/27.2.4
[py3]: https://www.python.org/downloads/windows/
