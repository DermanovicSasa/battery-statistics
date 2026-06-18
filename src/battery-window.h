#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define BATTERY_TYPE_WINDOW (battery_window_get_type())
G_DECLARE_FINAL_TYPE(BatteryWindow, battery_window, BATTERY, WINDOW, GObject)

BatteryWindow *battery_window_new(AdwApplication *application);
void battery_window_present(BatteryWindow *self);
void battery_window_refresh(BatteryWindow *self);
void battery_window_show_about(BatteryWindow *self);
void battery_window_show_preferences(BatteryWindow *self);
G_END_DECLS
