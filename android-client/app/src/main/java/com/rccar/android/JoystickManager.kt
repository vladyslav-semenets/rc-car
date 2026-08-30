package com.rccar.android

import android.content.Context
import android.hardware.input.InputManager
import android.os.Build
import android.os.VibrationEffect
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import kotlin.math.abs

class JoystickManager(context: Context) {

    // Observable state
    var isConnected = mutableStateOf(false)
    var controllerName = mutableStateOf("No controller")
    var leftStickX = mutableFloatStateOf(0f)
    var rightStickX = mutableFloatStateOf(0f)
    var rightTrigger = mutableFloatStateOf(0f)
    var leftTrigger = mutableFloatStateOf(0f)

    // Callbacks → CarController
    var onSteeringChanged: ((Float) -> Unit)? = null
    var onSteeringReleased: (() -> Unit)? = null
    var onGimbalYawChanged: ((Float) -> Unit)? = null
    var onGimbalYawReleased: (() -> Unit)? = null
    var onForward: ((Float) -> Unit)? = null
    var onBackward: ((Float) -> Unit)? = null
    var onStop: (() -> Unit)? = null
    var onButton: ((Button) -> Unit)? = null

    enum class Button {
        DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT,
        A, B, X, Y, L1, R1, L3, R3
    }

    private val stickDeadzone = 0.08f
    private val triggerDeadzone = 0.07f

    private var gamepadDevice: InputDevice? = null
    private val inputManager = context.getSystemService(Context.INPUT_SERVICE) as InputManager

    // D-pad state from HAT axes
    private var lastHatX = 0f
    private var lastHatY = 0f

    // Internal motion tracking to ensure clean transitions
    private var wasSteering = false
    private var wasGimbalTurning = false
    private var wasDriving = false

    private val deviceListener = object : InputManager.InputDeviceListener {
        override fun onInputDeviceAdded(deviceId: Int) {
            InputDevice.getDevice(deviceId)?.let { device ->
                if (isGamepad(device) && gamepadDevice == null) connect(device)
            }
        }
        override fun onInputDeviceRemoved(deviceId: Int) {
            if (gamepadDevice?.id == deviceId) {
                gamepadDevice = null
                isConnected.value = false
                controllerName.value = "No controller"
                onStop?.invoke()
                stopVibration()
            }
        }
        override fun onInputDeviceChanged(deviceId: Int) {}
    }

    init {
        inputManager.registerInputDeviceListener(deviceListener, null)
        // Auto-connect to already paired gamepad
        InputDevice.getDeviceIds().toList().mapNotNull { InputDevice.getDevice(it) }
            .firstOrNull { isGamepad(it) }
            ?.let { connect(it) }
    }

    private fun isGamepad(device: InputDevice): Boolean {
        val src = device.sources
        return (src and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD) ||
               (src and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK)
    }

    private fun connect(device: InputDevice) {
        gamepadDevice = device
        isConnected.value = true
        controllerName.value = device.name
    }

    // Called from Activity.dispatchGenericMotionEvent
    fun onMotionEvent(event: MotionEvent): Boolean {
        val src = event.source
        if (src and InputDevice.SOURCE_JOYSTICK != InputDevice.SOURCE_JOYSTICK &&
            src and InputDevice.SOURCE_GAMEPAD != InputDevice.SOURCE_GAMEPAD) return false

        // ── 1. Left stick X → steering (with deadband normalization) ──────────
        val rawLx = event.getAxisValue(MotionEvent.AXIS_X)
        val lx = if (abs(rawLx) > stickDeadzone) {
            val sign = if (rawLx > 0) 1f else -1f
            sign * ((abs(rawLx) - stickDeadzone) / (1f - stickDeadzone))
        } else 0f
        leftStickX.floatValue = lx

        if (abs(lx) > 0.001f) {
            wasSteering = true
            onSteeringChanged?.invoke(lx)
            startVibration((abs(lx) * 0.3f * 255).toInt())
        } else if (wasSteering) {
            wasSteering = false
            onSteeringReleased?.invoke()
            if (rightTrigger.floatValue <= 0.001f && leftTrigger.floatValue <= 0.001f) stopVibration()
        }

        // ── 2. Right stick X → gimbal yaw ─────────────────────────────────────
        val rawRx = event.getAxisValue(MotionEvent.AXIS_Z)
        val rx = if (abs(rawRx) > stickDeadzone) {
            val sign = if (rawRx > 0) 1f else -1f
            sign * ((abs(rawRx) - stickDeadzone) / (1f - stickDeadzone))
        } else 0f
        rightStickX.floatValue = rx

        if (abs(rx) > 0.001f) {
            wasGimbalTurning = true
            onGimbalYawChanged?.invoke(rx)
        } else if (wasGimbalTurning) {
            wasGimbalTurning = false
            onGimbalYawReleased?.invoke()
        }

        // ── 3. Triggers: R2 (Forward) & L2 (Backward) with deadband expansion ─
        val rawRt = maxOf(
            event.getAxisValue(MotionEvent.AXIS_RTRIGGER),
            event.getAxisValue(MotionEvent.AXIS_GAS)
        ).coerceIn(0f, 1f)

        val rawLt = maxOf(
            event.getAxisValue(MotionEvent.AXIS_LTRIGGER),
            event.getAxisValue(MotionEvent.AXIS_BRAKE)
        ).coerceIn(0f, 1f)

        val rt = if (rawRt > triggerDeadzone) (rawRt - triggerDeadzone) / (1f - triggerDeadzone) else 0f
        val lt = if (rawLt > triggerDeadzone) (rawLt - triggerDeadzone) / (1f - triggerDeadzone) else 0f

        rightTrigger.floatValue = rt
        leftTrigger.floatValue = lt

        if (rt > 0.001f) {
            wasDriving = true
            onForward?.invoke(rt)
            startVibration((rt * 0.7f * 255).toInt())
        } else if (lt > 0.001f) {
            wasDriving = true
            onBackward?.invoke(lt)
            startVibration((lt * 0.4f * 255).toInt())
        } else if (wasDriving) {
            // Both triggers released -> STOP CAR IMMEDIATELY
            wasDriving = false
            onStop?.invoke()
            if (!wasSteering) stopVibration()
        }

        // ── 4. D-pad via HAT axes (DualShock reports d-pad as axes) ───────────
        val hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
        if (hatX != lastHatX || hatY != lastHatY) {
            if (hatY < -0.5f && lastHatY >= -0.5f) onButton?.invoke(Button.DPAD_UP)
            if (hatY > 0.5f  && lastHatY <= 0.5f)  onButton?.invoke(Button.DPAD_DOWN)
            if (hatX < -0.5f && lastHatX >= -0.5f) onButton?.invoke(Button.DPAD_LEFT)
            if (hatX > 0.5f  && lastHatX <= 0.5f)  onButton?.invoke(Button.DPAD_RIGHT)
            lastHatX = hatX; lastHatY = hatY
        }

        return true
    }

    // Called from Activity.dispatchKeyEvent
    fun onKeyEvent(event: KeyEvent): Boolean {
        if (event.action != KeyEvent.ACTION_DOWN) return false
        val btn = when (event.keyCode) {
            KeyEvent.KEYCODE_DPAD_UP        -> Button.DPAD_UP
            KeyEvent.KEYCODE_DPAD_DOWN      -> Button.DPAD_DOWN
            KeyEvent.KEYCODE_DPAD_LEFT      -> Button.DPAD_LEFT
            KeyEvent.KEYCODE_DPAD_RIGHT     -> Button.DPAD_RIGHT
            KeyEvent.KEYCODE_BUTTON_A       -> Button.A
            KeyEvent.KEYCODE_BUTTON_B       -> Button.B
            KeyEvent.KEYCODE_BUTTON_X       -> Button.X
            KeyEvent.KEYCODE_BUTTON_Y       -> Button.Y
            KeyEvent.KEYCODE_BUTTON_L1      -> Button.L1
            KeyEvent.KEYCODE_BUTTON_R1      -> Button.R1
            KeyEvent.KEYCODE_BUTTON_THUMBL  -> Button.L3
            KeyEvent.KEYCODE_BUTTON_THUMBR  -> Button.R3
            else -> return false
        }
        onButton?.invoke(btn)
        return true
    }

    // MARK: - Vibration (uses gamepad's own motors)

    @Suppress("DEPRECATION")
    private fun startVibration(amplitude: Int) {
        val device = gamepadDevice ?: return
        val amp = amplitude.coerceIn(1, 255)
        try {
            val vibrator = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                device.vibratorManager.defaultVibrator
            } else {
                device.vibrator
            }
            if (!vibrator.hasVibrator()) return
            val effect = VibrationEffect.createWaveform(
                longArrayOf(0, 150),
                intArrayOf(0, amp),
                1 // repeat from index 1
            )
            vibrator.vibrate(effect)
        } catch (_: Exception) {}
    }

    @Suppress("DEPRECATION")
    fun stopVibration() {
        try {
            val device = gamepadDevice ?: return
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                device.vibratorManager.defaultVibrator.cancel()
            } else {
                device.vibrator.cancel()
            }
        } catch (_: Exception) {}
    }

    fun cleanup() {
        inputManager.unregisterInputDeviceListener(deviceListener)
        stopVibration()
    }
}
