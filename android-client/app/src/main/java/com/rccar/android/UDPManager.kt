package com.rccar.android

import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress

class UDPManager {
    var isConnected = mutableStateOf(false)
    var bytesSent = mutableIntStateOf(0)

    private var socket: DatagramSocket? = null
    private var remoteAddress: InetAddress? = null
    private var remotePort: Int = 8565
    private val scope = CoroutineScope(Dispatchers.IO)

    fun connect(host: String, port: Int) {
        scope.launch {
            try {
                socket?.close()
                socket = DatagramSocket()
                remoteAddress = InetAddress.getByName(host)
                remotePort = port
                isConnected.value = true
            } catch (e: Exception) {
                isConnected.value = false
            }
        }
    }

    fun send(data: ByteArray) {
        val addr = remoteAddress ?: return
        val sock = socket ?: return
        scope.launch {
            try {
                val packet = DatagramPacket(data, data.size, addr, remotePort)
                sock.send(packet)
                bytesSent.intValue += data.size
            } catch (_: Exception) {}
        }
    }

    fun disconnect() {
        socket?.close()
        socket = null
        isConnected.value = false
    }
}
