package org.lineageos.pcterminal.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ImageIndexTest {
    private val index = """
        almalinux;10;amd64;cloud;20261117_23:08;/images/almalinux/10/amd64/cloud/20261117_23:08/
        debian;trixie;amd64;default;20261118_05:24;/images/debian/trixie/amd64/default/20261118_05:24/
        debian;trixie;amd64;default;20261117_05:24;/images/debian/trixie/amd64/default/20261117_05:24/
        debian;bookworm;amd64;default;20261118_05:24;/images/debian/bookworm/amd64/default/20261118_05:24/
        debian;trixie;arm64;default;20261118_05:24;/images/debian/trixie/arm64/default/20261118_05:24/
        alpine;3.24;amd64;default;20261117_13:00;/images/alpine/3.24/amd64/default/20261117_13:00/
        broken line
        ubuntu;noble;amd64;default;20261118_07:42;/images/ubuntu/noble/amd64/default/20261118_07:42/
    """.trimIndent()

    @Test
    fun keepsTheNewestDefaultAmd64ImageOfEachRelease() {
        val images = ImageIndex.parse(index)
        assertEquals(listOf("alpine 3.24", "debian bookworm", "debian trixie", "ubuntu noble"), images.map { "${it.distro} ${it.release}" })
        val trixie = images.first { it.release == "trixie" }
        assertEquals("20261118_05:24", trixie.serial)
        assertEquals("https://images.linuxcontainers.org/images/debian/trixie/amd64/default/20261118_05:24/rootfs.tar.xz", trixie.rootfsUrl)
        assertEquals("https://images.linuxcontainers.org/images/debian/trixie/amd64/default/20261118_05:24/SHA256SUMS", trixie.sumsUrl)
    }

    @Test
    fun groupsReleasesByDistro() {
        val groups = ImageIndex.groupByDistro(ImageIndex.parse(index))
        assertEquals(listOf("alpine", "debian", "ubuntu"), groups.map { it.distro })
        assertEquals(listOf("trixie", "bookworm"), groups[1].images.map { it.release }) // newest name first
    }

    @Test
    fun readsTheRootfsChecksum() {
        val sums = "73e9618d0ea18678180eafbcbe559767154749a3575b2c3f9fc1e77d1ea61832  rootfs.tar.xz\nfa93  rootfs.squashfs\n"
        assertEquals("73e9618d0ea18678180eafbcbe559767154749a3575b2c3f9fc1e77d1ea61832", ImageIndex.findRootfsSum(sums))
        assertNull(ImageIndex.findRootfsSum("abc  other\n"))
    }
}
