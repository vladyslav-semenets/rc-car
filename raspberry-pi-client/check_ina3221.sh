#!/usr/bin/env bash
set -euo pipefail

# Quick INA3221 voltage check script using the same C driver (c/ina3221.c).
# Usage:
#   ./check_ina3221.sh               # Single reading
#   ./check_ina3221.sh --watch       # Live 1s polling (Ctrl+C to stop)
#   ./check_ina3221.sh --dev=/dev/i2c-3

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="${SCRIPT_DIR}/c/.env"

I2C_DEV=""
WATCH=0

for arg in "$@"; do
    case "$arg" in
        --dev=*) I2C_DEV="${arg#*=}" ;;
        --watch|-w) WATCH=1 ;;
        -h|--help)
            echo "Usage: $0 [--dev=/dev/i2c-3] [--watch]"
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg"
            echo "Usage: $0 [--dev=/dev/i2c-3] [--watch]"
            exit 1
            ;;
    esac
done

if [[ -z "$I2C_DEV" && -f "$ENV_FILE" ]]; then
    I2C_DEV=$(grep -E '^INA3221_I2C_DEV=' "$ENV_FILE" | tail -n1 | cut -d'=' -f2- | tr -d '[:space:]' || true)
fi

TMP_SRC=$(mktemp /tmp/ina3221_check_XXXXXX.c)
TMP_BIN=$(mktemp /tmp/ina3221_check_XXXXXX)
cleanup() {
    rm -f "$TMP_SRC" "$TMP_BIN"
}
trap cleanup EXIT

cat > "$TMP_SRC" << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "ina3221.h"

int main(int argc, char **argv) {
    const char *dev = (argc > 1 && argv[1][0] != '\0') ? argv[1] : NULL;
    int watch = (argc > 2) ? atoi(argv[2]) : 0;

    int fd = ina3221_open(dev);
    if (fd < 0) {
        fprintf(stderr, "[ERROR] INA3221 (0x40) not found on %s\n",
                dev ? dev : "auto-probed I2C buses (/dev/i2c-3, /dev/i2c-1, ...)");
        fprintf(stderr, "Hint: check wiring (SDA=GPIO12/Pin32, VCC=3.3V, GND) or run with sudo.\n");
        return 1;
    }

    do {
        float v1 = 0.0f, v2 = 0.0f;
        if (ina3221_read_bus_voltage(fd, INA3221_REG_VBUS_CH1, &v1) != 0 ||
            ina3221_read_bus_voltage(fd, INA3221_REG_VBUS_CH2, &v2) != 0) {
            fprintf(stderr, "[ERROR] Failed to read voltage registers from INA3221\n");
            ina3221_close(fd);
            return 1;
        }

        int p1 = ina3221_estimate_lipo_2s_percent(v1);
        int p2 = ina3221_estimate_lipo_2s_percent(v2);

        printf("BAT1: %.2f V (%3d%%) | BAT2: %.2f V (%3d%%)", v1, p1, v2, p2);
        if ((v1 > 1.0f && v1 < LIPO_2S_CRIT_V) || (v2 > 1.0f && v2 < LIPO_2S_CRIT_V)) {
            printf("  [CRITICAL]");
        } else if ((v1 > 1.0f && v1 < LIPO_2S_WARN_V) || (v2 > 1.0f && v2 < LIPO_2S_WARN_V)) {
            printf("  [LOW]");
        } else {
            printf("  [OK]");
        }
        printf("\n");
        fflush(stdout);

        if (watch) {
            sleep(1);
        }
    } while (watch);

    ina3221_close(fd);
    return 0;
}
EOF

gcc -O2 -Wall -I "${SCRIPT_DIR}/c" "$TMP_SRC" "${SCRIPT_DIR}/c/ina3221.c" -o "$TMP_BIN"
"$TMP_BIN" "$I2C_DEV" "$WATCH"
