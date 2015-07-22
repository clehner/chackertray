typedef struct _GMultiCurl GMultiCurl;

typedef size_t (*write_cb_fn)(char *data, size_t len, gpointer);

GMultiCurl *gmulticurl_new();
int gmulticurl_cleanup(GMultiCurl *);
int gmulticurl_request(GMultiCurl *, const char *url, write_cb_fn, gpointer);
