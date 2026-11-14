#!/usr/bin/env bash
set -euo pipefail

# Setup software I2C (i2c-gpio) on Raspberry Pi for INA3221 battery monitor.
# Default: SDA = GPIO 12 (Pin 32), SCL = GPIO 5 (Pin 29), Bus = 3 (/dev/i2c-3).

BUS=3
SDA_GPIO=12
SCL_GPIO=""

for arg in "$@"; do
    case "$arg" in
        --bus=*) BUS="${arg#*=}" ;;
        --sda=*) SDA_GPIO="${arg#*=}" ;;
        --scl=*) SCL_GPIO="${arg#*=}" ;;
        -h|--help)
            echo "Usage: sudo $0 [--sda=12] [--scl=5] [--bus=3]"
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg"
            echo "Usage: sudo $0 [--sda=12] [--scl=5] [--bus=3]"
            exit 1
            ;;
    esac
done

if [[ $EUID -ne 0 ]]; then
    echo "Please run as root: sudo $0 $*"
    exit 1
fi

if [[ -z "$SCL_GPIO" ]]; then
    if [[ -t 0 ]]; then
        read -r -p "Enter SCL GPIO number (default 5 = Pin 29): " input_scl
        SCL_GPIO="${input_scl:-5}"
    else
        SCL_GPIO=5
    fi
fi

# Locate Raspberry Pi boot config.txt
if [[ -f /boot/firmware/config.txt ]]; then
    CONFIG_TXT="/boot/firmware/config.txt"
elif [[ -f /boot/config.txt ]]; then
    CONFIG_TXT="/boot/config.txt"
else
    echo "Error: Could not find /boot/firmware/config.txt or /boot/config.txt"
    exit 1
fi

echo "==> Configuring INA3221 I2C bus=/dev/i2c-${BUS} (SDA=GPIO${SDA_GPIO}, SCL=GPIO${SCL_GPIO})"
echo "==> Boot config: ${CONFIG_TXT}"

# Install i2c-tools if missing
if ! command -v i2cdetect >/dev/null 2>&1; then
    echo "==> Installing i2c-tools..."
    apt-get update -qq
    apt-get install -y i2c-tools
fi

# Ensure i2c-dev kernel module loads on boot
if ! grep -qE '^\s*i2c-dev\s*$' /etc/modules 2>/dev/null; then
    echo "i2c-dev" >> /etc/modules
fi
modprobe i2c-dev || true

# Backup config.txt once
if [[ ! -f "${CONFIG_TXT}.bak.ina3221" ]]; then
    cp "$CONFIG_TXT" "${CONFIG_TXT}.bak.ina3221"
fi

# Remove any previous i2c-gpio line for this bus, then append clean overlay
OVERLAY_LINE="dtoverlay=i2c-gpio,bus=${BUS},i2c_gpio_sda=${SDA_GPIO},i2c_gpio_scl=${SCL_GPIO},i2c_gpio_delay_us=2"
TMP_CFG=$(mktemp)
grep -vE "^dtoverlay=i2c-gpio,bus=${BUS}\b" "$CONFIG_TXT" > "$TMP_CFG" || true
echo "$OVERLAY_LINE" >> "$TMP_CFG"
mv "$TMP_CFG" "$CONFIG_TXT"
echo "==> Added to ${CONFIG_TXT}: ${OVERLAY_LINE}"

# Add INA3221_I2C_DEV to raspberry-pi-client/c/.env if present or create it
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="${SCRIPT_DIR}/c/.env"
if [[ -f "$ENV_FILE" ]]; then
    if grep -qE '^INA3221_I2C_DEV=' "$ENV_FILE"; then
        sed -i.bak "s|^INA3221_I2C_DEV=.*|INA3221_I2C_DEV=/dev/i2c-${BUS}|" "$ENV_FILE"
        rm -f "${ENV_FILE}.bak"
    else
        echo "INA3221_I2C_DEV=/dev/i2c-${BUS}" >> "$ENV_FILE"
    fi
else
    echo "INA3221_I2C_DEV=/dev/i2c-${BUS}" > "$ENV_FILE"
fi
echo "==> Configured ${ENV_FILE} with INA3221_I2C_DEV=/dev/i2c-${BUS}"

# Try loading overlay dynamically if supported
if [[ ! -e "/dev/i2c-${BUS}" ]] && command -v dtoverlay >/dev/null 2>&1; then
    dtoverlay i2c-gpio bus="${BUS}" i2c_gpio_sda="${SDA_GPIO}" i2c_gpio_scl="${SCL_GPIO}" i2c_gpio_delay_us=2 || true
    sleep 0.5
fi

if [[ -e "/dev/i2c-${BUS}" ]]; then
    echo "==> /dev/i2c-${BUS} is active! Scanning for INA3221 (expected 0x40):"
    i2cdetect -y "${BUS}" || true
else
    echo "==> /dev/i2c-${BUS} will appear after reboot."
    echo "    Run 'sudo reboot' and then verify with: i2cdetect -y ${BUS}"
fi
