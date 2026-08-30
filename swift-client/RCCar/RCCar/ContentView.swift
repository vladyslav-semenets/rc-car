import SwiftUI

struct ContentView: View {
    @ObservedObject var car: CarController
    @StateObject private var joystick = JoystickManager()
    @StateObject private var pinger   = PingMonitor()
    @ObservedObject var ssh: SSHManager

    @AppStorage("piIP")       private var piIP       = "100.108.40.34"
    @AppStorage("webrtcPort") private var webrtcPort = "8889"
    @AppStorage("udpPort")    private var udpPort    = "8565"
    @AppStorage("sshUser")        private var sshUser        = "vladyslav"
    @AppStorage("sshPassword")    private var sshPassword    = "210716hiq"
    @AppStorage("mediamtxPath")   private var mediamtxPath   = "/home/vladyslav/mediametx/mediamtx"
    @AppStorage("mediamtxConfig") private var mediamtxConfig = "/home/vladyslav/rc-car-repo"

    var body: some View {
        HStack(spacing: 0) {

            // ── Stream ───────────────────────────────────────────────────
            ZStack {
                Color.black
                WebRTCView(urlString: "http://\(piIP):\(webrtcPort)/front")
            }
            .frame(minWidth: 800, minHeight: 450)

            // ── Side panel ───────────────────────────────────────────────
            VStack(alignment: .leading, spacing: 12) {

                // Connection
                GroupBox("Connection") {
                    VStack(alignment: .leading, spacing: 6) {
                        StatusRow(label: "Pi IP",  value: piIP)
                        StatusRow(label: "UDP",    value: udpPort)
                        HStack {
                            Circle()
                                .fill(car.udp.isConnected ? .green : .red)
                                .frame(width: 9, height: 9)
                            Text(car.udp.isConnected ? "UDP ready" : "UDP not ready")
                                .font(.caption)
                        }
                        StatusRow(label: "Sent", value: "\(car.udp.bytesSent) B")
                        HStack {
                            Circle()
                                .fill(pinger.isReachable ? .green : .red)
                                .frame(width: 9, height: 9)
                            Text("Ping")
                                .font(.caption)
                                .foregroundColor(.secondary)
                            Spacer()
                            Text(pinger.latency)
                                .font(.caption.monospaced())
                                .foregroundColor(pingColor)
                        }
                    }.padding(4)
                }

                // Joystick
                GroupBox("Controller") {
                    VStack(alignment: .leading, spacing: 6) {
                        HStack {
                            Circle()
                                .fill(joystick.isConnected ? .green : .orange)
                                .frame(width: 9, height: 9)
                            Text(joystick.isConnected ? joystick.controllerName : "No controller")
                                .font(.caption)
                        }
                        StickIndicator(x: joystick.leftStickX,  label: "L")
                        StickIndicator(x: joystick.rightStickX, label: "R")
                        TriggerBar(value: joystick.rightTrigger, label: "R2", color: .blue)
                        TriggerBar(value: joystick.leftTrigger,  label: "L2", color: .orange)
                    }.padding(4)
                }

                // Car state
                GroupBox("Car") {
                    VStack(alignment: .leading, spacing: 6) {
                        StatusRow(label: "Gear", value: "\(car.transmissionSpeed) / 8")
                        StatusRow(label: "Turn°", value: String(format: "%.1f", car.degreeOfTurns))
                        StatusRow(label: "Pitch", value: "\(car.pitchAngle)°")
                        StatusRow(label: "Gyro corr", value: car.isSteeringCalibrationOn ? "ON" : "OFF")
                    }.padding(4)
                }

                // mediamtx status
                GroupBox("mediamtx") {
                    VStack(alignment: .leading, spacing: 6) {
                        HStack {
                            Circle()
                                .fill(sshStatusColor)
                                .frame(width: 9, height: 9)
                            Text(sshStatusText)
                                .font(.caption)
                        }
                        Text(ssh.lastLog)
                            .font(.caption2)
                            .foregroundColor(.secondary)
                            .lineLimit(2)
                        HStack(spacing: 6) {
                            Button("Start") {
                                ssh.startMediaMTX(host: piIP, user: sshUser, password: sshPassword,
                                                  binaryPath: mediamtxPath, runDir: mediamtxConfig)
                            }
                            .disabled(ssh.mediaMTXStatus == .running || ssh.mediaMTXStatus == .connecting)
                            Button("Stop") {
                                ssh.stopMediaMTX(host: piIP, user: sshUser, password: sshPassword)
                            }
                            .disabled(ssh.mediaMTXStatus == .idle || ssh.mediaMTXStatus == .stopping)
                            Button("Test SSH") {
                                ssh.testSSH(host: piIP, user: sshUser, password: sshPassword)
                            }
                        }
                        .frame(maxWidth: .infinity)
                    }.padding(4)
                }

                // Motor config
                GroupBox {
                    VStack(alignment: .leading, spacing: 6) {
                        // Header with toggle
                        HStack {
                            Text("Motor Config")
                                .font(.caption).fontWeight(.semibold)
                            Spacer()
                            Text(car.motorConfigEnabled ? "ON" : "OFF")
                                .font(.caption2)
                                .foregroundColor(car.motorConfigEnabled ? .green : .secondary)
                            Toggle("", isOn: $car.motorConfigEnabled)
                                .labelsHidden()
                                .toggleStyle(.switch)
                                .scaleEffect(0.7)
                                .onChange(of: car.motorConfigEnabled) { _ in car.sendMotorConfig() }
                        }
                        if car.motorConfigEnabled {
                            Divider()
                            motorConfigRow("Front Trim (µs)", value: $car.cfg_frontTrimUs,      range: -50...50,  help: .frontTrim)
                            motorConfigRow("Rear Trim (µs)",  value: $car.cfg_rearTrimUs,       range: -50...50,  help: .rearTrim)
                            motorConfigRow("Slew (µs/step)",  value: $car.cfg_slewMaxUs,        range: 5...500,   help: .slew)
                            motorConfigRow("Dir Hold (ms)",   value: $car.cfg_dirChangeHoldMs,  range: 0...500,   help: .dirHold)
                            motorConfigRow("Front Lag",       value: $car.cfg_frontLagSteps,    range: 0...10,    help: .frontLag)
                            motorConfigRow("Rev Brake (ms)",  value: $car.cfg_reverseBrakeMs,   range: 0...600,   help: .revBrake)
                            motorConfigRow("Rev Gap (ms)",    value: $car.cfg_reverseNeutralMs, range: 0...300,   help: .revGap)
                            HStack(spacing: 6) {
                                Button("Reset") {
                                    car.cfg_frontTrimUs      = 15
                                    car.cfg_rearTrimUs       = 0
                                    car.cfg_slewMaxUs        = 25
                                    car.cfg_dirChangeHoldMs  = 150
                                    car.cfg_frontLagSteps    = 3
                                    car.cfg_reverseBrakeMs   = 250
                                    car.cfg_reverseNeutralMs = 120
                                }
                                .frame(maxWidth: .infinity)
                                Button("Send to Car") { car.sendMotorConfig() }
                                    .frame(maxWidth: .infinity)
                                    .buttonStyle(.borderedProminent)
                            }
                        } else {
                            Text("Простое управление без ограничений")
                                .font(.caption2).foregroundColor(.secondary)
                        }
                    }.padding(4)
                }

                // Buttons
                GroupBox("Controls") {
                    VStack(spacing: 6) {
                        Button("Init car (D-pad ↑)")       { car.initCar() }
                            .frame(maxWidth: .infinity)
                        Button("ESC Neutral (B)")          { car.neutral() }
                            .frame(maxWidth: .infinity)
                        Button("🔄 Unstuck")               { car.unstuck() }
                            .frame(maxWidth: .infinity)
                            .help("Раскачивает машину вперёд-назад чтобы выбраться. Нажми ещё раз или дай газ — отменит.")
                        Button("Reload stream") {
                            NotificationCenter.default.post(name: .reloadStream, object: nil)
                        }.frame(maxWidth: .infinity)
                    }.padding(4)
                }

                Spacer()

                SettingsLink { Text("Settings...") }
                    .frame(maxWidth: .infinity, alignment: .trailing)
            }
            .padding()
            .frame(width: 220)
            .background(.background)
        }
        .onAppear {
            setupJoystick()
            car.connect(host: piIP, port: UInt16(udpPort) ?? 8565)
            pinger.start(host: piIP)
        }
        .onDisappear {
            car.disconnect()
            pinger.stop()
        }
        .onChange(of: piIP) { newIP in
            pinger.updateHost(newIP)
        }
        .onChange(of: ssh.mediaMTXStatus) { status in
            if case .running = status {
                DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
                    NotificationCenter.default.post(name: .reloadStream, object: nil)
                }
            }
        }
    }

    // MARK: - Motor config row

    @ViewBuilder
    private func motorConfigRow(_ label: String, value: Binding<Int>, range: ClosedRange<Int>, help: MotorConfigHelp) -> some View {
        HStack(spacing: 4) {
            Text(label).font(.caption2).foregroundColor(.secondary).frame(width: 86, alignment: .leading)
            MotorHelpButton(help: help)
            Spacer()
            Stepper(value: value, in: range) {
                Text("\(value.wrappedValue)")
                    .font(.caption2.monospaced())
                    .frame(width: 32, alignment: .trailing)
            }
        }
    }

    // MARK: - Ping color

    private var pingColor: Color {
        guard pinger.isReachable,
              let ms = Double(pinger.latency.replacingOccurrences(of: " ms", with: "")) else {
            return .secondary
        }
        if ms < 30  { return .green }
        if ms < 80  { return .yellow }
        return .red
    }

    // MARK: - SSH status helpers

    private var sshStatusColor: Color {
        switch ssh.mediaMTXStatus {
        case .running:    return .green
        case .connecting: return .yellow
        case .stopping:   return .orange
        case .error:      return .red
        case .idle:       return .gray
        }
    }

    private var sshStatusText: String {
        switch ssh.mediaMTXStatus {
        case .running:       return "mediamtx running"
        case .connecting:    return "starting…"
        case .stopping:      return "stopping…"
        case .error(let e):  return "error: \(e)"
        case .idle:          return "stopped"
        }
    }

    // MARK: - Joystick wiring

    private func setupJoystick() {
        joystick.onSteeringChanged = { [weak car] v in
            guard let car else { return }
            car.turnTo(car.stickToSteering(v))
        }
        joystick.onSteeringReleased = { car.resetTurns() }

        joystick.onGimbalYawChanged = { [weak car] v in
            car?.gimbalYaw(car?.stickToGimbalYaw(v) ?? 0)
        }
        joystick.onGimbalYawReleased = { car.resetGimbal() }

        joystick.onForward  = { car.forward(trigger: $0) }
        joystick.onBackward = { car.backward(trigger: $0) }
        joystick.onStop     = { car.neutral() }

        joystick.onButton = { [weak car] btn in
            guard let car else { return }
            switch btn {
            case .dpadUp:    car.initCar()
            case .dpadDown:  car.toggleSteeringCalibration()
            case .dpadLeft:
                car.degreeOfTurns = min(car.degreeOfTurns + 1, 180)
                car.changeDegrees()
            case .dpadRight:
                car.degreeOfTurns = max(car.degreeOfTurns - 1, 0)
                car.changeDegrees()
            case .r3:        car.speedUp()
            case .l3:        car.speedDown()
            case .l1:
                car.pitchAngle = max(car.pitchAngle - 1, -90)
                car.gimbalPitch()
            case .r1:
                car.pitchAngle = min(car.pitchAngle + 1, 90)
                car.gimbalPitch()
            case .a:         car.stopCamera()
            case .y:         car.startCamera()
            case .b:         car.neutral()
            default:         break
            }
        }
    }
}

// MARK: - Motor Config Help

enum MotorConfigHelp {
    case frontTrim, rearTrim, slew, dirHold, frontLag, revBrake, revGap

    var title: String {
        switch self {
        case .frontTrim: return "Front Trim (µs)"
        case .rearTrim:  return "Rear Trim (µs)"
        case .slew:      return "Slew Rate (µs/шаг)"
        case .dirHold:   return "Dir Hold (мс)"
        case .frontLag:  return "Front Lag (шаги)"
        case .revBrake:  return "Rev Brake (мс)"
        case .revGap:    return "Rev Gap (мс)"
        }
    }

    var body: String {
        switch self {
        case .frontTrim:
            return """
            Смещение сигнала для переднего ESC.

            Каждый ESC имеет свою мёртвую зону — минимальный сигнал при котором мотор начинает крутиться. Этот параметр компенсирует разницу между передним и задним ESC.

            Схема:
            Газ 15% →  задний ██░░  крутится
                       передний ░░░░  не крутится ❌

            Газ 15% →  задний ██░░  крутится
            +Trim 15   передний ██░░  крутится ✅

            Когда увеличить (+):
            • Передние колёса стартуют позже задних на малом газу
            • На газу 10–20% только задние крутятся

            Когда уменьшить (−):
            • Передние колёса слегка крутятся в покое (мотор гудит без газа)
            • Передние стартуют слишком резко

            Диапазон: −50 до +50 мкс. Обычно 10–20.
            """

        case .rearTrim:
            return """
            Смещение сигнала для заднего ESC.

            Аналогично Front Trim, но для заднего ESC.

            Схема:
            Газ 15% →  передний ██░░  крутится
                       задний   ░░░░  не крутится ❌

            Когда увеличить (+):
            • Задние колёса стартуют позже передних
            • Машина тянет вперёд только передними

            Когда уменьшить (−):
            • Задние колёса крутятся в покое без газа

            По умолчанию 0 — задний ESC обычно более чувствителен.
            Диапазон: −50 до +50 мкс.
            """

        case .slew:
            return """
            Скорость нарастания газа (мкс за шаг 10 мс).

            Ограничивает насколько быстро может меняться сигнал ESC. Защищает трансмиссию от рывков.

            Схема:
            Резкий газ без slew:
            ░░░████████  — рывок, пробуксовка ❌

            Резкий газ со slew 25:
            ░░░▒▒▒▒████  — плавный разгон ✅

            Когда уменьшить (медленнее):
            • Машина дёргается при старте
            • Колёса пробуксовывают на старте
            • Трансмиссия щёлкает при резком газу
            • Значение: 10–15

            Когда увеличить (быстрее):
            • Машина реагирует на газ с заметной задержкой
            • Нужна более резкая реакция
            • Значение: 40–60

            По умолчанию 25 = полный разгон за ~200 мс.
            """

        case .dirHold:
            return """
            Пауза нейтрали при смене направления (мс).

            Когда переключаешься вперёд→назад, оба ESC сначала встают в нейтраль на это время. Защищает моторы и шестерни от удара обратным током.

            Схема:
            Вперёд → Назад:
            ████░░░░▓▓▓▓
                 ↑↑↑
                 Dir Hold — нейтраль здесь

            Когда уменьшить:
            • Переключение вперёд/назад кажется слишком медленным
            • Минимум 50 мс, меньше опасно для ESC

            Когда увеличить:
            • Слышен удар в трансмиссии при смене направления
            • ESC пищит при быстром переключении
            • Значение: 200–300 мс

            По умолчанию 150 мс.
            """

        case .frontLag:
            return """
            Задержка переднего ESC относительно заднего (в шагах по 10 мс).

            Задние колёса стартуют первыми — передние подхватывают через N × 10 мс. Снижает нагрузку на трансмиссию при старте на 6×6.

            Схема (lag = 3 = 30 мс):
            t=0ms:  задние  ▒▒░░░░░  стартуют
                    передние ░░░░░░░  ждут

            t=30ms: задние  ████░░░
                    передние ▒▒░░░░░  стартуют

            Когда уменьшить (к 0):
            • Машина едет по скользкой поверхности (лёд, мокрый асфальт)
            • Нужна максимальная тяга с обеих осей одновременно
            • Передние колёса не успевают за задними

            Когда увеличить:
            • Чувствуется рывок при старте
            • Хруст в передней трансмиссии на старте
            • Значение: 5–8 (50–80 мс)

            0 = синхронный старт обеих осей.
            """

        case .revBrake:
            return """
            Время тормозного импульса перед реверсом (мс).

            Hobbywing ESC не включает реверс сразу — сначала тормозит, потом нужен повторный сигнал. Этот параметр задаёт как долго посылать тормозной импульс.

            Схема:
            Газ назад → [тормоз RevBrake мс] → [нейтраль RevGap мс] → реверс

            ░░░[BRAKE][GAP]▓▓▓▓▓
                            ↑ реверс включился

            Когда увеличить:
            • Реверс не включается или включается через раз
            • ESC не успевает зарегистрировать тормозной сигнал
            • Попробуй 350–400 мс

            Когда уменьшить:
            • Большая задержка перед реверсом (машина долго не едет назад)
            • Попробуй 150–200 мс

            По умолчанию 250 мс.
            """

        case .revGap:
            return """
            Пауза нейтрали между тормозом и реверсом (мс).

            После тормозного импульса ESC нужна короткая нейтраль чтобы переключиться в режим реверса.

            Схема:
            [BRAKE 250ms][GAP][реверс]
                              ↑ этот промежуток

            Когда увеличить:
            • Реверс включается через раз даже при большом RevBrake
            • ESC не успевает сбросить тормозной режим
            • Попробуй 150–200 мс

            Когда уменьшить:
            • Задержка перед реверсом слишком большая
            • Попробуй 80–100 мс

            По умолчанию 120 мс.
            """
        }
    }
}

struct MotorHelpButton: View {
    let help: MotorConfigHelp
    @State private var showPopover = false

    var body: some View {
        Button {
            showPopover.toggle()
        } label: {
            Image(systemName: "questionmark.circle")
                .font(.system(size: 10))
                .foregroundColor(.secondary)
        }
        .buttonStyle(.plain)
        .popover(isPresented: $showPopover, arrowEdge: .trailing) {
            ScrollView {
                VStack(alignment: .leading, spacing: 8) {
                    Text(help.title)
                        .font(.headline)
                    Divider()
                    Text(help.body)
                        .font(.caption)
                        .fixedSize(horizontal: false, vertical: true)
                }
                .padding()
                .frame(width: 320)
            }
            .frame(maxHeight: 400)
        }
    }
}

// MARK: - Small UI components

struct StatusRow: View {
    let label, value: String
    var body: some View {
        HStack {
            Text(label).font(.caption).foregroundColor(.secondary)
            Spacer()
            Text(value).font(.caption.monospaced())
        }
    }
}

struct StickIndicator: View {
    let x: Float
    let label: String
    var body: some View {
        HStack(spacing: 6) {
            Text(label).font(.caption2).frame(width: 12)
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    Capsule().fill(Color.gray.opacity(0.2)).frame(height: 6)
                    let w = geo.size.width
                    let pos = CGFloat((x + 1) / 2) * w
                    Circle().fill(.blue).frame(width: 8, height: 8)
                        .offset(x: pos - 4, y: -1)
                }
            }.frame(height: 8)
            Text(String(format: "%.2f", x)).font(.caption2).frame(width: 36)
        }
    }
}

struct TriggerBar: View {
    let value: Float
    let label: String
    let color: Color
    var body: some View {
        HStack(spacing: 6) {
            Text(label).font(.caption2).frame(width: 20)
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    Capsule().fill(Color.gray.opacity(0.2))
                    Capsule().fill(color).frame(width: geo.size.width * CGFloat(value))
                }
            }.frame(height: 6)
            Text("\(Int(value * 100))%").font(.caption2).frame(width: 32)
        }
    }
}
