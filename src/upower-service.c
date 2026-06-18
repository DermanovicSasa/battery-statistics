#include "upower-service.h"

#include <string.h>

#define UPOWER_NAME "org.freedesktop.UPower"
#define UPOWER_PATH "/org/freedesktop/UPower"
#define UPOWER_IFACE "org.freedesktop.UPower"
#define DEVICE_IFACE "org.freedesktop.UPower.Device"
#define DISPLAY_DEVICE_PATH "/org/freedesktop/UPower/devices/DisplayDevice"
#define UPOWER_DEVICE_TYPE_BATTERY 2
#define REFRESH_SECONDS 60

enum 
{
    SIGNAL_CHANGED,
    N_SIGNALS,
};

static guint signals[N_SIGNALS];

struct _BatteryService
{
    GObject parent_instance;

    GDBusProxy *upower_proxy;
    GDBusProxy *device_proxy;
    gulong properties_handler;
    guint refresh_source;

    BatterySnapshot snapshot;
};

G_DEFINE_FINAL_TYPE(BatteryService, battery_service, G_TYPE_OBJECT)

static GVariant * proxy_get_property(GDBusProxy *proxy, const gchar *name)
{
    return proxy != NULL ? g_dbus_proxy_get_cached_property(proxy, name) : NULL;
}

static gboolean proxy_get_boolean(GDBusProxy *proxy, const gchar *name, gboolean fallback)
{
    g_autoptr(GVariant) value = proxy_get_property(proxy, name);
    return value != NULL ? g_variant_get_boolean(value) : fallback;
}

static guint proxy_get_uint(GDBusProxy *proxy, const gchar *name, guint fallback)
{
    g_autoptr(GVariant) value = proxy_get_property(proxy, name);
    return value != NULL ? g_variant_get_uint32(value) : fallback;
}

static gint64 proxy_get_int64(GDBusProxy *proxy, const gchar *name, gint64 fallback)
{
    g_autoptr(GVariant) value = proxy_get_property(proxy, name);
    return value != NULL ? g_variant_get_int64(value) : fallback;
}

static gdouble proxy_get_double(GDBusProxy *proxy, const gchar *name, gdouble fallback)
{
    g_autoptr(GVariant) value = proxy_get_property(proxy, name);
    return value != NULL ? g_variant_get_double(value) : fallback;
}

static gchar * proxy_dup_string(GDBusProxy *proxy, const gchar *name)
{
    g_autoptr(GVariant) value = proxy_get_property(proxy, name);
    return value != NULL ? g_variant_dup_string(value, NULL) : NULL;
}

static GDBusProxy * new_device_proxy(const gchar *path, GError **error)
{
    return g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL,
                                         UPOWER_NAME,
                                         path,
                                         DEVICE_IFACE,
                                         NULL,
                                         error);
}

static void set_snapshot_error(BatteryService *self, const gchar *message)
{
    battery_snapshot_clear(&self->snapshot);
    self->snapshot.error_message = g_strdup(message);
}

static void clear_connection(BatteryService *self)
{
    if (self->device_proxy != NULL && self->properties_handler != 0) {
        g_signal_handler_disconnect(self->device_proxy, self->properties_handler);
        self->properties_handler = 0;
    }

    g_clear_object(&self->device_proxy);
    g_clear_object(&self->upower_proxy);
}

static gboolean
load_history(BatteryService *self)
{
    BatterySnapshot *snapshot = &self->snapshot;
    memset(snapshot->hours, 0, sizeof(snapshot->hours));

    g_autoptr(GDateTime) now_dt = g_date_time_new_now_local();
    const gint64 now = g_date_time_to_unix(now_dt);
    const gint seconds_into_hour = g_date_time_get_minute(now_dt) * 60 +
                                   g_date_time_get_second(now_dt);
    const gint64 current_hour = now - seconds_into_hour;
    snapshot->first_hour = current_hour -
                           (BATTERY_HISTORY_HOURS - 1) * BATTERY_SECONDS_PER_HOUR;

    if (self->device_proxy == NULL)
        return FALSE;

    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) reply = g_dbus_proxy_call_sync(
        self->device_proxy,
        "GetHistory",
        g_variant_new("(suu)",
                      "charge",
                      BATTERY_HISTORY_HOURS * BATTERY_SECONDS_PER_HOUR,
                      288),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error);

    if (reply != NULL) {
        g_autoptr(GVariant) samples = NULL;
        GVariantIter iter;
        guint timestamp;
        gdouble value;
        guint state;

        g_variant_get(reply, "(@a(udu))", &samples);
        g_variant_iter_init(&iter, samples);

        while (g_variant_iter_next(&iter, "(udu)", &timestamp, &value, &state)) {
            if ((gint64) timestamp < snapshot->first_hour ||
                (gint64) timestamp > now + 60)
                continue;

            const gint index = (gint) (((gint64) timestamp - snapshot->first_hour) /
                                       BATTERY_SECONDS_PER_HOUR);
            if (index < 0 || index >= BATTERY_HISTORY_HOURS)
                continue;

            snapshot->hours[index].valid = TRUE;
            snapshot->hours[index].level = CLAMP(value, 0.0, 100.0);
            snapshot->hours[index].state = state;
            snapshot->hours[index].timestamp = timestamp;
        }
    } else {
        g_debug("UPower history is unavailable: %s",
                error != NULL ? error->message : "unknown error");
    }

    snapshot->hours[BATTERY_HISTORY_HOURS - 1].valid = TRUE;
    snapshot->hours[BATTERY_HISTORY_HOURS - 1].level =
        CLAMP(snapshot->percentage, 0.0, 100.0);
    snapshot->hours[BATTERY_HISTORY_HOURS - 1].state = snapshot->state;
    snapshot->hours[BATTERY_HISTORY_HOURS - 1].timestamp = now;

    return reply != NULL;
}

static void populate_snapshot(BatteryService *self)
{
    g_autofree gchar *device_path = g_strdup(self->snapshot.device_path);
    battery_snapshot_clear(&self->snapshot);
    self->snapshot.device_path = g_steal_pointer(&device_path);

    if (self->device_proxy == NULL) {
        self->snapshot.error_message = g_strdup("No system battery was found.");
        return;
    }

    self->snapshot.available = TRUE;
    self->snapshot.percentage = proxy_get_double(self->device_proxy, "Percentage", 0.0);
    self->snapshot.state = proxy_get_uint(self->device_proxy,
                                          "State",
                                          BATTERY_STATE_UNKNOWN);
    self->snapshot.time_to_empty = proxy_get_int64(self->device_proxy,
                                                   "TimeToEmpty",
                                                   0);
    self->snapshot.time_to_full = proxy_get_int64(self->device_proxy,
                                                  "TimeToFull",
                                                  0);
    self->snapshot.energy_rate = proxy_get_double(self->device_proxy,
                                                  "EnergyRate",
                                                  0.0);
    self->snapshot.capacity = proxy_get_double(self->device_proxy,
                                               "Capacity",
                                               0.0);
    self->snapshot.vendor = proxy_dup_string(self->device_proxy, "Vendor");
    self->snapshot.model = proxy_dup_string(self->device_proxy, "Model");
    self->snapshot.history_available = load_history(self);
}

static void on_properties_changed(GDBusProxy *proxy, GVariant *changed_properties,const gchar *const *invalidated_properties,
                                    gpointer user_data)
{
    (void) proxy;
    (void) changed_properties;
    (void) invalidated_properties;

    battery_service_refresh(BATTERY_SERVICE(user_data));
}

static gboolean connect_to_battery(BatteryService *self, GError **error)
{
    clear_connection(self);
    battery_snapshot_clear(&self->snapshot);

    self->upower_proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
                                                       G_DBUS_PROXY_FLAGS_NONE,
                                                       NULL,
                                                       UPOWER_NAME,
                                                       UPOWER_PATH,
                                                       UPOWER_IFACE,
                                                       NULL,
                                                       error);
    if (self->upower_proxy == NULL)
        return FALSE;

    g_autoptr(GVariant) reply = g_dbus_proxy_call_sync(self->upower_proxy,
                                                       "EnumerateDevices",
                                                       NULL,
                                                       G_DBUS_CALL_FLAGS_NONE,
                                                       -1,
                                                       NULL,
                                                       error);
    if (reply == NULL)
        return FALSE;

    g_autoptr(GVariant) paths = NULL;
    GVariantIter iter;
    const gchar *path;

    g_variant_get(reply, "(@ao)", &paths);
    g_variant_iter_init(&iter, paths);

    while (g_variant_iter_next(&iter, "&o", &path)) 
    {
        g_autoptr(GError) local_error = NULL;
        GDBusProxy *candidate = new_device_proxy(path, &local_error);

        if (candidate == NULL)
            continue;

        const guint type = proxy_get_uint(candidate, "Type", 0);
        const gboolean power_supply = proxy_get_boolean(candidate,
                                                         "PowerSupply",
                                                         FALSE);
        const gboolean present = proxy_get_boolean(candidate,
                                                    "IsPresent",
                                                    FALSE);

        if (type == UPOWER_DEVICE_TYPE_BATTERY && power_supply && present) {
            self->device_proxy = candidate;
            self->snapshot.device_path = g_strdup(path);
            break;
        }

        g_object_unref(candidate);
    }

    if (self->device_proxy == NULL)
    {
        g_autoptr(GError) display_error = NULL;
        GDBusProxy *display = new_device_proxy(DISPLAY_DEVICE_PATH,
                                               &display_error);

        if (display != NULL && proxy_get_boolean(display, "IsPresent", FALSE))
        {
            self->device_proxy = display;
            self->snapshot.device_path = g_strdup(DISPLAY_DEVICE_PATH);
        }
         else
        {
            g_clear_object(&display);
        }
    }

    if (self->device_proxy == NULL)
        return FALSE;

    self->properties_handler = g_signal_connect(self->device_proxy,
                                                "g-properties-changed",
                                                G_CALLBACK(on_properties_changed),
                                                self);
    return TRUE;
}

static gboolean refresh_timeout_cb(gpointer user_data)
{
    battery_service_refresh(BATTERY_SERVICE(user_data));
    return G_SOURCE_CONTINUE;
}

void battery_service_refresh(BatteryService *self)
{
    g_return_if_fail(BATTERY_IS_SERVICE(self));

    if (self->device_proxy == NULL) 
    {
        g_autoptr(GError) error = NULL;
        if (!connect_to_battery(self, &error)) 
        {
            set_snapshot_error(self, error != NULL ? error->message: "No system battery was found.");
            g_signal_emit(self, signals[SIGNAL_CHANGED], 0);
            return;
        }
    }

    populate_snapshot(self);
    g_signal_emit(self, signals[SIGNAL_CHANGED], 0);
}

void battery_service_retry(BatteryService *self)
{
    g_return_if_fail(BATTERY_IS_SERVICE(self));

    clear_connection(self);
    battery_service_refresh(self);
}

void battery_service_start(BatteryService *self)
{
    g_return_if_fail(BATTERY_IS_SERVICE(self));

    battery_service_refresh(self);

    if (self->refresh_source == 0) 
    {
        self->refresh_source = g_timeout_add_seconds(REFRESH_SECONDS, refresh_timeout_cb, self);
    }
}

const BatterySnapshot * battery_service_get_snapshot(BatteryService *self)
{
    g_return_val_if_fail(BATTERY_IS_SERVICE(self), NULL);
    return &self->snapshot;
}

static void battery_service_dispose(GObject *object)
{
    BatteryService *self = BATTERY_SERVICE(object);

    if (self->refresh_source != 0) {
        g_source_remove(self->refresh_source);
        self->refresh_source = 0;
    }

    clear_connection(self);
    battery_snapshot_clear(&self->snapshot);

    G_OBJECT_CLASS(battery_service_parent_class)->dispose(object);
}

static void battery_service_class_init(BatteryServiceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = battery_service_dispose;

    signals[SIGNAL_CHANGED] = g_signal_new("changed",
                                           G_TYPE_FROM_CLASS(klass),
                                           G_SIGNAL_RUN_LAST,
                                           0,
                                           NULL,
                                           NULL,
                                           NULL,
                                           G_TYPE_NONE,
                                           0);
}

static void battery_service_init(BatteryService *self)
{
    battery_snapshot_init(&self->snapshot);
}

BatteryService * battery_service_new(void)
{
    return g_object_new(BATTERY_TYPE_SERVICE, NULL);
}
