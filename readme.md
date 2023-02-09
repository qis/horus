# Horus
OBS plugin for Overwatch enemy detection using OpenCV and CUDA in under 3 milliseconds.

At 120 FPS, the total delay between mouse movement recognized by the system (DirectInput)
and new enemy positions recognized by the plugin ranges between 0.6 and 13.6 ms.

## Why
This is a hobby. I write cheats for games once every few years.

Usually, there are two types of cheats.

1. Internal - when the game process is hooked and executes your code.<br/>
   An example for CS:GO can be found [here](https://github.com/qis/jeeves).

2. External - when the game memory is read by an external process.<br/>
   An example for Overwatch that uses a system driver can be found
   [here](https://github.com/qis/overwatch).

There are ways to detect both approaches, even if anti-cheat software fails to do so.

This project falls into a separate category that uses image processing to detect enemies on
the screen and external hardware that simulates a mouse for input.

Detecting this solution without limiting the use of legitimate software like OBS is much
harder and nobody has done this yet.

## How
Here is a short summary of what this plugin does.

* Registers itself as a filter plygin in OBS and receives captured frames from it.

![Scan](res/images/scan.png "Scan")

* Converts each frame to the HSV colorspace and searches for colors that match enemy outlines.
* Copies detected enemy outline colors as a grayscale image to system memory.
* Uses CUDA to mask and remove pixels that are likely player names, special effects, etc.

![Mask](res/images/mask.png "Mask")

* Uses OpenCV to detect the remaining outlines as contours.
* Groups contours that likely belong to the same target.
* Creates convex hulls as target representations.

![Target](res/images/target.png "Target")

* Simulates mouse clicks when a target is under the cursor (with prediction).

## Next
This is a simple demo and many things can be improved. The following features will be added
in the future when I have time.

* Create concave hulls as target representations.
* Track targets to predict their movement in 3D space.
* Train a neural network to categorize the targets.

<details>
<summary>Installation</summary>

This repository exists for demonstration purposes only. Instructions are for the author's convenience.

1. Install [OBS-Studio][obs] to `C:\OBS`.
2. Extract [OBS-Studio][obs] source code to `C:\OBS\src`.
3. Install [Python 3][py3] to `C:\Python`.
4. Install [CUDA Toolkit][cuda] to `C:\CUDA`.
5. Clone this repository to `C:\OBS\horus`.

```cmd
git clone git@github.com:qis/horus C:/OBS/horus
cd C:\OBS\horus
git submodule update --init --depth 1
```

6. Install dependencies using [Conan][conan].

<!--
* Set the system environment variable `CONAN_USER_HOME_SHORT` to `None`.
* Upgrade pip with `python -m pip install --upgrade pip`.
* Upgrade conan with `pip install conan --upgrade`.
-->

```cmd
cd C:\OBS\horus
conan install . -if third_party -pr conan.profile
```

7. Build [OpenCV][opencv] in `x64 Native Tools Command Prompt for VS 2022`.

```cmd
cd C:\OBS\horus\third_party\opencv
cmake -B build --preset default
cmake --build build --target install
copy release\x64\vc17\bin\opencv_world470.dll C:\OBS\obs-plugins\64bit\
```

9. Configure [OBS-Studio][obs] and Overwatch according to [settings.md](settings.md).

</details>

[obs]: https://github.com/obsproject/obs-studio/releases/tag/27.2.4
[py3]: https://www.python.org/downloads/windows/
[cuda]: https://developer.nvidia.com/cuda-downloads
[conan]: https://conan.io/center/
[opencv]: https://github.com/opencv/opencv/releases
