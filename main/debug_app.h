#pragma once
/**
 * debug_app.h — Hardware test / debug mode entry point.
 *
 * Runs a sequential test of every peripheral on the board and reports
 * PASS / FAIL for each over the serial console (and via LEDs).
 *
 * Called from app_main() when CONFIG_APP_MODE_DEBUG is set.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Run the hardware test sequence.
 *
 * This function does not return — it loops at the end, blinking LEDs
 * to indicate overall pass/fail.
 *
 * Tests performed (in order):
 *   1. I2C bus scan — detect devices at expected addresses
 *   2. ISM330DHC — WHO_AM_I check + sample read
 *   3. BMP851 — chip ID check + forced measurement
 *   4. SEN66 — reset + start measurement + read one frame
 *   5. Display — init + fill screen test pattern
 *   6. Buttons — log state snapshot (not interactive)
 *   7. LEDs — blink each one
 *   8. Power good — read and report
 */
void debug_app_run(void);

#ifdef __cplusplus
}
#endif
