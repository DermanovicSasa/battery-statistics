#pragma once

#include <gio/gio.h>

#include "battery-model.h"

G_BEGIN_DECLS

#define BATTERY_TYPE_SERVICE (battery_service_get_type())
G_DECLARE_FINAL_TYPE(BatteryService, battery_service, BATTERY, SERVICE, GObject)

BatteryService *battery_service_new(void);

void battery_service_start(BatteryService *self);
void battery_service_retry(BatteryService *self);
void battery_service_refresh(BatteryService *self);

const BatterySnapshot *battery_service_get_snapshot(BatteryService *self);

G_END_DECLS
