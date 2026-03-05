package com.rover.app.ui

import android.content.Context
import android.graphics.BitmapFactory
import android.os.Bundle
import android.util.AttributeSet
import android.view.*
import android.widget.ImageView
import android.widget.TextView
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import com.rover.app.R
import com.rover.app.RoverProtocol
import com.rover.app.RoverViewModel
import com.rover.app.Transport
import kotlinx.coroutines.*
import java.io.BufferedInputStream
import java.io.ByteArrayOutputStream
import java.net.URL

// ============================================================
//  MjpegView — ImageView that streams MJPEG frames
// ============================================================
class MjpegView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null
) : ImageView(context, attrs) {

    private var streamJob: Job? = null
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    fun startStream(url: String) {
        stopStream()
        streamJob = scope.launch {
            runCatching {
                val conn = URL(url).openConnection().apply {
                    connectTimeout = 3000
                    readTimeout = 10000
                }
                val input = BufferedInputStream(conn.getInputStream())
                val buf   = ByteArray(65536)
                val jpeg  = ByteArrayOutputStream()
                var inJpeg = false

                while (isActive) {
                    val n = input.read(buf)
                    if (n < 0) break
                    for (i in 0 until n) {
                        val b = buf[i]
                        if (!inJpeg) {
                            // Look for JPEG SOI marker 0xFF 0xD8
                            if (jpeg.size() > 0 && jpeg.toByteArray().last() == 0xFF.toByte()
                                && b == 0xD8.toByte()) {
                                jpeg.write(b.toInt())
                                inJpeg = true
                            } else {
                                jpeg.reset()
                                jpeg.write(b.toInt())
                            }
                        } else {
                            jpeg.write(b.toInt())
                            // Look for JPEG EOI marker 0xFF 0xD9
                            val sz = jpeg.size()
                            if (sz >= 2) {
                                val bytes = jpeg.toByteArray()
                                if (bytes[sz-2] == 0xFF.toByte() && bytes[sz-1] == 0xD9.toByte()) {
                                    val imageBytes = bytes.copyOf()
                                    withContext(Dispatchers.Main) {
                                        val bmp = BitmapFactory.decodeByteArray(
                                            imageBytes, 0, imageBytes.size)
                                        setImageBitmap(bmp)
                                    }
                                    jpeg.reset()
                                    inJpeg = false
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    fun stopStream() {
        streamJob?.cancel()
        streamJob = null
    }

    override fun onDetachedFromWindow() {
        super.onDetachedFromWindow()
        scope.cancel()
    }
}

// ============================================================
//  CameraFragment — shows 2 MJPEG streams side by side
// ============================================================
class CameraFragment : Fragment() {

    private val vm: RoverViewModel by activityViewModels()

    private lateinit var cam0View: MjpegView
    private lateinit var cam1View: MjpegView
    private lateinit var urlText0: TextView
    private lateinit var urlText1: TextView

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?, saved: Bundle?
    ): View = inflater.inflate(R.layout.fragment_cameras, container, false)

    override fun onViewCreated(view: View, saved: Bundle?) {
        cam0View = view.findViewById(R.id.mjpeg0)
        cam1View = view.findViewById(R.id.mjpeg1)
        urlText0 = view.findViewById(R.id.urlCam0)
        urlText1 = view.findViewById(R.id.urlCam1)

        val state = vm.connection.value
        val ip    = state.ip

        if (ip.isNotEmpty()) {
            val url0 = RoverProtocol.mjpegUrl(ip, 0)
            val url1 = RoverProtocol.mjpegUrl(ip, 1)
            urlText0.text = url0
            urlText1.text = url1
            cam0View.startStream(url0)
            cam1View.startStream(url1)
        } else if (state.transport == Transport.BLE) {
            urlText0.text = "Camera streaming requires WiFi connection"
            urlText1.text = ""
        }
    }

    override fun onDestroyView() {
        cam0View.stopStream()
        cam1View.stopStream()
        super.onDestroyView()
    }
}
