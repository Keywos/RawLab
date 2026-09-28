package com.rawlab.android

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.util.Locale
import kotlin.math.roundToInt

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ToolIcon(icon: ImageVector, label: Int, enabled: Boolean = true, onClick: () -> Unit) {
    val text = stringResource(label)
    TooltipBox(positionProvider = TooltipDefaults.rememberPlainTooltipPositionProvider(),
        tooltip = { PlainTooltip { Text(text) } }, state = rememberTooltipState()) {
        IconButton(onClick = onClick, enabled = enabled) { Icon(icon, text) }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun EditorScreen(state: EditorState, onAlbum: () -> Unit, onFile: () -> Unit,
    onEdit: (EditSettings, Boolean) -> Unit, onReset: () -> Unit, onRetry: () -> Unit,
    onExport: () -> Unit, onMessageDismiss: () -> Unit, onLicenses: () -> Unit) {
    var compare by rememberSaveable { mutableStateOf(false) }
    val snackbar = remember { SnackbarHostState() }
    LaunchedEffect(state.message) {
        state.message?.let { snackbar.showSnackbar(it); onMessageDismiss() }
    }
    Scaffold(snackbarHost = { SnackbarHost(snackbar) }, topBar = {
        TopAppBar(title = { Text("RawLab", style = MaterialTheme.typography.titleLarge) }, actions = {
            ToolIcon(Icons.Outlined.PhotoLibrary, R.string.open_album, state.operation == Operation.NONE, onAlbum)
            ToolIcon(Icons.Outlined.FolderOpen, R.string.open_file, state.operation == Operation.NONE, onFile)
            TooltipBox(positionProvider = TooltipDefaults.rememberPlainTooltipPositionProvider(),
                tooltip = { PlainTooltip { Text(stringResource(R.string.compare)) } }, state = rememberTooltipState()) {
                IconToggleButton(checked = compare, onCheckedChange = { compare = it }, enabled = state.preview != null) {
                    Icon(Icons.Outlined.Compare, stringResource(R.string.compare))
                }
            }
            ToolIcon(Icons.Outlined.SaveAlt, R.string.export, state.canExport, onExport)
        })
    }) { padding ->
        BoxWithConstraints(Modifier.fillMaxSize().padding(padding)) {
            val dockHeight = if (maxHeight < 460.dp) 190.dp else 270.dp
            val wide = maxWidth >= 600.dp && maxWidth > maxHeight
            Column(Modifier.fillMaxSize()) {
                state.photo?.let { Text(it.name, Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
                    maxLines = 1, overflow = TextOverflow.Ellipsis, style = MaterialTheme.typography.labelMedium) }
                if (state.rendering || state.operation == Operation.EXPORT) {
                    Text(stringResource(when (state.operation) {
                        Operation.IMPORT -> R.string.importing
                        Operation.EXPORT -> R.string.exporting
                        else -> R.string.rendering
                    }), Modifier.padding(horizontal = 16.dp), style = MaterialTheme.typography.labelMedium)
                    LinearProgressIndicator(Modifier.fillMaxWidth())
                }
                if (wide && state.photo != null) {
                    Row(Modifier.weight(1f)) {
                        Column(Modifier.weight(1f)) {
                            EditorCanvas(state, compare, Modifier.weight(1f), onAlbum, onFile, onLicenses)
                            RenderError(state, onRetry)
                        }
                        VerticalDivider()
                        AdjustmentDock(state, Modifier.width(280.dp).fillMaxHeight(), onEdit, onReset)
                    }
                } else {
                    EditorCanvas(state, compare, Modifier.weight(1f), onAlbum, onFile, onLicenses)
                    RenderError(state, onRetry)
                    if (state.photo != null) {
                        HorizontalDivider()
                        AdjustmentDock(state, Modifier.fillMaxWidth().height(dockHeight), onEdit, onReset)
                    }
                }
            }
        }
    }
}

@Composable
private fun EditorCanvas(state: EditorState, compare: Boolean, modifier: Modifier,
    onAlbum: () -> Unit, onFile: () -> Unit, onLicenses: () -> Unit) {
    Box(modifier.fillMaxWidth().background(Color(0xFF18191A)), contentAlignment = Alignment.Center) {
        state.preview?.let { PhotoCanvas(it, compare) }
            ?: if (!state.rendering) EmptyEditor(onAlbum, onFile, onLicenses) else Unit
    }
}

@Composable
private fun RenderError(state: EditorState, onRetry: () -> Unit) {
    state.error?.let { error ->
        Row(Modifier.fillMaxWidth().heightIn(max = 96.dp).background(MaterialTheme.colorScheme.errorContainer)
            .padding(horizontal = 12.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(error, Modifier.weight(1f).verticalScroll(rememberScrollState()), style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onErrorContainer)
            if (state.photo != null) TextButton(onClick = onRetry, enabled = state.operation == Operation.NONE) { Text(stringResource(R.string.retry)) }
        }
    }
}

@Composable
private fun EmptyEditor(onAlbum: () -> Unit, onFile: () -> Unit, onLicenses: () -> Unit) {
    val icon = assetBitmap("AppIcon.png")
    Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(12.dp)) {
        icon?.let { Image(it.asImageBitmap(), null, Modifier.size(80.dp)) }
        Text(stringResource(R.string.empty_title), color = Color.White, style = MaterialTheme.typography.titleLarge)
        Button(onClick = onAlbum) { Icon(Icons.Outlined.PhotoLibrary, null); Spacer(Modifier.width(8.dp)); Text(stringResource(R.string.open_album)) }
        TextButton(onClick = onFile) { Text(stringResource(R.string.open_file), color = Color(0xFFE9CE55)) }
        TextButton(onClick = onLicenses) { Text(stringResource(R.string.licenses), color = Color(0xFFB8BABC)) }
    }
}

@Composable
private fun PhotoCanvas(pair: PreviewPair, compare: Boolean) {
    BoxWithConstraints(Modifier.fillMaxSize()) {
        if (!compare) PhotoPane(pair.result, null, Modifier.fillMaxSize())
        else if (maxWidth > maxHeight) Row(Modifier.fillMaxSize()) {
            PhotoPane(pair.neutral, stringResource(R.string.neutral), Modifier.weight(1f).fillMaxHeight())
            PhotoPane(pair.result, stringResource(R.string.result), Modifier.weight(1f).fillMaxHeight())
        } else Column(Modifier.fillMaxSize()) {
            PhotoPane(pair.neutral, stringResource(R.string.neutral), Modifier.weight(1f).fillMaxWidth())
            PhotoPane(pair.result, stringResource(R.string.result), Modifier.weight(1f).fillMaxWidth())
        }
    }
}

@Composable
private fun PhotoPane(bitmap: Bitmap, label: String?, modifier: Modifier) {
    Box(modifier.padding(4.dp)) {
        Image(bitmap.asImageBitmap(), label, Modifier.fillMaxSize(), contentScale = ContentScale.Fit)
        label?.let { Text(it, Modifier.align(Alignment.BottomStart).background(Color.Black.copy(alpha = .65f)).padding(6.dp),
            color = Color.White, style = MaterialTheme.typography.labelSmall) }
    }
}

@Composable
private fun AdjustmentDock(state: EditorState, modifier: Modifier, onEdit: (EditSettings, Boolean) -> Unit, onReset: () -> Unit) {
    var tool by rememberSaveable { mutableIntStateOf(0) }
    val titles = listOf(R.string.film, R.string.strength, R.string.exposure, R.string.white_balance)
    val edits = state.edits
    Column(modifier.verticalScroll(rememberScrollState())) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            LazyRow(Modifier.weight(1f)) {
                items(titles.size) { index ->
                    Tab(selected = tool == index, onClick = { tool = index }, enabled = state.controlsEnabled,
                        modifier = Modifier.widthIn(min = 76.dp), text = {
                            Text(stringResource(titles[index]), color = if (tool == index) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface)
                        })
                }
            }
            ToolIcon(Icons.Outlined.RestartAlt, R.string.reset, state.controlsEnabled, onReset)
        }
        when (tool) {
            0 -> LazyRow(contentPadding = PaddingValues(12.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                items(Film.all, key = { it.id }) { film ->
                    val artwork = if (film.file != null) assetBitmap("artwork/${film.id}.png") else null
                    Column(Modifier.width(80.dp).clickable(enabled = state.controlsEnabled) { onEdit(edits.copy(film = film.id), false) },
                        horizontalAlignment = Alignment.CenterHorizontally) {
                        Box(Modifier.size(72.dp).border(if (edits.film == film.id) 2.dp else 0.dp,
                            if (edits.film == film.id) MaterialTheme.colorScheme.primary else Color.Transparent).padding(3.dp), contentAlignment = Alignment.Center) {
                            if (artwork != null) Image(artwork.asImageBitmap(), film.name, Modifier.fillMaxSize())
                            else Icon(Icons.Outlined.Image, film.name, Modifier.size(32.dp))
                        }
                        Text(film.name, Modifier.heightIn(min = 46.dp).padding(top = 4.dp), maxLines = 3, style = MaterialTheme.typography.labelSmall)
                    }
                }
            }
            1 -> NumericControl(R.string.strength, edits.strength * 100, 0f..100f, "%", state.controlsEnabled,
                { value, dragging -> onEdit(edits.copy(strength = value / 100), dragging) }, { onEdit(edits.copy(strength = 1f), false) })
            2 -> NumericControl(R.string.exposure, edits.exposure, -5f..5f, "EV", state.controlsEnabled,
                { value, dragging -> onEdit(edits.copy(exposure = value), dragging) }, { onEdit(edits.copy(exposure = 0f), false) })
            3 -> {
                val calibrated = state.preview?.temperature?.isFinite() == true
                Row(Modifier.padding(horizontal = 12.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    FilterChip(selected = !edits.customWb, onClick = { onEdit(edits.copy(customWb = false), false) },
                        enabled = state.controlsEnabled, label = { Text(stringResource(R.string.as_shot)) })
                    FilterChip(selected = edits.customWb, onClick = { onEdit(edits.copy(customWb = true), false) },
                        enabled = state.controlsEnabled && calibrated, label = { Text(stringResource(R.string.custom_wb)) })
                }
                if (!calibrated) Text(stringResource(R.string.wb_unavailable), Modifier.padding(12.dp), style = MaterialTheme.typography.bodySmall)
                else {
                    NumericControl(R.string.temperature, edits.temperature, 2000f..50000f, "K", state.controlsEnabled,
                        { value, dragging -> onEdit(edits.copy(customWb = true, temperature = value), dragging) },
                        { onEdit(edits.copy(customWb = false), false) }, reciprocal = true)
                    NumericControl(R.string.tint, edits.tint, -150f..150f, "", state.controlsEnabled,
                        { value, dragging -> onEdit(edits.copy(customWb = true, tint = value), dragging) },
                        { onEdit(edits.copy(customWb = false), false) })
                }
            }
        }
    }
}

@Composable
private fun NumericControl(label: Int, value: Float, range: ClosedFloatingPointRange<Float>, unit: String,
    enabled: Boolean, onValue: (Float, Boolean) -> Unit, onReset: () -> Unit, reciprocal: Boolean = false) {
    var editing by remember { mutableStateOf(false) }
    val formatted = if (unit == "EV") String.format(Locale.ROOT, "%.2f", value) else value.roundToInt().toString()
    var input by remember { mutableStateOf("") }
    val parsed = input.toFloatOrNull()?.takeIf { it.isFinite() && it in range }
    Column(Modifier.padding(horizontal = 16.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(label), Modifier.weight(1f), style = MaterialTheme.typography.labelLarge)
            TextButton(onClick = { input = formatted; editing = true }, enabled = enabled) { Text("$formatted $unit") }
            ToolIcon(Icons.Outlined.RestartAlt, R.string.reset_value, enabled, onReset)
        }
        val sliderRange = if (reciprocal) (-1f / range.start)..(-1f / range.endInclusive) else range
        Slider(value = if (reciprocal) -1f / value else value, onValueChange = {
            onValue(if (reciprocal) (-1f / it).coerceIn(range) else it, true)
        }, onValueChangeFinished = { onValue(value, false) }, valueRange = sliderRange, enabled = enabled)
    }
    if (editing) AlertDialog(onDismissRequest = { editing = false }, title = { Text(stringResource(label)) },
        text = {
            OutlinedTextField(value = input, onValueChange = { input = it }, singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal), isError = parsed == null,
                label = { Text("${range.start} … ${range.endInclusive} $unit") },
                supportingText = { if (parsed == null) Text(stringResource(R.string.invalid_value)) })
        }, confirmButton = { TextButton(onClick = { parsed?.let { onValue(it, false) }; editing = false }, enabled = parsed != null) { Text(stringResource(R.string.confirm)) } },
        dismissButton = { TextButton(onClick = { editing = false }) { Text(stringResource(R.string.cancel)) } })
}

@Composable
private fun assetBitmap(path: String): Bitmap? {
    val context = LocalContext.current
    val bitmap by produceState<Bitmap?>(null, path) {
        value = withContext(Dispatchers.IO) {
            context.assets.open(path).use { BitmapFactory.decodeStream(it, null, BitmapFactory.Options().apply { inSampleSize = 4 }) }
        }
    }
    return bitmap
}
