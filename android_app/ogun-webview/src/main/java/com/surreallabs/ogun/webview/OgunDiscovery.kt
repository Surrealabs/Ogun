package com.surreallabs.ogun.webview

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.os.Handler
import android.os.Looper

/**
 * Discovers Ogun rovers on the local network via mDNS/NSD.
 *
 * The Pi server advertises `_rover._tcp` — this helper listens for it
 * and reports host + port back to the caller.
 *
 * Usage:
 *   val discovery = OgunDiscovery(context)
 *   discovery.listener = object : OgunDiscovery.Listener { … }
 *   discovery.start()
 *   // … later
 *   discovery.stop()
 */
class OgunDiscovery(context: Context) {

    companion object {
        const val SERVICE_TYPE = "_rover._tcp."
    }

    interface Listener {
        fun onRoverFound(name: String, host: String, port: Int)
        fun onRoverLost(name: String)
        fun onError(message: String) {}
    }

    var listener: Listener? = null

    private val nsdManager = context.getSystemService(Context.NSD_SERVICE) as NsdManager
    private val mainHandler = Handler(Looper.getMainLooper())
    private var discoveryActive = false

    private val discoveryListener = object : NsdManager.DiscoveryListener {
        override fun onDiscoveryStarted(serviceType: String) {
            discoveryActive = true
        }

        override fun onServiceFound(serviceInfo: NsdServiceInfo) {
            nsdManager.resolveService(serviceInfo, resolveListener())
        }

        override fun onServiceLost(serviceInfo: NsdServiceInfo) {
            mainHandler.post {
                listener?.onRoverLost(serviceInfo.serviceName)
            }
        }

        override fun onDiscoveryStopped(serviceType: String) {
            discoveryActive = false
        }

        override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) {
            discoveryActive = false
            mainHandler.post {
                listener?.onError("NSD start failed (error $errorCode)")
            }
        }

        override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) {
            mainHandler.post {
                listener?.onError("NSD stop failed (error $errorCode)")
            }
        }
    }

    private fun resolveListener() = object : NsdManager.ResolveListener {
        override fun onResolveFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {
            mainHandler.post {
                listener?.onError("Resolve failed for ${serviceInfo.serviceName} (error $errorCode)")
            }
        }

        override fun onServiceResolved(serviceInfo: NsdServiceInfo) {
            val host = serviceInfo.host?.hostAddress ?: return
            mainHandler.post {
                listener?.onRoverFound(serviceInfo.serviceName, host, serviceInfo.port)
            }
        }
    }

    fun start() {
        if (discoveryActive) return
        nsdManager.discoverServices(SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, discoveryListener)
    }

    fun stop() {
        if (!discoveryActive) return
        try {
            nsdManager.stopServiceDiscovery(discoveryListener)
        } catch (_: IllegalArgumentException) {
            // already stopped
        }
    }
}
