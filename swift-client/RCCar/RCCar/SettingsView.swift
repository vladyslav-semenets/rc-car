import SwiftUI

struct SettingsView: View {
    @ObservedObject var car: CarController

    @AppStorage("piIP")       private var piIP       = "100.108.40.34"
    @AppStorage("webrtcPort") private var webrtcPort = "8889"
    @AppStorage("udpPort")    private var udpPort    = "8565"
    @AppStorage("sshUser")        private var sshUser        = "vladyslav"
    @AppStorage("sshPassword")    private var sshPassword    = "210716hiq"
    @AppStorage("mediamtxPath")   private var mediamtxPath   = "/home/vladyslav/mediametx/mediamtx"
    @AppStorage("mediamtxConfig") private var mediamtxConfig = "/home/vladyslav/rc-car-repo"

    @State private var steeringExpo: Double = Double(UserDefaults.standard.object(forKey: "steeringExpo") as? Float ?? 0.5)
    @State private var gimbalExpo:   Double = Double(UserDefaults.standard.object(forKey: "gimbalExpo")   as? Float ?? 0.3)

    var body: some View {
        Form {
            Section("Raspberry Pi") {
                TextField("Tailscale IP", text: $piIP)
                TextField("WebRTC port",  text: $webrtcPort)
                TextField("UDP port",     text: $udpPort)
            }
            Section("SSH (for mediamtx)") {
                TextField("SSH user",        text: $sshUser)
                SecureField("SSH password",  text: $sshPassword)
                TextField("mediamtx binary", text: $mediamtxPath)
                TextField("Run directory",   text: $mediamtxConfig)
            }

            // MARK: Motor Config
            Section("Motor Config") {
                VStack(alignment: .leading, spacing: 10) {
                    motorRow("Front ESC Trim (µs)",
                             hint: "Boost front ESC deadband. +15 makes front start with rear.",
                             value: $car.cfg_frontTrimUs, range: -50...50)
                    Divider()
                    motorRow("Rear ESC Trim (µs)",
                             hint: "Offset rear ESC. Adjust if car pulls under load.",
                             value: $car.cfg_rearTrimUs, range: -50...50)
                    Divider()
                    motorRow("Slew Rate (µs/step)",
                             hint: "Max PWM change per 10ms. Lower = smoother. 25 = ~200ms full ramp.",
                             value: $car.cfg_slewMaxUs, range: 5...100)
                    Divider()
                    motorRow("Dir Change Hold (ms)",
                             hint: "Neutral pause when switching direction. Protects motors.",
                             value: $car.cfg_dirChangeHoldMs, range: 50...500)
                    Divider()
                    motorRow("Front Lag Steps",
                             hint: "How many 10ms steps front ESC lags behind rear. 3 = 30ms.",
                             value: $car.cfg_frontLagSteps, range: 0...10)
                    Divider()
                    motorRow("Reverse Brake (ms)",
                             hint: "Hobbywing brake pulse before reverse engages.",
                             value: $car.cfg_reverseBrakeMs, range: 100...600)
                    Divider()
                    motorRow("Reverse Neutral Gap (ms)",
                             hint: "Neutral pause between brake and reverse.",
                             value: $car.cfg_reverseNeutralMs, range: 50...300)
                }
                .padding(.vertical, 4)

                Button("Send to Car") {
                    car.sendMotorConfig()
                }
                .buttonStyle(.borderedProminent)
                .frame(maxWidth: .infinity)
            }

            Section("Stick Sensitivity") {
                VStack(alignment: .leading, spacing: 14) {
                    expoRow(label: "Steering (left stick)",
                            hint: "0 = linear  ·  1 = slow center, fast edges",
                            value: $steeringExpo,
                            color: .blue,
                            key: "steeringExpo")

                    Divider()

                    expoRow(label: "Gimbal Yaw (right stick)",
                            hint: "0 = linear  ·  1 = slow center, fast edges",
                            value: $gimbalExpo,
                            color: .orange,
                            key: "gimbalExpo")
                }
                .padding(.vertical, 4)
            }

            Section("Preview") {
                Text("Stream: http://\(piIP):\(webrtcPort)/front")
                    .font(.caption.monospaced()).foregroundColor(.secondary)
                Text("UDP: \(piIP):\(udpPort)")
                    .font(.caption.monospaced()).foregroundColor(.secondary)
                Text("SSH: \(sshUser)@\(piIP)")
                    .font(.caption.monospaced()).foregroundColor(.secondary)
            }
        }
        .formStyle(.grouped)
        .padding()
        .frame(width: 420)
    }

    @ViewBuilder
    private func motorRow(_ label: String, hint: String, value: Binding<Int>, range: ClosedRange<Int>) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(label).font(.caption).fontWeight(.medium)
                Spacer()
                Stepper(value: value, in: range) {
                    Text("\(value.wrappedValue)")
                        .font(.caption.monospaced())
                        .foregroundColor(.secondary)
                        .frame(width: 40, alignment: .trailing)
                }
            }
            Text(hint).font(.caption2).foregroundColor(.secondary)
        }
    }

    @ViewBuilder
    private func expoRow(label: String, hint: String, value: Binding<Double>, color: Color, key: String) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text(label).font(.caption).fontWeight(.medium)
                Spacer()
                Text(String(format: "%.2f", value.wrappedValue))
                    .font(.caption.monospaced())
                    .foregroundColor(.secondary)
                    .frame(width: 36, alignment: .trailing)
            }
            Slider(value: value, in: 0...1, step: 0.01) { _ in
                UserDefaults.standard.set(Float(value.wrappedValue), forKey: key)
            }
            .tint(color)

            Text(hint).font(.caption2).foregroundColor(.secondary)

            ExpoCurvePreview(expo: value.wrappedValue, color: color)
                .frame(height: 90)
        }
    }
}

// MARK: - Expo curve preview

struct ExpoCurvePreview: View {
    let expo: Double
    let color: Color

    var body: some View {
        GeometryReader { geo in
            let w = geo.size.width
            let h = geo.size.height
            let pad: CGFloat = 10

            ZStack {
                RoundedRectangle(cornerRadius: 6)
                    .fill(Color.gray.opacity(0.08))

                // Grid
                Path { p in
                    p.move(to: CGPoint(x: w / 2, y: pad));     p.addLine(to: CGPoint(x: w / 2, y: h - pad))
                    p.move(to: CGPoint(x: pad, y: h / 2));     p.addLine(to: CGPoint(x: w - pad, y: h / 2))
                }
                .stroke(Color.gray.opacity(0.3), lineWidth: 0.5)

                // Linear reference (dashed)
                Path { p in
                    p.move(to:    CGPoint(x: pad,     y: h - pad))
                    p.addLine(to: CGPoint(x: w - pad, y: pad))
                }
                .stroke(Color.gray.opacity(0.4), style: StrokeStyle(lineWidth: 1, dash: [4, 3]))

                // Expo curve
                expoCurvePath(w: w, h: h, pad: pad)
                    .stroke(color, style: StrokeStyle(lineWidth: 2, lineCap: .round, lineJoin: .round))

                // Axis labels
                VStack {
                    HStack {
                        Text("stick in →").font(.system(size: 7)).foregroundColor(.secondary)
                        Spacer()
                    }
                    Spacer()
                    HStack {
                        Spacer()
                        Text("↑ out").font(.system(size: 7)).foregroundColor(.secondary)
                    }
                }
                .padding(5)
            }
        }
    }

    private func expoCurvePath(w: CGFloat, h: CGFloat, pad: CGFloat) -> Path {
        Path { p in
            let steps = 80
            for i in 0...steps {
                let t  = Double(i) / Double(steps)
                let x  = t * 2 - 1                            // -1…1
                let y  = (1 - expo) * x + expo * x * x * x   // expo output
                let px = pad + CGFloat(t) * (w - pad * 2)
                let py = (h - pad) - CGFloat((y + 1) / 2) * (h - pad * 2)
                if i == 0 { p.move(to: CGPoint(x: px, y: py)) }
                else      { p.addLine(to: CGPoint(x: px, y: py)) }
            }
        }
    }
}
