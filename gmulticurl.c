/*
 * gmulticurl -- Use libcurl with glib, asynchronously
 * Copyright (c) 2015 Charles Lehner
 *
 * Based on the cURL project's ghiper.c, which was written by Jeff Pohlmeyer,
 * and is licensed as in the COPYING-curl file.
 *
 * gmulticurl is made available under the terms of the Fair License (Fair):
 *
 * Usage of the works is permitted provided that this instrument is retained
 * with the works, so that any entity that uses the works is notified of this
 * instrument.
 *
 * DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.
 */

#include <glib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <curl/curl.h>

#include "gmulticurl.h"

#define MSG_OUT noop   /* Change to "g_error" to write to stderr */
#define SHOW_VERBOSE 0    /* Set to non-zero for libcurl messages */
#define SHOW_PROGRESS 0   /* Set to non-zero to enable progress callback */


/* Global information, common to all connections */
struct _GMultiCurl {
  CURLM *multi;
  CURLSH *share;
  guint timer_event;
  int still_running;
};


/* Information associated with a specific easy handle */
typedef struct _ConnInfo {
  CURL *easy;
  char *url;
  GMultiCurl *global;
  char error[CURL_ERROR_SIZE];
  gpointer user_data;
  write_cb_fn on_write;
} ConnInfo;


/* Information associated with a specific socket */
typedef struct _SockInfo {
  curl_socket_t sockfd;
  CURL *easy;
  int action;
  long timeout;
  GIOChannel *ch;
  guint ev;
  GMultiCurl *global;
} SockInfo;


static void noop(const char *fmt, ...) {
  (void)fmt;
}


/* Die if we get a bad CURLMcode somewhere */
static void mcode_or_die(const char *where, CURLMcode code) {
  if ( CURLM_OK != code ) {
    const char *s;
    switch (code) {
      case     CURLM_BAD_HANDLE:         s="CURLM_BAD_HANDLE";         break;
      case     CURLM_BAD_EASY_HANDLE:    s="CURLM_BAD_EASY_HANDLE";    break;
      case     CURLM_OUT_OF_MEMORY:      s="CURLM_OUT_OF_MEMORY";      break;
      case     CURLM_INTERNAL_ERROR:     s="CURLM_INTERNAL_ERROR";     break;
      case     CURLM_BAD_SOCKET:         s="CURLM_BAD_SOCKET";         break;
      case     CURLM_UNKNOWN_OPTION:     s="CURLM_UNKNOWN_OPTION";     break;
      case     CURLM_LAST:               s="CURLM_LAST";               break;
      default: s="CURLM_unknown";
    }
    MSG_OUT("ERROR: %s returns %s\n", where, s);
    exit(code);
  }
}



/* Check for completed transfers, and remove their easy handles */
static void check_multi_info(GMultiCurl *g)
{
  char *eff_url;
  CURLMsg *msg;
  int msgs_left;
  ConnInfo *conn;
  CURL *easy;
  CURLcode res;

  MSG_OUT("REMAINING: %d\n", g->still_running);
  while ((msg = curl_multi_info_read(g->multi, &msgs_left))) {
    if (msg->msg == CURLMSG_DONE) {
      easy = msg->easy_handle;
      res = msg->data.result;
      curl_easy_getinfo(easy, CURLINFO_PRIVATE, &conn);
      curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &eff_url);
      MSG_OUT("DONE: %s => (%d) %s\n", eff_url, res, conn->error);
      curl_multi_remove_handle(g->multi, easy);
      free(conn->url);
      curl_easy_cleanup(easy);
      free(conn);
    }
  }
}



/* Called by glib when our timeout expires */
static gboolean timer_cb(gpointer data)
{
  GMultiCurl *g = (GMultiCurl *)data;
  CURLMcode rc;

  rc = curl_multi_socket_action(g->multi,
                                  CURL_SOCKET_TIMEOUT, 0, &g->still_running);
  mcode_or_die("timer_cb: curl_multi_socket_action", rc);
  check_multi_info(g);

  g->timer_event = 0;
  return G_SOURCE_REMOVE;
}



/* Update the event timer after curl_multi library calls */
static int update_timeout_cb(CURLM *multi, long timeout_ms, void *userp)
{
  struct timeval timeout;
  GMultiCurl *g=(GMultiCurl *)userp;
  timeout.tv_sec = timeout_ms/1000;
  timeout.tv_usec = (timeout_ms%1000)*1000;

  MSG_OUT("*** update_timeout_cb %ld => %ld:%ld ***\n",
              timeout_ms, timeout.tv_sec, timeout.tv_usec);

  g->timer_event = g_timeout_add(timeout_ms, timer_cb, g);
  return 0;
}




/* Called by glib when we get action on a multi socket */
static gboolean event_cb(GIOChannel *ch, GIOCondition condition, gpointer data)
{
  GMultiCurl *g = (GMultiCurl*) data;
  CURLMcode rc;
  int fd=g_io_channel_unix_get_fd(ch);

  int action =
    (condition & G_IO_IN ? CURL_CSELECT_IN : 0) |
    (condition & G_IO_OUT ? CURL_CSELECT_OUT : 0);

  rc = curl_multi_socket_action(g->multi, fd, action, &g->still_running);
  mcode_or_die("event_cb: curl_multi_socket_action", rc);

  check_multi_info(g);
  if(g->still_running) {
    return TRUE;
  } else {
    MSG_OUT("last transfer done, kill timeout\n");
    if (g->timer_event) {
      g_source_remove(g->timer_event);
      g->timer_event = 0;
    }
    return FALSE;
  }
}



/* Clean up the SockInfo structure */
static void remsock(SockInfo *f)
{
  if (!f) { return; }
  if (f->ev) { g_source_remove(f->ev); }
  g_free(f);
}



/* Assign information to a SockInfo structure */
static void setsock(SockInfo*f, curl_socket_t s, CURL*e, int act, GMultiCurl*g)
{
  GIOCondition kind =
     (act&CURL_POLL_IN?G_IO_IN:0)|(act&CURL_POLL_OUT?G_IO_OUT:0);

  f->sockfd = s;
  f->action = act;
  f->easy = e;
  if (f->ev) { g_source_remove(f->ev); }
  f->ev=g_io_add_watch(f->ch, kind, event_cb,g);

}



/* Initialize a new SockInfo structure */
static void addsock(curl_socket_t s, CURL *easy, int action, GMultiCurl *g)
{
  SockInfo *fdp = g_malloc0(sizeof(SockInfo));

  fdp->global = g;
  fdp->ch=g_io_channel_unix_new(s);
  setsock(fdp, s, easy, action, g);
  curl_multi_assign(g->multi, s, fdp);
}



/* CURLMOPT_SOCKETFUNCTION */
static int sock_cb(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp)
{
  GMultiCurl *g = (GMultiCurl*) cbp;
  SockInfo *fdp = (SockInfo*) sockp;
  static const char *whatstr[]={ "none", "IN", "OUT", "INOUT", "REMOVE" };

  MSG_OUT("socket callback: s=%d e=%p what=%s ", s, e, whatstr[what]);
  if (what == CURL_POLL_REMOVE) {
    MSG_OUT("\n");
    remsock(fdp);
  } else {
    if (!fdp) {
      MSG_OUT("Adding data: %s%s\n",
             what&CURL_POLL_IN?"READ":"",
             what&CURL_POLL_OUT?"WRITE":"" );
      addsock(s, e, what, g);
    }
    else {
      MSG_OUT(
        "Changing action from %d to %d\n", fdp->action, what);
      setsock(fdp, s, e, what, g);
    }
  }
  return 0;
}



/* CURLOPT_WRITEFUNCTION */
static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *data)
{
  size_t realsize = size * nmemb;
  ConnInfo *conn = (ConnInfo*) data;
  return conn->on_write(ptr, realsize, conn->user_data);
}



/* CURLOPT_PROGRESSFUNCTION */
static int prog_cb (void *p, double dltotal, double dlnow, double ult, double uln)
{
  ConnInfo *conn = (ConnInfo *)p;
  MSG_OUT("Progress: %s (%g/%g)\n", conn->url, dlnow, dltotal);
  return 0;
}



/* Create a new easy handle, and add it to the global curl_multi */
int gmulticurl_request(GMultiCurl *g, const gchar *url, write_cb_fn on_write,
    gpointer arg)
{
  ConnInfo *conn;
  CURLMcode rc;

  conn = g_malloc0(sizeof(ConnInfo));

  conn->error[0]='\0';

  CURL *easy = curl_easy_init();
  if (!easy) {
    return -1;
  }
  conn->easy = easy;
  conn->global = g;
  conn->url = g_strdup(url);
  conn->user_data = arg;
  conn->on_write = on_write;
  curl_easy_setopt(easy, CURLOPT_URL, conn->url);
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, conn);
  curl_easy_setopt(easy, CURLOPT_VERBOSE, (long)SHOW_VERBOSE);
  curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, conn->error);
  curl_easy_setopt(easy, CURLOPT_PRIVATE, conn);
  curl_easy_setopt(easy, CURLOPT_NOPROGRESS, SHOW_PROGRESS?0L:1L);
  curl_easy_setopt(easy, CURLOPT_PROGRESSFUNCTION, prog_cb);
  curl_easy_setopt(easy, CURLOPT_PROGRESSDATA, conn);
  curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 30L);
  curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(easy, CURLOPT_SHARE, g->share);

  MSG_OUT("Adding easy %p to multi %p (share %p) (%s)\n", easy,
    g->multi, g->share, url);
  rc =curl_multi_add_handle(g->multi, easy);
  if (rc != CURLM_OK) return -1;

  /* note that the add_handle() will set a time-out to trigger very soon so
     that the necessary socket_action() call will be called by this app */
  return 0;
}


GMultiCurl *gmulticurl_new()
{
  CURLM *m;
  CURLSH *s;
  GMultiCurl *g;

  m = curl_multi_init();
  if (!m) {
    return NULL;
  }
  s = curl_share_init();
  if (!s) {
    curl_multi_cleanup(m);
    return NULL;
  }

  g = g_malloc0(sizeof(GMultiCurl));
  g->multi = m;
  g->share = s;
  g->still_running = FALSE;
  g->timer_event = 0;

  curl_multi_setopt(m, CURLMOPT_SOCKETFUNCTION, sock_cb);
  curl_multi_setopt(m, CURLMOPT_SOCKETDATA, g);
  curl_multi_setopt(m, CURLMOPT_TIMERFUNCTION, update_timeout_cb);
  curl_multi_setopt(m, CURLMOPT_TIMERDATA, g);
  curl_share_setopt(s, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);

  /* we don't call any curl_multi_socket*() function yet as we have no handles
     added! */
  return g;
}

int gmulticurl_cleanup(GMultiCurl *g)
{
  CURLMcode mc = curl_multi_cleanup(g->multi);
  CURLSHcode sc = curl_share_cleanup(g->share);
  return mc || sc ? 1 : 0;
}
