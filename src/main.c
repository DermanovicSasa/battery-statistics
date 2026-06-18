#include <adwaita.h>

#include "battery-window.h"

#define APP_ID "rs.someone.BatteryStatistics"
#define WINDOW_DATA_KEY "battery-window-controller"

static BatteryWindow * get_window_controller(GApplication *application)
{
    return g_object_get_data(G_OBJECT(application), WINDOW_DATA_KEY);
}

static void on_activate(GApplication *application, gpointer user_data)
{
    (void) user_data;

    BatteryWindow *window = get_window_controller(application);
    if (window == NULL) {
        window = battery_window_new(ADW_APPLICATION(application));
        g_object_set_data_full(G_OBJECT(application),
                               WINDOW_DATA_KEY,
                               window,
                               g_object_unref);
    }

    battery_window_present(window);
}

static void on_refresh_action(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void) action;
    (void) parameter;

    BatteryWindow *window = get_window_controller(G_APPLICATION(user_data));
    if (window != NULL)
        battery_window_refresh(window);
}

static void on_theme_action(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{   (void) action;
    (void) parameter;
    BatteryWindow *win = get_window_controller(G_APPLICATION(user_data));
    if(win != NULL)
        battery_window_show_preferences(win);
}

static void on_about_action(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void) action;
    (void) parameter;

    BatteryWindow *window = get_window_controller(G_APPLICATION(user_data));
    if (window != NULL)
        battery_window_show_about(window);
}


int main(int argc, char **argv)
{
    g_autoptr(AdwApplication) application = adw_application_new(
        APP_ID,
        G_APPLICATION_DEFAULT_FLAGS);

    const GActionEntry actions[] = {
        {.name = "refresh", .activate = on_refresh_action},
        {.name = "about", .activate = on_about_action},
        {.name = "theme", .activate = on_theme_action}
    };
    g_action_map_add_action_entries(G_ACTION_MAP(application),
                                    actions,
                                    G_N_ELEMENTS(actions),
                                    application);

    const gchar *refresh_accels[] = {"<Primary>r", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(application),
                                          "app.refresh",
                                          refresh_accels);

    g_signal_connect(application, "activate", G_CALLBACK(on_activate), NULL);
    return g_application_run(G_APPLICATION(application), argc, argv);
}
