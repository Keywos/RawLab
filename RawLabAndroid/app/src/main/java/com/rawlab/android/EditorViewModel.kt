package com.rawlab.android

import android.app.Application
import android.graphics.Bitmap
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

enum class Operation { NONE, IMPORT, PICK_EXPORT, EXPORT }
data class PreviewPair(val neutral: Bitmap, val result: Bitmap, val temperature: Float, val tint: Float)
data class EditorState(
    val photo: ImportedPhoto? = null,
    val edits: EditSettings = EditSettings(),
    val preview: PreviewPair? = null,
    val operation: Operation = Operation.NONE,
    val rendering: Boolean = false,
    val exact: Boolean = false,
    val error: String? = null,
    val message: String? = null,
    val gpuEnabled: Boolean = true,
) {
    val canExport get() = photo != null && preview != null && exact && !rendering && operation == Operation.NONE
    val controlsEnabled get() = photo != null && operation == Operation.NONE
}

private sealed interface Work {
    data class Import(val uri: Uri, val gpuMode: Int) : Work
    data class Preview(val photo: ImportedPhoto, val edits: EditSettings, val interactive: Boolean,
        val gpuMode: Int, val importing: Boolean = false) : Work
    data class Export(val photo: ImportedPhoto, val edits: EditSettings, val destination: Uri?, val png: Boolean, val gpuMode: Int) : Work
}
private sealed interface WorkResult {
    data class Preview(val request: Work.Preview, val pair: PreviewPair) : WorkResult
    data object Export : WorkResult
}

class EditorViewModel(application: Application) : AndroidViewModel(application) {
    val storage = PhotoStorage(application)
    private var processor: NativeProcessor? = null
    private var currentRevision = 0L
    private val mutable = MutableStateFlow(EditorState())
    val state = mutable.asStateFlow()
    private var disposed = false
    private val queue = RenderQueue<Work, WorkResult>(::perform, { revision, result ->
        viewModelScope.launch {
            if (!disposed && revision == currentRevision) accept(result)
        }
    }, { processor?.close(); storage.close() })

    private fun engine() = processor ?: NativeProcessor().also { processor = it }

    private fun perform(work: Work): WorkResult = when (work) {
        is Work.Import -> {
            val photo = storage.import(work.uri)
            try { perform(Work.Preview(photo, EditSettings(), false, work.gpuMode, importing = true)) }
            catch (error: Throwable) { photo.file.delete(); throw error }
        }
        is Work.Preview -> {
            val edge = if (work.interactive) 1000 else 1600
            val native = engine()
            native.setGpuMode(work.gpuMode)
            val neutral = native.preview(work.photo.file, null, work.edits, edge, work.interactive)
            val lut = storage.filmPath(work.edits.film)
            val film = if (lut == null || work.edits.strength == 0f) neutral
                else native.preview(work.photo.file, lut, work.edits, edge, work.interactive)
            val neutralBitmap = neutral.bitmap()
            WorkResult.Preview(work, PreviewPair(neutralBitmap,
                if (neutral === film) neutralBitmap else film.bitmap(), neutral.temperature, neutral.tint))
        }
        is Work.Export -> {
            val output = storage.temporaryOutput(work.png)
            try {
                engine().setGpuMode(work.gpuMode)
                engine().export(work.photo.file, storage.filmPath(work.edits.film), work.edits, output, work.png)
                if (work.destination == null) storage.saveAlbum(output, work.png)
                else storage.saveDocument(output, work.destination, work.photo.uri)
            } finally { output.delete() }
            WorkResult.Export
        }
    }

    private fun accept(result: Result<WorkResult>) {
        result.fold({ value ->
            when (value) {
                is WorkResult.Preview -> {
                    val request = value.request
                    if (request.importing) {
                        mutable.value.photo?.file?.takeIf { it != request.photo.file }?.delete()
                    }
                    val settings = if (!request.edits.customWb && value.pair.temperature.isFinite())
                        request.edits.copy(temperature = value.pair.temperature.coerceIn(2000f, 50000f), tint = value.pair.tint.coerceIn(-150f, 150f))
                    else request.edits
                    mutable.value = mutable.value.copy(photo = request.photo, edits = settings, preview = value.pair,
                        operation = Operation.NONE, rendering = false, exact = !request.interactive, error = null)
                }
                WorkResult.Export -> mutable.value = mutable.value.copy(operation = Operation.NONE, message = text(R.string.export_done))
            }
        }, { error ->
            mutable.value = mutable.value.copy(operation = Operation.NONE, rendering = false,
                exact = mutable.value.exact,
                error = failure(error))
        })
    }

    fun importPhoto(uri: Uri?) {
        if (uri == null || mutable.value.operation != Operation.NONE) return
        mutable.value = mutable.value.copy(operation = Operation.IMPORT, rendering = true, error = null)
        currentRevision = queue.submit(Work.Import(uri, gpuMode()))
    }

    fun edit(edits: EditSettings, interactive: Boolean = false) {
        val current = mutable.value
        if (!current.controlsEnabled) return
        mutable.value = current.copy(edits = edits, rendering = true, exact = false, error = null)
        currentRevision = queue.submit(Work.Preview(current.photo!!, edits, interactive, gpuMode()))
    }

    fun retry() { edit(mutable.value.edits) }
    fun reset() { edit(mutable.value.edits.reset()) }

    private fun gpuMode() = if (mutable.value.gpuEnabled) NativeProcessor.AUTO else NativeProcessor.CPU
    fun setGpuEnabled(enabled: Boolean) {
        if (mutable.value.operation != Operation.NONE) return
        mutable.value = mutable.value.copy(gpuEnabled = enabled)
        if (mutable.value.photo != null) edit(mutable.value.edits)
    }

    fun beginExport(): Boolean {
        if (!mutable.value.canExport) return false
        mutable.value = mutable.value.copy(operation = Operation.PICK_EXPORT, error = null)
        return true
    }
    fun cancelExport() {
        if (mutable.value.operation == Operation.PICK_EXPORT) mutable.value = mutable.value.copy(operation = Operation.NONE)
    }
    fun export(destination: Uri?, png: Boolean) {
        val current = mutable.value
        if (current.operation != Operation.PICK_EXPORT || current.photo == null) return
        mutable.value = current.copy(operation = Operation.EXPORT)
        currentRevision = queue.submit(Work.Export(current.photo, current.edits, destination, png, gpuMode()))
    }
    fun dismissMessage() { mutable.value = mutable.value.copy(message = null) }
    private fun text(id: Int) = getApplication<Application>().getString(id)
    private fun failure(error: Throwable): String = if (error is OutOfMemoryError) text(R.string.memory_error)
        else text(R.string.process_error) + "\n" + (error.message ?: error.javaClass.simpleName)

    override fun onCleared() {
        disposed = true
        queue.close()
    }
}
