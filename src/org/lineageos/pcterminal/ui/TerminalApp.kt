package org.lineageos.pcterminal.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.PrimaryScrollableTabRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import org.lineageos.pcterminal.Page
import org.lineageos.pcterminal.R
import org.lineageos.pcterminal.TerminalTab
import org.lineageos.pcterminal.TerminalUi
import org.lineageos.pcterminal.TerminalViewModel

/** The window: the terminal tabs, or the picker, install and manage pages. */
@Composable
fun TerminalApp(viewModel: TerminalViewModel) {
    val ui by viewModel.ui.collectAsStateWithLifecycle()
    BackHandler(enabled = ui.page == Page.INSTALL || ui.page == Page.MANAGE, onBack = viewModel::onBack)
    Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.surfaceContainer) {
        Column(Modifier.fillMaxSize()) {
            TopBar(ui, viewModel)
            Box(Modifier.fillMaxSize()) { PAGES.getValue(ui.page)(ui, viewModel) }
        }
    }
}

private val PAGES: Map<Page, @Composable (TerminalUi, TerminalViewModel) -> Unit> = mapOf(
    Page.LOADING to { _, _ -> Loading() },
    Page.PICKER to { ui, vm -> PickerPage(ui, vm) },
    Page.INSTALL to { ui, vm -> InstallPage(ui, vm) },
    Page.MANAGE to { ui, vm -> ManagePage(ui, vm) },
    Page.TERMINAL to { ui, vm -> TerminalPage(ui, vm) },
)

@Composable
private fun Loading() {
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) { CircularProgressIndicator() }
}

@Composable
private fun TopBar(ui: TerminalUi, viewModel: TerminalViewModel) {
    Row(Modifier.fillMaxWidth().padding(horizontal = 8.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.weight(1f)) {
            if (ui.tabs.items.isNotEmpty()) TabStrip(ui, viewModel)
        }
        IconButton(onClick = viewModel::openNewTab, enabled = ui.tabs.items.isNotEmpty()) {
            Icon(Icons.Default.Add, stringResource(R.string.action_new_tab))
        }
        IconButton(onClick = { viewModel.show(Page.MANAGE) }) { Icon(Icons.Default.Tune, stringResource(R.string.action_manage)) }
        IconButton(onClick = viewModel::showInstall) { Icon(Icons.Default.Download, stringResource(R.string.action_install)) }
    }
}

@Composable
private fun TabStrip(ui: TerminalUi, viewModel: TerminalViewModel) {
    val selected = ui.tabs.selected.coerceAtLeast(0)
    PrimaryScrollableTabRow(selectedTabIndex = selected, edgePadding = 0.dp, containerColor = MaterialTheme.colorScheme.surfaceContainer) {
        ui.tabs.items.forEachIndexed { index, tab ->
            Tab(selected = index == selected && ui.page == Page.TERMINAL, onClick = { viewModel.selectTab(index) }) {
                TabLabel(tab, onClose = { viewModel.closeTab(tab) })
            }
        }
    }
}

@Composable
private fun TabLabel(tab: TerminalTab, onClose: () -> Unit) {
    Row(Modifier.padding(start = 12.dp), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        val title = tab.session.title.ifBlank { tab.container }
        val label = if (tab.isRoot) stringResource(R.string.tab_root, title) else title
        Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis)
        IconButton(onClick = onClose) { Icon(Icons.Default.Close, stringResource(R.string.action_close_tab)) }
    }
}

@Composable
private fun TerminalPage(ui: TerminalUi, viewModel: TerminalViewModel) {
    val tab = ui.tabs.current ?: return
    ErrorBanner(ui.error, viewModel::onErrorShown)
    TerminalView(
        tab,
        onSize = viewModel::onTerminalSize,
        onShortcut = { shortcut -> SHORTCUT_ACTIONS[shortcut]?.invoke(viewModel, tab) },
        modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
    )
}

private val SHORTCUT_ACTIONS: Map<Shortcut, (TerminalViewModel, TerminalTab) -> Unit> = mapOf(
    Shortcut.NEW_TAB to { vm, _ -> vm.openNewTab() },
    Shortcut.CLOSE_TAB to { vm, tab -> vm.closeTab(tab) },
    Shortcut.NEXT_TAB to { vm, _ -> vm.selectNextTab() },
    Shortcut.PREVIOUS_TAB to { vm, _ -> vm.selectPreviousTab() },
)
