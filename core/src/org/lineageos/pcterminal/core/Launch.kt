package org.lineageos.pcterminal.core

/** What the app shows when it opens: the one container's shell, a picker, or the install page. */
sealed interface LaunchPlan {
    data class OpenShell(val name: String) : LaunchPlan
    data object ShowPicker : LaunchPlan
    data object ShowInstall : LaunchPlan

    companion object {
        private val BY_COUNT: Map<Int, (List<Container>) -> LaunchPlan> = mapOf(
            0 to { _ -> ShowInstall },
            1 to { list -> OpenShell(list.single().name) },
        )

        fun decide(containers: List<Container>): LaunchPlan = BY_COUNT[containers.size]?.invoke(containers) ?: ShowPicker
    }
}
