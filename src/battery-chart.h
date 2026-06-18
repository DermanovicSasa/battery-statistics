#pragma once

#include <gtk/gtk.h>

#include "battery-model.h"

G_BEGIN_DECLS

#define BATTERY_TYPE_CHART (battery_chart_get_type())
G_DECLARE_FINAL_TYPE(BatteryChart, battery_chart, BATTERY, CHART, GObject)

BatteryChart *battery_chart_new(void);
GtkWidget *battery_chart_get_widget(BatteryChart *self);
void battery_chart_set_snapshot(BatteryChart *self,
                                const BatterySnapshot *snapshot);
void battery_chart_clear_selection(BatteryChart *self);

G_END_DECLS
