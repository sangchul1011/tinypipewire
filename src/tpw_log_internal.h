/* SPDX-License-Identifier: MIT */

#ifndef TPW_LOG_INTERNAL_H
#define TPW_LOG_INTERNAL_H

#include "tpw/tpw_log.h"

#if defined(__GNUC__) || defined(__clang__)
#define TPW_LOG_PRINTF_FMT(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))
#else
#define TPW_LOG_PRINTF_FMT(fmt_idx, args_idx)
#endif

/* Formats and delivers one message through the registered callback
 * (or stderr, if none is set) at `level`, tagged with the call
 * site's file/line; dropped if less severe than the configured
 * minimum level. Called through the tpw_log_<level>() macros below,
 * which supply __FILE__/__LINE__ automatically. */
void tpw_log_emit(tpw_log_level level, const char* file, int line, const char* fmt, ...) TPW_LOG_PRINTF_FMT(4, 5);

/* Mirrors PipeWire's own pw_log_error()/pw_log_warn()/... naming. */
#define tpw_log_error(...) tpw_log_emit(TPW_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define tpw_log_warning(...) tpw_log_emit(TPW_LOG_WARNING, __FILE__, __LINE__, __VA_ARGS__)
#define tpw_log_info(...) tpw_log_emit(TPW_LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define tpw_log_debug(...) tpw_log_emit(TPW_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define tpw_log_verbose(...) tpw_log_emit(TPW_LOG_VERBOSE, __FILE__, __LINE__, __VA_ARGS__)

#endif /* TPW_LOG_INTERNAL_H */
