import Foundation
import Combine

class ClientManager: ObservableObject {
    @Published var isRunning = false
    @Published var logs: [String] = []

    private var process: Process?
    private let maxLogs = 200

    var clientBinaryPath: String {
        get { UserDefaults.standard.string(forKey: "clientBinaryPath") ?? "" }
        set { UserDefaults.standard.set(newValue, forKey: "clientBinaryPath") }
    }
    var clientWorkingDir: String {
        get { UserDefaults.standard.string(forKey: "clientWorkingDir") ?? "" }
        set { UserDefaults.standard.set(newValue, forKey: "clientWorkingDir") }
    }

    func start() {
        guard !isRunning else { return }

        let binaryPath = clientBinaryPath.isEmpty
            ? defaultBinaryPath()
            : clientBinaryPath

        guard FileManager.default.fileExists(atPath: binaryPath) else {
            appendLog("❌ Binary not found: \(binaryPath)")
            appendLog("   Set path in Settings")
            return
        }

        let workDir = clientWorkingDir.isEmpty
            ? (binaryPath as NSString).deletingLastPathComponent
            : clientWorkingDir

        let p = Process()
        p.executableURL = URL(fileURLWithPath: binaryPath)
        p.currentDirectoryURL = URL(fileURLWithPath: workDir)

        // Pipe stdout + stderr
        let pipe = Pipe()
        p.standardOutput = pipe
        p.standardError = pipe

        pipe.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let data = handle.availableData
            guard !data.isEmpty, let text = String(data: data, encoding: .utf8) else { return }
            DispatchQueue.main.async {
                text.components(separatedBy: "\n")
                    .filter { !$0.isEmpty }
                    .forEach { self?.appendLog($0) }
            }
        }

        p.terminationHandler = { [weak self] _ in
            DispatchQueue.main.async {
                self?.isRunning = false
                self?.appendLog("⏹ Client stopped (exit code: \(p.terminationStatus))")
            }
        }

        do {
            try p.run()
            process = p
            isRunning = true
            appendLog("▶ Client started (PID \(p.processIdentifier))")
            appendLog("  Binary: \(binaryPath)")
        } catch {
            appendLog("❌ Failed to start: \(error.localizedDescription)")
        }
    }

    func stop() {
        guard let p = process, p.isRunning else {
            isRunning = false
            return
        }
        p.interrupt()  // SIGINT — graceful shutdown
        DispatchQueue.main.asyncAfter(deadline: .now() + 1) {
            if p.isRunning { p.terminate() }
        }
        process = nil
        isRunning = false
        appendLog("⏹ Client stopped by user")
    }

    private func appendLog(_ line: String) {
        let timestamp = DateFormatter.timeOnly.string(from: Date())
        logs.append("[\(timestamp)] \(line)")
        if logs.count > maxLogs {
            logs.removeFirst(logs.count - maxLogs)
        }
    }

    private func defaultBinaryPath() -> String {
        let home = NSHomeDirectory()
        let candidates = [
            home + "/rc-car-repo/client/c/cmake-build-debug/rccarclient",
            home + "/Documents/rc-car-repo/client/c/cmake-build-debug/rccarclient",
            home + "/Desktop/rc-car-repo/client/c/cmake-build-debug/rccarclient",
        ]
        return candidates.first { FileManager.default.fileExists(atPath: $0) } ?? ""
    }
}

private extension DateFormatter {
    static let timeOnly: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss"
        return f
    }()
}
