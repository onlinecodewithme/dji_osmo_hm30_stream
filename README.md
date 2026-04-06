# DJI Osmo HW / HM30 Stream Tools

A collection of tools for capturing, streaming, and receiving H.264 video streams via the DJI Osmo Action 5 Pro and SIYI HM30 ground station system.

## Components

The repository supplies three main tools:

1. **`stream_to_siyi.sh`** (GStreamer Bash Script)
   Captures UVC video from the *DJI Osmo Action 5 Pro* (`/dev/video0`) and passes the native H.264 stream directly to the SIYI HM30 Air unit over RTP/UDP.
   *Features zero-copy encoding passthrough: UVC provides native H.264, so CPU load is near zero.*

2. **`siyi_streamer`** (C++ Application)
   A standalone C++ streamer using `libav*` that generates a live 30 FPS SMPTE colour-bar test pattern and encodes it in H.264 (Constrained Baseline, `ultrafast`, `zerolatency`). Useful to verify the data link when the webcam isn't available.

3. **`siyi_receiver`** (C++ Application)
   An SDL2-based video receiver. Connects to the SIYI HM30 ground unit via RTSP (or raw RTP/UDP) to decode and display the low-latency H.264 video stream in a dedicated resizable window.

## Network Topology

```
[ DJI Osmo Action 5 Pro ] ---USB---> [ Computer ] ---Eth---> [ SIYI HM30 Air Unit ]   ~ Wireless ~   [ SIYI HM30 Ground Unit ] ---Eth---> [ Monitor PC ]
(Provides native H.264)              (Runs Sender)           (IP: 192.168.144.147)                   (IP: 192.168.144.12)                 (Runs Receiver)
```

## Prerequisites

- **FFmpeg Development Headers**: `libavcodec-dev`, `libavformat-dev`, `libavutil-dev`, `libswscale-dev`
- **SDL2**: `libsdl2-dev`
- **GStreamer** (for script): `gstreamer1.0-tools`, `gstreamer1.0-plugins-good`, `gstreamer1.0-plugins-base`

## Building C++ Tools

```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
```

## Usage

### 1. Streaming the DJI Webcam to Air Unit
Ensure your PC running the sender is on the `192.168.144.x` subnet.
```bash
./scripts/stream_to_siyi.sh                          # Streams 720p to 192.168.144.147:5600
./scripts/stream_to_siyi.sh 192.168.144.147 5600 1080p # Streams 1080p
```

### 2. Running the Test-Pattern Streamer (Optional)
```bash
./build/siyi_streamer --ip 192.168.144.147 --port 5600
```

### 3. Displaying the Received Video on Ground PC
```bash
# Pull RTSP directly from the HM30 Ground Unit
./build/siyi_receiver --rtsp rtsp://192.168.144.12:8554/stream

# Or, if the Ground Unit outputs raw RTP/UDP port 5600:
./build/siyi_receiver --udp 5600
```

## Developer Notes
- C++ Code uses RAII wrappers around FFmpeg types in `src/common/ffmpeg_ptr.hpp` to ensure no memory leaks during continuous streaming operations.
- The streamer uses `libx264` configured specifically for zero-latency, disabling B-frames entirely.
