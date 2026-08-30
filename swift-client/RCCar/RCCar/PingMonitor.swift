import Foundation
import Combine

class PingMonitor: ObservableObject {
    @Published var latency: String = "—"
    @Published var isReachable: Bool = false

    private var timer: Timer?
    private var currentHost: String = ""
    private var isRunning = false

    func start(host: String) {
        currentHost = host
        timer?.invalidate()
        timer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            self?.ping()
        }
        ping()
    }

    func stop() {
        timer?.invalidate()
        timer = nil
        DispatchQueue.main.async {
            self.latency = "—"
            self.isReachable = false
        }
    }

    func updateHost(_ host: String) {
        guard host != currentHost else { return }
        currentHost = host
        ping()
    }

    private func ping() {
        guard !currentHost.isEmpty, !isRunning else { return }
        isRunning = true
        let host = currentHost

        DispatchQueue.global(qos: .utility).async { [weak self] in
            let p = Process()
            p.executableURL = URL(fileURLWithPath: "/sbin/ping")
            p.arguments = ["-c", "1", "-t", "1", host]

            let pipe = Pipe()
            p.standardOutput = pipe
            p.standardError = Pipe()

            do {
                try p.run()
                p.waitUntilExit()
                let raw = String(data: pipe.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""

                // Parse "time=12.3 ms" or "time = 12.3 ms"
                var ms: String? = nil
                if let range = raw.range(of: #"time[= ]+(\d+\.?\d*)\s*ms"#, options: .regularExpression) {
                    let match = String(raw[range])
                    // Extract just the number
                    if let numRange = match.range(of: #"\d+\.?\d+"#, options: .regularExpression) {
                        ms = String(match[numRange])
                    }
                }

                DispatchQueue.main.async {
                    self?.isRunning = false
                    if let ms {
                        self?.latency = "\(ms) ms"
                        self?.isReachable = true
                    } else {
                        self?.latency = "timeout"
                        self?.isReachable = false
                    }
                }
            } catch {
                DispatchQueue.main.async {
                    self?.isRunning = false
                    self?.latency = "error"
                    self?.isReachable = false
                }
            }
        }
    }
}
