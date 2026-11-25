package org.lineageos.pcterminal.core

import java.text.Normalizer

/** Container and Linux user names, by the daemon's rules (containerd/PROTOCOL.md, "Names"). */
object Names {
    private val CONTAINER = Regex("[a-z0-9][a-z0-9-]{0,30}")
    private val USER = Regex("[a-z_][a-z0-9_-]{0,31}")
    private const val DEFAULT_USER = "user"
    private const val USER_MAX = 32
    private const val CONTAINER_MAX = 31

    fun isValidContainerName(name: String) = CONTAINER.matches(name)

    fun isValidUserName(name: String) = USER.matches(name) && name != "root"

    /** The distro's name made valid, with -2, -3... when taken. */
    fun suggestContainerName(distro: String, taken: Set<String>): String {
        val base = withoutAccents(distro).lowercase().replace(Regex("[^a-z0-9]+"), "-").trim('-').take(CONTAINER_MAX - 3).ifEmpty { "linux" }
        return generateSequence(1) { it + 1 }.map { if (it == 1) base else "$base-$it" }.first { it !in taken }
    }

    /** A Linux account name from the person's name: lower case ASCII, spaces as underscores. */
    fun buildUserName(personName: String): String {
        val plain = withoutAccents(personName).lowercase().trim().replace(Regex("\\s+"), "_").replace(Regex("[^a-z0-9_-]"), "")
        val name = (if (plain.firstOrNull()?.isDigit() == true) "_$plain" else plain).take(USER_MAX)
        return name.takeIf(::isValidUserName) ?: DEFAULT_USER
    }

    private fun withoutAccents(s: String) = Normalizer.normalize(s, Normalizer.Form.NFD).replace(Regex("\\p{Mn}+"), "")
}
