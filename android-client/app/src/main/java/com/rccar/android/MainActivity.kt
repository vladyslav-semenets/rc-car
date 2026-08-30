package com.rccar.android

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.view.KeyEvent
import android.view.MotionEvent
import android.webkit.PermissionRequest
import android.webkit.WebChromeClient
import android.webkit.WebSettings
import android.webkit.WebView
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat

class MainActivity : ComponentActivity() {

    private lateinit var car: CarController
    private lateinit var joystick: JoystickManager
    private val pinger = PingMonitor()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Fullscreen layout
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }

        window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        val prefs = getSharedPreferences("rccar", Context.MODE_PRIVATE)

        car = CarController(this, prefs)
        joystick = JoystickManager(this)

        setupJoystick()

        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                RequestBlePermissions()
                RCCarApp(car = car, joystick = joystick, pinger = pinger, prefs = prefs)
            }
        }

        // Auto-connect to Ground TX BLE
        car.connectBle()
    }

    override fun onDestroy() {
        super.onDestroy()
        car.disconnect()
        joystick.cleanup()
        pinger.stop()
    }

    // Route gamepad events to JoystickManager
    override fun dispatchGenericMotionEvent(ev: MotionEvent): Boolean =
        joystick.onMotionEvent(ev) || super.dispatchGenericMotionEvent(ev)

    override fun dispatchKeyEvent(ev: KeyEvent): Boolean =
        joystick.onKeyEvent(ev) || super.dispatchKeyEvent(ev)

    private fun setupJoystick() {
        joystick.onSteeringChanged  = { v -> car.turnTo(car.stickToSteering(v)) }
        joystick.onSteeringReleased = { car.resetTurns() }
        joystick.onGimbalYawChanged = { v -> car.gimbalYaw(car.stickToGimbalYaw(v)) }
        joystick.onGimbalYawReleased = { car.resetGimbal() }
        joystick.onForward  = { car.forward(it) }
        joystick.onBackward = { car.backward(it) }
        joystick.onStop     = { car.neutral() }
        joystick.onButton   = { btn ->
            when (btn) {
                JoystickManager.Button.DPAD_UP   -> car.initCar()
                JoystickManager.Button.DPAD_DOWN -> car.toggleSteeringCalibration()
                JoystickManager.Button.DPAD_LEFT -> {
                    car.degreeOfTurns.floatValue = (car.degreeOfTurns.floatValue + 1f).coerceAtMost(180f)
                    car.changeDegrees()
                }
                JoystickManager.Button.DPAD_RIGHT -> {
                    car.degreeOfTurns.floatValue = (car.degreeOfTurns.floatValue - 1f).coerceAtLeast(0f)
                    car.changeDegrees()
                }
                JoystickManager.Button.R3  -> car.speedUp()
                JoystickManager.Button.L3  -> car.speedDown()
                JoystickManager.Button.L1  -> {
                    car.pitchAngle.intValue = (car.pitchAngle.intValue - 1).coerceAtLeast(-90)
                    car.gimbalPitch()
                }
                JoystickManager.Button.R1  -> {
                    car.pitchAngle.intValue = (car.pitchAngle.intValue + 1).coerceAtMost(90)
                    car.gimbalPitch()
                }
                JoystickManager.Button.A   -> car.stopCamera()
                JoystickManager.Button.Y   -> car.startCamera()
                JoystickManager.Button.B   -> car.neutral()
                else -> {}
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Permissions Helper
// ─────────────────────────────────────────────────────────────────────────────

@Composable
fun RequestBlePermissions() {
    val context = androidx.compose.ui.platform.LocalContext.current
    val permissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        arrayOf(
            Manifest.permission.BLUETOOTH_SCAN,
            Manifest.permission.BLUETOOTH_CONNECT
        )
    } else {
        arrayOf(
            Manifest.permission.BLUETOOTH,
            Manifest.permission.BLUETOOTH_ADMIN,
            Manifest.permission.ACCESS_FINE_LOCATION
        )
    }

    val launcher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { }

    LaunchedEffect(Unit) {
        val missing = permissions.filter {
            ContextCompat.checkSelfPermission(context, it) != PackageManager.PERMISSION_GRANTED
        }
        if (missing.isNotEmpty()) {
            launcher.launch(missing.toTypedArray())
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Main UI
// ─────────────────────────────────────────────────────────────────────────────

@Composable
fun RCCarApp(car: CarController, joystick: JoystickManager, pinger: PingMonitor, prefs: android.content.SharedPreferences) {
    var showSettings by remember { mutableStateOf(false) }
    val piIP    by remember { mutableStateOf(prefs.getString("piIP", "100.108.40.34")!!) }
    val webPort by remember { mutableStateOf(prefs.getString("webrtcPort", "8889")!!) }

    // Update safety state whenever connections change
    LaunchedEffect(joystick.isConnected.value, car.ble.connectionState.value, car.udp.isConnected.value) {
        car.updateSafetyState(joystick.isConnected.value)
    }

    Row(modifier = Modifier.fillMaxSize().background(Color.Black)) {

        // ── Stream / HUD View ────────────────────────────────────────────────
        Box(modifier = Modifier.weight(1f).fillMaxHeight()) {
            WebRTCView(url = "http://$piIP:$webPort/front")

            // Top HUD Status Overlay
            Row(
                modifier = Modifier
                    .align(Alignment.TopStart)
                    .padding(8.dp)
                    .background(Color.Black.copy(alpha = 0.65f), RoundedCornerShape(8.dp))
                    .padding(horizontal = 10.dp, vertical = 6.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                // Gamepad Flag
                ConnectionFlag(
                    active = joystick.isConnected.value,
                    label = if (joystick.isConnected.value) "🎮 Gamepad Ready" else "🎮 No Gamepad"
                )

                // Ground TX BLE Flag
                val bleConnected = car.ble.connectionState.value == BleManager.State.CONNECTED
                val bleScanning  = car.ble.connectionState.value == BleManager.State.SCANNING
                ConnectionFlag(
                    active = bleConnected,
                    scanning = bleScanning,
                    label = when (car.ble.connectionState.value) {
                        BleManager.State.CONNECTED   -> "📡 LoRa TX Connected"
                        BleManager.State.SCANNING    -> "📡 Scanning TX..."
                        BleManager.State.CONNECTING  -> "📡 Connecting TX..."
                        BleManager.State.DISCONNECTED-> "📡 LoRa TX Offline"
                    }
                )

                // Drive Mode Status
                if (car.isDriveModeActive.value) {
                    Text(
                        "DRIVE MODE ACTIVE",
                        fontSize = 11.sp,
                        color = Color(0xFF4CAF50),
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                    )
                } else {
                    Text(
                        "STANDBY (LOCK)",
                        fontSize = 11.sp,
                        color = Color(0xFFFF9800),
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                    )
                }
            }
        }

        // ── Side panel ────────────────────────────────────────────────────────
        Column(
            modifier = Modifier
                .width(220.dp)
                .fillMaxHeight()
                .background(MaterialTheme.colorScheme.surface)
                .padding(8.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            // Ground TX (BLE NUS) Connection
            PanelCard("Ground TX (LoRa 868)") {
                StatusDot(
                    active = car.ble.connectionState.value == BleManager.State.CONNECTED,
                    text = car.ble.deviceName.value
                )
                StatusRow("Sent", "${car.ble.bytesSent.intValue} B")
                StatusRow("Downlink", "${car.downlinkPacketCount.intValue} pkts")

                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    if (car.ble.connectionState.value != BleManager.State.CONNECTED) {
                        Button(
                            onClick = { car.connectBle() },
                            modifier = Modifier.weight(1f),
                            contentPadding = PaddingValues(2.dp)
                        ) {
                            Text("Scan / Connect", fontSize = 10.sp)
                        }
                    } else {
                        OutlinedButton(
                            onClick = { car.ble.disconnect() },
                            modifier = Modifier.weight(1f),
                            contentPadding = PaddingValues(2.dp)
                        ) {
                            Text("Disconnect", fontSize = 10.sp)
                        }
                    }
                }
            }

            // Controller
            PanelCard("Controller") {
                StatusDot(
                    joystick.isConnected.value,
                    if (joystick.isConnected.value) joystick.controllerName.value else "No controller"
                )
                StickBar("L", joystick.leftStickX.floatValue)
                StickBar("R", joystick.rightStickX.floatValue)
                TriggerBar("R2", joystick.rightTrigger.floatValue, Color(0xFF4FC3F7))
                TriggerBar("L2", joystick.leftTrigger.floatValue, Color(0xFFFFB74D))
            }

            // Car State
            PanelCard("Vehicle Status") {
                StatusRow("Gear", "${car.transmissionSpeed.intValue} / 8")
                StatusRow("Turn°", "%.1f".format(car.degreeOfTurns.floatValue))
                StatusRow("Pitch", "${car.pitchAngle.intValue}°")
                StatusRow("Gyro", if (car.isSteeringCalibrationOn.value) "ON" else "OFF")
            }

            // Motor config panel
            MotorConfigPanel(car = car)

            // Controls
            PanelCard("Controls") {
                ControlButton("Init car (↑)") { car.initCar() }
                ControlButton("ESC Neutral (B)") { car.neutral() }
                ControlButton("🔄 Unstuck") { car.unstuck() }
            }

            Spacer(modifier = Modifier.weight(1f))

            TextButton(
                onClick = { showSettings = true },
                modifier = Modifier.fillMaxWidth()
            ) { Text("Settings", fontSize = 12.sp) }
        }
    }

    if (showSettings) {
        SettingsDialog(prefs = prefs, car = car, onDismiss = { showSettings = false })
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Status Flag Helper
// ─────────────────────────────────────────────────────────────────────────────

@Composable
fun ConnectionFlag(active: Boolean, scanning: Boolean = false, label: String) {
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        Box(modifier = Modifier.size(8.dp).background(
            color = when {
                active   -> Color(0xFF4CAF50)
                scanning -> Color(0xFFFFEB3B)
                else     -> Color(0xFFF44336)
            },
            shape = androidx.compose.foundation.shape.CircleShape
        ))
        Text(label, fontSize = 11.sp, color = Color.White)
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// WebView Stream
// ─────────────────────────────────────────────────────────────────────────────

@Composable
fun WebRTCView(url: String) {
    var currentUrl by remember { mutableStateOf(url) }

    AndroidView(
        factory = { ctx ->
            WebView(ctx).apply {
                settings.apply {
                    javaScriptEnabled = true
                    mediaPlaybackRequiresUserGesture = false
                    domStorageEnabled = true
                    mixedContentMode = WebSettings.MIXED_CONTENT_ALWAYS_ALLOW
                }
                webChromeClient = object : WebChromeClient() {
                    override fun onPermissionRequest(request: PermissionRequest) {
                        request.grant(request.resources)
                    }
                }
                setBackgroundColor(android.graphics.Color.BLACK)
                loadUrl(url)
            }
        },
        update = { webView ->
            if (currentUrl != url) {
                currentUrl = url
                webView.loadUrl(url)
            }
        },
        modifier = Modifier.fillMaxSize()
    )
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings Dialog
// ─────────────────────────────────────────────────────────────────────────────

@Composable
fun SettingsDialog(prefs: android.content.SharedPreferences, car: CarController, onDismiss: () -> Unit) {
    var piIP      by remember { mutableStateOf(prefs.getString("piIP", "100.108.40.34")!!) }
    var webPort   by remember { mutableStateOf(prefs.getString("webrtcPort", "8889")!!) }
    var udpPort   by remember { mutableStateOf(prefs.getInt("udpPort", 8565).toString()) }
    var stExpo    by remember { mutableFloatStateOf(car.steeringExpo.floatValue) }
    var gimExpo   by remember { mutableFloatStateOf(car.gimbalExpo.floatValue) }
    var stDamp    by remember { mutableFloatStateOf(car.highSpeedSteeringDamping.floatValue) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Settings") },
        text = {
            Column(
                modifier = Modifier.verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                OutlinedTextField(value = piIP,    onValueChange = { piIP = it },    label = { Text("Pi IP (for WebRTC)") },    singleLine = true)
                OutlinedTextField(value = webPort, onValueChange = { webPort = it }, label = { Text("WebRTC stream port") }, singleLine = true)
                OutlinedTextField(value = udpPort, onValueChange = { udpPort = it }, label = { Text("UDP port (Optional)") },   singleLine = true)

                Text("Steering Expo: ${"%.2f".format(stExpo)}", style = MaterialTheme.typography.labelMedium)
                Slider(value = stExpo, onValueChange = { stExpo = it }, valueRange = 0f..1f)

                Text("High-Speed Steering Damping: ${(stDamp * 100).toInt()}%", style = MaterialTheme.typography.labelMedium)
                Slider(value = stDamp, onValueChange = { stDamp = it }, valueRange = 0f..0.85f)

                Text("Gimbal Expo: ${"%.2f".format(gimExpo)}", style = MaterialTheme.typography.labelMedium)
                Slider(value = gimExpo, onValueChange = { gimExpo = it }, valueRange = 0f..1f)
            }
        },
        confirmButton = {
            TextButton(onClick = {
                prefs.edit()
                    .putString("piIP", piIP)
                    .putString("webrtcPort", webPort)
                    .putInt("udpPort", udpPort.toIntOrNull() ?: 8565)
                    .apply()
                car.steeringExpo.floatValue = stExpo
                car.gimbalExpo.floatValue = gimExpo
                car.highSpeedSteeringDamping.floatValue = stDamp
                car.saveExpo()
                onDismiss()
            }) { Text("Save") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        }
    )
}

// ─────────────────────────────────────────────────────────────────────────────
// Motor Config Panel & Components
// ─────────────────────────────────────────────────────────────────────────────

@Composable
fun MotorConfigPanel(car: CarController) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {

            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Motor Config", style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.primary, modifier = Modifier.weight(1f))
                Text(if (car.motorConfigEnabled.value) "ON" else "OFF",
                    fontSize = 10.sp,
                    color = if (car.motorConfigEnabled.value) Color(0xFF4CAF50) else Color.Gray)
                Spacer(modifier = Modifier.width(4.dp))
                Switch(
                    checked = car.motorConfigEnabled.value,
                    onCheckedChange = {
                        car.motorConfigEnabled.value = it
                        car.saveMotorConfig()
                        car.sendMotorConfig()
                    },
                    modifier = Modifier.height(20.dp)
                )
            }

            if (car.motorConfigEnabled.value) {
                Divider()
                MotorRow("Front Trim (µs)", car.cfg_frontTrimUs.intValue, -50..50,
                    help = "↑ если передние не стартуют с задними\n↓ если крутятся в покое") {
                    car.cfg_frontTrimUs.intValue = it
                }
                MotorRow("Rear Trim (µs)", car.cfg_rearTrimUs.intValue, -50..50,
                    help = "↑ если задние не стартуют с передними") {
                    car.cfg_rearTrimUs.intValue = it
                }
                MotorRow("Slew (µs/шаг)", car.cfg_slewMaxUs.intValue, 5..500,
                    help = "↓ плавнее разгон (10-15)\n↑ резче отклик (50+)") {
                    car.cfg_slewMaxUs.intValue = it
                }
                MotorRow("Dir Hold (мс)", car.cfg_dirChangeHoldMs.intValue, 0..500,
                    help = "Пауза при смене направления\n↑ если щёлкает трансмиссия") {
                    car.cfg_dirChangeHoldMs.intValue = it
                }
                MotorRow("Front Lag (шаги)", car.cfg_frontLagSteps.intValue, 0..10,
                    help = "Задержка переднего ESC\n0 = синхронно, 3 = 30мс") {
                    car.cfg_frontLagSteps.intValue = it
                }
                MotorRow("Rev Brake (мс)", car.cfg_reverseBrakeMs.intValue, 0..600,
                    help = "↑ если реверс не включается\n↓ если задержка перед реверсом большая") {
                    car.cfg_reverseBrakeMs.intValue = it
                }
                MotorRow("Rev Gap (мс)", car.cfg_reverseNeutralMs.intValue, 0..300,
                    help = "↑ если реверс через раз") {
                    car.cfg_reverseNeutralMs.intValue = it
                }
                Divider()
                Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    OutlinedButton(
                        onClick = { car.resetMotorConfig(); car.saveMotorConfig() },
                        modifier = Modifier.weight(1f),
                        contentPadding = PaddingValues(4.dp)
                    ) { Text("Reset", fontSize = 10.sp) }
                    Button(
                        onClick = { car.saveMotorConfig(); car.sendMotorConfig() },
                        modifier = Modifier.weight(1f),
                        contentPadding = PaddingValues(4.dp)
                    ) { Text("Send to Car", fontSize = 10.sp) }
                }
            } else {
                Text("Простое управление — без slew, lag и reverse arm",
                    fontSize = 10.sp, color = Color.Gray)
            }
        }
    }
}

@Composable
fun MotorRow(label: String, value: Int, range: IntRange, help: String, onChange: (Int) -> Unit) {
    var showHelp by remember { mutableStateOf(false) }
    Column {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(label, fontSize = 10.sp, color = Color.Gray, modifier = Modifier.weight(1f))
            IconButton(onClick = { showHelp = !showHelp },
                modifier = Modifier.size(16.dp)) {
                Text("?", fontSize = 9.sp, color = MaterialTheme.colorScheme.primary)
            }
            Spacer(modifier = Modifier.width(2.dp))
            IconButton(onClick = { if (value > range.first) onChange(value - 1) },
                modifier = Modifier.size(20.dp)) {
                Text("−", fontSize = 12.sp)
            }
            Text("$value", fontSize = 10.sp,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace,
                modifier = Modifier.width(28.dp),
                textAlign = androidx.compose.ui.text.style.TextAlign.Center)
            IconButton(onClick = { if (value < range.last) onChange(value + 1) },
                modifier = Modifier.size(20.dp)) {
                Text("+", fontSize = 12.sp)
            }
        }
        if (showHelp) {
            Text(help, fontSize = 9.sp, color = Color.Gray,
                modifier = Modifier.padding(start = 4.dp, bottom = 2.dp))
        }
    }
}

@Composable
fun PanelCard(title: String, content: @Composable ColumnScope.() -> Unit) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(title, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.primary)
            content()
        }
    }
}

@Composable
fun StatusRow(label: String, value: String) {
    Row(modifier = Modifier.fillMaxWidth()) {
        Text(label, fontSize = 11.sp, color = Color.Gray, modifier = Modifier.weight(1f))
        Text(value, fontSize = 11.sp, fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace)
    }
}

@Composable
fun StatusDot(active: Boolean, text: String) {
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        Box(modifier = Modifier.size(8.dp).background(
            if (active) Color(0xFF4CAF50) else Color(0xFFFF5722),
            shape = androidx.compose.foundation.shape.CircleShape
        ))
        Text(text, fontSize = 11.sp, maxLines = 1)
    }
}

@Composable
fun StickBar(label: String, value: Float) {
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        Text(label, fontSize = 10.sp, modifier = Modifier.width(12.dp))
        Box(modifier = Modifier.weight(1f).height(6.dp)) {
            Box(modifier = Modifier.fillMaxSize().background(Color.Gray.copy(alpha = 0.3f),
                shape = androidx.compose.foundation.shape.RoundedCornerShape(3.dp)))
            val pos = (value + 1f) / 2f
            Box(modifier = Modifier
                .size(8.dp)
                .offset(x = (pos * 100).dp - 4.dp)
                .background(Color(0xFF4FC3F7), shape = androidx.compose.foundation.shape.CircleShape)
                .align(Alignment.CenterStart))
        }
        Text("%.2f".format(value), fontSize = 10.sp,
            fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace,
            modifier = Modifier.width(34.dp))
    }
}

@Composable
fun TriggerBar(label: String, value: Float, color: Color) {
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        Text(label, fontSize = 10.sp, modifier = Modifier.width(20.dp))
        LinearProgressIndicator(
            progress = { value },
            modifier = Modifier.weight(1f).height(5.dp),
            color = color,
            trackColor = Color.Gray.copy(alpha = 0.3f)
        )
        Text("${(value * 100).toInt()}%", fontSize = 10.sp,
            fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace,
            modifier = Modifier.width(30.dp))
    }
}

@Composable
fun ControlButton(text: String, onClick: () -> Unit) {
    Button(onClick = onClick, modifier = Modifier.fillMaxWidth(), contentPadding = PaddingValues(4.dp)) {
        Text(text, fontSize = 11.sp)
    }
}
