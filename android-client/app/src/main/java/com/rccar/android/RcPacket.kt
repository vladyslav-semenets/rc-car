package com.rccar.android

/**
 * Compact 8-Byte High-Speed RC Packet (ExpressLRS / CRSF style).
 *
 * Drops RF Airtime from ~22 ms (MAVLink 41B) down to ~5.5 ms (8B).
 *
 * Layout:
 * [0] Sync (0xAA)
 * [1] Seq (0..255)
 * [2] Throttle (-100..+100)
 * [3] Steering (0..180 deg)
 * [4] Gimbal Yaw (-90..+90 deg)
 * [5] Gimbal Pitch (-45..+45 deg)
 * [6] Flags (bit 0: Gyro ESP, bit 1: Unstuck, bit 2: Cam, bit 3..6: Gear)
 * [7] CRC-8 (Polynomial 0x07)
 */
object RcPacket {
    const val SYNC_BYTE: Byte = 0xAA.toByte()
    const val PACKET_LEN = 8

    private var seq: Byte = 0

    fun encode(
        throttlePercent: Int,     // -100 to +100
        steeringAngleDeg: Float,  // 0 to 180 deg
        gimbalYawDeg: Float = 0f, // -90 to +90 deg
        gimbalPitchDeg: Int = 0,  // -45 to +45 deg
        gyroOn: Boolean = false,
        unstuckOn: Boolean = false,
        cameraOn: Boolean = false,
        gearLevel: Int = 1,       // 1 to 8
        reinitOn: Boolean = false
    ): ByteArray {
        val buf = ByteArray(PACKET_LEN)
        buf[0] = SYNC_BYTE
        buf[1] = seq++
        buf[2] = throttlePercent.coerceIn(-100, 100).toByte()
        buf[3] = steeringAngleDeg.toInt().coerceIn(0, 180).toByte()
        buf[4] = gimbalYawDeg.toInt().coerceIn(-120, 120).toByte()
        buf[5] = gimbalPitchDeg.coerceIn(-90, 90).toByte()

        var flags = 0
        if (gyroOn)    flags = flags or (1 shl 0)
        if (unstuckOn) flags = flags or (1 shl 1)
        if (cameraOn)  flags = flags or (1 shl 2)
        flags = flags or ((gearLevel.coerceIn(1, 8) and 0x0F) shl 3)
        if (reinitOn)  flags = flags or (1 shl 7)
        buf[6] = flags.toByte()

        buf[7] = computeCrc8(buf, 0, 7)
        return buf
    }

    fun computeCrc8(data: ByteArray, offset: Int, length: Int): Byte {
        var crc = 0x00
        for (i in offset until offset + length) {
            crc = crc xor (data[i].toInt() and 0xFF)
            for (j in 0 until 8) {
                crc = if ((crc and 0x80) != 0) {
                    ((crc shl 1) xor 0x07) and 0xFF
                } else {
                    (crc shl 1) and 0xFF
                }
            }
        }
        return crc.toByte()
    }
}

data class TelemetryData(
    val seq: Int,
    val bat1Volts: Float,
    val bat2Volts: Float,
    val bat1Percent: Int,
    val bat2Percent: Int,
    val isBat1Low: Boolean,
    val isBat1Crit: Boolean,
    val isBat2Low: Boolean,
    val isBat2Crit: Boolean,
    val isSensorOnline: Boolean
)

/**
 * Compact 8-Byte Downlink Telemetry Packet (Rover -> LoRa -> BLE -> Android).
 *
 * Layout:
 * [0] Sync (0xAB)
 * [1] Seq (0..255)
 * [2..3] Bat1 mV (uint16 LE)
 * [4..5] Bat2 mV (uint16 LE)
 * [6] Flags (bit 0: bat1 low, bit 1: bat1 crit, bit 2: bat2 low, bit 3: bat2 crit, bit 4: sensor online)
 * [7] CRC-8 (Polynomial 0x07)
 */
object TelemetryPacket {
    const val SYNC_BYTE: Byte = 0xAB.toByte()
    const val PACKET_LEN = 8

    private const val LIPO_2S_MAX_V = 8.40f
    private const val LIPO_2S_CRIT_V = 6.40f

    fun decode(data: ByteArray): TelemetryData? {
        if (data.size < PACKET_LEN) return null
        if (data[0] != SYNC_BYTE) return null

        val expectedCrc = RcPacket.computeCrc8(data, 0, PACKET_LEN - 1)
        if (data[PACKET_LEN - 1] != expectedCrc) return null

        val seq = data[1].toInt() and 0xFF
        val bat1Mv = (data[2].toInt() and 0xFF) or ((data[3].toInt() and 0xFF) shl 8)
        val bat2Mv = (data[4].toInt() and 0xFF) or ((data[5].toInt() and 0xFF) shl 8)
        val flags = data[6].toInt() and 0xFF

        val v1 = bat1Mv / 1000f
        val v2 = bat2Mv / 1000f

        return TelemetryData(
            seq = seq,
            bat1Volts = v1,
            bat2Volts = v2,
            bat1Percent = estimateLipo2sPercent(v1),
            bat2Percent = estimateLipo2sPercent(v2),
            isBat1Low = (flags and (1 shl 0)) != 0,
            isBat1Crit = (flags and (1 shl 1)) != 0,
            isBat2Low = (flags and (1 shl 2)) != 0,
            isBat2Crit = (flags and (1 shl 3)) != 0,
            isSensorOnline = (flags and (1 shl 4)) != 0
        )
    }

    private fun estimateLipo2sPercent(voltage: Float): Int {
        if (voltage >= LIPO_2S_MAX_V) return 100
        if (voltage <= LIPO_2S_CRIT_V) return 0
        return (((voltage - LIPO_2S_CRIT_V) / (LIPO_2S_MAX_V - LIPO_2S_CRIT_V)) * 100f).toInt()
    }
}
