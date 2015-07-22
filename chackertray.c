/*
 * chackertray -- Hacker News in your system tray
 * Copyright (c) 2015 Charles Lehner
 * Fair License (Fair)
 */
#define LICENSE "\
Fair License (Fair)\
\n\n\
Usage of the works is permitted provided that this instrument \
is retained with the works, so that any entity that uses the \
works is notified of this instrument.\
\n\n\
DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY."

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <stdlib.h>

#include "gmulticurl.h"

#define VERSION "1.0.0"
#define APPNAME "chackertray"
#define COPYRIGHT "Copyright (c) 2015 Charles Lehner"
#define COMMENTS "Hacker News for your system tray."
#define WEBSITE "https://github.com/clehner/chackertray"

#define LOGO_ICON "mail-send-receive"

#define HN_API "https://hacker-news.firebaseio.com/v0/"

#define MAX_STORIES 10

const guint refresh_timeout = 120;

GtkStatusIcon *status_icon;
GtkWidget *app_menu;
GtkWidget *settings_menu;
GMultiCurl *gmulticurl;

struct story {
    GtkWidget *menu_item;
    guint num;
    guint id;
    gchar *url;
    gchar *title;
    gchar *description;
    guint num_comments;
    guint points;
    time_t time;
};

struct story stories[MAX_STORIES];

static void menu_init_item(struct story *);
static void refresh_stories();
static void refresh_story(struct story *);
static size_t topstories_on_data(gchar *data, size_t len, gpointer arg);
static size_t story_on_data(gchar *data, size_t len, gpointer arg);
static void update_story(struct story *story);

static void menu_on_about(GtkMenuItem *menuItem, gpointer userData)
{
    gtk_show_about_dialog(NULL,
            "program-name", APPNAME,
            "version", VERSION,
            "logo-icon-name", LOGO_ICON,
            "copyright", COPYRIGHT,
            "comments", COMMENTS,
            "website", WEBSITE,
            "license", LICENSE,
            "wrap-license", TRUE,
            NULL);
}

static gboolean status_icon_on_button_press(GtkStatusIcon *status_icon,
    GdkEventButton *event, gpointer user_data)
{
    /* Show the app menu on left click */
    GtkWidget *menu = event->button == 1 ? app_menu : settings_menu;

    if (menu == app_menu) {
        refresh_stories(FALSE);
    }

    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, gtk_status_icon_position_menu,
            status_icon, event->button, event->time);

    return TRUE;
}

int main(int argc, char *argv[])
{
    GtkWidget *item;
    GtkMenuShell *menu;

    gtk_init(&argc, &argv);

    /* Status icon */
    status_icon = gtk_status_icon_new_from_icon_name(LOGO_ICON);
    gtk_status_icon_set_visible(status_icon, TRUE);

    g_signal_connect(G_OBJECT(status_icon), "button_press_event",
        G_CALLBACK(status_icon_on_button_press), NULL);

    /* App menu */
    app_menu = gtk_menu_new();
    menu = GTK_MENU_SHELL(app_menu);

    /* Story items */
    for (guint i = 0; i < MAX_STORIES; i++) {
        menu_init_item(&stories[i]);
    }

    /* Settings menu */
    settings_menu = gtk_menu_new();
    menu = GTK_MENU_SHELL(settings_menu);

    /* Refresh */
    item = gtk_menu_item_new_with_mnemonic(_("_Refresh"));
    gpointer immediate = GINT_TO_POINTER(FALSE);
    g_signal_connect(item, "activate", G_CALLBACK(refresh_stories), immediate);
    gtk_menu_shell_append(menu, item);

    gtk_menu_shell_append(menu, gtk_separator_menu_item_new());

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

    if (!(gmulticurl = gmulticurl_new()))
        g_warning("gmulticurl init error");

    gtk_main();

    if (gmulticurl_cleanup(gmulticurl))
        g_warning("gmulticurl init error");

    return 0;
}

static void menu_on_item(GtkMenuItem *menuItem, struct story *item)
{
    GError *error;
    const gchar *argv[] = {"xdg-open", item->url, NULL};
    if (!g_spawn_async(NULL, (gchar **)argv, NULL, G_SPAWN_SEARCH_PATH,
                NULL, NULL, NULL, &error)) {
        g_warning("Launching item failed: %s", error->message);
        g_error_free(error);
    }
}

static void menu_init_item(struct story *item)
{
    GtkWidget *menu_item = gtk_menu_item_new_with_label(item->title);
    GtkMenuShell *menu = GTK_MENU_SHELL(app_menu);

    g_signal_connect(menu_item, "activate", G_CALLBACK(menu_on_item), item);
    item->menu_item = menu_item;

    gtk_menu_shell_append(menu, menu_item);
}

static gboolean refresh_stories_cb(gpointer timer)
{
    *(gint *)timer = 0;
    return G_SOURCE_REMOVE;
}

static void refresh_stories(gboolean immediate)
{
    static gint refresh_timer;

    if (refresh_timer) {
        if (immediate)
            g_source_remove(refresh_timer);
        else
            return;
    }

    refresh_timer = gdk_threads_add_timeout_seconds_full(G_PRIORITY_LOW,
            refresh_timeout, refresh_stories_cb, &refresh_timer, NULL);

    g_print("refreshing stories\n");

    const gchar *url = HN_API "topstories.json";
    if (gmulticurl_request(gmulticurl, url, topstories_on_data, NULL)) {
        g_warning("gmulticurl_request error");
    }
}

static size_t topstories_on_data(gchar *data, size_t len, gpointer arg)
{
    char *p = data;
    unsigned int n;

    if (*p++ != '[') {
        g_warning("expected [ in top stories json");
        return 0;
    }

    for (n = 0; *p && n < MAX_STORIES; n++) {
        struct story *story = &stories[n];
        story->id = atoi(p);
        p = strchr(p, ',');
        if (p) p++;
        refresh_story(story);
    }

    return len;
}

static void refresh_story(struct story *story)
{
    gchar url[1024];
    guint id = story->id;

    if (g_snprintf(url, sizeof url, "%sitem/%u.json", HN_API, id) < 0) {
        g_warning("failed to build item url");
    }

    if (gmulticurl_request(gmulticurl, url, story_on_data, story)) {
        g_warning("gmulticurl_request error");
    }
}

void extract_quote(gchar *str)
{
    gchar c;
    gchar *in = str;
    gchar *out = str;
    gboolean escape = FALSE;

    while ((c = *in++) && (c != '"' || escape)) {
        if (c == '\\')
            escape ^= 1;
        if (escape)
            continue;
        if (in != out)
            *out++ = c;
    }
    *out = '\0';
}

static size_t story_on_data(gchar *data, size_t len, gpointer arg)
{
    struct story *story = arg;
    gchar *title, *url, *num_comments, *points, *time;

    if (!(title = strstr(data, "\"title\":\""))) {
        g_warning("couldn't find item title");
        return 0;
    }

    if (!(url = strstr(data, "\"url\":\""))) {
        g_warning("couldn't find item url");
        return 0;
    }

    if (!(num_comments = strstr(data, "\"descendants\":"))) {
        g_warning("couldn't find number of comments");
        return 0;
    }

    if (!(points = strstr(data, "\"score\":"))) {
        g_warning("couldn't find item points");
        return 0;
    }

    if (!(time = strstr(data, "\"time\":"))) {
        g_warning("couldn't find item time");
        return 0;
    }

    title += 9;
    extract_quote(title);
    story->title = title;

    url += 7;
    extract_quote(url);
    if (story->url)
        g_free(story->url);
    story->url = g_strdup(url);

    story->num_comments = atoi(num_comments + 14);
    story->points = atoi(points + 8);
    story->time = atol(time + 7);

    update_story(story);

    story->title = NULL;

    return len;
}

static void update_story(struct story *story)
{
    GtkMenuItem *menu_item = GTK_MENU_ITEM(story->menu_item);
    gchar title[512];

    g_snprintf(title, sizeof title, "%03u/%03u   %s",
            story->points, story->num_comments, story->title);

    gtk_menu_item_set_label(menu_item, title);
    gtk_widget_set_tooltip_text(story->menu_item, story->description);
    gtk_widget_show(story->menu_item);
}

/* vim: set expandtab ts=4 sw=4: */
