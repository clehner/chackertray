#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <stdlib.h>

#include "gmulticurl.h"

#define VERSION "0.0.0"
#define APPNAME "chackertray"
#define COPYRIGHT "Copyright (c) 2015 Charles Lehner"
#define COMMENTS "Hacker News for your system tray."
#define WEBSITE "http://github.com/clehner/chackertray"

#define LOGO_ICON "mail-send-receive"

#define HN_API "https://hacker-news.firebaseio.com/v0/"

static unsigned int max_stories = 5;

GtkStatusIcon *status_icon;
GtkWidget *app_menu;
GtkWidget *settings_menu;
GMultiCurl *gmulticurl;

struct item {
    char *name;
    char *description;
};

static void menu_add_item(struct item *);
static void refresh_items();

static void menu_on_about(GtkMenuItem *menuItem, gpointer userData)
{
	gtk_show_about_dialog(NULL,
			"program-name", APPNAME,
			"version", VERSION,
			"logo-icon-name", LOGO_ICON,
			"copyright", COPYRIGHT,
			"comments", COMMENTS,
			"website", WEBSITE,
			NULL);
}

static gboolean status_icon_on_button_press(GtkStatusIcon *status_icon,
    GdkEventButton *event, gpointer user_data)
{
    /* Show the app menu on left click */
    GtkMenu *menu = GTK_MENU(event->button == 1 ? app_menu : settings_menu);

    gtk_menu_popup(menu, NULL, NULL, gtk_status_icon_position_menu,
            status_icon, event->button, event->time);

    return TRUE;
}

int main(int argc, char *argv[])
{
    GtkWidget *item;
	GtkMenuShell *menu;

    gtk_init(&argc, &argv);

    /* Status icon */
    status_icon = gtk_status_icon_new_from_stock(GTK_STOCK_GO_UP);
    gtk_status_icon_set_visible(status_icon, TRUE);

    g_signal_connect(G_OBJECT(status_icon), "button_press_event",
        G_CALLBACK(status_icon_on_button_press), NULL);

    /* App menu */
    app_menu = gtk_menu_new();
    menu = GTK_MENU_SHELL(app_menu);

    item = gtk_menu_item_new_with_mnemonic(_("_Refresh"));
    g_signal_connect(item, "activate", G_CALLBACK(refresh_items), NULL);
    gtk_menu_shell_append(menu, item);

    /* Settings menu */
    settings_menu = gtk_menu_new();
    menu = GTK_MENU_SHELL(settings_menu);

    /* About */
    item = gtk_menu_item_new_with_mnemonic(_("_About"));
    g_signal_connect(item, "activate", G_CALLBACK(menu_on_about), NULL);
    gtk_menu_shell_append(menu, item);

    /* Quit */
    item = gtk_menu_item_new_with_mnemonic(_("_Quit"));
    g_signal_connect(item, "activate", G_CALLBACK(gtk_main_quit), NULL);
    gtk_menu_shell_append(menu, item);

    gtk_widget_show_all(app_menu);
    gtk_widget_show_all(settings_menu);

    struct item x = {
        .name = "name",
        .description = "asdf",
    };
    menu_add_item(&x);

    if (!(gmulticurl = gmulticurl_new()))
        g_warning("gmulticurl init error");

    gtk_main();

    if (gmulticurl_cleanup(gmulticurl))
        g_warning("gmulticurl init error");

    return 0;
}

static void menu_on_item(GtkMenuItem *menuItem, struct item *item)
{
    GError *error;
    const gchar *argv[] = {"xdg-open", item->name, NULL};
    if (!g_spawn_async(NULL, (gchar **)argv, NULL, G_SPAWN_SEARCH_PATH,
                NULL, NULL, NULL, &error)) {
        g_warning("Launching item failed: %s", error->message);
        g_error_free(error);
    }
}

static void menu_add_item(struct item *item)
{
    GtkWidget *menu_item = gtk_menu_item_new_with_label(item->name);
    GtkMenuShell *menu = GTK_MENU_SHELL(app_menu);

    gtk_widget_set_tooltip_text(menu_item, item->description);
    g_signal_connect(menu_item, "activate", G_CALLBACK(menu_on_item), item);

    gtk_widget_show(menu_item);
    gtk_menu_shell_append(menu, menu_item);
}

static size_t topstories_on_data(char *data, size_t len, gpointer arg)
{
    char *p = data;
    unsigned int n;

    if (*p++ != '[') {
        g_warning("expected [ in top stories json");
        return 0;
    }

    for (n = 0; *p && n < max_stories; n++) {
        long id = atol(p);
        g_print("item: %ld\n", id);
        p = strchr(p, ',');
        if (p) p++;
    }

    return len;
}

static void refresh_items()
{
    g_print("refresh\n");
    const gchar *url = HN_API "topstories.json";
    if (gmulticurl_request(gmulticurl, url, topstories_on_data, NULL)) {
        g_warning("gmulticurl_request error");
    }
}

/* vim: set expandtab ts=4 sw=4 */
