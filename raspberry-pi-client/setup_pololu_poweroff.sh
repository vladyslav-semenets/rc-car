#!/usr/bin/env bash
set -euo pipefail

# Configure Pololu #2812 Pushbutton Power Switch auto-poweroff on Raspberry Pi 4:
#   - Pololu OFF pin <- 1N4007 cathode, anode <- GPIO 26 (Physical Pin 37)
#   - Enables dtoverlay=gpio-poweroff,gpiopin=26,active_low=0 in config.txt
#   - Ensures passwordless sudo for shutdown/poweroff/reboot via /etc/sudoers.d/rc-car-power
#
# Run once on the Raspberry Pi:
#   sudo ./setup_pololu_poweroff.sh
# Then reboot so gpio-poweroff takes effect.

GPIO_PIN="${1:-26}"
OVERLAY_LINE="dtoverlay=gpio-poweroff,gpiopin=${GPIO_PIN},active_low=0"

if [[ $EUID -ne 0 ]]; then
    echo "Please run as root: sudo $0"
    exit 1
fi

if [[ -f /boot/firmware/config.txt ]]; then
    CONFIG_TXT="/boot/firmware/config.txt"
elif [[ -f /boot/config.txt ]]; then
    CONFIG_TXT="/boot/config.txt"
else
    echo "Error: Could not find /boot/firmware/config.txt or /boot/config.txt"
    exit 1
fi

if [[ ! -f "${CONFIG_TXT}.bak" ]]; then
    cp "$CONFIG_TXT" "${CONFIG_TXT}.bak"
    echo "[OK] Backed up ${CONFIG_TXT} -> ${CONFIG_TXT}.bak"
fi

# Remove any previous gpio-poweroff lines and append the configured one idempotently
sed -i.tmp '/^dtoverlay=gpio-poweroff/d' "$CONFIG_TXT"
rm -f "${CONFIG_TXT}.tmp"

if ! grep -Fxq "$OVERLAY_LINE" "$CONFIG_TXT"; then
    echo "$OVERLAY_LINE" >> "$CONFIG_TXT"
fi
echo "[OK] Added to ${CONFIG_TXT}: ${OVERLAY_LINE}"

# Configure passwordless shutdown/poweroff/reboot for the invoking user (or sudo group)
TARGET_USER="${SUDO_USER:-vladyslav}"
SUDOERS_FILE="/etc/sudoers.d/rc-car-power"
cat > "$SUDOERS_FILE" <<EOF
${TARGET_USER} ALL=(ALL) NOPASSWD: /sbin/shutdown, /sbin/poweroff, /sbin/reboot, /usr/sbin/shutdown, /usr/sbin/poweroff, /usr/sbin/reboot
EOF
chmod 0440 "$SUDOERS_FILE"
if command -v visudo >/dev/null 2>&1; then
    visudo -cf "$SUDOERS_FILE"
fi
echo "[OK] Configured passwordless shutdown in ${SUDOERS_FILE} for ${TARGET_USER}"

echo ""
echo "Done. Reboot the Raspberry Pi once to activate gpio-poweroff:"
echo "  sudo reboot"
