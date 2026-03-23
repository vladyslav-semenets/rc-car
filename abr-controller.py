#!/usr/bin/env python3
"""
Adaptive Bitrate Controller for RC Car streaming.

Monitors network quality (ping to Mac + tc cake queue stats) and
dynamically adjusts the ffmpeg video bitrate by:
  1. Writing the target bitrate to /tmp/rc_car_bitrate
  2. Killing ffmpeg — MediaMTX restarts it (runOnInitRestart: yes)
     and the new instance reads the updated bitrate from the file.

Bitrate tiers (kbps):
  Tier 0 — 600k   (very poor signal / indoors)
  Tier 1 — 1200k  (poor signal)
  Tier 2 — 2000k  (moderate — default start)
  Tier 3 — 3000k  (good signal)
  Tier 4 — 4000k  (excellent — outdoor, strong 4G)
"""

import subprocess
import time
import os
import sys
import logging

# ─── Configuration ─────────────────────────────────────────────────────────────

# Tailscale IP of your Mac — used for latency measurement
MAC_TAILSCALE_IP = '100.78.40.118'

# Network interface used for 4G (USB tethering)
TETHERING_IFACE = 'usb0'

# File that mediamtx reads to determine ffmpeg bitrate
BITRATE_FILE = '/tmp/rc_car_bitrate'

# How often (seconds) to sample network quality
SAMPLE_INTERVAL = 3

# Bitrate tiers — ordered low to high
BITRATE_TIERS = ['600k', '1200k', '2000k', '3000k', '4000k']

# Start at tier 2 (2000k — safe default)
INITIAL_TIER = 2

# How many consecutive BAD samples before stepping down a tier
BAD_SAMPLES_TO_DOWNGRADE = 2

# How many consecutive GOOD samples before stepping up a tier
GOOD_SAMPLES_TO_UPGRADE = 5

# Latency thresholds (ms) — above = bad, below = good
LATENCY_BAD_MS  = 150
LATENCY_GOOD_MS = 60

# Dropped packet threshold — above = bad
DROPS_BAD = 3

# Minimum seconds between tier changes (avoid thrashing)
MIN_CHANGE_INTERVAL = 10

# ─── Logging ───────────────────────────────────────────────────────────────────

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [ABR] %(message)s',
    datefmt='%H:%M:%S',
)
log = logging.getLogger('abr')

# ─── Helpers ───────────────────────────────────────────────────────────────────

def get_ping_ms(host: str) -> float:
    """Ping host 3 times, return average RTT in ms. Returns 999 on failure."""
    try:
        result = subprocess.run(
            ['ping', '-c', '3', '-W', '1', '-q', host],
            capture_output=True, text=True, timeout=6
        )
        for line in result.stdout.splitlines():
            if 'avg' in line or 'rtt' in line:
                # Format: rtt min/avg/max/mdev = 10.1/15.2/20.3/4.1 ms
                parts = line.split('=')[-1].strip().split('/')
                if len(parts) >= 2:
                    return float(parts[1])
    except Exception as e:
        log.warning(f'ping failed: {e}')
    return 999.0


def get_cake_drops(iface: str) -> int:
    """Return total dropped packets reported by tc cake on the interface."""
    try:
        result = subprocess.run(
            ['tc', '-s', 'qdisc', 'show', 'dev', iface],
            capture_output=True, text=True, timeout=3
        )
        for line in result.stdout.splitlines():
            if 'dropped' in line:
                for token in line.split():
                    if token.rstrip(',').isdigit():
                        idx = line.split().index(token)
                        if idx > 0 and 'dropped' in line.split()[idx - 1]:
                            return int(token.rstrip(','))
    except Exception as e:
        log.warning(f'tc stats failed: {e}')
    return 0


def write_bitrate(bitrate: str):
    """Write the target bitrate string to the shared file."""
    with open(BITRATE_FILE, 'w') as f:
        f.write(bitrate)


def restart_ffmpeg():
    """
    Kill the running ffmpeg instance.
    MediaMTX (runOnInitRestart: yes) will restart it automatically,
    and the new process will read the updated BITRATE_FILE.
    """
    result = subprocess.run(
        ['pkill', '-f', 'ffmpeg.*video0'],
        capture_output=True
    )
    if result.returncode == 0:
        log.info('ffmpeg restarted to apply new bitrate')
    else:
        log.warning('ffmpeg process not found — may not be running yet')


def apply_tier(tier: int):
    """Write bitrate file and restart ffmpeg."""
    bitrate = BITRATE_TIERS[tier]
    write_bitrate(bitrate)
    restart_ffmpeg()
    log.info(f'→ Tier {tier}: {bitrate}')

# ─── Main loop ─────────────────────────────────────────────────────────────────

def main():
    current_tier = INITIAL_TIER
    write_bitrate(BITRATE_TIERS[current_tier])

    good_samples = 0
    bad_samples  = 0
    prev_drops   = get_cake_drops(TETHERING_IFACE)
    last_change  = 0.0

    log.info(f'Starting ABR controller — initial tier {current_tier} ({BITRATE_TIERS[current_tier]})')
    log.info(f'Monitoring: ping to {MAC_TAILSCALE_IP}, drops on {TETHERING_IFACE}')

    while True:
        time.sleep(SAMPLE_INTERVAL)

        # ── Measure ──────────────────────────────────────────────────────────
        ping_ms    = get_ping_ms(MAC_TAILSCALE_IP)
        total_drops = get_cake_drops(TETHERING_IFACE)
        new_drops  = max(0, total_drops - prev_drops)
        prev_drops = total_drops

        log.info(
            f'ping={ping_ms:.0f}ms  drops={new_drops}  '
            f'tier={current_tier} ({BITRATE_TIERS[current_tier]})'
        )

        # ── Classify ─────────────────────────────────────────────────────────
        is_bad  = ping_ms > LATENCY_BAD_MS  or new_drops > DROPS_BAD
        is_good = ping_ms < LATENCY_GOOD_MS and new_drops == 0

        now = time.time()

        if is_bad:
            bad_samples  += 1
            good_samples  = 0

            if (bad_samples >= BAD_SAMPLES_TO_DOWNGRADE
                    and current_tier > 0
                    and now - last_change > MIN_CHANGE_INTERVAL):
                current_tier -= 1
                bad_samples   = 0
                last_change   = now
                log.info(f'Network degraded — downgrading bitrate')
                apply_tier(current_tier)

        elif is_good:
            good_samples += 1
            bad_samples   = 0

            if (good_samples >= GOOD_SAMPLES_TO_UPGRADE
                    and current_tier < len(BITRATE_TIERS) - 1
                    and now - last_change > MIN_CHANGE_INTERVAL):
                current_tier += 1
                good_samples  = 0
                last_change   = now
                log.info(f'Network improved — upgrading bitrate')
                apply_tier(current_tier)

        else:
            # Neutral — neither clearly good nor bad, hold current tier
            good_samples = 0
            bad_samples  = 0


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        log.info('Stopped.')
        sys.exit(0)
