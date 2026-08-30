package com.rccar.android

import android.content.Context
import android.content.SharedPreferences
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

class CarController(
    context: Context,
    private val prefs: SharedPreferences
) {
    val ble = BleManager(context)
    val udp = UDPManager()

    // Transport selection ("BLE" for Ground TX LoRa, "UDP" for direct 4G/Wi-Fi)
    var transportMode = mutableStateOf("BLE")
    
    // Safety State Machine
    var isGamepadConnected = mutableStateOf(false)
    var isDriveModeActive = mutableStateOf(false)

    // Vehicle control state
    var transmissionSpeed = mutableIntStateOf(1)
    var degreeOfTurns = mutableFloatStateOf(86f)
    var currentSteeringAngle = mutableFloatStateOf(86f)
    var currentGimbalYaw = mutableFloatStateOf(0f)
    var pitchAngle = mutableIntStateOf(0)
    var isSteeringCalibrationOn = mutableStateOf(false)
    var isUnstuckActive = mutableStateOf(false)
    var steeringExpo = mutableFloatStateOf(prefs.getFloat("steeringExpo", 0.5f))
    var gimbalExpo = mutableFloatStateOf(prefs.getFloat("gimbalExpo", 0.3f))
    var highSpeedSteeringDamping = mutableFloatStateOf(prefs.getFloat("highSpeedSteeringDamping", 0.50f))

    // Motor configuration
    var motorConfigEnabled = mutableStateOf(prefs.getBoolean("motorConfigEnabled", true))
    var cfg_frontTrimUs      = mutableIntStateOf(prefs.getInt("cfg_frontTrimUs",      15))
    var cfg_rearTrimUs       = mutableIntStateOf(prefs.getInt("cfg_rearTrimUs",        0))
    var cfg_slewMaxUs        = mutableIntStateOf(prefs.getInt("cfg_slewMaxUs",         25))
    var cfg_dirChangeHoldMs  = mutableIntStateOf(prefs.getInt("cfg_dirChangeHoldMs",  150))
    var cfg_frontLagSteps    = mutableIntStateOf(prefs.getInt("cfg_frontLagSteps",      3))
    var cfg_reverseBrakeMs   = mutableIntStateOf(prefs.getInt("cfg_reverseBrakeMs",   250))
    var cfg_reverseNeutralMs = mutableIntStateOf(prefs.getInt("cfg_reverseNeutralMs", 120))

    // Downlink Telemetry State
    var downlinkPacketCount = mutableIntStateOf(0)
    var lastTelemetryTimeMs = mutableStateOf(0L)

    enum class DriveAction {
        STOPPED,
        FORWARD,
        BACKWARD
    }
    private var currentDriveAction = DriveAction.STOPPED
    private var currentThrottlePercent = 0

    private val scope = CoroutineScope(Dispatchers.IO)
    private var heartbeatJob: Job? = null
    private var controlStreamJob: Job? = null

    init {
        ble.onTelemetryReceived = { bytes ->
            downlinkPacketCount.intValue++
            lastTelemetryTimeMs.value = System.currentTimeMillis()
        }
    }

    fun updateSafetyState(gamepadConnected: Boolean) {
        isGamepadConnected.value = gamepadConnected
        val isLinkReady = when (transportMode.value) {
            "BLE" -> ble.connectionState.value == BleManager.State.CONNECTED
            "UDP" -> udp.isConnected.value
            else -> ble.connectionState.value == BleManager.State.CONNECTED || udp.isConnected.value
        }
        isDriveModeActive.value = isGamepadConnected.value && isLinkReady
    }

    // MARK: - Connection

    fun connectBle() {
        ble.startScan()
        startHeartbeat()
        startControlStream()
    }

    fun connectUdp(host: String, port: Int) {
        udp.connect(host, port)
        startHeartbeat()
        startControlStream()
        scope.launch {
            delay(500)
            sendMotorConfig()
        }
    }

    fun disconnect() {
        controlStreamJob?.cancel()
        heartbeatJob?.cancel()
        currentDriveAction = DriveAction.STOPPED
        currentThrottlePercent = 0
        ble.disconnect()
        udp.disconnect()
        isDriveModeActive.value = false
    }

    // MARK: - Car commands

    fun initCar() {
        currentSteeringAngle.floatValue = degreeOfTurns.floatValue
        send(1, p1 = 50f, p2 = degreeOfTurns.floatValue)
    }

    fun turnTo(d: Float) {
        currentSteeringAngle.floatValue = d
        if (ble.connectionState.value == BleManager.State.CONNECTED) {
            sendCompactPacket()
        } else {
            send(4, p1 = d)
        }
    }

    fun changeDegrees() {
        send(2, p1 = degreeOfTurns.floatValue)
    }

    fun resetTurns() {
        currentSteeringAngle.floatValue = degreeOfTurns.floatValue
        if (ble.connectionState.value == BleManager.State.CONNECTED) {
            sendCompactPacket()
        } else {
            scope.launch {
                repeat(3) {
                    send(3, p1 = degreeOfTurns.floatValue)
                    delay(15)
                }
            }
        }
    }

    fun neutral() {
        currentDriveAction = DriveAction.STOPPED
        currentThrottlePercent = 0
        if (ble.connectionState.value == BleManager.State.CONNECTED) {
            sendCompactPacket()
        } else {
            scope.launch {
                repeat(3) {
                    send(7)
                    delay(15)
                }
            }
        }
    }

    fun unstuck() {
        isUnstuckActive.value = true
        if (ble.connectionState.value == BleManager.State.CONNECTED) {
            sendCompactPacket()
        } else {
            send(16, p1 = degreeOfTurns.floatValue)
        }
        scope.launch {
            delay(1000)
            isUnstuckActive.value = false
        }
    }

    fun startCamera()          { send(8) }
    fun stopCamera()           { send(9) }

    fun resetGimbal() {
        currentGimbalYaw.floatValue = 0f
        pitchAngle.intValue = 0
        if (ble.connectionState.value == BleManager.State.CONNECTED) {
            sendCompactPacket()
        } else {
            scope.launch {
                repeat(3) {
                    send(12)
                    delay(15)
                }
            }
        }
    }

    fun gimbalPitch() {
        if (ble.connectionState.value == BleManager.State.CONNECTED) {
            sendCompactPacket()
        } else {
            send(11, p1 = pitchAngle.intValue.toFloat())
        }
    }

    fun gimbalYaw(deg: Float) {
        currentGimbalYaw.floatValue = deg
        if (ble.connectionState.value == BleManager.State.CONNECTED) {
            sendCompactPacket()
        } else {
            send(10, p1 = deg)
        }
    }

    fun forward(trigger: Float) {
        val speed = limitSpeed((trigger * 100).toInt())
        if (speed > 0) {
            currentThrottlePercent = speed
            currentDriveAction = DriveAction.FORWARD
            if (ble.connectionState.value == BleManager.State.CONNECTED) {
                sendCompactPacket()
            } else {
                send(5, p1 = speed.toFloat())
            }
        } else {
            neutral()
        }
    }

    fun backward(trigger: Float) {
        val speed = (trigger * 100).toInt()
        if (speed > 0) {
            currentThrottlePercent = -speed
            currentDriveAction = DriveAction.BACKWARD
            if (ble.connectionState.value == BleManager.State.CONNECTED) {
                sendCompactPacket()
            } else {
                send(6, p1 = speed.toFloat())
            }
        } else {
            neutral()
        }
    }

    fun toggleSteeringCalibration() {
        val on = isSteeringCalibrationOn.value
        isSteeringCalibrationOn.value = !on
        if (ble.connectionState.value == BleManager.State.CONNECTED) {
            sendCompactPacket()
        } else {
            send(if (on) 14 else 13)
        }
    }

    fun speedUp()   { if (transmissionSpeed.intValue < 8) transmissionSpeed.intValue++ }
    fun speedDown() { if (transmissionSpeed.intValue > 1) transmissionSpeed.intValue-- }

    // MARK: - Stick mapping (Speed-Sensitive Dynamic Dual-Rates)

    fun stickToSteering(v: Float): Float {
        val curved = applyExpo(v, steeringExpo.floatValue)
        // Dynamic Speed-Sensitive Steering:
        // Dampen steering throw progressively as throttle / gear increases to prevent rollovers
        val throttleFraction = (Math.abs(currentThrottlePercent) / 100f).coerceIn(0f, 1f)
        val gearFraction = (transmissionSpeed.intValue / 8f).coerceIn(0f, 1f)
        val speedFactor = maxOf(throttleFraction, gearFraction)
        
        // Non-linear power curve so damping kicks in firmly as soon as the car is moving fast
        val damping = (Math.pow(speedFactor.toDouble(), 0.75).toFloat() * highSpeedSteeringDamping.floatValue)
            .coerceIn(0f, 0.85f)
            
        val effectiveTurn = curved * (1.0f - damping)

        return if (effectiveTurn >= 0) degreeOfTurns.floatValue * (1f - effectiveTurn)
        else degreeOfTurns.floatValue + (-effectiveTurn) * (140f - degreeOfTurns.floatValue)
    }

    fun stickToGimbalYaw(v: Float): Float = applyExpo(v, gimbalExpo.floatValue) * 90f

    fun saveExpo() {
        prefs.edit()
            .putFloat("steeringExpo", steeringExpo.floatValue)
            .putFloat("gimbalExpo", gimbalExpo.floatValue)
            .putFloat("highSpeedSteeringDamping", highSpeedSteeringDamping.floatValue)
            .apply()
    }

    fun sendMotorConfig() {
        if (motorConfigEnabled.value) {
            send(15,
                p1 = cfg_frontTrimUs.intValue.toFloat(),
                p2 = cfg_rearTrimUs.intValue.toFloat(),
                p3 = cfg_slewMaxUs.intValue.toFloat(),
                p4 = cfg_dirChangeHoldMs.intValue.toFloat(),
                p5 = cfg_frontLagSteps.intValue.toFloat(),
                p6 = cfg_reverseBrakeMs.intValue.toFloat(),
                p7 = cfg_reverseNeutralMs.intValue.toFloat()
            )
        } else {
            send(15, p1 = 0f, p2 = 0f, p3 = 500f, p4 = 0f, p5 = 0f, p6 = 0f, p7 = 0f)
        }
    }

    fun saveMotorConfig() {
        prefs.edit()
            .putBoolean("motorConfigEnabled",  motorConfigEnabled.value)
            .putInt("cfg_frontTrimUs",          cfg_frontTrimUs.intValue)
            .putInt("cfg_rearTrimUs",           cfg_rearTrimUs.intValue)
            .putInt("cfg_slewMaxUs",            cfg_slewMaxUs.intValue)
            .putInt("cfg_dirChangeHoldMs",      cfg_dirChangeHoldMs.intValue)
            .putInt("cfg_frontLagSteps",        cfg_frontLagSteps.intValue)
            .putInt("cfg_reverseBrakeMs",       cfg_reverseBrakeMs.intValue)
            .putInt("cfg_reverseNeutralMs",     cfg_reverseNeutralMs.intValue)
            .apply()
    }

    fun resetMotorConfig() {
        cfg_frontTrimUs.intValue      = 15
        cfg_rearTrimUs.intValue       = 0
        cfg_slewMaxUs.intValue        = 25
        cfg_dirChangeHoldMs.intValue  = 150
        cfg_frontLagSteps.intValue    = 3
        cfg_reverseBrakeMs.intValue   = 250
        cfg_reverseNeutralMs.intValue = 120
    }

    // MARK: - Private

    private fun applyExpo(v: Float, expo: Float): Float {
        val e = expo.coerceIn(0f, 1f)
        return (1f - e) * v + e * v * v * v
    }

    private fun limitSpeed(speed: Int): Int {
        val limits = listOf(1 to 20, 2 to 35, 3 to 45, 5 to 65, 6 to 85, 8 to 100)
        for ((level, max) in limits.reversed()) {
            if (transmissionSpeed.intValue >= level) return speed.coerceAtMost(max)
        }
        return speed.coerceAtMost(20)
    }

    private fun send(cmd: Int, p1: Float = 0f, p2: Float = 0f, p3: Float = 0f,
                     p4: Float = 0f, p5: Float = 0f, p6: Float = 0f, p7: Float = 0f) {
        val data = MAVLink.commandLong(cmd.toUShort(), p1, p2, p3, p4, p5, p6, p7)

        if (ble.connectionState.value == BleManager.State.CONNECTED) {
            ble.sendMavlinkBytes(data)
        }
        if (udp.isConnected.value) {
            udp.send(data)
        }
    }

    private fun sendCompactPacket() {
        val bytes = RcPacket.encode(
            throttlePercent = currentThrottlePercent,
            steeringAngleDeg = currentSteeringAngle.floatValue,
            gimbalYawDeg = currentGimbalYaw.floatValue,
            gimbalPitchDeg = pitchAngle.intValue,
            gyroOn = isSteeringCalibrationOn.value,
            unstuckOn = isUnstuckActive.value,
            gearLevel = transmissionSpeed.intValue
        )
        ble.sendMavlinkBytes(bytes)
    }

    private fun startHeartbeat() {
        heartbeatJob?.cancel()
        heartbeatJob = scope.launch {
            while (true) {
                if (udp.isConnected.value) {
                    val hb = MAVLink.heartbeat()
                    udp.send(hb)
                }
                delay(100)
            }
        }
    }

    private fun startControlStream() {
        controlStreamJob?.cancel()
        controlStreamJob = scope.launch {
            while (true) {
                if (ble.connectionState.value == BleManager.State.CONNECTED) {
                    sendCompactPacket()
                } else if (udp.isConnected.value) {
                    when (currentDriveAction) {
                        DriveAction.FORWARD -> send(5, p1 = currentThrottlePercent.toFloat())
                        DriveAction.BACKWARD -> send(6, p1 = (-currentThrottlePercent).toFloat())
                        DriveAction.STOPPED -> { /* idle */ }
                    }
                }
                delay(20) // 50 Hz streaming loop!
            }
        }
    }
}
