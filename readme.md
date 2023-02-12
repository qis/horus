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
**Here is a short summary of what this plugin does.**

1. Registers itself as a filter plygin in OBS and receives captured frames.

   ![Frame](res/images/1-frame.png "Frame")

2. Converts each frame to the HSV colorspace.

   ![HSV](res/images/2-hsv.png "HSV")

3. Searches for colors that match enemy outlines and copies the data to system memory.

   ![Scan](res/images/3-scan.png "Scan")

4. Uses CUDA to mask and remove pixels that are likely player names, special effects, etc.

   ![Mask](res/images/4-mask.png "Mask")

5. Uses OpenCV to detect the remaining outlines as contours.

   ![Contours](res/images/5-contours.png "Contours")

6. Groups contours that likely belong to the same target.

   ![Groups](res/images/6-groups.png "Groups")

7. Creates convex hulls as target representations (see "Reaper" demo).

   ![Hulls](res/images/7-hulls.png "Hulls")

8. Uses CUDA to reduce the hull shape until it roughly matches the outlines.

   ![Shapes](res/images/8-shapes.png "Shapes")

9. Creates polygons as target representations (see "Ana" demo).

   ![Polygons](res/images/9-polygons.png "Polygons")

## Demo
Short clip that demonstrates the result.

[![Reaper](res/images/demos/reaper.jpg)](https://youtu.be/QO6qQR8j-lU "Reaper")

## Next
This is a simple demo and many things can be improved. The following features will be added
in the future when I have time.

* Create concave hulls as target representations.
* Track targets to predict their movement in 3D space.
* Train a neural network to categorize the targets.

## Build
This repository exists for demonstration purposes only. Build instructions are for the author's convenience.

<details>

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
