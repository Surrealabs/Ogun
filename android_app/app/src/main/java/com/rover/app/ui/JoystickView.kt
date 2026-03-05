package com.rover.app.ui

import android.content.Context
import android.graphics.*
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import kotlin.math.*

// ============================================================
//  JoystickView — floating-knob virtual joystick
//  Reports x, y in range [-1, +1]
// ============================================================
class JoystickView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    // Callback: (x, y) both in -1..1
    var onMove: ((x: Float, y: Float) -> Unit)? = null

    private val outerPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 4f
        color = Color.argb(180, 100, 200, 255)
    }
    private val innerPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
        color = Color.argb(220, 80, 160, 255)
    }
    private val bgPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
        color = Color.argb(60, 255, 255, 255)
    }

    private var cx = 0f; private var cy = 0f  // center
    private var outerR = 0f; private var innerR = 0f
    private var knobX = 0f; private var knobY = 0f
    private var activePointerId = -1

    var x: Float = 0f; private set
    var y: Float = 0f; private set

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        cx = w / 2f; cy = h / 2f
        outerR = (minOf(w, h) / 2f) * 0.88f
        innerR = outerR * 0.40f
        knobX = cx; knobY = cy
    }

    override fun onDraw(canvas: Canvas) {
        // Background circle
        canvas.drawCircle(cx, cy, outerR, bgPaint)
        canvas.drawCircle(cx, cy, outerR, outerPaint)
        // Cross-hair
        canvas.drawLine(cx - outerR, cy, cx + outerR, cy, outerPaint)
        canvas.drawLine(cx, cy - outerR, cx, cy + outerR, outerPaint)
        // Knob
        canvas.drawCircle(knobX, knobY, innerR, innerPaint)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val idx = event.actionIndex
                activePointerId = event.getPointerId(idx)
                moveKnob(event.getX(idx), event.getY(idx))
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                val idx = event.findPointerIndex(activePointerId)
                if (idx >= 0) moveKnob(event.getX(idx), event.getY(idx))
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP,
            MotionEvent.ACTION_CANCEL -> {
                val idx = event.actionIndex
                if (event.getPointerId(idx) == activePointerId) recenter()
                return true
            }
        }
        return super.onTouchEvent(event)
    }

    private fun moveKnob(tx: Float, ty: Float) {
        val dx = tx - cx; val dy = ty - cy
        val dist = sqrt(dx * dx + dy * dy)
        val ratio = if (dist > outerR) (outerR / dist) else 1f
        knobX = cx + dx * ratio
        knobY = cy + dy * ratio
        x = (knobX - cx) / outerR
        y = -((knobY - cy) / outerR)   // invert Y so up = positive
        onMove?.invoke(x, y)
        invalidate()
    }

    private fun recenter() {
        knobX = cx; knobY = cy
        x = 0f; y = 0f
        onMove?.invoke(0f, 0f)
        invalidate()
    }
}
