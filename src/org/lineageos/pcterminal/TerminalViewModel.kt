package org.lineageos.pcterminal

import android.app.Application
import android.os.UserManager
import android.system.Os
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import java.io.FileDescriptor
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import org.lineageos.pcterminal.core.Container
import org.lineageos.pcterminal.core.ContainerClient
import org.lineageos.pcterminal.core.ContainerError
import org.lineageos.pcterminal.core.ContainerState
import org.lineageos.pcterminal.core.CpuReading
import org.lineageos.pcterminal.core.DaemonResult
import org.lineageos.pcterminal.core.DistroGroup
import org.lineageos.pcterminal.core.DistroImage
import org.lineageos.pcterminal.core.Formats
import org.lineageos.pcterminal.core.ImageIndex
import org.lineageos.pcterminal.core.LaunchPlan
import org.lineageos.pcterminal.core.Names
import org.lineageos.pcterminal.core.Tabs
import org.lineageos.pcterminal.core.valueOrNull

enum class Page { LOADING, PICKER, INSTALL, MANAGE, TERMINAL }

sealed interface Catalog {
    data object Loading : Catalog
    data class Loaded(val groups: List<DistroGroup>) : Catalog
    data object Failed : Catalog
}

sealed interface InstallState {
    data class Running(val name: String, val step: InstallStep?) : InstallState
    data class Failed(val failure: InstallFailure) : InstallState
}

/** What the resources line of a running container shows. */
data class Usage(val memoryBytes: Long?, val cpuPercent: Double?, val pids: Long?, val diskBytes: Long?)

data class TerminalUi(
    val page: Page = Page.LOADING,
    val containers: List<Container> = emptyList(),
    val tabs: Tabs<TerminalTab> = Tabs(),
    val catalog: Catalog = Catalog.Loading,
    val install: InstallState? = null,
    val usage: Map<String, Usage> = emptyMap(),
    val error: ContainerError? = null,
    val busy: Set<String> = emptySet(),
)

/** The containers, the open shells and the install, for one window. */
class TerminalViewModel(application: Application) : AndroidViewModel(application) {
    private val state = MutableStateFlow(TerminalUi())
    val ui: StateFlow<TerminalUi> = state
    private val installer = Installer(application.cacheDir)
    private var lastSize = DEFAULT_ROWS to DEFAULT_COLS
    private var usageJob: Job? = null
    private val cpuReadings = mutableMapOf<String, CpuReading>()

    val userName: String = Names.buildUserName(application.getSystemService(UserManager::class.java)?.userName.orEmpty())

    init {
        viewModelScope.launch(Dispatchers.IO) { onLaunch() }
    }

    private fun onLaunch() {
        val result = DaemonConnection.withClient { it.list() }
        val containers = result.valueOrNull() ?: return state.update { it.copy(page = Page.PICKER, error = ContainerError.UNREACHABLE) }
        state.update { it.copy(containers = containers) }
        when (val plan = LaunchPlan.decide(containers)) {
            is LaunchPlan.OpenShell -> openShell(plan.name, asRoot = false)
            LaunchPlan.ShowPicker -> show(Page.PICKER)
            LaunchPlan.ShowInstall -> showInstall()
        }
    }

    // ---- pages ---------------------------------------------------------------------------

    fun show(page: Page) {
        state.update { it.copy(page = page, error = null) }
        if (page == Page.MANAGE) watchUsage() else stopUsage()
    }

    fun showInstall() {
        show(Page.INSTALL)
        if (state.value.catalog is Catalog.Loaded) return
        state.update { it.copy(catalog = Catalog.Loading) }
        viewModelScope.launch(Dispatchers.IO) {
            val catalog = runCatching { Catalog.Loaded(ImageIndex.groupByDistro(installer.fetchImages())) }.getOrElse { Catalog.Failed }
            state.update { it.copy(catalog = catalog) }
        }
    }

    fun onBack() = show(if (state.value.tabs.items.isEmpty()) Page.PICKER else Page.TERMINAL)

    fun onErrorShown() = state.update { it.copy(error = null) }

    fun refresh() {
        viewModelScope.launch(Dispatchers.IO) { reloadContainers() }
    }

    private fun reloadContainers() {
        val result = DaemonConnection.withClient { it.list() }
        state.update { ui -> result.valueOrNull()?.let { ui.copy(containers = it) } ?: ui.copy(error = ContainerError.UNREACHABLE) }
    }

    // ---- shells --------------------------------------------------------------------------

    fun openShell(name: String, asRoot: Boolean) {
        viewModelScope.launch(Dispatchers.IO) {
            withBusy(name) {
                val (rows, cols) = lastSize
                val dns = findDnsServers(getApplication())
                val opened = DaemonConnection.withClient { client ->
                    // A stopped container starts here with the network's name servers.
                    client.start(name, dns)
                    client.openShell(name, rows, cols, asRoot)
                }
                when (val result = opened) {
                    is DaemonResult.Ok -> addTab(TerminalTab(name, asRoot, result.value.pty, rows, cols, ::sendResize))
                    is DaemonResult.Failed -> state.update { it.copy(error = result.error) }
                }
                reloadContainers()
            }
        }
    }

    private fun addTab(tab: TerminalTab) {
        state.update { it.copy(tabs = it.tabs.open(tab), page = Page.TERMINAL) }
        tab.start(::onShellEnded)
    }

    private fun onShellEnded(tab: TerminalTab) {
        state.update { ui ->
            val index = ui.tabs.items.indexOf(tab)
            val tabs = ui.tabs.close(index)
            ui.copy(tabs = tabs, page = if (tabs.items.isEmpty() && ui.page == Page.TERMINAL) Page.PICKER else ui.page)
        }
        if (state.value.page == Page.PICKER) reloadContainers()
    }

    fun selectTab(index: Int) = state.update { it.copy(tabs = it.tabs.select(index), page = Page.TERMINAL) }

    fun selectNextTab() = state.update { it.copy(tabs = it.tabs.selectNext()) }

    fun selectPreviousTab() = state.update { it.copy(tabs = it.tabs.selectPrevious()) }

    fun closeTab(tab: TerminalTab) = tab.close()

    /** A new shell in the container of the tab in front, or the picker when there is none. */
    fun openNewTab() {
        val current = state.value.tabs.current ?: return show(Page.PICKER)
        openShell(current.container, current.isRoot)
    }

    fun onTerminalSize(rows: Int, cols: Int) {
        lastSize = rows to cols
    }

    private fun sendResize(pty: FileDescriptor, rows: Int, cols: Int) {
        val copy = runCatching { Os.dup(pty) }.getOrNull() ?: return
        viewModelScope.launch(Dispatchers.IO) {
            DaemonConnection.withClient { it.resize(copy, rows, cols) }
            runCatching { Os.close(copy) }
        }
    }

    // ---- managing ------------------------------------------------------------------------

    fun start(name: String) = runOnContainer(name) { it.start(name, findDnsServers(getApplication())) }

    fun stop(name: String) = runOnContainer(name) { it.stop(name) }

    fun delete(name: String) = runOnContainer(name) { client ->
        state.value.tabs.items.filter { it.container == name }.forEach(TerminalTab::close)
        client.delete(name)
    }

    fun rename(name: String, newName: String) = runOnContainer(name) { it.rename(name, newName) }

    private fun <T> runOnContainer(name: String, action: (ContainerClient<FileDescriptor>) -> DaemonResult<T>) {
        viewModelScope.launch(Dispatchers.IO) {
            withBusy(name) {
                val result = DaemonConnection.withClient(action)
                if (result is DaemonResult.Failed) state.update { it.copy(error = result.error) }
                reloadContainers()
            }
        }
    }

    private inline fun withBusy(name: String, block: () -> Unit) {
        state.update { it.copy(busy = it.busy + name) }
        try {
            block()
        } finally {
            state.update { it.copy(busy = it.busy - name) }
        }
    }

    // ---- resources -----------------------------------------------------------------------

    private fun watchUsage() {
        if (usageJob?.isActive == true) return
        usageJob = viewModelScope.launch(Dispatchers.IO) {
            while (isActive) {
                readUsage()
                delay(USAGE_EVERY_MS)
            }
        }
    }

    private fun stopUsage() {
        usageJob?.cancel()
        usageJob = null
        cpuReadings.clear()
    }

    private fun readUsage() {
        val usage = state.value.containers.associate { c ->
            val stats = DaemonConnection.withClient { it.stats(c.name) }.valueOrNull()
            val now = System.currentTimeMillis()
            val reading = stats?.cpuUsec?.let { CpuReading(it, now) }
            val percent = reading?.let { Formats.findCpuPercent(cpuReadings[c.name], it) }
            reading?.let { cpuReadings[c.name] = it }
            c.name to Usage(stats?.memoryBytes, percent, stats?.pids, stats?.diskBytes)
        }
        state.update { it.copy(usage = usage) }
    }

    // ---- install -------------------------------------------------------------------------

    fun suggestName(image: DistroImage): String = Names.suggestContainerName(image.distro, state.value.containers.map { it.name }.toSet())

    fun isNameTaken(name: String) = state.value.containers.any { it.name == name }

    fun install(image: DistroImage, name: String) {
        state.update { it.copy(install = InstallState.Running(name, null)) }
        viewModelScope.launch(Dispatchers.IO) {
            val outcome = installer.install(image, name, userName) { step ->
                state.update { it.copy(install = InstallState.Running(name, step)) }
            }
            when (outcome) {
                is InstallOutcome.Done -> {
                    state.update { it.copy(install = null) }
                    reloadContainers()
                    openShell(outcome.container.name, asRoot = false)
                }
                is InstallOutcome.Failed -> state.update { it.copy(install = InstallState.Failed(outcome.failure)) }
            }
        }
    }

    fun onInstallFailureShown() = state.update { it.copy(install = null) }

    fun isRunning(name: String) = state.value.containers.any { it.name == name && it.state == ContainerState.RUNNING }

    override fun onCleared() {
        state.value.tabs.items.forEach(TerminalTab::close)
    }

    private companion object {
        const val DEFAULT_ROWS = 24
        const val DEFAULT_COLS = 80
        const val USAGE_EVERY_MS = 2_000L
    }
}
