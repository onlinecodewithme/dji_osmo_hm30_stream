#!/bin/bash
# ============================================================================
# SIYI HM30 — GStreamer Webcam-to-Air-Unit Streamer
#
# Captures H.264 from the DJI Osmo Action 5 Pro (USB webcam) and sends
# it as RTP/UDP to the SIYI HM30 Air unit. Zero-copy H.264 passthrough
# (no re-encoding needed — camera outputs H.264 natively).
#
# Usage:
#   ./stream_to_siyi.sh                         # defaults (Broadcast to all IPs)
#   ./stream_to_siyi.sh 192.168.144.161 5600     # custom IP/port
#   ./stream_to_siyi.sh 192.168.144.161 5600 1080p  # 1080p mode
# ============================================================================

# --- Configuration ---
DEST_IP="${1:-255.255.255.255}"
DEST_PORT="${2:-5600}"
RESOLUTION="${3:-720p}"
get_camera_device() {
    for sysdev in /sys/class/video4linux/video* ; do
        if [ -d "$sysdev" ] && [ -f "$sysdev/name" ]; then
            name=$(cat "$sysdev/name" 2>/dev/null)
            if [[ "$name" == *"Action"* ]] || [[ "$name" == *"DJI"* ]]; then
                echo "/dev/$(basename $sysdev)"
                return
            fi
        fi
    done
    
    # We must NOT fall back simply to /dev/video0 or /dev/video2 on this rig, 
    # because /dev/video0 is firmly attached to the ZED X stereo capture card.
    # If the user hasn't tapped "Webcam" on the DJI, it's safer to return empty 
    # and stay on the "STREAM IS OFFLINE" screen than arbitrarily streaming the ZED!
    echo ""
}

if [ "$RESOLUTION" = "1080p" ]; then
    WIDTH=1920
    HEIGHT=1080
    FONT_S="Sans 15"
    FONT_L="Sans Bold 40"
    TIME_FONT="Sans Bold 15"
else
    WIDTH=1280
    HEIGHT=720
    FONT_S="Sans 10"
    FONT_L="Sans Bold 30"
    TIME_FONT="Sans Bold 10"
fi

echo "=== SIYI HM30 GStreamer Webcam Streamer ==="
echo "Camera      : DJI Osmo Action 5 Pro ($DEVICE)"
echo "Resolution  : ${WIDTH}x${HEIGHT} @ 30fps"
if [ "$DEST_IP" = "255.255.255.255" ]; then
    echo "Destination : rtp://${DEST_IP}:${DEST_PORT} (Broadcast - Accessible from any IP)"
else
    echo "Destination : rtp://${DEST_IP}:${DEST_PORT}"
fi
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
        v4l2src device=${CURRENT_DEV} ! \
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
        videotestsrc pattern=solid-color foreground-color=${BG_COLOR} is-live=true ! \
        "video/x-raw,format=I420,width=${WIDTH},height=${HEIGHT},framerate=30/1" ! \
        textoverlay text="<span foreground='white' font_desc='${FONT_S}'>STREAM IS</span>&#10;<span foreground='#F53855' font_desc='${FONT_L}'>OFFLINE</span>" valignment=center halignment=center draw-shadow=false ! \
        textoverlay text="XAVIER UGV" valignment=bottom halignment=center ypad=50 font-desc="${FONT_S}" color=$((16#FFFFFFFF)) draw-shadow=false ! \
        clockoverlay time-format="[ %H:%M:%S ]" valignment=top halignment=right xpad=50 ypad=50 font-desc="${TIME_FONT}" color=${CLK_COLOR} shaded-background=true ! \
        x264enc tune=zerolatency bitrate=2000 speed-preset=ultrafast ! \
        rtph264pay config-interval=1 pt=96 ! \
        udpsink host=${DEST_IP} port=${DEST_PORT} sync=false > /dev/null 2>&1 &
    GST_PID=$!
}

while true; do
    CURRENT_DEV=$(get_camera_device)
    
    if [ -n "$CURRENT_DEV" ]; then
        if [ "$STATE" != "CAMERA" ]; then
            echo ">>> Camera ($CURRENT_DEV) connected. Streaming hardware H.264 feed..."
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
