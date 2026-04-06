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

set -e

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

# --- GStreamer Pipeline ---
# The Osmo Action 5 Pro outputs H.264 byte-stream directly via UVC.
# We just parse, packetize into RTP, and send over UDP.
gst-launch-1.0 -v \
    v4l2src device=${DEVICE} ! \
    "video/x-h264,stream-format=byte-stream,alignment=au,width=${WIDTH},height=${HEIGHT},framerate=30/1" ! \
    h264parse ! \
    rtph264pay config-interval=1 pt=96 ! \
    udpsink host=${DEST_IP} port=${DEST_PORT} sync=false
