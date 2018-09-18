/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *             Neil Jagdish Patel <njp@o-hand.com>
 *             Emmanuele Bassi <ebassi@openedhand.com>
 *
 * Copyright (C) 2006 OpenedHand
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#ifndef __CLUTTER_MODEL_H__
#define __CLUTTER_MODEL_H__

#include <clutter/clutter-types.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_MODEL              (clutter_model_get_type ())
#define CLUTTER_MODEL(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_MODEL, ClutterModel))
#define CLUTTER_MODEL_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_MODEL, ClutterModelClass))
#define CLUTTER_IS_MODEL(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_MODEL))
#define CLUTTER_IS_MODEL_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_MODEL))
#define CLUTTER_MODEL_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_MODEL, ClutterModelClass))

typedef struct _ClutterModel            ClutterModel;
typedef struct _ClutterModelClass       ClutterModelClass;
typedef struct _ClutterModelPrivate     ClutterModelPrivate;
typedef struct _ClutterModelIter        ClutterModelIter;
typedef struct _ClutterModelIterClass   ClutterModelIterClass;
typedef struct _ClutterModelIterPrivate ClutterModelIterPrivate;


/**
 * ClutterModelFilterFunc:
 * @model: a #ClutterModel
 * @iter: the iterator for the row
 * @user_data: data passed to clutter_model_set_filter()
 *
 * Filters the content of a row in the model.
 *
 * Return value: If the row should be displayed, return %TRUE
 *
 * Since: 0.6
 *
 * Deprecated: 1.24: Implement filters using a custom #GListModel instead
 */
typedef gboolean (*ClutterModelFilterFunc) (ClutterModel     *model,
                                            ClutterModelIter *iter,
                                            gpointer          user_data);

/**
 * ClutterModelSortFunc:
 * @model: a #ClutterModel
 * @a: a #GValue representing the contents of the row
 * @b: a #GValue representing the contents of the second row
 * @user_data: data passed to clutter_model_set_sort()
 *
 * Compares the content of two rows in the model.
 *
 * Return value: a positive integer if @a is after @b, a negative integer if
 *   @a is before @b, or 0 if the rows are the same
 *
 * Since: 0.6
 *
 * Deprecated: 1.24: Implement sorting using a custom #GListModel instead
 */
typedef gint (*ClutterModelSortFunc) (ClutterModel *model,
                                      const GValue *a,
                                      const GValue *b,
                                      gpointer      user_data);

/**
 * ClutterModelForeachFunc:
 * @model: a #ClutterModel
 * @iter: the iterator for the row
 * @user_data: data passed to clutter_model_foreach()
 *
 * Iterates on the content of a row in the model
 *
 * Return value: %TRUE if the iteration should continue, %FALSE otherwise
 *
 * Since: 0.6
 *
 * Deprecated: 1.24: Use #GListModel
 */
typedef gboolean (*ClutterModelForeachFunc) (ClutterModel     *model,
                                             ClutterModelIter *iter,
                                             gpointer          user_data);

/**
 * ClutterModel:
 *
 * Base class for list models. The #ClutterModel structure contains
 * only private data and should be manipulated using the provided
 * API.
 *
 * Since: 0.6
 *
 * Deprecated: 1.24: Use #GListModel instead
 */
struct _ClutterModel
{
  /*< private >*/
  GObject parent_instance;

  ClutterModelPrivate *priv;
};

/**
 * ClutterModelClass:
 * @row_added: signal class handler for ClutterModel::row-added
 * @row_removed: signal class handler for ClutterModel::row-removed
 * @row_changed: signal class handler for ClutterModel::row-changed
 * @sort_changed: signal class handler for ClutterModel::sort-changed
 * @filter_changed: signal class handler for ClutterModel::filter-changed
 * @get_column_name: virtual function for returning the name of a column
 * @get_column_type: virtual function for returning the type of a column
 * @get_iter_at_row: virtual function for returning an iterator for the
 *   given row
 * @get_n_rows: virtual function for returning the number of rows
 *   of the model
 * @get_n_columns: virtual function for retuning the number of columns
 *   of the model
 * @resort: virtual function for sorting the model using the passed
 *   sorting function
 * @insert_row: virtual function for inserting a row at the given index
 *   and returning an iterator pointing to it; if the index is a negative
 *   integer, the row should be appended to the model
 * @remove_row: virtual function for removing a row at the given index
 *
 * Class for #ClutterModel instances.
 *
 * Since: 0.6
 *
 * Deprecated: 1.24: Use #GListModel instead
 */
struct _ClutterModelClass 
{
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/
  /* vtable */
  guint             (* get_n_rows)      (ClutterModel         *model);
  guint             (* get_n_columns)   (ClutterModel         *model);
  const gchar *     (* get_column_name) (ClutterModel         *model,
                                         guint                 column);
  GType             (* get_column_type) (ClutterModel         *model,
                                         guint                 column);
  ClutterModelIter *(* insert_row)      (ClutterModel         *model,
                                         gint                  index_);
  void              (* remove_row)      (ClutterModel         *model,
                                         guint                 row);
  ClutterModelIter *(* get_iter_at_row) (ClutterModel         *model,
                                         guint                 row);
  void              (* resort)          (ClutterModel         *model,
                                         ClutterModelSortFunc  func,
                                         gpointer              data);

  /* signals */
  void              (* row_added)       (ClutterModel     *model,
                                         ClutterModelIter *iter);
  void              (* row_removed)     (ClutterModel     *model,
                                         ClutterModelIter *iter);
  void              (* row_changed)     (ClutterModel     *model,
                                         ClutterModelIter *iter);
  void              (* sort_changed)    (ClutterModel     *model);
  void              (* filter_changed)  (ClutterModel     *model);

  /*< private >*/
  /* padding for future expansion */
  void (*_clutter_model_1) (void);
  void (*_clutter_model_2) (void);
  void (*_clutter_model_3) (void);
  void (*_clutter_model_4) (void);
  void (*_clutter_model_5) (void);
  void (*_clutter_model_6) (void);
  void (*_clutter_model_7) (void);
  void (*_clutter_model_8) (void);
};

CLUTTER_DEPRECATED_FOR(g_list_model_get_type)
GType                 clutter_model_get_type           (void) G_GNUC_CONST;

CLUTTER_DEPRECATED_FOR(GListModel)
void                  clutter_model_set_types          (ClutterModel     *model,
                                                        guint             n_columns,
                                                        GType            *types);
CLUTTER_DEPRECATED_FOR(GListModel)
void                  clutter_model_set_names          (ClutterModel     *model,
                                                        guint             n_columns,
                                                        const gchar * const names[]);

CLUTTER_DEPRECATED_FOR(GListModel)
void                  clutter_model_append             (ClutterModel     *model,
                                                        ...);
CLUTTER_DEPRECATED_FOR(GListModel)
void                  clutter_model_appendv            (ClutterModel     *model,
                                                        guint             n_columns,
                                                        guint            *columns,
                                                        GValue           *values);
CLUTTER_DEPRECATED_FOR(GListModel)
void                  clutter_model_prepend            (ClutterModel     *model,
                                                        ...);
CLUTTER_DEPRECATED_FOR(GListModel)
void                  clutter_model_prependv           (ClutterModel     *model,
                                                        guint             n_columns,
                                                        guint            *columns,
                                                        GValue           *values);
CLUTTER_DEPRECATED_FOR(GListModel)
void                  clutter_model_insert             (ClutterModel     *model,
                                                        guint             row,
                                                        ...);
CLUTTER_DEPRECATED_FOR(GListModel)
void                  clutter_model_insertv            (ClutterModel     *model,
                                                        guint             row,
                                                        guint             n_columns,
                                                        guint            *columns,
                                                        GValue           *values);
CLUTTER_DEPRECATED_FOR(GListModel)
void                  clutter_model_insert_value       (ClutterModel     *model,
                                                        guint             row,
                                                        guint             column,
                                                        const GValue     *value);
CLUTTER_DEPRECATED_FOR(GListModel)
void                  clutter_model_remove             (ClutterModel     *model,
                                                        guint             row);

CLUTTER_DEPRECATED_FOR(GListModel)
guint                 clutter_model_get_n_rows         (ClutterModel     *model);
CLUTTER_DEPRECATED_FOR(GListModel)
guint                 clutter_model_get_n_columns      (ClutterModel     *model);
CLUTTER_DEPRECATED_FOR(GListModel)
const gchar *         clutter_model_get_column_name    (ClutterModel     *model,
                                                        guint             column);
CLUTTER_DEPRECATED_FOR(GListModel)
GType                 clutter_model_get_column_type    (ClutterModel     *model,
                                                        guint             column);

CLUTTER_DEPRECATED_FOR(GListModel)
ClutterModelIter *    clutter_model_get_first_iter     (ClutterModel     *model);
CLUTTER_DEPRECATED_FOR(GListModel)
ClutterModelIter *    clutter_model_get_last_iter      (ClutterModel     *model);
CLUTTER_DEPRECATED_FOR(GListModel)
ClutterModelIter *    clutter_model_get_iter_at_row    (ClutterModel     *model,
                                                        guint             row);

CLUTTER_DEPRECATED_FOR(GListModel)
void                  clutter_model_set_sorting_column (ClutterModel     *model,
                                                        gint              column);
CLUTTER_DEPRECATED_FOR(GListModel)
gint                  clutter_model_get_sorting_column (ClutterModel     *model);

CLUTTER_DEPRECATED_FOR(GListModel)
void                  clutter_model_foreach            (ClutterModel     *model,
                                                        ClutterModelForeachFunc func, 
                                                        gpointer          user_data);
CLUTTER_DEPRECATED_FOR(GListModel)
void                  clutter_model_set_sort           (ClutterModel     *model, 
                                                        gint              column,
                                                        ClutterModelSortFunc func, 
                                                        gpointer          user_data,
                                                        GDestroyNotify    notify);
CLUTTER_DEPRECATED_FOR(GListModel)
void                  clutter_model_set_filter         (ClutterModel     *model, 
                                                        ClutterModelFilterFunc func, 
                                                        gpointer          user_data,
                                                        GDestroyNotify    notify);
CLUTTER_DEPRECATED_FOR(GListModel)
gboolean              clutter_model_get_filter_set     (ClutterModel     *model);

CLUTTER_DEPRECATED_FOR(GListModel)
void                  clutter_model_resort             (ClutterModel     *model);
CLUTTER_DEPRECATED_FOR(GListModel)
gboolean              clutter_model_filter_row         (ClutterModel     *model,
                                                        guint             row);
CLUTTER_DEPRECATED_FOR(GListModel)
gboolean              clutter_model_filter_iter        (ClutterModel     *model,
                                                        ClutterModelIter *iter);

/*
 * ClutterModelIter 
 */

#define CLUTTER_TYPE_MODEL_ITER                 (clutter_model_iter_get_type ())
#define CLUTTER_MODEL_ITER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_MODEL_ITER, ClutterModelIter))
#define CLUTTER_MODEL_ITER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_MODEL_ITER, ClutterModelIterClass))
#define CLUTTER_IS_MODEL_ITER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_MODEL_ITER))
#define CLUTTER_IS_MODEL_ITER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_MODEL_ITER))
#define CLUTTER_MODEL_ITER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_MODEL_ITER, ClutterModelIterClass))

/**
 * ClutterModelIter:
 *
 * Base class for list models iters. The #ClutterModelIter structure
 * contains only private data and should be manipulated using the
 * provided API.
 *
 * Since: 0.6
 *
 * Deprecated: 1.24: Use custom iterators for #GListModel
 */
struct _ClutterModelIter
{
  /*< private >*/
  GObject parent_instance;

  ClutterModelIterPrivate *priv;
};

/**
 * ClutterModelIterClass:
 * @get_value: Virtual function for retrieving the value at the given
 *   column of the row pointed by the iterator
 * @set_value: Virtual function for setting the value at the given
 *   column of the row pointer by the iterator
 * @is_last: Virtual function for knowing whether the iterator points
 *   at the last row in the model
 * @is_first: Virtual function for knowing whether the iterator points
 *   at the first row in the model
 * @next: Virtual function for moving the iterator to the following
 *   row in the model
 * @prev: Virtual function for moving the iterator toe the previous
 *   row in the model
 * @get_model: Virtual function for getting the model to which the
 *   iterator belongs to
 * @get_row: Virtual function for getting the row to which the iterator
 *   points
 * @copy: Virtual function for copying a #ClutterModelIter.
 *
 * Class for #ClutterModelIter instances.
 *
 * Since: 0.6
 *
 * Deprecated: 1.24: Use custom iterators for #GListModel
 */
struct _ClutterModelIterClass 
{
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/
  /* vtable not signals */
  void              (* get_value) (ClutterModelIter *iter, 
                                   guint             column, 
                                   GValue           *value);
  void              (* set_value) (ClutterModelIter *iter, 
                                   guint             column, 
                                   const GValue     *value);

  gboolean          (* is_first)  (ClutterModelIter *iter);
  gboolean          (* is_last)   (ClutterModelIter *iter);

  ClutterModelIter *(* next)      (ClutterModelIter *iter);
  ClutterModelIter *(* prev)      (ClutterModelIter *iter);

  ClutterModel *    (* get_model) (ClutterModelIter *iter);
  guint             (* get_row)   (ClutterModelIter *iter);

  ClutterModelIter *(* copy)      (ClutterModelIter *iter);

  /*< private >*/
  /* padding for future expansion */
  void (*_clutter_model_iter_1) (void);
  void (*_clutter_model_iter_2) (void);
  void (*_clutter_model_iter_3) (void);
  void (*_clutter_model_iter_4) (void);
  void (*_clutter_model_iter_5) (void);
  void (*_clutter_model_iter_6) (void);
  void (*_clutter_model_iter_7) (void);
  void (*_clutter_model_iter_8) (void);
};

CLUTTER_DEPRECATED
GType             clutter_model_iter_get_type   (void) G_GNUC_CONST;

CLUTTER_DEPRECATED
void              clutter_model_iter_get        (ClutterModelIter *iter,
                                                 ...);
CLUTTER_DEPRECATED
void              clutter_model_iter_get_valist (ClutterModelIter *iter,
                                                 va_list          args);
CLUTTER_DEPRECATED
void              clutter_model_iter_get_value  (ClutterModelIter *iter,
                                                 guint             column,
                                                 GValue           *value);
CLUTTER_DEPRECATED
void              clutter_model_iter_set        (ClutterModelIter *iter,
                                                 ...);
CLUTTER_DEPRECATED
void              clutter_model_iter_set_valist (ClutterModelIter *iter,
                                                 va_list          args);
CLUTTER_DEPRECATED
void              clutter_model_iter_set_value  (ClutterModelIter *iter,
                                                 guint             column,
                                                 const GValue     *value);

CLUTTER_DEPRECATED
gboolean          clutter_model_iter_is_first   (ClutterModelIter *iter);
CLUTTER_DEPRECATED
gboolean          clutter_model_iter_is_last    (ClutterModelIter *iter);
CLUTTER_DEPRECATED
ClutterModelIter *clutter_model_iter_next       (ClutterModelIter *iter);
CLUTTER_DEPRECATED
ClutterModelIter *clutter_model_iter_prev       (ClutterModelIter *iter);

CLUTTER_DEPRECATED
ClutterModel *    clutter_model_iter_get_model  (ClutterModelIter *iter);
CLUTTER_DEPRECATED
guint             clutter_model_iter_get_row    (ClutterModelIter *iter);

CLUTTER_DEPRECATED
ClutterModelIter *clutter_model_iter_copy       (ClutterModelIter *iter);

G_END_DECLS

#endif /* __CLUTTER_MODEL_H__ */
