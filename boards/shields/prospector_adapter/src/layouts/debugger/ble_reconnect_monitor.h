#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * Replace the normal connection log with a pending reconnect warning.
 *
 * Must be called from the display work queue.
 */
bool zmk_debugger_reconnect_monitor_log_if_pending(uint8_t source);
