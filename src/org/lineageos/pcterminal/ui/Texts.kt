package org.lineageos.pcterminal.ui

import org.lineageos.pcterminal.InstallFailure
import org.lineageos.pcterminal.R
import org.lineageos.pcterminal.core.ContainerError

/** What each daemon error tells the person. */
val ERROR_TEXTS: Map<ContainerError, Int> = mapOf(
    ContainerError.UNREACHABLE to R.string.error_unreachable,
    ContainerError.START_FAILED to R.string.error_start,
    ContainerError.EXEC_FAILED to R.string.error_exec,
    ContainerError.RUNNING to R.string.error_running,
    ContainerError.EXISTS to R.string.error_exists,
    ContainerError.BAD_NAME to R.string.error_bad_name,
    ContainerError.NO_SPACE to R.string.install_failed_space,
)

fun findErrorText(error: ContainerError): Int = ERROR_TEXTS[error] ?: R.string.error_generic

val INSTALL_FAILURE_TEXTS: Map<InstallFailure, Int> = mapOf(
    InstallFailure.DOWNLOAD to R.string.install_failed_download,
    InstallFailure.CHECKSUM to R.string.install_failed_checksum,
    InstallFailure.SPACE to R.string.install_failed_space,
    InstallFailure.ARCHIVE to R.string.install_failed_archive,
    InstallFailure.EXISTS to R.string.error_exists,
    InstallFailure.UNREACHABLE to R.string.error_unreachable,
    InstallFailure.OTHER to R.string.error_generic,
)
