package org.lineageos.pcterminal.core

/** One rootfs of images.linuxcontainers.org: the newest build of a distro release. */
data class DistroImage(val distro: String, val release: String, val serial: String, val path: String) {
    val rootfsUrl get() = "${ImageIndex.SERVER}${path}rootfs.tar.xz"
    val sumsUrl get() = "${ImageIndex.SERVER}${path}SHA256SUMS"
}

data class DistroGroup(val distro: String, val images: List<DistroImage>)

/** The image server's index (meta/1.0/index-system): "distro;release;arch;variant;serial;path" lines. */
object ImageIndex {
    const val SERVER = "https://images.linuxcontainers.org"
    const val INDEX_URL = "$SERVER/meta/1.0/index-system"
    private const val ARCH = "amd64"
    private const val VARIANT = "default"
    private const val FIELDS = 6

    fun parse(text: String): List<DistroImage> = text.lineSequence()
        .map { it.trim().split(';') }
        .filter { it.size == FIELDS && it[2] == ARCH && it[3] == VARIANT }
        .map { DistroImage(it[0], it[1], it[4], it[5]) }
        .groupBy { it.distro to it.release }
        .values.map { builds -> builds.maxBy { it.serial } }
        .sortedWith(compareBy({ it.distro }, { it.release }))

    /** Distros by name; inside each, releases with the newest build first. */
    fun groupByDistro(images: List<DistroImage>): List<DistroGroup> = images.groupBy { it.distro }
        .map { (distro, list) -> DistroGroup(distro, list.sortedWith(compareByDescending<DistroImage> { it.serial }.thenByDescending { it.release })) }
        .sortedBy { it.distro }

    /** The checksum of rootfs.tar.xz in a SHA256SUMS file. */
    fun findRootfsSum(sums: String): String? = sums.lineSequence()
        .map { it.trim().split(Regex("\\s+")) }
        .firstOrNull { it.size == 2 && it[1] == "rootfs.tar.xz" }
        ?.first()
}
