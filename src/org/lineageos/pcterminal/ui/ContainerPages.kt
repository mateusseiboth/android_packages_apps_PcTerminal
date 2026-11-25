package org.lineageos.pcterminal.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import java.util.Locale
import org.lineageos.pcterminal.R
import org.lineageos.pcterminal.TerminalUi
import org.lineageos.pcterminal.TerminalViewModel
import org.lineageos.pcterminal.Usage
import org.lineageos.pcterminal.core.Container
import org.lineageos.pcterminal.core.ContainerError
import org.lineageos.pcterminal.core.ContainerState
import org.lineageos.pcterminal.core.Formats
import org.lineageos.pcterminal.core.Names

private val PAGE_MAX_WIDTH = 960.dp
private val PAGE_PADDING = PaddingValues(horizontal = 32.dp, vertical = 16.dp)

/** A centered column, as wide as the window up to [PAGE_MAX_WIDTH]. */
@Composable
fun Page(content: LazyListScope.() -> Unit) {
    LazyColumn(
        Modifier.widthIn(max = PAGE_MAX_WIDTH).fillMaxHeight().fillMaxWidth(),
        contentPadding = PAGE_PADDING,
        verticalArrangement = Arrangement.spacedBy(12.dp),
        content = content,
    )
}

@Composable
fun ErrorBanner(error: ContainerError?, onDismiss: () -> Unit) {
    error ?: return
    AlertDialog(
        onDismissRequest = onDismiss,
        confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_ok)) } },
        text = { Text(stringResource(findErrorText(error))) },
    )
}

private val STATE_TEXTS = mapOf(ContainerState.RUNNING to R.string.state_running, ContainerState.STOPPED to R.string.state_stopped)

@Composable
private fun ContainerHeader(container: Container) {
    Text(container.name, style = MaterialTheme.typography.titleMedium)
    Text(
        stringResource(R.string.container_summary, container.distro, container.release, stringResource(STATE_TEXTS.getValue(container.state))),
        style = MaterialTheme.typography.bodyMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

// ---- picker ----------------------------------------------------------------------------------

@Composable
fun PickerPage(ui: TerminalUi, viewModel: TerminalViewModel) {
    ErrorBanner(ui.error, viewModel::onErrorShown)
    Page {
        item { Text(stringResource(R.string.picker_title), style = MaterialTheme.typography.headlineSmall) }
        if (ui.containers.isEmpty()) item { Text(stringResource(R.string.manage_empty)) }
        items(ui.containers, key = { it.name }) { container ->
            Card(Modifier.fillMaxWidth()) {
                Row(Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) { ContainerHeader(container) }
                    Button(onClick = { viewModel.openShell(container.name, asRoot = false) }, enabled = container.name !in ui.busy) {
                        Text(stringResource(R.string.action_open))
                    }
                }
            }
        }
        item {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = viewModel::showInstall) { Text(stringResource(R.string.action_install)) }
                OutlinedButton(onClick = { viewModel.show(org.lineageos.pcterminal.Page.MANAGE) }) { Text(stringResource(R.string.action_manage)) }
            }
        }
    }
}

// ---- manage ----------------------------------------------------------------------------------

private sealed interface Dialog {
    data class Delete(val name: String) : Dialog
    data class Rename(val name: String) : Dialog
}

@Composable
fun ManagePage(ui: TerminalUi, viewModel: TerminalViewModel) {
    var dialog by remember { mutableStateOf<Dialog?>(null) }
    ErrorBanner(ui.error, viewModel::onErrorShown)
    Page {
        item { Text(stringResource(R.string.manage_title), style = MaterialTheme.typography.headlineSmall) }
        if (ui.containers.isEmpty()) item { Text(stringResource(R.string.manage_empty)) }
        items(ui.containers, key = { it.name }) { container ->
            ManageCard(container, ui.usage[container.name], container.name in ui.busy, viewModel, onDialog = { dialog = it })
        }
    }
    when (val d = dialog) {
        is Dialog.Delete -> DeleteDialog(d.name, onConfirm = { viewModel.delete(d.name) }, onDismiss = { dialog = null })
        is Dialog.Rename -> RenameDialog(d.name, ui, onConfirm = { viewModel.rename(d.name, it) }, onDismiss = { dialog = null })
        null -> Unit
    }
}

@Composable
private fun ManageCard(container: Container, usage: Usage?, isBusy: Boolean, viewModel: TerminalViewModel, onDialog: (Dialog) -> Unit) {
    val isRunning = container.state == ContainerState.RUNNING
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            ContainerHeader(container)
            usage?.let { UsageLine(it) }
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = { viewModel.openShell(container.name, asRoot = false) }, enabled = !isBusy) { Text(stringResource(R.string.action_open)) }
                OutlinedButton(onClick = { viewModel.openShell(container.name, asRoot = true) }, enabled = !isBusy) {
                    Text(stringResource(R.string.action_open_root))
                }
                val toggle = if (isRunning) viewModel::stop else viewModel::start
                OutlinedButton(onClick = { toggle(container.name) }, enabled = !isBusy) {
                    Text(stringResource(if (isRunning) R.string.action_stop else R.string.action_start))
                }
                OutlinedButton(onClick = { onDialog(Dialog.Rename(container.name)) }, enabled = !isBusy && !isRunning) {
                    Text(stringResource(R.string.action_rename))
                }
                OutlinedButton(onClick = { onDialog(Dialog.Delete(container.name)) }, enabled = !isBusy) { Text(stringResource(R.string.action_delete)) }
            }
        }
    }
}

@Composable
private fun UsageLine(usage: Usage) {
    val parts = listOfNotNull(
        usage.memoryBytes?.let { stringResource(R.string.stats_memory, Formats.formatBytes(it)) },
        usage.cpuPercent?.let { stringResource(R.string.stats_cpu, String.format(Locale.getDefault(), "%.0f%%", it)) },
        usage.pids?.let { stringResource(R.string.stats_processes, it.toInt()) },
        usage.diskBytes?.let { stringResource(R.string.stats_disk, Formats.formatBytes(it)) },
    )
    Text(parts.joinToString("   "), style = MaterialTheme.typography.bodySmall)
}

@Composable
private fun DeleteDialog(name: String, onConfirm: () -> Unit, onDismiss: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.delete_title, name)) },
        text = { Text(stringResource(R.string.delete_text)) },
        confirmButton = {
            TextButton(onClick = {
                onConfirm()
                onDismiss()
            }) { Text(stringResource(R.string.action_delete)) }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_cancel)) } },
    )
}

@Composable
private fun RenameDialog(name: String, ui: TerminalUi, onConfirm: (String) -> Unit, onDismiss: () -> Unit) {
    var text by remember { mutableStateOf(name) }
    val problem = findNameProblem(text, ui.containers.map { it.name }.toSet() - name)
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.rename_title, name)) },
        text = {
            OutlinedTextField(
                value = text,
                onValueChange = { text = it },
                label = { Text(stringResource(R.string.install_name_label)) },
                isError = problem != null,
                supportingText = { problem?.let { Text(stringResource(it)) } },
                singleLine = true,
            )
        },
        confirmButton = {
            TextButton(enabled = problem == null && text != name, onClick = {
                onConfirm(text)
                onDismiss()
            }) { Text(stringResource(R.string.action_rename)) }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_cancel)) } },
    )
}

/** Rules a new container name must pass, with what to say when it does not. */
private val NAME_RULES: List<Pair<(String, Set<String>) -> Boolean, Int>> = listOf(
    { name: String, _: Set<String> -> Names.isValidContainerName(name) } to R.string.error_bad_name,
    { name: String, taken: Set<String> -> name !in taken } to R.string.error_exists,
)

/** Why a container name cannot be used, or null. */
fun findNameProblem(name: String, taken: Set<String>): Int? = NAME_RULES.firstOrNull { (passes, _) -> !passes(name, taken) }?.second
