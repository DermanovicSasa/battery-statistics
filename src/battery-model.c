#include "battery-model.h"

#include <string.h>

void battery_snapshot_init(BatterySnapshot *snapshot)
{
    g_return_if_fail(snapshot != NULL);
    memset(snapshot, 0, sizeof(*snapshot));
}

void battery_snapshot_clear(BatterySnapshot *snapshot)
{
    if (snapshot == NULL)
        return;

    g_clear_pointer(&snapshot->vendor, g_free);
    g_clear_pointer(&snapshot->model, g_free);
    g_clear_pointer(&snapshot->device_path, g_free);
    g_clear_pointer(&snapshot->error_message, g_free);
    memset(snapshot, 0, sizeof(*snapshot));
}

void battery_snapshot_copy(BatterySnapshot *destination, const BatterySnapshot *source)
{
    g_return_if_fail(destination != NULL);
    g_return_if_fail(source != NULL);

    battery_snapshot_clear(destination);
    *destination = *source;

    destination->vendor = g_strdup(source->vendor);
    destination->model = g_strdup(source->model);
    destination->device_path = g_strdup(source->device_path);
    destination->error_message = g_strdup(source->error_message);
}

const gchar * battery_state_name(guint state)
{
    switch (state) 
    {
    case BATTERY_STATE_CHARGING:
        return "Charging";
    case BATTERY_STATE_DISCHARGING:
        return "Discharging";
    case BATTERY_STATE_EMPTY:
        return "Empty";
    case BATTERY_STATE_FULLY_CHARGED:
        return "Fully charged";
    case BATTERY_STATE_PENDING_CHARGE:
        return "Waiting to charge";
    case BATTERY_STATE_PENDING_DISCHARGE:
        return "Waiting to discharge";
    default:
        return "Battery state unknown";
    }
}

gboolean battery_state_is_charging(guint state)
{
    return state == BATTERY_STATE_CHARGING || state == BATTERY_STATE_PENDING_CHARGE;
}

gchar * battery_format_duration(gint64 seconds)
{
    if (seconds <= 0)
        return NULL;

    const gint64 hours = seconds / 3600;
    const gint64 minutes = (seconds % 3600) / 60;

    if (hours > 0 && minutes > 0)
        return g_strdup_printf("%" G_GINT64_FORMAT " h %" G_GINT64_FORMAT " min",
                               hours,
                               minutes);
    if (hours > 0)
        return g_strdup_printf("%" G_GINT64_FORMAT " h", hours);

    return g_strdup_printf("%" G_GINT64_FORMAT " min",
                           MAX((gint64) 1, minutes));
}

gchar * battery_snapshot_device_name(const BatterySnapshot *snapshot)
{
    g_return_val_if_fail(snapshot != NULL, g_strdup("System battery"));

    const gboolean has_vendor = snapshot->vendor != NULL && *snapshot->vendor != '\0';
    const gboolean has_model = snapshot->model != NULL && *snapshot->model != '\0';

    if (has_vendor && has_model)
        return g_strdup_printf("%s %s", snapshot->vendor, snapshot->model);
    if (has_model)
        return g_strdup(snapshot->model);
    if (has_vendor)
        return g_strdup(snapshot->vendor);

    return g_strdup("System battery");
}

gint battery_snapshot_count_hours(const BatterySnapshot *snapshot)
{
    g_return_val_if_fail(snapshot != NULL, 0);

    gint count = 0;
    for (gint i = 0; i < BATTERY_HISTORY_HOURS; i++)
        count += snapshot->hours[i].valid ? 1 : 0;

    return count;
}

gboolean battery_snapshot_net_change(const BatterySnapshot *snapshot, gdouble *change_out)
{
    g_return_val_if_fail(snapshot != NULL, FALSE);

    gint first = -1;
    gint last = -1;

    for (gint i = 0; i < BATTERY_HISTORY_HOURS; i++) 
    {
        if (!snapshot->hours[i].valid)
            continue;

        if (first < 0)
            first = i;
        last = i;
    }

    if (first < 0 || last <= first)
        return FALSE;

    if (change_out != NULL)
        *change_out = snapshot->hours[last].level - snapshot->hours[first].level;

    return TRUE;
}
