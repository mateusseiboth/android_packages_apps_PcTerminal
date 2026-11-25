package org.lineageos.pcterminal.core

import org.junit.Assert.assertEquals
import org.junit.Test

class LaunchTest {
    private fun container(name: String) = Container(name, "debian", "trixie", 0, "user", ContainerState.STOPPED)

    @Test
    fun oneContainerOpensItsShell() {
        assertEquals(LaunchPlan.OpenShell("debian"), LaunchPlan.decide(listOf(container("debian"))))
    }

    @Test
    fun severalContainersShowThePicker() {
        assertEquals(LaunchPlan.ShowPicker, LaunchPlan.decide(listOf(container("a"), container("b"))))
    }

    @Test
    fun noContainerOffersAnInstall() {
        assertEquals(LaunchPlan.ShowInstall, LaunchPlan.decide(emptyList()))
    }
}
