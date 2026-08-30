import Foundation
import Combine

class CarController: ObservableObject {
    @Published var transmissionSpeed = 1   // 1-8
    @Published var degreeOfTurns: Float = 86.0
    @Published var pitchAngle: Int = 0
    @Published var isSteeringCalibrationOn = false

    /// 0 = linear, 1 = full cubic (slow center, fast edges)
    @Published var steeringExpo: Float {
        didSet { UserDefaults.standard.set(steeringExpo, forKey: "steeringExpo") }
    }
    @Published var gimbalExpo: Float {
        didSet { UserDefaults.standard.set(gimbalExpo, forKey: "gimbalExpo") }
    }

    // MARK: - Motor config (sent to Pi via cmd 15)
    @Published var motorConfigEnabled: Bool {
        didSet { UserDefaults.standard.set(motorConfigEnabled, forKey: "motorConfigEnabled") }
    }
    @Published var cfg_frontTrimUs: Int {
        didSet { UserDefaults.standard.set(cfg_frontTrimUs, forKey: "cfg_frontTrimUs") }
    }
    @Published var cfg_rearTrimUs: Int {
        didSet { UserDefaults.standard.set(cfg_rearTrimUs, forKey: "cfg_rearTrimUs") }
    }
    @Published var cfg_slewMaxUs: Int {
        didSet { UserDefaults.standard.set(cfg_slewMaxUs, forKey: "cfg_slewMaxUs") }
    }
    @Published var cfg_dirChangeHoldMs: Int {
        didSet { UserDefaults.standard.set(cfg_dirChangeHoldMs, forKey: "cfg_dirChangeHoldMs") }
    }
    @Published var cfg_frontLagSteps: Int {
        didSet { UserDefaults.standard.set(cfg_frontLagSteps, forKey: "cfg_frontLagSteps") }
    }
    @Published var cfg_reverseBrakeMs: Int {
        didSet { UserDefaults.standard.set(cfg_reverseBrakeMs, forKey: "cfg_reverseBrakeMs") }
    }
    @Published var cfg_reverseNeutralMs: Int {
        didSet { UserDefaults.standard.set(cfg_reverseNeutralMs, forKey: "cfg_reverseNeutralMs") }
    }

    let udp = UDPManager()
    private var heartbeatTask: Task<Void, Never>?
    private var cancellables = Set<AnyCancellable>()

    init() {
        steeringExpo = UserDefaults.standard.object(forKey: "steeringExpo") as? Float ?? 0.5
        gimbalExpo   = UserDefaults.standard.object(forKey: "gimbalExpo")   as? Float ?? 0.3

        motorConfigEnabled   = UserDefaults.standard.object(forKey: "motorConfigEnabled")   as? Bool ?? true
        cfg_frontTrimUs      = UserDefaults.standard.object(forKey: "cfg_frontTrimUs")      as? Int ?? 15
        cfg_rearTrimUs       = UserDefaults.standard.object(forKey: "cfg_rearTrimUs")       as? Int ?? 0
        cfg_slewMaxUs        = UserDefaults.standard.object(forKey: "cfg_slewMaxUs")        as? Int ?? 25
        cfg_dirChangeHoldMs  = UserDefaults.standard.object(forKey: "cfg_dirChangeHoldMs")  as? Int ?? 150
        cfg_frontLagSteps    = UserDefaults.standard.object(forKey: "cfg_frontLagSteps")    as? Int ?? 3
        cfg_reverseBrakeMs   = UserDefaults.standard.object(forKey: "cfg_reverseBrakeMs")   as? Int ?? 250
        cfg_reverseNeutralMs = UserDefaults.standard.object(forKey: "cfg_reverseNeutralMs") as? Int ?? 120
    }

    private let speedLimits: [(level: Int, max: Int)] = [
        (1, 20), (2, 35), (3, 45), (5, 65), (6, 85), (8, 100)
    ]

    // MARK: - Connection

    func connect(host: String, port: UInt16) {
        udp.connect(host: host, port: port)
        startHeartbeat()
        // Send saved motor config to Pi after connecting
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { [weak self] in
            self?.sendMotorConfig()
        }
    }

    func sendMotorConfig() {
        if motorConfigEnabled {
            send(15,
                 p1: Float(cfg_frontTrimUs),
                 p2: Float(cfg_rearTrimUs),
                 p3: Float(cfg_slewMaxUs),
                 p4: Float(cfg_dirChangeHoldMs),
                 p5: Float(cfg_frontLagSteps),
                 p6: Float(cfg_reverseBrakeMs),
                 p7: Float(cfg_reverseNeutralMs))
        } else {
            // Disabled — original simple behavior: no slew, no lag, no reverse arm, no trim
            send(15, p1: 0, p2: 0, p3: 500, p4: 0, p5: 0, p6: 0, p7: 0)
        }
    }

    func disconnect() {
        heartbeatTask?.cancel()
        udp.disconnect()
    }

    // MARK: - Car commands

    func initCar()         { send(1, p1: 50, p2: degreeOfTurns) }
    func turnTo(_ d: Float){ send(4, p1: d) }
    func changeDegrees()   { send(2, p1: degreeOfTurns) }
    func resetTurns()      { send(3, p1: degreeOfTurns) }
    func neutral()         { send(7) }
    func unstuck()         { send(16, p1: degreeOfTurns) }
    func startCamera()     { send(8) }
    func stopCamera()      { send(9) }
    func resetGimbal()     { send(12) }

    func forward(trigger: Float) {
        let speed = limitSpeed(Int(trigger * 100))
        send(5, p1: Float(speed))
    }

    func backward(trigger: Float) {
        send(6, p1: Float(Int(trigger * 100)))
    }

    func gimbalYaw(_ degrees: Float)  { send(10, p1: degrees) }
    func gimbalPitch()                { send(11, p1: Float(pitchAngle)) }

    func toggleSteeringCalibration() {
        send(isSteeringCalibrationOn ? 14 : 13)
        isSteeringCalibrationOn.toggle()
    }

    func speedUp()   { if transmissionSpeed < 8 { transmissionSpeed += 1 } }
    func speedDown() { if transmissionSpeed > 1 { transmissionSpeed -= 1 } }

    // MARK: - Stick mapping

    /// expo curve: 0=linear, 1=full cubic. Preserves sign.
    static func applyExpo(_ v: Float, expo: Float) -> Float {
        let e = max(0, min(1, expo))
        return (1 - e) * v + e * v * v * v
    }

    /// Converts left stick X (-1…1) to servo degrees
    func stickToSteering(_ v: Float) -> Float {
        let curved = CarController.applyExpo(v, expo: steeringExpo)
        // 0 → degreeOfTurns (neutral), +1 → 0° (right), -1 → 140° (left)
        if curved >= 0 { return degreeOfTurns * (1 - curved) }
        else           { return degreeOfTurns + (-curved) * (140 - degreeOfTurns) }
    }

    /// Converts right stick X (-1…1) to gimbal yaw degrees
    func stickToGimbalYaw(_ v: Float) -> Float {
        CarController.applyExpo(v, expo: gimbalExpo) * 90
    }

    // MARK: - Private

    private func limitSpeed(_ speed: Int) -> Int {
        for limit in speedLimits.reversed() where transmissionSpeed >= limit.level {
            return min(speed, limit.max)
        }
        return min(speed, 20)
    }

    private func send(_ cmd: Int, p1: Float = 0, p2: Float = 0, p3: Float = 0,
                      p4: Float = 0, p5: Float = 0, p6: Float = 0, p7: Float = 0) {
        let data = MAVLink.commandLong(command: UInt16(cmd),
                                       p1: p1, p2: p2, p3: p3,
                                       p4: p4, p5: p5, p6: p6, p7: p7)
        print("[CMD] cmd=\(cmd) p1=\(p1) p2=\(p2) bytes=\(data.count) hex=\(data.map { String(format: "%02X", $0) }.joined(separator: " "))")
        udp.send(data)
    }

    private func startHeartbeat() {
        heartbeatTask?.cancel()
        heartbeatTask = Task.detached { [weak self] in
            while !Task.isCancelled {
                self?.udp.send(MAVLink.heartbeat())
                try? await Task.sleep(nanoseconds: 100_000_000)
            }
        }
    }
}
