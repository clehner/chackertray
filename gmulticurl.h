/*
 * gmulticurl -- Use libcurl with glib, asynchronously
 * Copyright (c) 2015 Charles Lehner
 *
 * Usage of the works is permitted provided that this instrument is retained
 * with the works, so that any entity that uses the works is notified of this
 * instrument.
 *
 * Fair License (Fair)
 *
 * DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.
 */

typedef struct _GMultiCurl GMultiCurl;

typedef size_t (*write_cb_fn)(gchar *data, size_t len, gpointer);

GMultiCurl *gmulticurl_new();
int gmulticurl_cleanup(GMultiCurl *);
int gmulticurl_request(GMultiCurl *, const gchar *url, write_cb_fn, gpointer);
