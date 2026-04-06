#!/bin/bash
# ============================================================================
# SIYI HM30 — GStreamer Webcam-to-Air-Unit Streamer
#
# Captures H.264 from the DJI Osmo Action 5 Pro (USB webcam) and sends
# it as RTP/UDP to the SIYI HM30 Air unit. Zero-copy H.264 passthrough
# (no re-encoding needed — camera outputs H.264 natively).
#
# Usage:
#   ./stream_to_siyi.sh                         # defaults
#   ./stream_to_siyi.sh 192.168.144.147 5600     # custom IP/port
#   ./stream_to_siyi.sh 192.168.144.147 5600 1080p  # 1080p mode
# ============================================================================

# --- Configuration ---
DEST_IP="${1:-192.168.144.147}"
DEST_PORT="${2:-5600}"
RESOLUTION="${3:-720p}"
DEVICE="/dev/video0"

if [ "$RESOLUTION" = "1080p" ]; then
    WIDTH=1920
    HEIGHT=1080
    FONT_S="Sans 40"
    FONT_L="Sans Bold 120"
    TIME_FONT="Sans Bold 50"
else
    WIDTH=1280
    HEIGHT=720
    FONT_S="Sans 30"
    FONT_L="Sans Bold 80"
    TIME_FONT="Sans Bold 40"
fi

echo "=== SIYI HM30 GStreamer Webcam Streamer ==="
echo "Camera      : DJI Osmo Action 5 Pro ($DEVICE)"
echo "Resolution  : ${WIDTH}x${HEIGHT} @ 30fps"
echo "Destination : rtp://${DEST_IP}:${DEST_PORT}"
echo "Encoding    : Hardware H.264 / Software Fallback"
echo "Press Ctrl+C to stop."
echo ""

STATE="NONE"
GST_PID=""

cleanup() {
    echo -e "\nCaught Ctrl+C! Stopping streams..."
    [ -n "$GST_PID" ] && kill -9 $GST_PID 2>/dev/null
    exit 0
}
trap cleanup INT TERM

start_camera() {
    [ -n "$GST_PID" ] && kill -9 $GST_PID 2>/dev/null
    gst-launch-1.0 -q -e \
        v4l2src device=${DEVICE} ! \
        "video/x-h264,stream-format=byte-stream,alignment=au,width=${WIDTH},height=${HEIGHT},framerate=30/1" ! \
        h264parse ! rtph264pay config-interval=1 pt=96 ! \
        udpsink host=${DEST_IP} port=${DEST_PORT} sync=false > /dev/null 2>&1 &
    GST_PID=$!
}

start_fallback() {
    [ -n "$GST_PID" ] && kill -9 $GST_PID 2>/dev/null
    BG_COLOR=$((16#FF1A202C))
    CLK_COLOR=$((16#FFF53855))
    gst-launch-1.0 -q -e \
        videotestsrc pattern=solid-color foreground-color=${BG_COLOR} ! \
        "video/x-raw,width=${WIDTH},height=${HEIGHT},framerate=30/1" ! \
        textoverlay text="<span foreground='white' font_desc='${FONT_S}'>STREAM IS</span>&#10;<span foreground='#F53855' font_desc='${FONT_L}'>OFFLINE</span>" valignment=center halignment=center draw-shadow=false ! \
        clockoverlay time-format="[ %H:%M:%S ]" valignment=top halignment=right xpad=50 ypad=50 font-desc="${TIME_FONT}" color=${CLK_COLOR} shaded-background=true ! \
        x264enc tune=zerolatency bitrate=2000 speed-preset=ultrafast ! \
        rtph264pay config-interval=1 pt=96 ! \
        udpsink host=${DEST_IP} port=${DEST_PORT} sync=false > /dev/null 2>&1 &
    GST_PID=$!
}

while true; do
    if [ -e "$DEVICE" ]; then
        if [ "$STATE" != "CAMERA" ]; then
            echo ">>> Camera ($DEVICE) connected. Streaming hardware H.264 feed..."
            start_camera
            STATE="CAMERA"
        fi
        if ! kill -0 $GST_PID 2>/dev/null; then
            STATE="NONE"
        fi
    else
        if [ "$STATE" != "FALLBACK" ]; then
            echo ">>> Camera missing. Streaming waiting screen..."
            start_fallback
            STATE="FALLBACK"
        fi
        if ! kill -0 $GST_PID 2>/dev/null; then
            STATE="NONE"
        fi
    fi
    sleep 0.5
done
