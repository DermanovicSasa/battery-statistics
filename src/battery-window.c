#include "battery-window.h"

#include "battery-chart.h"
#include "battery-model.h"
#include "upower-service.h"

#include <math.h>

struct _BatteryWindow
{
    GObject parent_instance;

    GtkWidget *window;
    GtkWidget *stack;
    GtkWidget *main_page;
    GtkWidget *status_page;

    GtkWidget *percentage_label;
    GtkWidget *status_label;
    GtkWidget *device_label;
    GtkWidget *rate_value;
    GtkWidget *health_value;
    GtkWidget *change_value;
    GtkWidget *history_note;
    GtkWidget *change_row;

    BatteryService *service;
    BatteryChart *chart;
};

G_DEFINE_FINAL_TYPE(BatteryWindow, battery_window, G_TYPE_OBJECT)

static GtkWidget * make_value_label(void)
{
    GtkWidget *label = gtk_label_new("—");
    gtk_widget_add_css_class(label, "numeric");
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    return label;
}

static GtkWidget * make_stat_row(const gchar *title, GtkWidget **value_out)
{
    GtkWidget *row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);

    GtkWidget *value = make_value_label();
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), value);
    *value_out = value;

    return row;
}

static GtkWidget * create_chart_info_button(void)
{
    GtkWidget *button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(button),
                                  "dialog-information-symbolic");
    gtk_widget_add_css_class(button, "flat");
    gtk_widget_set_tooltip_text(button, "About the chart");
    gtk_accessible_update_property(GTK_ACCESSIBLE(button),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   "About the battery chart",
                                   -1);

    GtkWidget *popover = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), TRUE);

    GtkWidget *label = gtk_label_new(
        "Each position represents one local hour. A bar uses the latest "
        "battery level UPower reported in that hour. Missing hours stay "
        "empty, and a bolt marks charging. Click a bar to keep its details "
        "open, or use the arrow keys while the chart is focused.");
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 38);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_margin_start(label, 14);
    gtk_widget_set_margin_end(label, 14);
    gtk_widget_set_margin_top(label, 12);
    gtk_widget_set_margin_bottom(label, 12);
    gtk_popover_set_child(GTK_POPOVER(popover), label);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), popover);

    return button;
}

static GtkWidget * create_app_menu_button(void)
{
    g_autoptr(GMenu) menu = g_menu_new();
    g_menu_append(menu, "Refresh", "app.refresh");
    g_menu_append(menu, "About Battery Statistics", "app.about");
    g_menu_append(menu, "Quit", "app.quit");

    GtkWidget *button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(button), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(button), G_MENU_MODEL(menu));
    gtk_widget_set_tooltip_text(button, "Main menu");
    gtk_accessible_update_property(GTK_ACCESSIBLE(button),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   "Main menu",
                                   -1);
    return button;
}

static GtkWidget * create_status_page(BatteryWindow *self)
{
    GtkWidget *page = adw_status_page_new();
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(page), "battery-symbolic");
    adw_status_page_set_title(ADW_STATUS_PAGE(page),
                              "Battery Information Unavailable");
    adw_status_page_set_description(
        ADW_STATUS_PAGE(page),
        "No system battery is currently available through UPower.");

    GtkWidget *retry = gtk_button_new_with_label("Try Again");
    gtk_widget_add_css_class(retry, "suggested-action");
    gtk_widget_set_halign(retry, GTK_ALIGN_CENTER);
    g_signal_connect_swapped(retry,
                             "clicked",
                             G_CALLBACK(battery_window_refresh),
                             self);
    adw_status_page_set_child(ADW_STATUS_PAGE(page), retry);

    return page;
}

static GtkWidget * create_main_page(BatteryWindow *self)
{
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);

    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 780);
    adw_clamp_set_tightening_threshold(ADW_CLAMP(clamp), 600);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), clamp);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_margin_start(content, 18);
    gtk_widget_set_margin_end(content, 18);
    gtk_widget_set_margin_top(content, 24);
    gtk_widget_set_margin_bottom(content, 24);
    adw_clamp_set_child(ADW_CLAMP(clamp), content);

    GtkWidget *summary = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_halign(summary, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(content), summary);

    self->percentage_label = gtk_label_new("—");
    gtk_widget_set_halign(self->percentage_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(self->percentage_label, "title-1");
    gtk_widget_add_css_class(self->percentage_label, "numeric");
    gtk_widget_add_css_class(self->percentage_label, "accent");
    gtk_box_append(GTK_BOX(summary), self->percentage_label);

    self->status_label = gtk_label_new("Reading battery information…");
    gtk_widget_set_halign(self->status_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(self->status_label, "heading");
    gtk_box_append(GTK_BOX(summary), self->status_label);

    self->device_label = gtk_label_new("UPower");
    gtk_widget_set_halign(self->device_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(self->device_label, "dim-label");
    gtk_box_append(GTK_BOX(summary), self->device_label);

    GtkWidget *chart_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 9);
    gtk_box_append(GTK_BOX(content), chart_section);

    GtkWidget *chart_heading = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(chart_section), chart_heading);

    GtkWidget *chart_title = gtk_label_new("Battery level");
    gtk_widget_set_halign(chart_title, GTK_ALIGN_START);
    gtk_widget_set_hexpand(chart_title, TRUE);
    gtk_widget_add_css_class(chart_title, "heading");
    gtk_box_append(GTK_BOX(chart_heading), chart_title);

    GtkWidget *period = gtk_label_new("Last 24 hours");
    gtk_widget_add_css_class(period, "caption");
    gtk_widget_add_css_class(period, "dim-label");
    gtk_widget_set_valign(period, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(chart_heading), period);
    gtk_box_append(GTK_BOX(chart_heading), create_chart_info_button());

    GtkWidget *chart_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(chart_card, "card");
    gtk_widget_set_margin_top(chart_card, 2);
    gtk_box_append(GTK_BOX(chart_section), chart_card);

    self->chart = battery_chart_new();
    GtkWidget *chart_widget = battery_chart_get_widget(self->chart);
    gtk_widget_set_margin_start(chart_widget, 8);
    gtk_widget_set_margin_end(chart_widget, 8);
    gtk_widget_set_margin_top(chart_widget, 8);
    gtk_widget_set_margin_bottom(chart_widget, 6);
    gtk_box_append(GTK_BOX(chart_card), chart_widget);

    self->history_note = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(self->history_note), TRUE);
    gtk_label_set_xalign(GTK_LABEL(self->history_note), 0.0f);
    gtk_widget_add_css_class(self->history_note, "caption");
    gtk_widget_add_css_class(self->history_note, "dim-label");
    gtk_widget_set_visible(self->history_note, FALSE);
    gtk_box_append(GTK_BOX(chart_section), self->history_note);

    GtkWidget *stats_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 9);
    gtk_box_append(GTK_BOX(content), stats_section);

    GtkWidget *stats_title = gtk_label_new("Battery details");
    gtk_widget_set_halign(stats_title, GTK_ALIGN_START);
    gtk_widget_add_css_class(stats_title, "heading");
    gtk_box_append(GTK_BOX(stats_section), stats_title);

    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(list, "boxed-list");
    gtk_box_append(GTK_BOX(stats_section), list);

    gtk_list_box_append(GTK_LIST_BOX(list),
                        make_stat_row("Power use", &self->rate_value));
    gtk_list_box_append(GTK_LIST_BOX(list),
                        make_stat_row("Battery health", &self->health_value));
    self->change_row = make_stat_row("Used in the last 24 hours",
                                     &self->change_value);
    gtk_list_box_append(GTK_LIST_BOX(list), self->change_row);

    return scrolled;
}

static void update_status_page(BatteryWindow *self, const BatterySnapshot *snapshot)
{
    const gchar *description = snapshot != NULL && snapshot->error_message != NULL
                                   ? snapshot->error_message
                                   : "No system battery is currently available through UPower.";
    adw_status_page_set_description(ADW_STATUS_PAGE(self->status_page),
                                    description);
    gtk_stack_set_visible_child(GTK_STACK(self->stack), self->status_page);
}

static void update_history_note(BatteryWindow *self, const BatterySnapshot *snapshot)
{
    const gint measured = battery_snapshot_count_hours(snapshot);

    if (measured >= BATTERY_HISTORY_HOURS) {
        gtk_widget_set_visible(self->history_note, FALSE);
        return;
    }

    g_autofree gchar *text = measured == 0
        ? g_strdup("UPower has not recorded hourly history yet.")
        : g_strdup_printf("Some hourly data is unavailable · %d of 24 hours measured",
                          measured);

    gtk_label_set_text(GTK_LABEL(self->history_note), text);
    gtk_widget_set_visible(self->history_note, TRUE);
}

static void update_main_page(BatteryWindow *self, const BatterySnapshot *snapshot)
{
    gtk_stack_set_visible_child(GTK_STACK(self->stack), self->main_page);

    g_autofree gchar *percentage = g_strdup_printf("%.0f%%", snapshot->percentage);
    gtk_label_set_text(GTK_LABEL(self->percentage_label), percentage);

    const gint64 estimate = battery_state_is_charging(snapshot->state)
                                ? snapshot->time_to_full
                                : snapshot->time_to_empty;
    g_autofree gchar *duration = battery_format_duration(estimate);
    g_autofree gchar *status = NULL;

    if (duration == NULL) {
        status = g_strdup(battery_state_name(snapshot->state));
    } else if (battery_state_is_charging(snapshot->state)) {
        status = g_strdup_printf("%s · %s until full",
                                 battery_state_name(snapshot->state),
                                 duration);
    } else {
        status = g_strdup_printf("%s · About %s remaining",
                                 battery_state_name(snapshot->state),
                                 duration);
    }
    gtk_label_set_text(GTK_LABEL(self->status_label), status);

    g_autofree gchar *device_name = battery_snapshot_device_name(snapshot);
    gtk_label_set_text(GTK_LABEL(self->device_label), device_name);

    g_autofree gchar *rate = snapshot->energy_rate > 0.01
        ? g_strdup_printf("%.1f W", snapshot->energy_rate)
        : g_strdup("—");
    gtk_label_set_text(GTK_LABEL(self->rate_value), rate);

    g_autofree gchar *health = snapshot->capacity > 0.0
        ? g_strdup_printf("%.0f%%", snapshot->capacity)
        : g_strdup("—");
    gtk_label_set_text(GTK_LABEL(self->health_value), health);

    gdouble change = 0.0;
    if (battery_snapshot_net_change(snapshot, &change)) {
        const gdouble magnitude = fabs(change);
        g_autofree gchar *change_text = g_strdup_printf("%.0f%%", magnitude);
        gtk_label_set_text(GTK_LABEL(self->change_value), change_text);

        if (change < -0.5) {
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->change_row),
                                          "Used in the last 24 hours");
        } else if (change > 0.5) {
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->change_row),
                                          "Charged in the last 24 hours");
        } else {
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->change_row),
                                          "Net change in the last 24 hours");
        }
    } else {
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->change_row),
                                      "Used in the last 24 hours");
        gtk_label_set_text(GTK_LABEL(self->change_value), "—");
    }

    battery_chart_set_snapshot(self->chart, snapshot);
    update_history_note(self, snapshot);
}

static void on_service_changed(BatteryService *service, gpointer user_data)
{
    BatteryWindow *self = BATTERY_WINDOW(user_data);
    const BatterySnapshot *snapshot = battery_service_get_snapshot(service);

    if (snapshot == NULL || !snapshot->available) {
        update_status_page(self, snapshot);
        return;
    }

    update_main_page(self, snapshot);
}

static void build_window(BatteryWindow *self, AdwApplication *application)
{
    self->window = adw_application_window_new(GTK_APPLICATION(application));
    g_object_add_weak_pointer(G_OBJECT(self->window),
                              (gpointer *) &self->window);
    gtk_window_set_title(GTK_WINDOW(self->window), "Battery Statistics");
    gtk_window_set_default_size(GTK_WINDOW(self->window), 760, 620);
    gtk_widget_set_size_request(self->window, 390, 420);

    GtkWidget *toolbar_view = adw_toolbar_view_new();
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(self->window),
                                       toolbar_view);

    GtkWidget *header_bar = adw_header_bar_new();
    GtkWidget *title = adw_window_title_new("Battery Statistics", "");
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header_bar), title);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header_bar), create_app_menu_button());
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view), header_bar);

    self->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(self->stack),
                                  GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(self->stack), 180);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_view), self->stack);

    self->main_page = create_main_page(self);
    self->status_page = create_status_page(self);
    gtk_stack_add_named(GTK_STACK(self->stack), self->main_page, "main");
    gtk_stack_add_named(GTK_STACK(self->stack), self->status_page, "status");
    gtk_stack_set_visible_child(GTK_STACK(self->stack), self->main_page);
}

void battery_window_refresh(BatteryWindow *self)
{
    g_return_if_fail(BATTERY_IS_WINDOW(self));
    battery_chart_clear_selection(self->chart);
    battery_service_retry(self->service);
}

void battery_window_present(BatteryWindow *self)
{
    g_return_if_fail(BATTERY_IS_WINDOW(self));

    if (self->window != NULL)
        gtk_window_present(GTK_WINDOW(self->window));
}

void battery_window_show_about(BatteryWindow *self)
{
    g_return_if_fail(BATTERY_IS_WINDOW(self));
    if (self->window == NULL)
        return;

#if ADW_CHECK_VERSION(1, 5, 0)
    AdwAboutDialog *about = ADW_ABOUT_DIALOG(adw_about_dialog_new());
    adw_about_dialog_set_application_name(about, "Battery Statistics");
    adw_about_dialog_set_application_icon(about, "battery-symbolic");
    adw_about_dialog_set_version(about, "0.1.0");
    adw_about_dialog_set_developer_name(about, "centurion");
    adw_about_dialog_set_comments(
        about,
        "A native GTK 4 and libadwaita viewer for live UPower battery data.");
    adw_about_dialog_set_license_type(about, GTK_LICENSE_GPL_3_0);
    adw_dialog_present(ADW_DIALOG(about), self->window);
#else
    GtkWidget *about = adw_about_window_new();
    adw_about_window_set_application_name(ADW_ABOUT_WINDOW(about),
                                          "Battery Statistics");
    adw_about_window_set_application_icon(ADW_ABOUT_WINDOW(about),
                                          "battery-symbolic");
    adw_about_window_set_version(ADW_ABOUT_WINDOW(about), "0.1.0");
    adw_about_window_set_developer_name(ADW_ABOUT_WINDOW(about),
                                       "centurion");
    adw_about_window_set_comments(
        ADW_ABOUT_WINDOW(about),
        "A native GTK 4 and libadwaita viewer for live UPower battery data.");
    adw_about_window_set_license_type(ADW_ABOUT_WINDOW(about),
                                      GTK_LICENSE_GPL_3_0);
    gtk_window_set_transient_for(GTK_WINDOW(about), GTK_WINDOW(self->window));
    gtk_window_present(GTK_WINDOW(about));
#endif
}

static void battery_window_dispose(GObject *object)
{
    BatteryWindow *self = BATTERY_WINDOW(object);

    if (self->window != NULL) {
        g_object_remove_weak_pointer(G_OBJECT(self->window),
                                     (gpointer *) &self->window);
        gtk_window_destroy(GTK_WINDOW(self->window));
        self->window = NULL;
    }

    g_clear_object(&self->chart);
    g_clear_object(&self->service);

    G_OBJECT_CLASS(battery_window_parent_class)->dispose(object);
}

static void battery_window_class_init(BatteryWindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = battery_window_dispose;
}

static void battery_window_init(BatteryWindow *self)
{
    self->service = battery_service_new();
    g_signal_connect(self->service, "changed", G_CALLBACK(on_service_changed), self);
}

BatteryWindow * battery_window_new(AdwApplication *application)
{
    g_return_val_if_fail(ADW_IS_APPLICATION(application), NULL);

    BatteryWindow *self = g_object_new(BATTERY_TYPE_WINDOW, NULL);
    build_window(self, application);
    battery_service_start(self->service);
    return self;
}
