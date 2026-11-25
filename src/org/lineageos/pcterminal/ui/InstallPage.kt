package org.lineageos.pcterminal.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
import androidx.compose.material3.LinearProgressIndicator
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
import org.lineageos.pcterminal.Catalog
import org.lineageos.pcterminal.InstallState
import org.lineageos.pcterminal.InstallStep
import org.lineageos.pcterminal.R
import org.lineageos.pcterminal.TerminalUi
import org.lineageos.pcterminal.TerminalViewModel
import org.lineageos.pcterminal.core.DistroGroup
import org.lineageos.pcterminal.core.DistroImage
import org.lineageos.pcterminal.core.Downloads
import org.lineageos.pcterminal.core.Formats

/** Choose a distribution and release, name the container, then follow the download and the install. */
@Composable
fun InstallPage(ui: TerminalUi, viewModel: TerminalViewModel) {
    var chosen by remember { mutableStateOf<DistroImage?>(null) }
    var name by remember { mutableStateOf("") }
    InstallFailureDialog(ui.install, viewModel::onInstallFailureShown)
    Page {
        item { Text(stringResource(R.string.install_title), style = MaterialTheme.typography.headlineSmall) }
        (ui.install as? InstallState.Running)?.let { running ->
            item { InstallProgress(running) }
            return@Page
        }
        chosen?.let { image ->
            item { NameForm(image, name, ui, onName = { name = it }, onInstall = { viewModel.install(image, name) }, onBack = { chosen = null }) }
            return@Page
        }
        catalogItems(ui.catalog, viewModel) { image ->
            chosen = image
            name = viewModel.suggestName(image)
        }
    }
}

private fun androidx.compose.foundation.lazy.LazyListScope.catalogItems(
    catalog: Catalog,
    viewModel: TerminalViewModel,
    onChoose: (DistroImage) -> Unit,
) {
    when (catalog) {
        Catalog.Loading -> item {
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
                CircularProgressIndicator()
                Text(stringResource(R.string.install_loading))
            }
        }
        Catalog.Failed -> item {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(stringResource(R.string.install_list_failed))
                OutlinedButton(onClick = viewModel::showInstall) { Text(stringResource(R.string.action_retry)) }
            }
        }
        is Catalog.Loaded -> items(catalog.groups, key = { it.distro }) { DistroCard(it, onChoose) }
    }
}

@Composable
private fun DistroCard(group: DistroGroup, onChoose: (DistroImage) -> Unit) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(group.distro, style = MaterialTheme.typography.titleMedium)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                group.images.forEach { image ->
                    FilterChip(selected = false, onClick = { onChoose(image) }, label = { Text(image.release) })
                }
            }
        }
    }
}

@Composable
private fun NameForm(image: DistroImage, name: String, ui: TerminalUi, onName: (String) -> Unit, onInstall: () -> Unit, onBack: () -> Unit) {
    val problem = findNameProblem(name, ui.containers.map { it.name }.toSet())
    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text(stringResource(R.string.install_chosen, image.distro, image.release), style = MaterialTheme.typography.titleMedium)
        OutlinedTextField(
            value = name,
            onValueChange = onName,
            label = { Text(stringResource(R.string.install_name_label)) },
            isError = problem != null,
            supportingText = { problem?.let { Text(stringResource(it)) } },
            singleLine = true,
        )
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = onInstall, enabled = problem == null) { Text(stringResource(R.string.install_button)) }
            OutlinedButton(onClick = onBack) { Text(stringResource(R.string.action_back)) }
        }
    }
}

@Composable
private fun InstallProgress(running: InstallState.Running) {
    Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text(running.name, style = MaterialTheme.typography.titleMedium)
        when (val step = running.step) {
            is InstallStep.Downloading -> {
                Text(stringResource(R.string.install_downloading, Formats.formatBytes(step.done), Formats.formatBytes(step.total.coerceAtLeast(step.done))))
                Downloads.findFraction(step.done, step.total)?.let { LinearProgressIndicator(progress = { it }, Modifier.fillMaxWidth()) }
                    ?: LinearProgressIndicator(Modifier.fillMaxWidth())
            }
            InstallStep.Checking -> {
                Text(stringResource(R.string.install_checking))
                LinearProgressIndicator(Modifier.fillMaxWidth())
            }
            is InstallStep.Extracting -> {
                Text(stringResource(R.string.install_extracting, step.files.toString()))
                LinearProgressIndicator(Modifier.fillMaxWidth())
            }
            null -> LinearProgressIndicator(Modifier.fillMaxWidth())
        }
    }
}

@Composable
private fun InstallFailureDialog(install: InstallState?, onDismiss: () -> Unit) {
    val failed = install as? InstallState.Failed ?: return
    AlertDialog(
        onDismissRequest = onDismiss,
        confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_ok)) } },
        text = { Text(stringResource(INSTALL_FAILURE_TEXTS.getValue(failed.failure))) },
    )
}
