import GameController
import CoreHaptics
import Foundation

class JoystickManager: ObservableObject {
    @Published var isConnected = false
    @Published var controllerName = "No controller"
    @Published var leftStickX: Float = 0
    @Published var rightStickX: Float = 0
    @Published var rightTrigger: Float = 0
    @Published var leftTrigger: Float = 0

    // Callbacks → CarController
    var onSteeringChanged: ((Float) -> Void)?
    var onSteeringReleased: (() -> Void)?
    var onGimbalYawChanged: ((Float) -> Void)?
    var onGimbalYawReleased: (() -> Void)?
    var onForward: ((Float) -> Void)?
    var onBackward: ((Float) -> Void)?
    var onStop: (() -> Void)?
    var onButton: ((Button) -> Void)?

    enum Button {
        case dpadUp, dpadDown, dpadLeft, dpadRight
        case a, b, x, y, l1, r1, l3, r3
    }

    private let deadzone: Float = 0.09

    // ── Haptics ──────────────────────────────────────────────────────────
    private var hapticEngine: CHHapticEngine?
    private var hapticPlayer: CHHapticPatternPlayer?
    private var currentRumbleIntensity: Float = 0

    init() {
        NotificationCenter.default.addObserver(self, selector: #selector(didConnect(_:)),    name: .GCControllerDidConnect,    object: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(didDisconnect(_:)), name: .GCControllerDidDisconnect, object: nil)
        GCController.startWirelessControllerDiscovery {}
        GCController.controllers().first.map(setup)
    }

    @objc private func didConnect(_ n: Notification) {
        (n.object as? GCController).map(setup)
    }

    @objc private func didDisconnect(_ n: Notification) {
        stopRumble()
        hapticEngine = nil
        DispatchQueue.main.async {
            self.isConnected = false
            self.controllerName = "No controller"
            self.leftStickX = 0; self.rightStickX = 0
            self.rightTrigger = 0; self.leftTrigger = 0
        }
    }

    private func setup(_ controller: GCController) {
        guard let pad = controller.extendedGamepad else { return }
        DispatchQueue.main.async {
            self.isConnected = true
            self.controllerName = controller.productCategory
        }
        setupHaptics(for: controller)

        // Left stick → steering
        pad.leftThumbstick.xAxis.valueChangedHandler = { [weak self] _, v in
            guard let self else { return }
            DispatchQueue.main.async { self.leftStickX = v }
            if abs(v) > deadzone {
                onSteeringChanged?(v)
                updateOrStartRumble(intensity: abs(v) * 0.3)
            } else {
                onSteeringReleased?()
                stopRumble()
            }
        }

        // Right stick → gimbal yaw
        pad.rightThumbstick.xAxis.valueChangedHandler = { [weak self] _, v in
            guard let self else { return }
            DispatchQueue.main.async { self.rightStickX = v }
            if abs(v) > deadzone { onGimbalYawChanged?(v) }
            else { onGimbalYawReleased?() }
        }

        // R2 → forward + rumble
        pad.rightTrigger.valueChangedHandler = { [weak self] _, v, _ in
            guard let self else { return }
            DispatchQueue.main.async { self.rightTrigger = v }
            if v > 0.03 {
                onForward?(v)
                updateOrStartRumble(intensity: v * 0.7)
            } else {
                onStop?()
                stopRumble()
            }
        }

        // L2 → backward + rumble (softer)
        pad.leftTrigger.valueChangedHandler = { [weak self] _, v, _ in
            guard let self else { return }
            DispatchQueue.main.async { self.leftTrigger = v }
            if v > 0.03 {
                onBackward?(v)
                updateOrStartRumble(intensity: v * 0.4)
            } else {
                onStop?()
                stopRumble()
            }
        }

        // Buttons
        let map: [(GCControllerButtonInput, Button)] = [
            (pad.dpad.up,    .dpadUp),   (pad.dpad.down,  .dpadDown),
            (pad.dpad.left,  .dpadLeft), (pad.dpad.right, .dpadRight),
            (pad.buttonA, .a), (pad.buttonB, .b),
            (pad.buttonX, .x), (pad.buttonY, .y),
            (pad.leftShoulder, .l1), (pad.rightShoulder, .r1),
        ]
        for (btn, type) in map {
            btn.pressedChangedHandler = { [weak self] _, _, pressed in
                if pressed { self?.onButton?(type) }
            }
        }
        pad.leftThumbstickButton?.pressedChangedHandler  = { [weak self] _, _, p in if p { self?.onButton?(.l3) } }
        pad.rightThumbstickButton?.pressedChangedHandler = { [weak self] _, _, p in if p { self?.onButton?(.r3) } }
    }

    // MARK: - Haptics

    private func setupHaptics(for controller: GCController) {
        // Try localities in order — DualShock 4 on macOS often needs .handles or .all
        let localities: [GCHapticsLocality] = [.handles, .all, .default, .leftHandle, .rightHandle]
        var engine: CHHapticEngine?
        for loc in localities {
            if let e = controller.haptics?.createEngine(withLocality: loc) {
                engine = e
                print("[Haptic] Engine created with locality: \(loc.rawValue)")
                break
            }
        }
        guard let engine else {
            print("[Haptic] Controller \(controller.productCategory) doesn't support haptics")
            return
        }
        hapticEngine = engine

        // Restart engine if the system stops it (e.g. audio interruption)
        engine.stoppedHandler = { [weak self] reason in
            print("[Haptic] Engine stopped (reason: \(reason.rawValue)) — restarting")
            self?.hapticPlayer = nil
            try? self?.hapticEngine?.start()
        }
        engine.resetHandler = { [weak self] in
            print("[Haptic] Engine reset — restarting")
            self?.hapticPlayer = nil
            try? self?.hapticEngine?.start()
        }

        do {
            try engine.start()
            print("[Haptic] Engine started ✓")
        } catch {
            print("[Haptic] Failed to start: \(error)")
        }
    }

    private func updateOrStartRumble(intensity: Float) {
        // Re-use running player if intensity hasn't changed much
        let delta = abs(intensity - currentRumbleIntensity)
        if hapticPlayer != nil && delta < 0.1 { return }
        // Otherwise restart with new intensity
        stopRumble()
        startRumble(intensity: intensity)
    }

    private func startRumble(intensity: Float) {
        guard let engine = hapticEngine else {
            print("[Haptic] startRumble: no engine")
            return
        }

        let intensityParam = CHHapticEventParameter(parameterID: .hapticIntensity, value: max(0.01, intensity))
        let sharpnessParam = CHHapticEventParameter(parameterID: .hapticSharpness, value: 0.3)

        let event = CHHapticEvent(
            eventType: .hapticContinuous,
            parameters: [intensityParam, sharpnessParam],
            relativeTime: 0,
            duration: 30
        )

        do {
            let pattern = try CHHapticPattern(events: [event], parameters: [])
            let player  = try engine.makePlayer(with: pattern)
            hapticPlayer = player
            currentRumbleIntensity = intensity
            try player.start(atTime: CHHapticTimeImmediate)
            print("[Haptic] rumble started intensity=\(String(format: "%.2f", intensity))")
        } catch {
            print("[Haptic] startRumble error: \(error)")
        }
    }

    private func stopRumble() {
        guard hapticPlayer != nil else { return }
        do {
            try hapticPlayer?.stop(atTime: CHHapticTimeImmediate)
            print("[Haptic] rumble stopped")
        } catch {
            print("[Haptic] stopRumble error: \(error)")
        }
        hapticPlayer = nil
        currentRumbleIntensity = 0
    }

    // MARK: - Public test

    func testRumble() {
        print("[Haptic] testRumble called, engine=\(hapticEngine != nil), player=\(hapticPlayer != nil)")
        stopRumble()
        startRumble(intensity: 1.0)
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) { [weak self] in
            self?.stopRumble()
        }
    }
}
