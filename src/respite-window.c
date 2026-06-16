/* respite-window.c
 *
 * Copyright 2026 pawan
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include "respite-window.h"

struct _RespiteWindow
{
	AdwApplicationWindow  parent_instance;

	/* Template widgets */
	AdwSpinRow          *work_interval_row;
	AdwSpinRow          *break_duration_row;
	AdwSpinRow          *postpone_allowance_row;
	AdwSpinRow          *postpone_duration_row;
	AdwSpinRow          *pre_break_warning_row;
	AdwSwitchRow        *autostart_row;
};

G_DEFINE_FINAL_TYPE (RespiteWindow, respite_window, ADW_TYPE_APPLICATION_WINDOW)

static void
respite_window_class_init (RespiteWindowClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	gtk_widget_class_set_template_from_resource (widget_class, "/com/texoviva/respite/respite-window.ui");
	gtk_widget_class_bind_template_child (widget_class, RespiteWindow, work_interval_row);
	gtk_widget_class_bind_template_child (widget_class, RespiteWindow, break_duration_row);
	gtk_widget_class_bind_template_child (widget_class, RespiteWindow, postpone_allowance_row);
	gtk_widget_class_bind_template_child (widget_class, RespiteWindow, postpone_duration_row);
	gtk_widget_class_bind_template_child (widget_class, RespiteWindow, pre_break_warning_row);
	gtk_widget_class_bind_template_child (widget_class, RespiteWindow, autostart_row);
}

static void
respite_window_init (RespiteWindow *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));
}
