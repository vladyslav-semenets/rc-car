import SwiftUI

struct SettingsView: View {
    @AppStorage("piIP") private var piIP = "100.108.40.34"
    @AppStorage("webrtcPort") private var webrtcPort = "8889"
    @AppStorage("clientBinaryPath") private var clientBinaryPath = ""
    @AppStorage("clientWorkingDir") private var clientWorkingDir = ""

    var body: some View {
        Form {
            Section("Raspberry Pi") {
                TextField("Pi Tailscale IP", text: $piIP)
                    .textFieldStyle(.roundedBorder)
                TextField("WebRTC Port", text: $webrtcPort)
                    .textFieldStyle(.roundedBorder)
            }

            Section("Client Binary") {
                HStack {
                    TextField("Path to rccarclient", text: $clientBinaryPath)
                        .textFieldStyle(.roundedBorder)
                    Button("Browse...") {
                        selectFile(title: "Select rccarclient binary") { path in
                            clientBinaryPath = path
                            clientWorkingDir = (path as NSString).deletingLastPathComponent
                        }
                    }
                }

                HStack {
                    TextField("Working directory", text: $clientWorkingDir)
                        .textFieldStyle(.roundedBorder)
                    Button("Browse...") {
                        selectDirectory(title: "Select working directory") { path in
                            clientWorkingDir = path
                        }
                    }
                }

                Text("Working dir should contain the .env file")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }

            Section("Stream URL Preview") {
                Text("http://\(piIP):\(webrtcPort)/front")
                    .font(.system(.caption, design: .monospaced))
                    .foregroundColor(.secondary)
            }
        }
        .formStyle(.grouped)
        .padding()
        .frame(width: 480)
    }

    private func selectFile(title: String, completion: @escaping (String) -> Void) {
        let panel = NSOpenPanel()
        panel.title = title
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        if panel.runModal() == .OK, let url = panel.url {
            completion(url.path)
        }
    }

    private func selectDirectory(title: String, completion: @escaping (String) -> Void) {
        let panel = NSOpenPanel()
        panel.title = title
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        if panel.runModal() == .OK, let url = panel.url {
            completion(url.path)
        }
    }
}
