import SwiftUI
import WebKit

struct WebRTCView: NSViewRepresentable {
    let urlString: String

    func makeNSView(context: Context) -> WKWebView {
        let config = WKWebViewConfiguration()

        // Allow camera/microphone for WebRTC
        config.mediaTypesRequiringUserActionForPlayback = []

        let webView = WKWebView(frame: .zero, configuration: config)
        webView.navigationDelegate = context.coordinator
        webView.allowsMagnification = false

        // Remove scroll bounce
        webView.enclosingScrollView?.hasVerticalScroller = false
        webView.enclosingScrollView?.hasHorizontalScroller = false

        load(urlString: urlString, into: webView)

        // Listen for reload requests
        NotificationCenter.default.addObserver(
            forName: .reloadStream,
            object: nil,
            queue: .main
        ) { _ in
            load(urlString: urlString, into: webView)
        }

        return webView
    }

    func updateNSView(_ webView: WKWebView, context: Context) {
        guard let current = webView.url?.absoluteString, current != urlString else { return }
        load(urlString: urlString, into: webView)
    }

    private func load(urlString: String, into webView: WKWebView) {
        guard let url = URL(string: urlString) else { return }
        let request = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData)
        webView.load(request)
    }

    func makeCoordinator() -> Coordinator { Coordinator() }

    class Coordinator: NSObject, WKNavigationDelegate {
        func webView(_ webView: WKWebView, didFail navigation: WKNavigation!, withError error: Error) {
            print("[WebRTC] Navigation failed: \(error.localizedDescription)")
        }

        func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
            print("[WebRTC] Failed to load: \(error.localizedDescription)")
            // Retry after 3 seconds
            DispatchQueue.main.asyncAfter(deadline: .now() + 3) {
                NotificationCenter.default.post(name: .reloadStream, object: nil)
            }
        }
    }
}
