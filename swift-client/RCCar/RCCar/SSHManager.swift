import Foundation

@MainActor
class SSHManager: ObservableObject {
    @Published var mediaMTXStatus: MediaMTXStatus = .idle
    @Published var lastLog: String = ""

    enum MediaMTXStatus: Equatable {
        case idle, connecting, running, stopping, error(String)
    }

    // MARK: - Public (called from UI, already on MainActor)

    func startMediaMTX(host: String, user: String, password: String,
                       binaryPath: String = "/home/vladyslav/mediametx/mediamtx",
                       runDir: String = "/home/vladyslav/rc-car-repo") {
        guard mediaMTXStatus != .connecting && mediaMTXStatus != .running else { return }
        mediaMTXStatus = .connecting
        lastLog = "[SSH] connecting to \(user)@\(host)…"

        Task {
            // 1. Check if already running
            let checkOut = (try? await ssh(host: host, user: user, password: password,
                                           command: "pgrep -x mediamtx > /dev/null && echo RUNNING || echo STOPPED")) ?? ""
            if checkOut.contains("RUNNING") {
                mediaMTXStatus = .running
                lastLog = "[mediamtx] already running"
                return
            }

            // 2. Start
            let out = (try? await ssh(host: host, user: user, password: password,
                                      command: "cd \(runDir) && nohup \(binaryPath) > /tmp/mediamtx.log 2>&1 </dev/null & echo PID:$!")) ?? ""
            if out.contains("PID:") {
                let pid = out.components(separatedBy: "PID:").last?.trimmingCharacters(in: .whitespacesAndNewlines) ?? "?"
                mediaMTXStatus = .running
                lastLog = "[mediamtx] started (PID \(pid))"
            } else {
                let log = (try? await ssh(host: host, user: user, password: password,
                                          command: "tail -5 /tmp/mediamtx.log 2>/dev/null || echo 'no log'")) ?? ""
                mediaMTXStatus = .error("start failed")
                lastLog = "[mediamtx] failed: \(log.trimmingCharacters(in: .whitespacesAndNewlines))"
            }
        }
    }

    func stopMediaMTX(host: String, user: String, password: String) {
        guard mediaMTXStatus == .running || mediaMTXStatus == .connecting else { return }
        mediaMTXStatus = .stopping
        lastLog = "[mediamtx] stopping…"

        Task {
            _ = try? await ssh(host: host, user: user, password: password,
                               command: "pkill -x mediamtx; echo DONE")
            mediaMTXStatus = .idle
            lastLog = "[mediamtx] stopped"
        }
    }

    func testSSH(host: String, user: String, password: String) {
        mediaMTXStatus = .connecting
        lastLog = "[SSH] testing…"

        Task {
            do {
                let out = try await ssh(host: host, user: user, password: password,
                                        command: "bash -lc 'which mediamtx && echo BINARY_OK || echo BINARY_MISSING; pgrep -a mediamtx || echo not_running'")
                mediaMTXStatus = .idle
                lastLog = out.trimmingCharacters(in: .whitespacesAndNewlines)
                print("[SSH test full output]\n\(out)")
            } catch {
                mediaMTXStatus = .error(error.localizedDescription)
                lastLog = "FAILED: \(error.localizedDescription)"
            }
        }
    }

    // MARK: - SSH via expect (nonisolated so it runs off the main thread)

    nonisolated func ssh(host: String, user: String, password: String, command: String) async throws -> String {
        try await withCheckedThrowingContinuation { continuation in
            DispatchQueue.global(qos: .utility).async {
                let safePass = password
                    .replacingOccurrences(of: "\\", with: "\\\\")
                    .replacingOccurrences(of: "\"", with: "\\\"")
                    .replacingOccurrences(of: "[", with: "\\[")
                    .replacingOccurrences(of: "]", with: "\\]")
                let safeCmd = command
                    .replacingOccurrences(of: "\"", with: "\\\"")

                let script = """
set timeout 20
spawn ssh -o StrictHostKeyChecking=no \
          -o ConnectTimeout=8 \
          -o BatchMode=no \
          -o PreferredAuthentications=publickey,password \
          \(host.contains("@") ? host : "\(user)@\(host)") "\(safeCmd)"
expect {
    -re {(?i)password.*:} { send "\(safePass)\\r"; exp_continue }
    -re {\\(yes/no\\)}    { send "yes\\r"; exp_continue }
    eof                   {}
    timeout               { exit 2 }
}
"""
                let p = Process()
                p.executableURL = URL(fileURLWithPath: "/usr/bin/expect")
                p.arguments = ["-c", script]
                p.environment = ProcessInfo.processInfo.environment

                let outPipe = Pipe(); let errPipe = Pipe()
                p.standardOutput = outPipe; p.standardError = errPipe

                do {
                    try p.run(); p.waitUntilExit()
                    let raw = String(data: outPipe.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
                    let err = String(data: errPipe.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
                    // Strip 'spawn ...' line that expect echoes
                    let out = raw.components(separatedBy: "\n").filter { !$0.hasPrefix("spawn ") }.joined(separator: "\n")
                    print("[SSH] exit=\(p.terminationStatus) out=\(out.prefix(300))")
                    if !err.isEmpty { print("[SSH] err=\(err.prefix(200))") }
                    continuation.resume(returning: out)
                } catch {
                    continuation.resume(throwing: error)
                }
            }
        }
    }
}
