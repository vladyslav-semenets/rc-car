import Foundation
import Darwin   // POSIX sockets

/// UDP sender using raw POSIX sockets — behaves exactly like the C client.
class UDPManager: ObservableObject {
    @Published var isConnected = false
    @Published var bytesSent: Int = 0

    private var sockfd: Int32 = -1
    private var remoteAddr = sockaddr_in()
    private let sendQueue = DispatchQueue(label: "com.rccar.udp.send")

    // MARK: - Public

    func connect(host: String, port: UInt16) {
        disconnect()

        let fd = socket(AF_INET, SOCK_DGRAM, 0)
        guard fd >= 0 else {
            print("[UDP] socket() failed: \(String(cString: strerror(errno)))")
            return
        }

        var addr = sockaddr_in()
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_port   = port.bigEndian

        guard inet_pton(AF_INET, host, &addr.sin_addr) == 1 else {
            print("[UDP] inet_pton() failed for host '\(host)'")
            close(fd)
            return
        }

        sockfd      = fd
        remoteAddr  = addr

        print("[UDP] socket ready → \(host):\(port)")
        DispatchQueue.main.async { self.isConnected = true }
    }

    func send(_ data: Data) {
        guard sockfd >= 0 else { return }
        sendQueue.async { [self] in
            var addr = remoteAddr
            let sent = data.withUnsafeBytes { ptr in
                withUnsafeMutablePointer(to: &addr) { addrPtr in
                    addrPtr.withMemoryRebound(to: sockaddr.self, capacity: 1) { saPtr in
                        sendto(sockfd, ptr.baseAddress, data.count, 0, saPtr, socklen_t(MemoryLayout<sockaddr_in>.size))
                    }
                }
            }
            if sent < 0 {
                print("[UDP] sendto() failed: \(String(cString: strerror(errno)))")
            } else {
                DispatchQueue.main.async { self.bytesSent += sent }
            }
        }
    }

    func disconnect() {
        if sockfd >= 0 { close(sockfd); sockfd = -1 }
        DispatchQueue.main.async {
            self.isConnected = false
            self.bytesSent   = 0
        }
    }
}
