#include "battery-chart.h"

#include <adwaita.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <string.h>

#define CHART_LEFT 42.0
#define CHART_RIGHT 16.0
#define CHART_TOP 18.0
#define CHART_BOTTOM 34.0
#define INFO_CARD_WIDTH 154
#define INFO_CARD_HEIGHT 82

struct _BatteryChart 
{
    GObject parent_instance;

    GtkWidget *root;
    GtkWidget *drawing_area;
    GtkWidget *info_card;
    GtkWidget *info_value;
    GtkWidget *info_time;
    GtkWidget *info_state;

    BatteryHour hours[BATTERY_HISTORY_HOURS];
    gint64 first_hour;
    gint hovered_index;
    gint selected_index;
};

G_DEFINE_FINAL_TYPE(BatteryChart, battery_chart, G_TYPE_OBJECT)

static gdouble chart_plot_width(gint width)
{
    return MAX(1.0, width - CHART_LEFT - CHART_RIGHT);
}

static gdouble chart_plot_height(gint height)
{
    return MAX(1.0, height - CHART_TOP - CHART_BOTTOM);
}

static gdouble chart_hour_step(gdouble plot_width)
{
    return plot_width / BATTERY_HISTORY_HOURS;
}

static gdouble chart_bar_width(gdouble hour_step)
{
    return CLAMP(hour_step * 0.48, 4.0, 13.0);
}

static gdouble chart_bar_x(gint index, gdouble hour_step, gdouble bar_width)
{
    return CHART_LEFT + index * hour_step + (hour_step - bar_width) / 2.0;
}

static gint active_index(const BatteryChart *self)
{
    return self->selected_index >= 0
               ? self->selected_index
               : self->hovered_index;
}

static GdkRGBA get_foreground(GtkWidget *widget)
{
    GdkRGBA color;
#if GTK_CHECK_VERSION(4, 10, 0)
    gtk_widget_get_color(widget, &color);
#else
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gtk_style_context_get_color(gtk_widget_get_style_context(widget), &color);
    G_GNUC_END_IGNORE_DEPRECATIONS
#endif
    return color;
}

static GdkRGBA get_accent_color(void)
{
    AdwStyleManager *manager = adw_style_manager_get_default();
    GdkRGBA color;

#if ADW_CHECK_VERSION(1, 6, 0)
    adw_accent_color_to_standalone_rgba(adw_style_manager_get_accent_color(manager),
                                        adw_style_manager_get_dark(manager),
                                        &color);
#else
    gdk_rgba_parse(&color,
                   adw_style_manager_get_dark(manager)
                       ? "#81d0ff"
                       : "#0461be");
#endif

    return color;
}

static void rounded_rectangle(cairo_t *cr, gdouble x, gdouble y, gdouble width, gdouble height, gdouble radius)
{
    radius = MIN(radius, MIN(width, height) / 2.0);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + width - radius, y + radius, radius, -G_PI / 2.0, 0);
    cairo_arc(cr, x + width - radius, y + height - radius, radius, 0, G_PI / 2.0);
    cairo_arc(cr, x + radius, y + height - radius, radius, G_PI / 2.0, G_PI);
    cairo_arc(cr, x + radius, y + radius, radius, G_PI, 3.0 * G_PI / 2.0);
    cairo_close_path(cr);
}

static void draw_text(cairo_t *cr, GtkWidget *widget, const gchar *text, gdouble x,gdouble y, PangoAlignment alignment, 
    const GdkRGBA *color)
{
    g_autoptr(PangoLayout) layout = gtk_widget_create_pango_layout(widget, text);
    PangoContext *pctx = gtk_widget_get_pango_context(widget);
    PangoFontDescription *font = pango_font_description_new();
    pango_context_get_font_description(pctx);
    pango_font_description_set_absolute_size(font, pango_font_description_get_size(font) * 0.75);
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    pango_layout_set_alignment(layout, alignment);

    gint text_width = 0;
    gint text_height = 0;
    pango_layout_get_pixel_size(layout, &text_width, &text_height);

    if (alignment == PANGO_ALIGN_CENTER)
        x -= text_width / 2.0;
    else if (alignment == PANGO_ALIGN_RIGHT)
        x -= text_width;

    gdk_cairo_set_source_rgba(cr, color);
    cairo_move_to(cr, x, y - text_height / 2.0);
    pango_cairo_show_layout(cr, layout);
}

static void
draw_charging_bolt(cairo_t *cr, gdouble center_x, gdouble y, const GdkRGBA *color)
{
    gdk_cairo_set_source_rgba(cr, color);
    cairo_move_to(cr, center_x + 1.0, y);
    cairo_line_to(cr, center_x - 3.0, y + 6.0);
    cairo_line_to(cr, center_x, y + 6.0);
    cairo_line_to(cr, center_x - 1.0, y + 12.0);
    cairo_line_to(cr, center_x + 4.0, y + 5.0);
    cairo_line_to(cr, center_x + 1.0, y + 5.0);
    cairo_close_path(cr);
    cairo_fill(cr);
}

static void draw_chart(GtkDrawingArea *area, cairo_t *cr, gint width, gint height, gpointer user_data)
{
    BatteryChart *self = BATTERY_CHART(user_data);
    GtkWidget *widget = GTK_WIDGET(area);
    const GdkRGBA foreground = get_foreground(widget);
    const GdkRGBA accent = get_accent_color();
    const gboolean high_contrast = adw_style_manager_get_high_contrast(
    adw_style_manager_get_default());
    const gdouble plot_width = chart_plot_width(width);
    const gdouble plot_height = chart_plot_height(height);
    const gdouble hour_step = chart_hour_step(plot_width);
    const gdouble bar_width = chart_bar_width(hour_step);
    const gint active = active_index(self);

    GdkRGBA grid = foreground;
    grid.alpha = high_contrast ? 0.34 : 0.14;

    cairo_set_line_width(cr, 1.0);
    for (gint step = 0; step <= 4; step++) 
    {
        const gdouble y = CHART_TOP + plot_height * step / 4.0;
        gdk_cairo_set_source_rgba(cr, &grid);
        cairo_move_to(cr, CHART_LEFT, floor(y) + 0.5);
        cairo_line_to(cr, CHART_LEFT + plot_width, floor(y) + 0.5);
        cairo_stroke(cr);

        GdkRGBA label_color = foreground;
        label_color.alpha = high_contrast ? 0.9 : 0.62;
        g_autofree gchar *label = g_strdup_printf("%d", 100 - step * 25);
        draw_text(cr, widget, label, CHART_LEFT - 7.0, y, PANGO_ALIGN_RIGHT, &label_color);
    }

    GdkRGBA line = accent;
    line.alpha = high_contrast ? 0.65 : 0.30;
    cairo_set_line_width(cr, high_contrast ? 2.0 : 1.5);

    for (gint i = 1; i < BATTERY_HISTORY_HOURS; i++) 
    {
        if (!self->hours[i - 1].valid || !self->hours[i].valid)
            continue;

        const gdouble x1 = chart_bar_x(i - 1, hour_step, bar_width) + bar_width / 2.0;
        const gdouble x2 = chart_bar_x(i, hour_step, bar_width) + bar_width / 2.0;
        const gdouble y1 = CHART_TOP + plot_height *
                           (1.0 - self->hours[i - 1].level / 100.0);
        const gdouble y2 = CHART_TOP + plot_height *
                           (1.0 - self->hours[i].level / 100.0);

        gdk_cairo_set_source_rgba(cr, &line);
        cairo_move_to(cr, x1, y1);
        cairo_line_to(cr, x2, y2);
        cairo_stroke(cr);
    }

    if (active >= 0 && active < BATTERY_HISTORY_HOURS && self->hours[active].valid) 
    {
        GdkRGBA guide = accent;
        guide.alpha = high_contrast ? 0.55 : 0.22;
        const gdouble center_x = chart_bar_x(active, hour_step, bar_width) +
                                 bar_width / 2.0;
        cairo_set_line_width(cr, 1.0);
        gdk_cairo_set_source_rgba(cr, &guide);
        cairo_move_to(cr, floor(center_x) + 0.5, CHART_TOP);
        cairo_line_to(cr, floor(center_x) + 0.5, CHART_TOP + plot_height);
        cairo_stroke(cr);
    }

    gint valid_count = 0;
    for (gint i = 0; i < BATTERY_HISTORY_HOURS; i++) 
    {
        if (!self->hours[i].valid)
            continue;

        valid_count++;
        const gdouble bar_height = MAX(1.0,
                                       self->hours[i].level / 100.0 * plot_height);
        const gdouble x = chart_bar_x(i, hour_step, bar_width);
        const gdouble y = CHART_TOP + plot_height - bar_height;
        const gboolean is_active = i == active;

        GdkRGBA bar = accent;
        if (battery_state_is_charging(self->hours[i].state))
            bar.alpha = is_active ? 0.88 : 0.55;
        else
            bar.alpha = is_active ? 1.0 : 0.82;

        gdk_cairo_set_source_rgba(cr, &bar);
        rounded_rectangle(cr, x, y, bar_width,bar_height, MIN(5.0, bar_width / 2.0));
        cairo_fill_preserve(cr);

        if (is_active) 
        {
            GdkRGBA outline = accent;
            outline.alpha = 1.0;
            cairo_set_line_width(cr, high_contrast ? 3.0 : 2.0);
            gdk_cairo_set_source_rgba(cr, &outline);
            cairo_stroke(cr);
        } 
        else 
        {
            cairo_new_path(cr);
        }

        if (battery_state_is_charging(self->hours[i].state)) 
        {
            GdkRGBA bolt = foreground;
            bolt.alpha = high_contrast ? 1.0 : 0.86;
            draw_charging_bolt(cr, x + bar_width / 2.0, MAX(CHART_TOP + 2.0, y - 14.0), &bolt);
        }

        if (i == BATTERY_HISTORY_HOURS - 1) 
        {
            GdkRGBA marker = foreground;
            marker.alpha = 0.9;
            gdk_cairo_set_source_rgba(cr, &marker);
            cairo_arc(cr, x + bar_width / 2.0, y, is_active ? 3.5 : 2.5, 0, 2.0 * G_PI);
            cairo_fill(cr);
        }
    }

    GdkRGBA axis_color = foreground;
    axis_color.alpha = high_contrast ? 0.9 : 0.62;

    const gboolean narrow = width < 520;
    const gint wide_indices[] = {0, 6, 12, 18, 23};
    const gint narrow_indices[] = {0, 12, 23};
    const gint *indices = narrow ? narrow_indices : wide_indices;
    const guint n_indices = narrow ? G_N_ELEMENTS(narrow_indices) : G_N_ELEMENTS(wide_indices);

    for (guint n = 0; n < n_indices; n++) 
    {
        const gint i = indices[n];
        g_autofree gchar *label = NULL;

        if (i == BATTERY_HISTORY_HOURS - 1) 
        {
            label = g_strdup("Now");
        } 
        else 
        {
            const gint64 stamp = self->first_hour + (gint64) i * BATTERY_SECONDS_PER_HOUR;
            g_autoptr(GDateTime) dt = g_date_time_new_from_unix_local(stamp);
            label = g_date_time_format(dt, narrow ? "%H" : "%H:%M");
        }

        const gdouble x = CHART_LEFT + (i + 0.5) * hour_step;
        draw_text(cr, widget, label, x, CHART_TOP + plot_height + 18.0, PANGO_ALIGN_CENTER, &axis_color);
    }

    if (valid_count == 0) 
    {
        GdkRGBA empty = foreground;
        empty.alpha = 0.68;
        draw_text(cr, widget, "Battery history is not available yet", CHART_LEFT + plot_width / 2.0,
                  CHART_TOP + plot_height / 2.0, PANGO_ALIGN_CENTER, &empty);
    }

    if(gtk_widget_has_visible_focus(widget))
    {
        GdkRGBA focus = accent;
        focus.alpha = 0.9;
        cairo_set_line_width(cr, 2.0);
        gdk_cairo_set_source_rgba(cr, &focus);
        rounded_rectangle(cr, 1.0, 1.0, MAX(1.0, width - 2.0), MAX(1.0, height - 2.0), 8.0);
        cairo_stroke(cr);
    }
}

static gint index_at_position(BatteryChart *self, gdouble x, gdouble y)
{
    const gint width = gtk_widget_get_width(self->drawing_area);
    const gint height = gtk_widget_get_height(self->drawing_area);
    const gdouble plot_width = chart_plot_width(width);
    const gdouble plot_height = chart_plot_height(height);

    if (x < CHART_LEFT || x >= CHART_LEFT + plot_width ||
        y < CHART_TOP || y > CHART_TOP + plot_height)
        return -1;

    const gdouble hour_step = chart_hour_step(plot_width);
    const gint index = (gint) floor((x - CHART_LEFT) / hour_step);

    if (index < 0 || index >= BATTERY_HISTORY_HOURS ||
        !self->hours[index].valid)
        return -1;

    const gdouble bar_width = chart_bar_width(hour_step);
    const gdouble bar_x = chart_bar_x(index, hour_step, bar_width);

    return x >= bar_x && x <= bar_x + bar_width ? index : -1;
}

static void update_accessible_description(BatteryChart *self)
{
    const gint index = active_index(self);

    if (index < 0 || index >= BATTERY_HISTORY_HOURS || !self->hours[index].valid)
     {
        gtk_accessible_update_property(
            GTK_ACCESSIBLE(self->drawing_area),
            GTK_ACCESSIBLE_PROPERTY_LABEL,
            "Battery level history for the last 24 hours",
            GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
            "Use the Left and Right arrow keys to inspect measured hours.",
            -1);
        return;
    }

    const guint64 stamp = self->hours[index].timestamp != 0
                              ? (gint64) self->hours[index].timestamp
                              : self->first_hour +
                                    (gint64) index * BATTERY_SECONDS_PER_HOUR;
    g_autoptr(GDateTime) dt = g_date_time_new_from_unix_local((gint64) stamp);
    g_autofree gchar *time = g_date_time_format(dt, "%H:%M");
    g_autofree gchar *description = g_strdup_printf(
        "%s, %.0f percent, %s. Use Left and Right arrow keys to inspect another hour.",
        time,
        self->hours[index].level,
        battery_state_name(self->hours[index].state));

    gtk_accessible_update_property(
        GTK_ACCESSIBLE(self->drawing_area),
        GTK_ACCESSIBLE_PROPERTY_LABEL,
        "Battery level history",
        GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
        description,
        -1);
}

static void position_info_card(BatteryChart *self, gint index)
{
    const gint width = gtk_widget_get_width(self->drawing_area);
    const gint height = gtk_widget_get_height(self->drawing_area);
    const gdouble plot_width = chart_plot_width(width);
    const gdouble plot_height = chart_plot_height(height);
    const gdouble hour_step = chart_hour_step(plot_width);
    const gdouble bar_width = chart_bar_width(hour_step);
    const gdouble bar_x = chart_bar_x(index, hour_step, bar_width);
    const gdouble bar_y = CHART_TOP + plot_height *
                          (1.0 - self->hours[index].level / 100.0);

    const gint max_x = MAX(8, width - INFO_CARD_WIDTH - 8);
    const gint card_x = CLAMP((gint) round(bar_x + bar_width / 2.0 -
                                          INFO_CARD_WIDTH / 2.0),
                              8,
                              max_x);
    const gint card_y = bar_y > INFO_CARD_HEIGHT + 18
                            ? (gint) round(bar_y - INFO_CARD_HEIGHT - 10)
                            : MIN(height - INFO_CARD_HEIGHT - 8,
                                  (gint) round(bar_y + 10));

    gtk_widget_set_margin_start(self->info_card, MAX(0, card_x));
    gtk_widget_set_margin_top(self->info_card, MAX(0, card_y));
}

static void update_info_card(BatteryChart *self)
{
    const gint index = active_index(self);

    if (index < 0 || index >= BATTERY_HISTORY_HOURS || !self->hours[index].valid) {
        gtk_widget_set_visible(self->info_card, FALSE);
        update_accessible_description(self);
        return;
    }

    const guint64 stamp = self->hours[index].timestamp != 0
                              ?(gint64) self->hours[index].timestamp
                              : self->first_hour +
                                    (gint64) index * BATTERY_SECONDS_PER_HOUR;
    g_autoptr(GDateTime) dt = g_date_time_new_from_unix_local((gint64) stamp);
    g_autofree gchar *time = g_date_time_format(dt, "%H:%M");
    g_autofree gchar *value = g_strdup_printf("%.0f%%", self->hours[index].level);

    gtk_label_set_text(GTK_LABEL(self->info_value), value);
    gtk_label_set_text(GTK_LABEL(self->info_time), time);
    gtk_label_set_text(GTK_LABEL(self->info_state),
                       battery_state_name(self->hours[index].state));

    position_info_card(self, index);
    gtk_widget_set_visible(self->info_card, TRUE);
    update_accessible_description(self);
}

static void set_hovered_index(BatteryChart *self, gint index)
{
    if (self->hovered_index == index)
        return;

    self->hovered_index = index;
    update_info_card(self);
    gtk_widget_queue_draw(self->drawing_area);
}

static void set_selected_index(BatteryChart *self, gint index)
{
    if (index >= 0 &&
        (index >= BATTERY_HISTORY_HOURS || !self->hours[index].valid))
        index = -1;

    if (self->selected_index == index)
        return;

    self->selected_index = index;
    update_info_card(self);
    gtk_widget_queue_draw(self->drawing_area);
}

static void on_motion(GtkEventControllerMotion *controller, gdouble x, gdouble y, gpointer user_data)
{
    (void) controller;
    BatteryChart *self = BATTERY_CHART(user_data);
    set_hovered_index(self, index_at_position(self, x, y));
}

static void on_leave(GtkEventControllerMotion *controller, gpointer user_data)
{
    (void) controller;
    set_hovered_index(BATTERY_CHART(user_data), -1);
}

static void on_pressed(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data)
{
    (void) gesture;
    (void) n_press;

    BatteryChart *self = BATTERY_CHART(user_data);
    gtk_widget_grab_focus(self->drawing_area);
    set_selected_index(self, index_at_position(self, x, y));
}

static gint find_valid_from(BatteryChart *self, gint start, gint direction)
{
    for (gint i = start; i >= 0 && i < BATTERY_HISTORY_HOURS; i += direction) 
    {
        if (self->hours[i].valid)
            return i;
    }
    return -1;
}

static gboolean on_key_pressed(GtkEventControllerKey *controller,guint keyval,guint keycode,
                                GdkModifierType state,  gpointer user_data)
{
    (void) controller;
    (void) keycode;
    (void) state;

    BatteryChart *self = BATTERY_CHART(user_data);
    const gint current = active_index(self);
    gint next = -1;

    switch (keyval) {
    case GDK_KEY_Left:
    case GDK_KEY_KP_Left:
        next = find_valid_from(self,
                               current >= 0 ? current - 1
                                            : BATTERY_HISTORY_HOURS - 1,
                               -1);
        break;
    case GDK_KEY_Right:
    case GDK_KEY_KP_Right:
        next = find_valid_from(self, current >= 0 ? current + 1 : 0, 1);
        break;
    case GDK_KEY_Home:
    case GDK_KEY_KP_Home:
        next = find_valid_from(self, 0, 1);
        break;
    case GDK_KEY_End:
    case GDK_KEY_KP_End:
        next = find_valid_from(self, BATTERY_HISTORY_HOURS - 1, -1);
        break;
    case GDK_KEY_Escape:
        set_selected_index(self, -1);
        return TRUE;
    default:
        return FALSE;
    }

    if (next >= 0) {
        set_selected_index(self, next);
        return TRUE;
    }

    return FALSE;
}

static void on_focus_changed(GObject *object,GParamSpec *pspec,gpointer user_data)
{
    (void) object;
    (void) pspec;
    BatteryChart *self = BATTERY_CHART(user_data);
    gtk_widget_queue_draw(self->drawing_area);
}

static void install_chart_css(void)
{
    static gsize installed = 0;

    if (!g_once_init_enter(&installed))
        return;

    static const char css[] =
        ".battery-hover-card {"
        "  background-color:"
        "    mix(@card_bg_color, @accent_bg_color, 0.30);"
        "  color: @card_fg_color;"
        "  border: 1px solid alpha(@card_fg_color, 0.38);"
        "  border-radius: 12px;"
        "  box-shadow: 0 8px 24px alpha(black, 0.45);"
        "}";

    GtkCssProvider *provider = gtk_css_provider_new();

#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_css_provider_load_from_string(provider, css);
#else
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gtk_css_provider_load_from_data(provider, css, -1);
    G_GNUC_END_IGNORE_DEPRECATIONS
#endif

    GdkDisplay *display = gdk_display_get_default();

    if (display != NULL) {
        gtk_style_context_add_provider_for_display(
            display,
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    g_object_unref(provider);
    g_once_init_leave(&installed, 1);
}
static GtkWidget* create_info_card(BatteryChart *self)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_add_css_class(card, "battery-hover-card");
    gtk_widget_set_halign(card, GTK_ALIGN_START);
    gtk_widget_set_valign(card, GTK_ALIGN_START);
    gtk_widget_set_can_target(card, FALSE);
    gtk_widget_set_size_request(card, INFO_CARD_WIDTH, -1);
    gtk_widget_set_margin_start(card, 8);
    gtk_widget_set_margin_top(card, 8);

    GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_margin_start(inner, 12);
    gtk_widget_set_margin_end(inner, 12);
    gtk_widget_set_margin_top(inner, 9);
    gtk_widget_set_margin_bottom(inner, 9);
    gtk_box_append(GTK_BOX(card), inner);

    self->info_value = gtk_label_new("—");
    gtk_widget_set_halign(self->info_value, GTK_ALIGN_START);
    gtk_widget_add_css_class(self->info_value, "title-3");
    gtk_widget_add_css_class(self->info_value, "numeric");
    gtk_box_append(GTK_BOX(inner), self->info_value);

    self->info_time = gtk_label_new("");
    gtk_widget_set_halign(self->info_time, GTK_ALIGN_START);
    gtk_widget_add_css_class(self->info_time, "caption");
    gtk_box_append(GTK_BOX(inner), self->info_time);

    self->info_state = gtk_label_new("");
    gtk_widget_set_halign(self->info_state, GTK_ALIGN_START);
    gtk_widget_add_css_class(self->info_state, "caption");
    gtk_box_append(GTK_BOX(inner), self->info_state);

    gtk_widget_set_visible(card, FALSE);
    return card;
}

static void battery_chart_dispose(GObject *object)
{
    BatteryChart *self = BATTERY_CHART(object);
    g_clear_object(&self->root);
    G_OBJECT_CLASS(battery_chart_parent_class)->dispose(object);
}

static void battery_chart_class_init(BatteryChartClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = battery_chart_dispose;
}

static void battery_chart_init(BatteryChart *self)
{
    install_chart_css();
    self->hovered_index = -1;
    self->selected_index = -1;

    self->root = gtk_overlay_new();
    g_object_ref_sink(self->root);

    self->drawing_area = g_object_new(GTK_TYPE_DRAWING_AREA,
                                      "accessible-role",
                                      GTK_ACCESSIBLE_ROLE_GROUP,
                                      NULL);
    gtk_widget_set_size_request(self->drawing_area, -1, 286);
    gtk_widget_set_hexpand(self->drawing_area, TRUE);
    gtk_widget_set_focusable(self->drawing_area, TRUE);
    gtk_widget_set_focus_on_click(self->drawing_area, TRUE);
    gtk_widget_set_overflow(self->drawing_area, GTK_OVERFLOW_HIDDEN);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(self->drawing_area),
                                   draw_chart,
                                   self,
                                   NULL);
    gtk_overlay_set_child(GTK_OVERLAY(self->root), self->drawing_area);

    self->info_card = create_info_card(self);
    gtk_overlay_add_overlay(GTK_OVERLAY(self->root), self->info_card);

    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), self);
    g_signal_connect(motion, "leave", G_CALLBACK(on_leave), self);
    gtk_widget_add_controller(self->drawing_area, motion);

    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_pressed), self);
    gtk_widget_add_controller(self->drawing_area, GTK_EVENT_CONTROLLER(click));

    GtkEventController *key = gtk_event_controller_key_new();
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_key_pressed), self);
    gtk_widget_add_controller(self->drawing_area, key);

    g_signal_connect(self->drawing_area,
                     "notify::has-focus",
                     G_CALLBACK(on_focus_changed),
                     self);

    update_accessible_description(self);
}

BatteryChart* battery_chart_new(void)
{
    return g_object_new(BATTERY_TYPE_CHART, NULL);
}

GtkWidget* battery_chart_get_widget(BatteryChart *self)
{
    g_return_val_if_fail(BATTERY_IS_CHART(self), NULL);
    return self->root;
}

void battery_chart_set_snapshot(BatteryChart *self, const BatterySnapshot *snapshot)
{
    g_return_if_fail(BATTERY_IS_CHART(self));
    g_return_if_fail(snapshot != NULL);

    memcpy(self->hours, snapshot->hours, sizeof(self->hours));
    self->first_hour = snapshot->first_hour;

    if (self->selected_index >= 0 &&
        !self->hours[self->selected_index].valid)
        self->selected_index = -1;
    if (self->hovered_index >= 0 &&
        !self->hours[self->hovered_index].valid)
        self->hovered_index = -1;

    update_info_card(self);
    gtk_widget_queue_draw(self->drawing_area);
}

void battery_chart_clear_selection(BatteryChart *self)
{
    g_return_if_fail(BATTERY_IS_CHART(self));
    set_selected_index(self, -1);
}
