#pragma once
/**
 * production_app.h — Full application entry point.
 *
 * Creates the application event loop, starts sensor/button/display tasks,
 * and runs the event-driven main loop.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Run the production application.
 *
 * This function does not return. It creates the event loop, starts all
 * tasks, and processes events indefinitely.
 */
void production_app_run(void);

#ifdef __cplusplus
}
#endif
