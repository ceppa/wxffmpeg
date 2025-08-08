# FFmpeg Video Converter (C++ / wxWidgets Thread)

A simple example of integrating **FFmpeg** video decoding, pixel format conversion, and **H.264 encoding** (via libx264) into a **wxWidgets thread**.

This project demonstrates:

- Opening an input video file with FFmpeg
- Finding and decoding the video stream
- Converting frames to `YUV420p` (required by most H.264 encoders)
- Encoding video using **libx264**
- Muxing encoded packets into a chosen container format
- Running the conversion in a separate worker thread using **wxThread**

---

## ✨ Features

- Input: Any video format supported by FFmpeg
- Output: H.264-encoded video in user-specified container (`mp4`, `mkv`, etc.)
- Automatic pixel format conversion (via libswscale)
- Threaded execution so GUI remains responsive
- Simple logging to track progress
- Easily extendable to support audio streams or stream copying

---

## 📦 Dependencies

You need:

- **FFmpeg** (libraries and headers: `libavformat`, `libavcodec`, `libavutil`, `libswscale`)
- **libx264** (H.264 encoder)
- **wxWidgets** (for threading and GUI integration)
- C++17 or later

Example on Ubuntu/Debian:

```bash
sudo apt update
sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libx264-dev libwxgtk3.2-dev
````

On macOS with Homebrew:

```bash
brew install ffmpeg wxwidgets
```

On Windows:

* Install FFmpeg developer libraries (from [ffmpeg.org](https://ffmpeg.org/download.html) or MSYS2).
* Install wxWidgets and configure your compiler include/lib paths.

---

## ⚙️ Build

Example using `g++` on Linux/macOS:

```bash
g++ -std=c++17 -o converter \
    ConverterThread.cpp \
    `wx-config --cxxflags --libs` \
    -lavformat -lavcodec -lavutil -lswscale -lx264
```

If using CMake:

```cmake
cmake_minimum_required(VERSION 3.10)
project(VideoConverter)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libavutil libswscale)

find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

add_executable(converter ConverterThread.cpp)
target_include_directories(converter PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(converter PRIVATE ${wxWidgets_LIBRARIES} ${FFMPEG_LIBRARIES} x264)
```

---

## ▶️ Usage

```cpp
// Create and start the converter thread
ConverterThread* thread = new ConverterThread(input_path, output_format, handler);
if (thread->Run() != wxTHREAD_NO_ERROR) {
    // Handle error
}
```

Where:

* `input_path` → path to the input video file
* `output_format` → desired container format (e.g., `"mp4"`, `"mkv"`)
* `handler` → object implementing `setRunning(bool)` for progress/state handling

The thread will:

1. Open the input video file
2. Decode video frames
3. Convert pixel format to `YUV420p`
4. Encode with H.264
5. Write output to file
6. Clean up resources

---

## 🗂 File Structure

```
.
├── ConverterThread.cpp   # Main threaded video converter implementation
├── ConverterThread.h     # Thread class definition
├── README.md             # This file
└── (other wxWidgets GUI files)
```

---

## 🔧 Extending

* **Add audio encoding:** Detect and open audio streams, send to encoder, mux alongside video.
* **Copy streams:** Instead of re-encoding, set stream parameters and write packets directly (`codecpar` copy).
* **Set encoder options:** Pass `AVDictionary*` to `avcodec_open2()` to adjust bitrate, preset, CRF, etc.
* **Progress updates:** Calculate percentage based on `pkt->pts` vs `duration`.

---

## 📄 License

This code is provided under the **MIT License**.
FFmpeg is licensed under **LGPL/GPL**, and libx264 is **GPL** — ensure your usage complies with their licenses.

---

## 📚 References

* [FFmpeg Documentation](https://ffmpeg.org/documentation.html)
* [wxWidgets Documentation](https://docs.wxwidgets.org/)
* [libx264](https://www.videolan.org/developers/x264.html)

