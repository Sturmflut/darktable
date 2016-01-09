/*
    This file is part of darktable,
    copyright (c) 2011 Henrik Andersson.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "common/darktable.h"
#include "common/debug.h"
#include "common/styles.h"
#include "control/conf.h"
#include "control/control.h"
#include "develop/develop.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"
#include "gui/styles.h"
#include "libs/lib.h"
#include "libs/lib_api.h"
#include "views/undo.h"

DT_MODULE(1)


typedef struct dt_lib_history_t
{
  /* vbox with managed history items */
  GtkWidget *history_box;
  GtkWidget *create_button;
//   GtkWidget *apply_button;
  GtkWidget *compress_button;
  gboolean record_undo;
  dt_undo_history_t prev;
} dt_lib_history_t;

/* compress history stack */
static void _lib_history_compress_clicked_callback(GtkWidget *widget, gpointer user_data);
static void _lib_history_button_clicked_callback(GtkWidget *widget, gpointer user_data);
static void _lib_history_create_style_button_clicked_callback(GtkWidget *widget, gpointer user_data);
/* signal callback for history change */
static void _lib_history_change_callback(gpointer instance, gpointer user_data);



const char *name(dt_lib_module_t *self)
{
  return _("history");
}

uint32_t views(dt_lib_module_t *self)
{
  return DT_VIEW_DARKROOM;
}

uint32_t container(dt_lib_module_t *self)
{
  return DT_UI_CONTAINER_PANEL_LEFT_CENTER;
}

int position()
{
  return 900;
}

void init_key_accels(dt_lib_module_t *self)
{
  dt_accel_register_lib(self, NC_("accel", "create style from history"), 0, 0);
//   dt_accel_register_lib(self, NC_("accel", "apply style from popup menu"), 0, 0);
  dt_accel_register_lib(self, NC_("accel", "compress history stack"), 0, 0);
}

void connect_key_accels(dt_lib_module_t *self)
{
  dt_lib_history_t *d = (dt_lib_history_t *)self->data;

  dt_accel_connect_button_lib(self, "create style from history", d->create_button);
//   dt_accel_connect_button_lib(self, "apply style from popup menu", d->apply_button);
  dt_accel_connect_button_lib(self, "compress history stack", d->compress_button);
}

void gui_init(dt_lib_module_t *self)
{
  /* initialize ui widgets */
  dt_lib_history_t *d = (dt_lib_history_t *)g_malloc0(sizeof(dt_lib_history_t));
  self->data = (void *)d;

  d->record_undo = TRUE;
  d->prev.snapshot = NULL;

  self->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, DT_PIXEL_APPLY_DPI(5));
  gtk_widget_set_name(self->widget, "history-ui");
  d->history_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  GtkWidget *hhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, DT_PIXEL_APPLY_DPI(5));

  d->compress_button = dtgtk_button_new(NULL, /*CPF_DO_NOT_USE_BORDER | CPF_STYLE_FLAT*/0);
  gtk_button_set_label(GTK_BUTTON(d->compress_button), _("compress history stack"));
  gtk_widget_set_tooltip_text(d->compress_button, _("create a minimal history stack which produces the same image"));
  g_signal_connect(G_OBJECT(d->compress_button), "clicked", G_CALLBACK(_lib_history_compress_clicked_callback), NULL);

  /* add toolbar button for creating style */
  d->create_button = dtgtk_button_new(dtgtk_cairo_paint_styles, CPF_DO_NOT_USE_BORDER);
  gtk_widget_set_size_request(d->create_button, DT_PIXEL_APPLY_DPI(24), -1);
  g_signal_connect(G_OBJECT(d->create_button), "clicked",
                   G_CALLBACK(_lib_history_create_style_button_clicked_callback), NULL);
  gtk_widget_set_tooltip_text(d->create_button, _("create a style from the current history stack"));

  /* add buttons to buttonbox */
  gtk_box_pack_start(GTK_BOX(hhbox), d->compress_button, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(hhbox), d->create_button, FALSE, FALSE, 0);

  /* add history list and buttonbox to widget */
  gtk_box_pack_start(GTK_BOX(self->widget), d->history_box, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), hhbox, FALSE, FALSE, 0);


  gtk_widget_show_all(self->widget);

  /* connect to history change signal for updating the history view */
  dt_control_signal_connect(darktable.signals, DT_SIGNAL_DEVELOP_HISTORY_CHANGE,
                            G_CALLBACK(_lib_history_change_callback), self);
}

void gui_cleanup(dt_lib_module_t *self)
{
  dt_control_signal_disconnect(darktable.signals, G_CALLBACK(_lib_history_change_callback), self);

  g_free(self->data);
  self->data = NULL;
}

static GtkWidget *_lib_history_create_button(dt_lib_module_t *self, int num, const char *label,
                                             gboolean enabled, gboolean selected)
{
  /* create label */
  GtkWidget *widget = NULL;
  gchar numlabel[256];
  if(num == -1)
    g_snprintf(numlabel, sizeof(numlabel), "%d - %s", num + 1, label);
  else
  {
    if(enabled)
      g_snprintf(numlabel, sizeof(numlabel), "%d - %s", num + 1, label);
    else
      g_snprintf(numlabel, sizeof(numlabel), "%d - %s (%s)", num + 1, label, _("off"));
  }

  /* create toggle button */
  widget = gtk_toggle_button_new_with_label(numlabel);
  gtk_widget_set_halign(gtk_bin_get_child(GTK_BIN(widget)), GTK_ALIGN_START);
  g_object_set_data(G_OBJECT(widget), "history_number", GINT_TO_POINTER(num + 1));
  g_object_set_data(G_OBJECT(widget), "label", (gpointer)label);
  if(selected) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), TRUE);

  /* set callback when clicked */
  g_signal_connect(G_OBJECT(widget), "clicked", G_CALLBACK(_lib_history_button_clicked_callback), self);

  /* associate the history number */
  g_object_set_data(G_OBJECT(widget), "history-number", GINT_TO_POINTER(num + 1));

  return widget;
}

static GList *_duplicate_history(GList *hist)
{
  GList *result = NULL;

  GList *h = g_list_first(hist);
  while(h)
  {
    const dt_dev_history_item_t *old = (dt_dev_history_item_t *)(h->data);

    dt_dev_history_item_t *new = (dt_dev_history_item_t *)malloc(sizeof(dt_dev_history_item_t));

    memcpy(new, old, sizeof(dt_dev_history_item_t));

    new->params = malloc(old->module->params_size);
    new->blend_params = malloc(sizeof(dt_develop_blend_params_t));

    memcpy(new->params, old->params, old->module->params_size);
    memcpy(new->blend_params, old->blend_params, sizeof(dt_develop_blend_params_t));

    result = g_list_append(result, new);

    h = g_list_next(h);
  }
  return result;
}

static void pop_undo(gpointer user_data, dt_undo_type_t type, dt_undo_data_t *data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;

  if(type == DT_UNDO_HISTORY)
  {
    dt_lib_history_t *d = (dt_lib_history_t *)self->data;
    dt_undo_history_t *hist = (dt_undo_history_t *)data;

    GList *current_params = darktable.develop->history;
    const int end = darktable.develop->history_end;

    darktable.develop->history = hist->snapshot;
    darktable.develop->history_end = hist->end;

    hist->snapshot = current_params;
    hist->end = end;

    // disable recording undo as the _lib_history_change_callback will be triggered by the calls below
    d->record_undo = FALSE;

    dt_dev_write_history(darktable.develop);
    dt_dev_reload_history_items(darktable.develop);
  }
}

static void _history_undo_data_free(gpointer data)
{
  dt_undo_history_t *hist = (dt_undo_history_t *)data;
  GList *snapshot = hist->snapshot;
  g_list_free_full(snapshot, dt_dev_free_history_item);
  free(data);
}

static void _lib_history_change_callback(gpointer instance, gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  dt_lib_history_t *d = (dt_lib_history_t *)self->data;

  /* first destroy all buttons in list */
  gtk_container_foreach(GTK_CONTAINER(d->history_box), (GtkCallback)gtk_widget_destroy, 0);

  /* add default which always should be */
  int num = -1;
  gtk_box_pack_start(GTK_BOX(d->history_box),
                     _lib_history_create_button(self, num, _("original"), FALSE, darktable.develop->history_end == 0),
                     TRUE, TRUE, 0);
  num++;

  if (d->record_undo == TRUE)
  {
    /* record undo/redo history snapshot */
    if (d->prev.snapshot != NULL)
    {
      dt_undo_history_t *hist = malloc(sizeof(dt_undo_history_t));
      hist->snapshot=d->prev.snapshot;
      hist->end=d->prev.end;
      dt_undo_record(darktable.undo, self, DT_UNDO_HISTORY, (dt_undo_data_t *)hist, &pop_undo, _history_undo_data_free);
    }

    // record current history for next iteration, we duplicate the whole history
    d->prev.snapshot = _duplicate_history(darktable.develop->history);
    d->prev.end = darktable.develop->history_end;
  }
  else
    d->record_undo = TRUE;

  /* lock history mutex */
  dt_pthread_mutex_lock(&darktable.develop->history_mutex);

  /* iterate over history items and add them to list*/
  GList *history = g_list_first(darktable.develop->history);
  while(history)
  {
    dt_dev_history_item_t *hitem = (dt_dev_history_item_t *)(history->data);

    gchar *label;
    if(!hitem->multi_name[0] || strcmp(hitem->multi_name, "0") == 0)
      label = g_strdup_printf("%s", hitem->module->name());
    else
      label = g_strdup_printf("%s %s", hitem->module->name(), hitem->multi_name);

    gboolean selected = (num == darktable.develop->history_end - 1);
    GtkWidget *widget = _lib_history_create_button(self, num, label, hitem->enabled, selected);
    g_free(label);

    gtk_box_pack_start(GTK_BOX(d->history_box), widget, TRUE, TRUE, 0);
    gtk_box_reorder_child(GTK_BOX(d->history_box), widget, 0);
    num++;

    history = g_list_next(history);
  }

  /* show all widgets */
  gtk_widget_show_all(d->history_box);

  dt_pthread_mutex_unlock(&darktable.develop->history_mutex);
}

static void _lib_history_compress_clicked_callback(GtkWidget *widget, gpointer user_data)
{
  const int imgid = darktable.develop->image_storage.id;
  if(!imgid) return;
  // make sure the right history is in there:
  dt_dev_write_history(darktable.develop);
  sqlite3_stmt *stmt;

  // compress history
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "DELETE FROM main.history WHERE imgid = ?1 AND num "
                                                             "NOT IN (SELECT MAX(num) FROM main.history WHERE "
                                                             "imgid = ?1 AND num < ?2 GROUP BY operation, "
                                                             "multi_priority)", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, darktable.develop->history_end);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  // load new history and write it back to ensure that all history are properly numbered without a gap
  dt_dev_reload_history_items(darktable.develop);
  dt_dev_write_history(darktable.develop);

  // then we can get the item to select in the new clean-up history retrieve the position of the module
  // corresponding to the history end.
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "SELECT IFNULL(MAX(num)+1, 0) FROM main.history "
                                                             "WHERE imgid=?1", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);

  if (sqlite3_step(stmt) == SQLITE_ROW)
    darktable.develop->history_end = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);

  // select the new history end corresponding to the one before the history compression
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "UPDATE main.images SET history_end=?2 WHERE id=?1",
                              -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, darktable.develop->history_end);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  dt_dev_reload_history_items(darktable.develop);
  dt_dev_modulegroups_set(darktable.develop, dt_dev_modulegroups_get(darktable.develop));
}

static void _lib_history_button_clicked_callback(GtkWidget *widget, gpointer user_data)
{
  static int reset = 0;
  if(reset) return;
  if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) return;

  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  dt_lib_history_t *d = (dt_lib_history_t *)self->data;
  reset = 1;

  /* inactivate all toggle buttons */
  GList *children = gtk_container_get_children(GTK_CONTAINER(d->history_box));
  for(GList *l = children; l != NULL; l = g_list_next(l))
  {
    GtkToggleButton *b = GTK_TOGGLE_BUTTON(l->data);
    if(b != GTK_TOGGLE_BUTTON(widget)) g_object_set(G_OBJECT(b), "active", FALSE, (gchar *)0);
  }
  g_list_free(children);

  reset = 0;
  if(darktable.gui->reset) return;

  /* revert to given history item. */
  int num = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "history-number"));
  dt_dev_pop_history_items(darktable.develop, num);
  dt_dev_modulegroups_set(darktable.develop, dt_dev_modulegroups_get(darktable.develop));
}

static void _lib_history_create_style_button_clicked_callback(GtkWidget *widget, gpointer user_data)
{
  if(darktable.develop->image_storage.id)
  {
    dt_dev_write_history(darktable.develop);
    dt_gui_styles_dialog_new(darktable.develop->image_storage.id);
  }
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
