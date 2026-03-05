package com.surreallabs.ogun.webview

import android.annotation.SuppressLint
import android.content.Context
import android.util.AttributeSet
import android.webkit.JavascriptInterface
import android.webkit.WebChromeClient
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient

/**
 * Drop-in WebView that connects to an Ogun rover's Web UI.
 *
 * Usage from veve:
 *   val ctrl = OgunControlView(context)
 *   ctrl.connect("192.168.1.42")
 *   parentLayout.addView(ctrl)
 *
 * Or add via XML and call connect() later.
 */
@SuppressLint("SetJavaScriptEnabled")
class OgunControlView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : WebView(context, attrs, defStyleAttr) {

    var roverHost: String? = null
        private set

    /** Listener for events forwarded from the rover's web UI. */
    var eventListener: EventListener? = null

    interface EventListener {
        /** Telemetry JSON received from the rover WebSocket. */
        fun onTelemetry(json: String) {}
        /** Connection state changed. */
        fun onConnectionChanged(connected: Boolean) {}
        /** An error occurred inside the WebView. */
        fun onError(message: String) {}
    }

    init {
        settings.javaScriptEnabled = true
        settings.domStorageEnabled = true
        settings.mediaPlaybackRequiresUserGesture = false

        webViewClient = object : WebViewClient() {
            override fun shouldOverrideUrlLoading(
                view: WebView, request: WebResourceRequest
            ): Boolean = false

            override fun onPageFinished(view: WebView?, url: String?) {
                super.onPageFinished(view, url)
                eventListener?.onConnectionChanged(true)
            }

            override fun onReceivedError(
                view: WebView?, errorCode: Int,
                description: String?, failingUrl: String?
            ) {
                eventListener?.onError(description ?: "WebView error $errorCode")
                eventListener?.onConnectionChanged(false)
            }
        }

        webChromeClient = WebChromeClient()

        addJavascriptInterface(Bridge(), "OgunBridge")
    }

    /**
     * Load the rover's Web UI.  [host] is the IP or hostname, port defaults to 8080.
     */
    fun connect(host: String, port: Int = 8080) {
        roverHost = host
        loadUrl("http://$host:$port")
    }

    /** Disconnect and show a blank page. */
    fun disconnect() {
        roverHost = null
        loadUrl("about:blank")
        eventListener?.onConnectionChanged(false)
    }

    /** Send a raw JSON command to the rover through the Web UI's WebSocket. */
    fun sendCommand(json: String) {
        val escaped = json.replace("\\", "\\\\").replace("'", "\\'")
        evaluateJavascript("window.ogunSend && window.ogunSend('$escaped')", null)
    }

    /** Convenience: send drive command. */
    fun drive(y: Float, rot: Float) {
        sendCommand("""{"type":"drive","x":0,"y":$y,"rot":$rot}""")
    }

    /** Convenience: send e-stop. */
    fun estop() {
        sendCommand("""{"type":"estop"}""")
    }

    // JS → native bridge
    private inner class Bridge {
        @JavascriptInterface
        fun onTelemetry(json: String) {
            post { eventListener?.onTelemetry(json) }
        }

        @JavascriptInterface
        fun onConnectionChanged(connected: Boolean) {
            post { eventListener?.onConnectionChanged(connected) }
        }
    }
}
