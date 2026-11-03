// SPDX-License-Identifier: GPL-2.0-only
/*
 * desk: the table of programs. Anything listed here can be started by name
 * (File/Run in Hub, `desk <name>` in the shell).
 */
#include <hix/kernel.h>
#include <desk/desk.h>

const struct app *const desk_apps[] = {
	&app_hub, &app_files, &app_notes, &app_calc, &app_clock,
	&app_sysinfo, &app_tasks, &app_term, &app_mines, &app_reversi,
	&app_snake, &app_bricks, &app_chess,
};

const int desk_apps_count = ARRAY_SIZE(desk_apps);
