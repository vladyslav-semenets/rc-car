package com.rccar.android

import androidx.compose.runtime.mutableStateOf
import kotlinx.coroutines.*

class PingMonitor {

    val latency   = mutableStateOf("—")
    val isReachable = mutableStateOf(false)

    private var job: Job? = null
    private var host = ""
    private val scope = CoroutineScope(Dispatchers.IO)

    fun start(h: String) {
        host = h
        job?.cancel()
        job = scope.launch {
            while (isActive) {
                ping()
                delay(1000)
            }
        }
    }

    fun stop() { job?.cancel() }

    fun updateHost(h: String) { host = h }

    private fun ping() {
        try {
            val proc = Runtime.getRuntime().exec("/system/bin/ping -c 1 -W 1 $host")
            val out  = proc.inputStream.bufferedReader().readText()
            proc.waitFor()

            val ms = Regex("time[= ]+(\\d+\\.?\\d*)\\s*ms").find(out)?.groupValues?.get(1)
            if (ms != null) {
                latency.value   = "${ms.toFloat().toInt()} ms"
                isReachable.value = true
            } else {
                latency.value   = "—"
                isReachable.value = false
            }
        } catch (e: Exception) {
            latency.value   = "—"
            isReachable.value = false
        }
    }
}
