# Ogun – Android

This directory contains the Android side of the Ogun rover project:

| Module | Type | Purpose |
|--------|------|---------|
| `:app` | Application | Standalone RoverControl app (BLE + WiFi direct control) |
| `:ogun-webview` | AAR Library | Thin WebView adapter for embedding in **veve** |

## ogun-webview

A lightweight Android library that wraps the rover's on-board Web UI (served by the Pi on port 8080) into a reusable `OgunControlView`.  
veve (or any host app) imports this AAR to get a full rover control panel without reimplementing the protocol.

### What's inside

| Class | Description |
|-------|-------------|
| `OgunControlView` | `WebView` subclass — loads `http://<rover>:8080`, exposes `drive()`, `estop()`, `sendCommand()`, and a JS↔native bridge for telemetry events |
| `OgunDiscovery` | mDNS/NSD helper — listens for `_rover._tcp` on the LAN and reports found/lost rovers with host + port |

### Integration in veve

**Gradle dependency** (local project or published AAR):

```kotlin
// settings.gradle.kts — if consuming as a local project
include(":ogun-webview")
project(":ogun-webview").projectDir = file("path/to/ogun/android_app/ogun-webview")

// app/build.gradle.kts
dependencies {
    implementation(project(":ogun-webview"))
    // or from Maven: implementation("com.surreallabs:ogun-webview:1.0")
}
```

**Discover + connect:**

```kotlin
import com.surreallabs.ogun.webview.OgunControlView
import com.surreallabs.ogun.webview.OgunDiscovery

// 1. Find the rover on the network
val discovery = OgunDiscovery(context)
discovery.listener = object : OgunDiscovery.Listener {
    override fun onRoverFound(name: String, host: String, port: Int) {
        controlView.connect(host, port)
    }
    override fun onRoverLost(name: String) { /* handle disconnect */ }
}
discovery.start()

// 2. Add the control view to any layout
val controlView = OgunControlView(context)
parentLayout.addView(controlView)

// 3. Listen for telemetry from the rover
controlView.eventListener = object : OgunControlView.EventListener {
    override fun onTelemetry(json: String) {
        // Update veve's own UI with rover sensor data
    }
    override fun onConnectionChanged(connected: Boolean) {
        // Show/hide connection indicator
    }
}

// 4. Send commands programmatically (optional — the WebView UI handles this too)
controlView.drive(y = 0.5f, rot = 0.0f)
controlView.estop()

// 5. Clean up
discovery.stop()
controlView.disconnect()
```

### How it works

```
veve app
  └─ OgunControlView (WebView)
       │  loads http://<rover-ip>:8080
       │  ← Pi serves index.html + static assets
       │  ← WebSocket /ws for commands & telemetry
       │
       └─ JS ↔ OgunBridge (JavascriptInterface)
            → onTelemetry(json)  → EventListener
            → onConnectionChanged(connected)
```

The full control UI (joystick, cameras, GPIO toggles, OTA) lives in the Pi's `webui/index.html`.  
`OgunControlView` is intentionally thin — it just hosts that page and bridges events to native code.

### Building

From the `android_app/` directory:

```bash
./gradlew :ogun-webview:assembleRelease   # build AAR
./gradlew :ogun-webview:publishToMavenLocal  # publish locally (needs maven-publish plugin)
```

Or via the unified build system from the repo root:

```bash
make android        # builds all Android modules
ogun build android  # same, via CLI
```

### Requirements

- Android SDK 26+ (Android 8.0)
- Rover Pi server running with WebUI enabled (`webui_port = 8080`)
- Rover and phone on the same network (or hotspot)
