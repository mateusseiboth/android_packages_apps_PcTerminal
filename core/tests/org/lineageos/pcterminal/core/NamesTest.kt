package org.lineageos.pcterminal.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class NamesTest {
    @Test
    fun containerNamesFollowTheDaemonRule() {
        assertTrue(Names.isValidContainerName("debian-2"))
        assertFalse(Names.isValidContainerName("Debian"))
        assertFalse(Names.isValidContainerName("-x"))
        assertFalse(Names.isValidContainerName("a".repeat(32)))
    }

    @Test
    fun suggestsAFreeNameFromTheDistro() {
        assertEquals("debian", Names.suggestContainerName("debian", setOf("ubuntu")))
        assertEquals("debian-2", Names.suggestContainerName("debian", setOf("debian")))
        assertEquals("debian-3", Names.suggestContainerName("debian", setOf("debian", "debian-2")))
        assertEquals("opensuse-tumbleweed", Names.suggestContainerName("openSUSE Tumbleweed", emptySet()))
    }

    @Test
    fun buildsALinuxUserNameFromThePersonsName() {
        assertEquals("joao", Names.buildUserName("João"))
        assertEquals("maria_silva", Names.buildUserName("Maria Silva"))
        assertEquals("user", Names.buildUserName(""))
        assertEquals("user", Names.buildUserName("root"))
        assertEquals("_2pac", Names.buildUserName("2Pac"))
        assertEquals("a".repeat(32), Names.buildUserName("a".repeat(40)))
    }
}
