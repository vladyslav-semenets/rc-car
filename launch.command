#!/bin/bash

# ── Config ────────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CLIENT_DIR="$SCRIPT_DIR/client/c"
BINARY="$CLIENT_DIR/cmake-build-debug/rccarclient"
ENV_FILE="$CLIENT_DIR/.env"

# Read Pi IP from .env
PI_IP=$(grep RASPBERRY_PI_IP "$ENV_FILE" | cut -d= -f2)
STREAM_URL="http://$PI_IP:8889/front"

# ── Open WebRTC stream in browser ─────────────────────────────────────────────
echo "[launcher] Opening stream: $STREAM_URL"
open "$STREAM_URL"

# ── Start C controller app ────────────────────────────────────────────────────
echo "[launcher] Starting RC car controller..."
cd "$CLIENT_DIR" && "$BINARY"
