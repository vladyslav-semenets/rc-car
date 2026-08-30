import Foundation

// MAVLink v2 encoder — HEARTBEAT and COMMAND_LONG only.
// Wire format: 0xFD | LEN | INCOMPAT | COMPAT | SEQ | SYSID | COMPID | MSGID(3) | PAYLOAD | CRC(2)
// Trailing zero bytes are stripped from payload (MAVLink v2 spec).
enum MAVLink {

    private static let crcExtra: [UInt32: UInt8] = [0: 50, 76: 152]
    private static var sequence: UInt8 = 0

    // MARK: - Public

    static func heartbeat() -> Data {
        var p = Data(count: 9)
        p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 0  // custom_mode (uint32 LE)
        p[4] = 10  // MAV_TYPE_GROUND_ROVER
        p[5] = 0   // MAV_AUTOPILOT_GENERIC
        p[6] = 0   // base_mode
        p[7] = 4   // MAV_STATE_ACTIVE
        p[8] = 3   // mavlink_version
        return frame(msgID: 0, payload: p)
    }

    static func commandLong(command: UInt16,
                            p1: Float = 0, p2: Float = 0, p3: Float = 0,
                            p4: Float = 0, p5: Float = 0, p6: Float = 0,
                            p7: Float = 0) -> Data {
        var p = Data(count: 33)
        writeFloat(p1, into: &p, at: 0)
        writeFloat(p2, into: &p, at: 4)
        writeFloat(p3, into: &p, at: 8)
        writeFloat(p4, into: &p, at: 12)
        writeFloat(p5, into: &p, at: 16)
        writeFloat(p6, into: &p, at: 20)
        writeFloat(p7, into: &p, at: 24)
        p[28] = UInt8(command & 0xFF)   // command lo
        p[29] = UInt8(command >> 8)     // command hi
        p[30] = 1   // target_system
        p[31] = 1   // target_component
        p[32] = 0   // confirmation (will be stripped as trailing zero)
        return frame(msgID: 76, payload: p)
    }

    // MARK: - Private

    private static func writeFloat(_ v: Float, into data: inout Data, at offset: Int) {
        var val = v
        withUnsafeBytes(of: &val) { for i in 0..<4 { data[offset + i] = $0[i] } }
    }

    /// Build a MAVLink v2 frame, stripping trailing zero bytes from the payload.
    private static func frame(msgID: UInt32, payload: Data) -> Data {
        // Strip trailing zeros (MAVLink v2 spec §11.3)
        var trimmedPayload = payload
        while trimmedPayload.last == 0 && trimmedPayload.count > 1 {
            trimmedPayload.removeLast()
        }
        // If payload is all zeros the last byte must stay (but heartbeat has mavlink_version=3 so it's fine)
        if payload.allSatisfy({ $0 == 0 }) { trimmedPayload = Data([0]) }

        let len = UInt8(trimmedPayload.count)
        let seq = sequence; sequence = sequence &+ 1

        // Header (9 bytes, everything after 0xFD):
        // LEN INCOMPAT COMPAT SEQ SYSID COMPID MSGID_L MSGID_M MSGID_H
        let header: [UInt8] = [
            len,
            0,    // incompat_flags
            0,    // compat_flags
            seq,
            1,    // sysid
            200,  // compid
            UInt8(msgID & 0xFF),
            UInt8((msgID >> 8) & 0xFF),
            UInt8((msgID >> 16) & 0xFF),
        ]

        var crc: UInt16 = 0xFFFF
        for b in header          { crc = crcStep(b, crc) }
        for b in trimmedPayload  { crc = crcStep(b, crc) }
        if let extra = crcExtra[msgID] { crc = crcStep(extra, crc) }

        var out = Data()
        out.append(0xFD)
        out.append(contentsOf: header)
        out.append(contentsOf: trimmedPayload)
        out.append(UInt8(crc & 0xFF))
        out.append(UInt8(crc >> 8))
        return out
    }

    private static func crcStep(_ b: UInt8, _ crc: UInt16) -> UInt16 {
        var tmp = UInt16(b) ^ (crc & 0xFF)
        tmp ^= (tmp << 4) & 0xFF
        return (crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4)
    }
}
