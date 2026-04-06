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
else
    WIDTH=1280
    HEIGHT=720
fi

echo "=== SIYI HM30 GStreamer Webcam Streamer ==="
echo "Camera      : DJI Osmo Action 5 Pro ($DEVICE)"
echo "Resolution  : ${WIDTH}x${HEIGHT} @ 30fps"
echo "Destination : rtp://${DEST_IP}:${DEST_PORT}"
echo "Encoding    : H.264 passthrough (zero-copy from camera)"
echo "Press Ctrl+C to stop."
echo ""

# The Osmo Action 5 Pro outputs H.264 byte-stream directly via UVC.
# We just parse, packetize into RTP, and send over UDP.
# If the camera disconnects, we stream a "Waiting..." screen using software encoding.

while true; do
    if [ -e "$DEVICE" ]; then
        echo ">>> Camera ($DEVICE) detected. Streaming hardware H.264 feed..."
        gst-launch-1.0 -e \
            v4l2src device=${DEVICE} ! \
            "video/x-h264,stream-format=byte-stream,alignment=au,width=${WIDTH},height=${HEIGHT},framerate=30/1" ! \
            h264parse ! \
            rtph264pay config-interval=1 pt=96 ! \
            udpsink host=${DEST_IP} port=${DEST_PORT} sync=false
            
        echo ">>> Camera pipeline ended (disconnected?). Switching to waiting screen..."
    else
        echo ">>> Camera ($DEVICE) missing. Streaming waiting screen..."
        # Fallback stream: black background, centered text, software H.264 encode
        # We specify the timeout so it periodically checks for the camera again
        # We use 'timeout 3' to restart the loop and check if camera came back
        # Actually it's better to just stream this constantly until the camera appears.
        # But GStreamer doesn't easily let us abort when a new device appears.
        # Simple solution: Stream the waiting screen for 2 seconds, exit, check again.
        
        BG_COLOR=$((16#FF1A202C))
        CLK_COLOR=$((16#FFF53855))
        
        gst-launch-1.0 -e \
            videotestsrc pattern=solid-color foreground-color=${BG_COLOR} num-buffers=60 ! \
            "video/x-raw,width=${WIDTH},height=${HEIGHT},framerate=30/1" ! \
            textoverlay text="<span foreground='white' size='40000'>STREAM IS</span>&#10;<span foreground='#F53855' size='120000' font_weight='bold'>OFFLINE</span>" valignment=center halignment=center draw-shadow=false ! \
            clockoverlay time-format="[ %H:%M ]" valignment=top halignment=right xpad=50 ypad=50 font-desc="Sans Bold 60" color=${CLK_COLOR} shaded-background=true ! \
            x264enc tune=zerolatency bitrate=2000 speed-preset=ultrafast ! \
            rtph264pay config-interval=1 pt=96 ! \
            udpsink host=${DEST_IP} port=${DEST_PORT} sync=false
    fi
    sleep 0.5
done
