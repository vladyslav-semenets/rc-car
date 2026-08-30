import SwiftUI
import WebKit

struct WebRTCView: NSViewRepresentable {
    let urlString: String

    func makeNSView(context: Context) -> WKWebView {
        let cfg = WKWebViewConfiguration()
        cfg.mediaTypesRequiringUserActionForPlayback = []
        cfg.allowsAirPlayForMediaPlayback = false

        // Enable JavaScript (modern API)
        let pagePrefs = WKWebpagePreferences()
        pagePrefs.allowsContentJavaScript = true
        cfg.defaultWebpagePreferences = pagePrefs

        let wv = WKWebView(frame: .zero, configuration: cfg)
        wv.navigationDelegate = context.coordinator
        wv.uiDelegate        = context.coordinator
        wv.allowsMagnification = false
        wv.wantsLayer = true
        wv.layer?.backgroundColor = NSColor.black.cgColor

        context.coordinator.webView = wv
        context.coordinator.urlString = urlString

        load(urlString, into: wv, coordinator: context.coordinator)

        NotificationCenter.default.addObserver(
            forName: .reloadStream, object: nil, queue: .main
        ) { _ in
            load(urlString, into: wv, coordinator: context.coordinator)
        }
        return wv
    }

    func updateNSView(_ wv: WKWebView, context: Context) {
        guard context.coordinator.urlString != urlString else { return }
        context.coordinator.urlString = urlString
        load(urlString, into: wv, coordinator: context.coordinator)
    }

    private func load(_ url: String, into wv: WKWebView, coordinator: Coordinator) {
        guard let u = URL(string: url) else { return }
        coordinator.lastError = nil
        coordinator.isLoading = true
        wv.load(URLRequest(url: u, cachePolicy: .reloadIgnoringLocalAndRemoteCacheData))
    }

    func makeCoordinator() -> Coordinator { Coordinator() }

    // MARK: - Coordinator

    class Coordinator: NSObject, WKNavigationDelegate, WKUIDelegate {
        weak var webView: WKWebView?
        var urlString: String = ""
        var lastError: String? = nil
        var isLoading: Bool = false
        private var retryTimer: Timer?

        func webView(_ wv: WKWebView, didFinish _: WKNavigation!) {
            isLoading = false
            lastError = nil
            retryTimer?.invalidate()
            retryTimer = nil
            print("[WebRTC] page loaded: \(wv.url?.absoluteString ?? "-")")
        }

        func webView(_ wv: WKWebView,
                     didFailProvisionalNavigation _: WKNavigation!,
                     withError error: Error) {
            handleError(wv: wv, error: error)
        }

        func webView(_ wv: WKWebView,
                     didFail _: WKNavigation!,
                     withError error: Error) {
            handleError(wv: wv, error: error)
        }

        private func handleError(wv: WKWebView, error: Error) {
            let msg = (error as NSError).localizedDescription
            isLoading = false
            lastError = msg
            print("[WebRTC] error: \(msg) — retry in 3s")
            scheduleRetry(wv: wv)
        }

        private func scheduleRetry(wv: WKWebView) {
            retryTimer?.invalidate()
            retryTimer = Timer.scheduledTimer(withTimeInterval: 3, repeats: false) { [weak self, weak wv] _ in
                guard let self, let wv else { return }
                guard let u = URL(string: self.urlString) else { return }
                self.isLoading = true
                wv.load(URLRequest(url: u, cachePolicy: .reloadIgnoringLocalAndRemoteCacheData))
            }
        }

        // Allow all media playback decisions automatically
        func webView(_ wv: WKWebView,
                     requestMediaCapturePermissionFor origin: WKSecurityOrigin,
                     initiatedByFrame frame: WKFrameInfo,
                     type: WKMediaCaptureType,
                     decisionHandler: @escaping (WKPermissionDecision) -> Void) {
            decisionHandler(.grant)
        }
    }
}

// MARK: - Overlay view (shown in ContentView)

struct StreamOverlay: View {
    let urlString: String
    @State private var showURL = false

    var body: some View {
        VStack(spacing: 8) {
            Image(systemName: "video.slash")
                .font(.system(size: 40))
                .foregroundColor(.secondary)
            Text("Connecting to stream…")
                .font(.caption)
                .foregroundColor(.secondary)
            Button(showURL ? urlString : "Show URL") {
                showURL.toggle()
            }
            .font(.caption2)
            Button("Open in Browser") {
                if let u = URL(string: urlString) { NSWorkspace.shared.open(u) }
            }
            .font(.caption2)
        }
    }
}

extension Notification.Name {
    static let reloadStream = Notification.Name("reloadStream")
}
