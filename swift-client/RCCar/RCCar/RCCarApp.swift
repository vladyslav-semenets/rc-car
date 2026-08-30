import SwiftUI
import AppKit

@main
struct RCCarApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate
    @StateObject private var car = CarController()

    var body: some Scene {
        WindowGroup {
            ContentView(car: car, ssh: appDelegate.ssh)
        }
        .windowStyle(.hiddenTitleBar)
        .windowResizability(.contentSize)
        .commands { CommandGroup(replacing: .newItem) {} }

        Settings { SettingsView(car: car) }
    }
}

// MARK: - AppDelegate handles mediamtx lifecycle

@MainActor
class AppDelegate: NSObject, NSApplicationDelegate {
    let ssh = SSHManager()

    private var piIP:         String { UserDefaults.standard.string(forKey: "piIP")          ?? "100.108.40.34" }
    private var sshUser:      String { UserDefaults.standard.string(forKey: "sshUser")       ?? "vladyslav" }
    private var sshPass:      String { UserDefaults.standard.string(forKey: "sshPassword")   ?? "210716hiq" }
    private var mediamtxPath: String { UserDefaults.standard.string(forKey: "mediamtxPath")  ?? "/home/vladyslav/mediametx/mediamtx" }
    private var mediamtxDir:  String { UserDefaults.standard.string(forKey: "mediamtxConfig") ?? "/home/vladyslav/rc-car-repo" }

    func applicationDidFinishLaunching(_ notification: Notification) {
        ssh.startMediaMTX(host: piIP, user: sshUser, password: sshPass,
                          binaryPath: mediamtxPath, runDir: mediamtxDir)
    }

    func applicationWillTerminate(_ notification: Notification) {
        guard ssh.mediaMTXStatus == .running else { return }
        let semaphore = DispatchSemaphore(value: 0)
        Task.detached { [self] in
            _ = try? await ssh.ssh(host: piIP, user: sshUser, password: sshPass,
                                   command: "pkill -x mediamtx; echo DONE")
            semaphore.signal()
        }
        _ = semaphore.wait(timeout: .now() + 8)
        print("[SSH] mediamtx killed, exiting")
    }
}
