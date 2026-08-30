package com.rccar.android

import java.nio.ByteBuffer
import java.nio.ByteOrder

object MAVLink {
    private val crcExtra = mapOf(
        0u to 50u.toUByte(),   // HEARTBEAT
        4u to 237u.toUByte(),  // PING
        76u to 152u.toUByte(), // COMMAND_LONG
        109u to 185u.toUByte() // RADIO_STATUS
    )
    private var sequence: UByte = 0u

    data class ParsedMessage(
        val msgId: UInt,
        val sysId: UByte,
        val compId: UByte,
        val payload: ByteArray
    )

    // HEARTBEAT — msg_id=0
    fun heartbeat(): ByteArray {
        val payload = ByteArray(9)
        val bb = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN)
        bb.putInt(0)          // custom_mode
        bb.put(0)             // type
        bb.put(0)             // autopilot
        bb.put(0)             // base_mode
        bb.put(10)            // system_status (MAV_STATE_STANDBY)
        bb.put(3)             // mavlink_version
        return frame(msgID = 0u, payload = payload)
    }

    // PING — msg_id=4
    fun ping(timeUsec: Long, seq: Int = 0, targetSystem: UByte = 1u, targetComponent: UByte = 1u): ByteArray {
        val payload = ByteArray(14)
        val bb = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN)
        bb.putLong(timeUsec)
        bb.putInt(seq)
        bb.put(targetSystem.toByte())
        bb.put(targetComponent.toByte())
        return frame(msgID = 4u, payload = payload)
    }

    // COMMAND_LONG — msg_id=76
    fun commandLong(
        command: UShort,
        p1: Float = 0f, p2: Float = 0f, p3: Float = 0f, p4: Float = 0f,
        p5: Float = 0f, p6: Float = 0f, p7: Float = 0f
    ): ByteArray {
        val payload = ByteArray(33)
        val bb = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN)
        bb.putFloat(p1); bb.putFloat(p2); bb.putFloat(p3); bb.putFloat(p4)
        bb.putFloat(p5); bb.putFloat(p6); bb.putFloat(p7)
        bb.putShort(command.toShort())
        bb.put(1)   // target_system
        bb.put(1)   // target_component
        bb.put(0)   // confirmation
        return frame(msgID = 76u, payload = payload)
    }

    // Parser for incoming MAVLink v2 frames
    fun parse(data: ByteArray): List<ParsedMessage> {
        val results = mutableListOf<ParsedMessage>()
        var i = 0
        while (i < data.size) {
            if (data[i] == 0xFD.toByte() && i + 10 <= data.size) {
                val len = data[i + 1].toUByte().toInt()
                val totalLen = 10 + len + 2
                if (i + totalLen <= data.size) {
                    val sysId = data[i + 5].toUByte()
                    val compId = data[i + 6].toUByte()
                    val msgId = (data[i + 7].toUByte().toUInt()) or
                            (data[i + 8].toUByte().toUInt() shl 8) or
                            (data[i + 9].toUByte().toUInt() shl 16)
                    val payload = data.copyOfRange(i + 10, i + 10 + len)
                    results.add(ParsedMessage(msgId, sysId, compId, payload))
                    i += totalLen
                    continue
                }
            }
            i++
        }
        return results
    }

    private fun frame(msgID: UInt, payload: ByteArray): ByteArray {
        // Strip trailing zeros (MAVLink v2 §11.3)
        var len = payload.size
        while (len > 0 && payload[len - 1] == 0.toByte()) len--
        val stripped = payload.copyOf(len)

        val seq = sequence++
        val header = byteArrayOf(
            0xFD.toByte(),
            stripped.size.toByte(),
            0, 0,               // incompat, compat flags
            seq.toByte(),
            1,                  // sysid
            200.toByte(),       // compid
            (msgID and 0xFFu).toByte(),
            ((msgID shr 8) and 0xFFu).toByte(),
            ((msgID shr 16) and 0xFFu).toByte()
        )

        // CRC over header[1..] + payload + crcExtra
        var crc = 0xFFFFu
        for (j in 1 until header.size) crc = crcStep(header[j], crc)
        for (b in stripped) crc = crcStep(b, crc)
        crcExtra[msgID]?.let { extra -> crc = crcStep(extra.toByte(), crc) }

        val crcLo = (crc and 0xFFu).toByte()
        val crcHi = ((crc shr 8) and 0xFFu).toByte()

        return header + stripped + byteArrayOf(crcLo, crcHi)
    }

    private fun crcStep(b: Byte, crc: UInt): UInt {
        val tmp = b.toUByte().toUInt() xor (crc and 0xFFu)
        val tmp2 = tmp xor ((tmp shl 4) and 0xFFu)
        return (crc shr 8) xor (tmp2 shl 8) xor (tmp2 shl 3) xor (tmp2 shr 4)
    }
}
