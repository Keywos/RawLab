package com.rawlab.android

import android.graphics.Bitmap
import java.io.File
import java.nio.ByteBuffer

class NativeFrame(val width: Int, val height: Int, val pixels: ByteArray, val temperature: Float, val tint: Float) {
    fun bitmap(): Bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888).also {
        it.copyPixelsFromBuffer(ByteBuffer.wrap(pixels))
    }
}

class NativeProcessor : AutoCloseable {
    private var handle = nativeCreate().also { check(it != 0L) }

    @Synchronized
    fun preview(input: File, lut: File?, settings: EditSettings, edge: Int, interactive: Boolean): NativeFrame {
        check(handle != 0L) { "Processor is closed" }
        return checkNotNull(nativeProcess(handle, input.path, lut?.path, null, settings.strength,
            settings.exposure, settings.customWb, settings.temperature, settings.tint, edge, interactive, false))
    }

    @Synchronized
    fun export(input: File, lut: File?, settings: EditSettings, output: File, png: Boolean) {
        check(handle != 0L) { "Processor is closed" }
        require(input.canonicalPath != output.canonicalPath)
        nativeProcess(handle, input.path, lut?.path, output.path, settings.strength,
            settings.exposure, settings.customWb, settings.temperature, settings.tint, 0, false, png)
    }

    @Synchronized
    override fun close() {
        if (handle != 0L) { nativeDestroy(handle); handle = 0L }
    }

    private external fun nativeCreate(): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeProcess(handle: Long, input: String, lut: String?, output: String?,
        strength: Float, exposure: Float, customWb: Boolean, temperature: Float, tint: Float,
        edge: Int, interactive: Boolean, png: Boolean): NativeFrame?

    companion object { init { System.loadLibrary("rawlab-jni") } }
}
