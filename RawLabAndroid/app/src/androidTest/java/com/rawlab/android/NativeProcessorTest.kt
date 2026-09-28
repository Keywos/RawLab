package com.rawlab.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

@RunWith(AndroidJUnit4::class)
class NativeProcessorTest {
    @Test fun gpuCanBeDisabledAndRestoredWithoutLosingSession() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val input = File(instrumentation.targetContext.cacheDir, "gpu-mode-test.arw")
        instrumentation.context.assets.open("DSC09067.ARW").use { from -> input.outputStream().use { from.copyTo(it) } }
        try {
            NativeProcessor(NativeProcessor.FORCE).use { processor ->
                assertEquals(2, processor.preview(input, null, EditSettings(), 400, false).backend)
                processor.setGpuMode(NativeProcessor.CPU)
                assertEquals(0, processor.preview(input, null, EditSettings(), 400, false).backend)
                processor.setGpuMode(NativeProcessor.AUTO)
                assertEquals(2, processor.preview(input, null, EditSettings(), 400, false).backend)
            }
        } finally { input.delete() }
    }
    @Test fun realRawPreviewAndFullResolutionExport() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        val input = File(context.cacheDir, "native-test.arw")
        instrumentation.context.assets.open("DSC09067.ARW").use { from -> input.outputStream().use { from.copyTo(it) } }
        try {
            NativeProcessor().use { processor ->
                val storage = PhotoStorage(context)
                val neutral = processor.preview(input, null, EditSettings(), 400, false)
                assertEquals(400, maxOf(neutral.width, neutral.height))
                assertEquals(neutral.width * neutral.height * 4, neutral.pixels.size)
                val film = processor.preview(input, storage.filmPath("velvia"), EditSettings(film = "velvia"), 400, false)
                assertFalse(neutral.pixels.contentEquals(film.pixels))
                val bright = processor.preview(input, null, EditSettings(exposure = 1f), 400, false)
                assertFalse(neutral.pixels.contentEquals(bright.pixels))
                if (neutral.temperature.isFinite()) {
                    val warm = processor.preview(input, null, EditSettings(customWb = true, temperature = 9000f), 400, false)
                    assertFalse(neutral.pixels.contentEquals(warm.pixels))
                }
                val out = File(context.cacheDir, "native-export.png")
                processor.export(input, null, EditSettings(), out, true)
                val header = ByteArray(26)
                java.io.DataInputStream(out.inputStream()).use { it.readFully(header) }
                assertEquals(16, header[24].toInt())
                fun intAt(i: Int) = java.nio.ByteBuffer.wrap(header, i, 4).int
                assertEquals(7008, intAt(16))
                assertEquals(4672, intAt(20))
                out.delete()
                val jpeg = File(context.cacheDir, "native-export.jpg")
                processor.export(input, null, EditSettings(), jpeg, false)
                val dimensions = android.graphics.BitmapFactory.Options().apply { inJustDecodeBounds = true }
                android.graphics.BitmapFactory.decodeFile(jpeg.path, dimensions)
                assertEquals(7008, dimensions.outWidth)
                assertEquals(4672, dimensions.outHeight)
                jpeg.delete()
                assertThrows(Exception::class.java) { processor.preview(File(context.cacheDir, "missing.dng"), null, EditSettings(), 400, false) }
            }
        } finally { input.delete() }
    }

    @Test fun closedProcessorCannotRender() {
        val processor = NativeProcessor()
        processor.close()
        processor.close()
        assertThrows(IllegalStateException::class.java) { processor.preview(File("missing.dng"), null, EditSettings(), 400, false) }
    }
}
