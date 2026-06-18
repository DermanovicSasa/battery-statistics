#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define BATTERY_HISTORY_HOURS 24
#define BATTERY_SECONDS_PER_HOUR 3600

typedef enum {
    BATTERY_STATE_UNKNOWN = 0,
    BATTERY_STATE_CHARGING = 1,
    BATTERY_STATE_DISCHARGING = 2,
    BATTERY_STATE_EMPTY = 3,
    BATTERY_STATE_FULLY_CHARGED = 4,
    BATTERY_STATE_PENDING_CHARGE = 5,
    BATTERY_STATE_PENDING_DISCHARGE = 6,
} BatteryState;

typedef struct {
    gboolean valid;
    gdouble level;
    guint state;
    guint64 timestamp;
} BatteryHour;

typedef struct {
    gboolean available;
    gboolean history_available;

    gdouble percentage;
    guint state;
    gint64 time_to_empty;
    gint64 time_to_full;
    gdouble energy_rate;
    gdouble capacity;

    gchar *vendor;
    gchar *model;
    gchar *device_path;
    gchar *error_message;

    gint64 first_hour;
    BatteryHour hours[BATTERY_HISTORY_HOURS];
} BatterySnapshot;

void battery_snapshot_init(BatterySnapshot *snapshot);
void battery_snapshot_clear(BatterySnapshot *snapshot);
void battery_snapshot_copy(BatterySnapshot *destination,
                           const BatterySnapshot *source);

const gchar *battery_state_name(guint state);
gboolean battery_state_is_charging(guint state);
gchar *battery_format_duration(gint64 seconds);
gchar *battery_snapshot_device_name(const BatterySnapshot *snapshot);
gint battery_snapshot_count_hours(const BatterySnapshot *snapshot);
gboolean battery_snapshot_net_change(const BatterySnapshot *snapshot,
                                     gdouble *change_out);

G_END_DECLS
