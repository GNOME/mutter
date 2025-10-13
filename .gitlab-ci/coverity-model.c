/* This is the source code of our Coverity model. Meant to enhance
 * the accuracy of reports, improve understanding of the existing
 * code, or avoid false positives.
 *
 * This is not a real C file, and can not use #include directives.
 * These are basically hints about the function behavior, in a way
 * that Coverity can understand.
 *
 * The model file must be uploaded manually by someone with
 * access permission to the Mutter console at
 * https://scan.coverity.com/projects/mutter.
 */

#define NULL ((void *) 0)
#define FALSE 0
#define TRUE !FALSE

typedef unsigned int gboolean;
typedef void* gpointer;
typedef struct _GObject { int ref_count; } GObject;
typedef struct _GSource { int ref_count; } GSource;
typedef struct _GMainContext { int ref_count; } GMainContext;
typedef struct _GFile { GObject parent } GFile;
typedef struct _GCancellable { GObject parent } GCancellable;
typedef struct _GAsyncResult { GObject parent } GAsyncResult;

typedef void (*GAsyncReadyCallback) (GObject      *object,
                                     GAsyncResult *res,
                                     gpointer      user_data);

GSource *
g_source_ref (GSource *source)
{
  source->ref_count++;
}

void
g_source_attach (GSource      *source,
                 GMainContext *context)
{
  source->ref_count++;
}

void
g_source_unref (GSource *source)
{
  source->ref_count--;

  if (source->ref_count <= 0)
    __coverity_free__ (source);
}

GObject *
g_object_ref (GObject *object)
{
  object->ref_count++;
}

void
g_object_unref (GObject *object)
{
  object->ref_count--;

  if (object->ref_count <= 0)
    __coverity_free__ (object);
}

void
g_file_replace_contents_async (GFile               *file,
			       const char          *contents,
			       size_t               len,
			       const char          *etag,
			       gboolean             make_backup,
			       unsigned int         flags,
			       GCancellable        *cancellable,
			       GAsyncReadyCallback  cb,
			       gpointer             user_data)
{
  __coverity_escape__ (user_data);
}
