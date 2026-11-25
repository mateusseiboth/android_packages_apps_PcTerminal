package org.lineageos.pcterminal

import android.os.ParcelFileDescriptor
import java.io.BufferedInputStream
import java.io.File
import java.io.FileInputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import org.lineageos.pcterminal.core.Container
import org.lineageos.pcterminal.core.ContainerError
import org.lineageos.pcterminal.core.DaemonResult
import org.lineageos.pcterminal.core.DistroImage
import org.lineageos.pcterminal.core.Downloads
import org.lineageos.pcterminal.core.ImageIndex
import org.tukaani.xz.XZInputStream

sealed interface InstallStep {
    data class Downloading(val done: Long, val total: Long) : InstallStep
    data object Checking : InstallStep
    data class Extracting(val bytes: Long, val files: Long) : InstallStep
}

enum class InstallFailure { DOWNLOAD, CHECKSUM, SPACE, ARCHIVE, EXISTS, UNREACHABLE, OTHER }

sealed interface InstallOutcome {
    data class Done(val container: Container) : InstallOutcome
    data class Failed(val failure: InstallFailure) : InstallOutcome
}

/**
 * Downloads a rootfs.tar.xz into the cache, checks it against the server's SHA256SUMS, then streams it
 * decoded through a pipe to pc-containerd, which extracts it. Blocking: call it off the main thread.
 */
class Installer(private val cacheDir: File) {
    fun fetchImages(): List<DistroImage> = ImageIndex.parse(readText(ImageIndex.INDEX_URL))

    fun install(image: DistroImage, name: String, user: String, onStep: (InstallStep) -> Unit): InstallOutcome {
        val file = File(cacheDir, "rootfs-${image.distro}-${image.release}.tar.xz")
        return try {
            val expected = ImageIndex.findRootfsSum(readText(image.sumsUrl)) ?: return InstallOutcome.Failed(InstallFailure.DOWNLOAD)
            val actual = download(image.rootfsUrl, file, onStep)
            onStep(InstallStep.Checking)
            if (actual != expected) return InstallOutcome.Failed(InstallFailure.CHECKSUM)
            create(file, image, name, user, onStep)
        } catch (e: IOException) {
            InstallOutcome.Failed(InstallFailure.DOWNLOAD)
        } finally {
            file.delete()
        }
    }

    private fun create(file: File, image: DistroImage, name: String, user: String, onStep: (InstallStep) -> Unit): InstallOutcome {
        val (read, write) = ParcelFileDescriptor.createPipe()
        val feeder = Thread { feed(file, write) }.apply { start() }
        val result = read.use {
            DaemonConnection.withClient { client ->
                client.create(name, image.distro, image.release, user, read.fileDescriptor) { bytes, files ->
                    onStep(InstallStep.Extracting(bytes, files))
                }
            }
        }
        feeder.join()
        return when (result) {
            is DaemonResult.Ok -> InstallOutcome.Done(result.value)
            is DaemonResult.Failed -> InstallOutcome.Failed(FAILURES[result.error] ?: InstallFailure.OTHER)
        }
    }

    /** The decoded tar into the pipe; a daemon that stops reading ends it early (EPIPE). */
    private fun feed(file: File, write: ParcelFileDescriptor) {
        ParcelFileDescriptor.AutoCloseOutputStream(write).use { out ->
            runCatching { XZInputStream(BufferedInputStream(FileInputStream(file))).use { it.copyTo(out, BUFFER) } }
        }
    }

    private fun download(url: String, file: File, onStep: (InstallStep) -> Unit): String {
        val connection = open(url)
        val total = connection.contentLengthLong
        return connection.inputStream.use { input ->
            file.outputStream().use { output -> Downloads.copyWithSha256(input, output) { onStep(InstallStep.Downloading(it, total)) } }
        }
    }

    private fun readText(url: String): String = open(url).inputStream.use { it.readBytes().toString(Charsets.UTF_8) }

    private fun open(url: String): HttpURLConnection {
        val connection = URL(url).openConnection() as HttpURLConnection
        connection.connectTimeout = TIMEOUT_MS
        connection.readTimeout = TIMEOUT_MS
        connection.instanceFollowRedirects = true
        if (connection.responseCode != HttpURLConnection.HTTP_OK) throw IOException("HTTP ${connection.responseCode}")
        return connection
    }

    private companion object {
        const val TIMEOUT_MS = 30_000
        const val BUFFER = 64 * 1024
        val FAILURES = mapOf(
            ContainerError.NO_SPACE to InstallFailure.SPACE,
            ContainerError.BAD_ARCHIVE to InstallFailure.ARCHIVE,
            ContainerError.EXISTS to InstallFailure.EXISTS,
            ContainerError.UNREACHABLE to InstallFailure.UNREACHABLE,
        )
    }
}

