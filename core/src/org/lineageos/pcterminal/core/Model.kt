package org.lineageos.pcterminal.core

enum class ContainerState(val code: String) {
    RUNNING("running"),
    STOPPED("stopped");

    companion object {
        fun fromCode(code: Any?) = entries.firstOrNull { it.code == code } ?: STOPPED
    }
}

data class Container(
    val name: String,
    val distro: String,
    val release: String,
    val created: Long,
    val user: String,
    val state: ContainerState,
)

data class ContainerStats(
    val state: ContainerState,
    val memoryBytes: Long?,
    val cpuUsec: Long?,
    val pids: Long?,
    val diskBytes: Long?,
)

/** A shell on a pty: its pid inside the container and the pty master. */
data class Shell<F>(val pid: Long, val pty: F)

/** The daemon's error codes (PROTOCOL.md), plus UNREACHABLE when it cannot be talked to. */
enum class ContainerError(val code: String) {
    BAD_REQUEST("bad_request"),
    BAD_NAME("bad_name"),
    BAD_USER("bad_user"),
    NOT_FOUND("not_found"),
    EXISTS("exists"),
    RUNNING("running"),
    NO_FD("no_fd"),
    BAD_ARCHIVE("bad_archive"),
    NO_SPACE("no_space"),
    START_FAILED("start_failed"),
    EXEC_FAILED("exec_failed"),
    NO_ICON("no_icon"),
    IO("io"),
    UNREACHABLE("unreachable");

    companion object {
        fun fromCode(code: Any?) = entries.firstOrNull { it.code == code } ?: IO
    }
}

sealed interface DaemonResult<T> {
    data class Ok<T>(val value: T) : DaemonResult<T>
    data class Failed<T>(val error: ContainerError) : DaemonResult<T>
}

fun <T> DaemonResult<T>.valueOrNull(): T? = (this as? DaemonResult.Ok)?.value
