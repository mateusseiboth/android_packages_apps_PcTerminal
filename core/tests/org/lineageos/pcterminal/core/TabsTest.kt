package org.lineageos.pcterminal.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class TabsTest {
    @Test
    fun opensSelectsAndClosesTabs() {
        var tabs = Tabs<String>()
        tabs = tabs.open("a").open("b")
        assertEquals(listOf("a", "b"), tabs.items)
        assertEquals(1, tabs.selected)
        tabs = tabs.select(0)
        assertEquals("a", tabs.current)
        tabs = tabs.close(0)
        assertEquals(listOf("b"), tabs.items)
        assertEquals(0, tabs.selected)
        tabs = tabs.close(0)
        assertNull(tabs.current)
    }

    @Test
    fun closingALaterTabKeepsTheSelection() {
        val tabs = Tabs<String>().open("a").open("b").open("c").select(0).close(2)
        assertEquals("a", tabs.current)
        assertEquals(1, Tabs<String>().open("a").open("b").open("c").select(2).close(0).selected)
    }

    @Test
    fun selectsTheNextAndPreviousTabAround() {
        val tabs = Tabs<String>().open("a").open("b").open("c")
        assertEquals(0, tabs.selectNext().selected)
        assertEquals(1, tabs.selectPrevious().selected)
        assertEquals(tabs, tabs.select(9))
    }
}
