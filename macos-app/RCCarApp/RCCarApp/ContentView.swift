import SwiftUI

struct ContentView: View {
    @StateObject private var clientManager = ClientManager()
    @AppStorage("piIP") private var piIP = "100.108.40.34"
    @AppStorage("webrtcPort") private var webrtcPort = "8889"

    var streamURL: String {
        "http://\(piIP):\(webrtcPort)/front"
    }

    var body: some View {
        HStack(spacing: 0) {
            // ── WebRTC Stream ─────────────────────────────────────────────
            WebRTCView(urlString: streamURL)
                .frame(minWidth: 800, minHeight: 450)

            // ── Side Panel ────────────────────────────────────────────────
            VStack(alignment: .leading, spacing: 16) {
                // Status
                GroupBox("Connection") {
                    VStack(alignment: .leading, spacing: 8) {
                        StatusRow(label: "Pi IP", value: piIP)
                        StatusRow(label: "Stream", value: "\(piIP):\(webrtcPort)")
                        HStack {
                            Circle()
                                .fill(clientManager.isRunning ? Color.green : Color.red)
                                .frame(width: 10, height: 10)
                            Text(clientManager.isRunning ? "Client running" : "Client stopped")
                                .font(.caption)
                        }
                    }
                    .padding(4)
                }

                // Controls
                GroupBox("Client") {
                    VStack(spacing: 8) {
                        Button(clientManager.isRunning ? "Stop Client" : "Start Client") {
                            if clientManager.isRunning {
                                clientManager.stop()
                            } else {
                                clientManager.start()
                            }
                        }
                        .buttonStyle(.borderedProminent)
                        .tint(clientManager.isRunning ? .red : .green)
                        .frame(maxWidth: .infinity)

                        Button("Reload Stream") {
                            NotificationCenter.default.post(name: .reloadStream, object: nil)
                        }
                        .frame(maxWidth: .infinity)
                    }
                    .padding(4)
                }

                // Log
                GroupBox {
                    HStack {
                        Text("Log").font(.headline)
                        Spacer()
                        Button {
                            NSPasteboard.general.clearContents()
                            NSPasteboard.general.setString(
                                clientManager.logs.joined(separator: "\n"),
                                forType: .string
                            )
                        } label: {
                            Image(systemName: "doc.on.doc")
                        }
                        .buttonStyle(.borderless)
                        .help("Copy logs")
                    }
                    ScrollViewReader { proxy in
                        ScrollView {
                            LazyVStack(alignment: .leading, spacing: 2) {
                                ForEach(clientManager.logs.indices, id: \.self) { i in
                                    Text(clientManager.logs[i])
                                        .font(.system(size: 10, design: .monospaced))
                                        .foregroundColor(.secondary)
                                        .id(i)
                                }
                            }
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding(4)
                        }
                        .onChange(of: clientManager.logs.count) { _, count in
                            if count > 0 {
                                proxy.scrollTo(count - 1, anchor: .bottom)
                            }
                        }
                    }
                }
                .frame(maxHeight: .infinity)

                Spacer()

                // Settings link
                SettingsLink {
                    Text("Settings...")
                }
                .frame(maxWidth: .infinity, alignment: .trailing)
            }
            .padding()
            .frame(width: 220)
            .background(.background)
        }
        .onAppear {
            if !clientManager.clientBinaryPath.isEmpty {
                clientManager.start()
            }
        }
        .onDisappear {
            clientManager.stop()
        }
    }
}

struct StatusRow: View {
    let label: String
    let value: String

    var body: some View {
        HStack {
            Text(label)
                .font(.caption)
                .foregroundColor(.secondary)
            Spacer()
            Text(value)
                .font(.caption.monospaced())
        }
    }
}

extension Notification.Name {
    static let reloadStream = Notification.Name("reloadStream")
}
