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
        gearLevel: Int = 1        // 1 to 8
    ): ByteArray {
        val buf = ByteArray(PACKET_LEN)
        buf[0] = SYNC_BYTE
        buf[1] = seq++
        buf[2] = throttlePercent.coerceIn(-100, 100).toByte()
        buf[3] = steeringAngleDeg.toInt().coerceIn(0, 180).toByte()
        buf[4] = gimbalYawDeg.toInt().coerceIn(-90, 90).toByte()
        buf[5] = gimbalPitchDeg.coerceIn(-45, 45).toByte()

        var flags = 0
        if (gyroOn)    flags = flags or (1 shl 0)
        if (unstuckOn) flags = flags or (1 shl 1)
        if (cameraOn)  flags = flags or (1 shl 2)
        flags = flags or ((gearLevel.coerceIn(1, 8) and 0x0F) shl 3)
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
