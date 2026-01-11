#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <glib.h>

static cairo_surface_t *surface = NULL;
static GtkWidget *window = NULL;
static GtkWidget *scrolled_window = NULL;
static GtkWidget *drawing_area = NULL;

static GList *undo_stack = NULL;
static GList *redo_stack = NULL;

static int canvas_width = 800;
static int canvas_height = 600;

typedef enum {
    TOOL_PEN,
    TOOL_RECT,
    TOOL_ELLIPSE,
    TOOL_ARROW,
    TOOL_REDACT
} ToolType;

static ToolType current_tool = TOOL_PEN;
static GdkRGBA current_color = {0, 0, 0, 1}; 
static double current_size = 3.0;
static double zoom_level = 1.0;
static gboolean is_modified = FALSE;

static double last_x = 0;
static double last_y = 0;
static gboolean is_drawing = FALSE;

static double start_x = 0;
static double start_y = 0;
static double end_x = 0;
static double end_y = 0;

/* Forward declarations */
static void clear_surface(void);
static void load_image_to_surface(const char *filename);
static void on_tool_clicked(GtkToolButton *btn, gpointer data);
static void on_color_set(GtkColorButton *widget, gpointer data);
static void on_size_changed(GtkSpinButton *spin, gpointer data);
static void free_stack(GList **stack);
static void update_drawing_area_size(void);
static void apply_redact(cairo_surface_t *surf, double sx, double sy, double ex, double ey);
static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data);
static gboolean perform_save(void);
static void on_quit_menu(GtkWidget *w, gpointer data);

#define CLAMP_VAL(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))

void show_error(GtkWindow *parent, const char *message) {
    GtkWidget *dialog;
    dialog = gtk_message_dialog_new(parent,
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_ERROR,
                                    GTK_BUTTONS_CLOSE,
                                    "%s", message);
    
    gtk_window_set_title(GTK_WINDOW(dialog), "Error");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void update_drawing_area_size(void) {
    if (drawing_area) {
        int w = canvas_width * zoom_level;
        int h = canvas_height * zoom_level;
        gtk_widget_set_size_request(drawing_area, w, h);
        gtk_widget_queue_draw(drawing_area);
    }
}

cairo_surface_t* copy_surface(cairo_surface_t *src) {
    if (!src) return NULL;
    cairo_surface_t *dest = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, canvas_width, canvas_height);
    cairo_t *cr = cairo_create(dest);
    cairo_set_source_surface(cr, src, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    return dest;
}

static void push_undo() {
    if (!surface) return;
    free_stack(&redo_stack);
    cairo_surface_t *copy = copy_surface(surface);
    undo_stack = g_list_prepend(undo_stack, copy);
}

static void on_undo(GtkWidget *w, gpointer data) {
    if (!undo_stack) return;

    cairo_surface_t *current_copy = copy_surface(surface);
    redo_stack = g_list_prepend(redo_stack, current_copy);

    cairo_surface_t *prev = (cairo_surface_t *)undo_stack->data;
    undo_stack = g_list_delete_link(undo_stack, undo_stack);

    if (surface) cairo_surface_destroy(surface);
    surface = prev;

    is_modified = TRUE;
    gtk_widget_queue_draw(drawing_area);
}

static void on_redo(GtkWidget *w, gpointer data) {
    if (!redo_stack) return;

    cairo_surface_t *current_copy = copy_surface(surface);
    undo_stack = g_list_prepend(undo_stack, current_copy);

    cairo_surface_t *next = (cairo_surface_t *)redo_stack->data;
    redo_stack = g_list_delete_link(redo_stack, redo_stack);

    if (surface) cairo_surface_destroy(surface);
    surface = next;

    is_modified = TRUE;
    gtk_widget_queue_draw(drawing_area);
}

static void on_zoom_in(GtkWidget *w, gpointer data) {
    zoom_level *= 1.2;
    update_drawing_area_size();
}

static void on_zoom_out(GtkWidget *w, gpointer data) {
    zoom_level /= 1.2;
    update_drawing_area_size();
}

static void free_stack(GList **stack) {
    if (!*stack) return;
    GList *l;
    for (l = *stack; l != NULL; l = l->next) {
        cairo_surface_destroy((cairo_surface_t *)l->data);
    }
    g_list_free(*stack);
    *stack = NULL;
}

static void apply_redact(cairo_surface_t *surf, double sx, double sy, double ex, double ey) {
    cairo_surface_flush(surf);
    
    int x1 = (int)fmin(sx, ex);
    int y1 = (int)fmin(sy, ey);
    int x2 = (int)fmax(sx, ex);
    int y2 = (int)fmax(sy, ey);

    int w = cairo_image_surface_get_width(surf);
    int h = cairo_image_surface_get_height(surf);
    int stride = cairo_image_surface_get_stride(surf);
    unsigned char *data = cairo_image_surface_get_data(surf);

    if (x1 < 0) x1 = 0; if (y1 < 0) y1 = 0;
    if (x2 > w) x2 = w; if (y2 > h) y2 = h;

    for (int y = y1; y < y2; y++) {
        for (int x = x1; x < x2; x++) {
            int offset_x = (rand() % 50) - 5;
            int offset_y = (rand() % 50) - 5;
            
            int src_x = CLAMP_VAL(x + offset_x, 0, w - 1);
            int src_y = CLAMP_VAL(y + offset_y, 0, h - 1);

            uint32_t *src_p = (uint32_t*)(data + src_y * stride + src_x * 4);
            uint32_t pixel = *src_p;

            int r = (pixel >> 16) & 0xFF;
            int g = (pixel >> 8) & 0xFF;
            int b = (pixel) & 0xFF;

            r += (rand() % 40) - 20; r = CLAMP_VAL(r, 0, 255);
            g += (rand() % 40) - 20; g = CLAMP_VAL(g, 0, 255);
            b += (rand() % 40) - 20; b = CLAMP_VAL(b, 0, 255);

            uint32_t *dst_p = (uint32_t*)(data + y * stride + x * 4);
            *dst_p = (0xFF000000) | (r << 16) | (g << 8) | b; 
        }
    }
    cairo_surface_mark_dirty(surf);
}

void draw_shape(cairo_t *cr, ToolType tool, double x1, double y1, double x2, double y2) {
    if (tool == TOOL_REDACT) {
        double rx = fmin(x1, x2);
        double ry = fmin(y1, y2);
        double rw = fabs(x2 - x1);
        double rh = fabs(y2 - y1);

        if (rw > 1 && rh > 1) {
            cairo_surface_t *preview = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)rw, (int)rh);
            cairo_t *pcr = cairo_create(preview);

            cairo_set_source_surface(pcr, surface, -rx, -ry);
            cairo_paint(pcr);
            cairo_destroy(pcr);

            apply_redact(preview, 0, 0, rw, rh);

            cairo_set_source_surface(cr, preview, rx, ry);
            cairo_rectangle(cr, rx, ry, rw, rh);
            cairo_fill(cr);

            cairo_surface_destroy(preview);

            cairo_set_source_rgba(cr, 1, 0, 0, 0.5);
            cairo_set_line_width(cr, 2.0);

            const double dashes[] = {4.0, 1.0};
            int num_dashes = 2;
            double offset = 0;

            // cairo_set_dash(cr, dashes, num_dashes, offset);
            // cairo_rectangle(cr, rx, ry, rw, rh);
            // cairo_stroke(cr);
        }
        return;
    }

    cairo_set_source_rgba(cr, current_color.red, current_color.green, current_color.blue, current_color.alpha);
    cairo_set_line_width(cr, current_size);

    if (tool == TOOL_RECT) {
        cairo_rectangle(cr, x1, y1, x2 - x1, y2 - y1);
        cairo_stroke(cr);
    } 
    else if (tool == TOOL_ELLIPSE) {
        cairo_save(cr);
        cairo_translate(cr, x1 + (x2 - x1) / 2.0, y1 + (y2 - y1) / 2.0);
        cairo_scale(cr, (x2 - x1) / 2.0, (y2 - y1) / 2.0);
        cairo_arc(cr, 0, 0, 1.0, 0, 2 * M_PI);
        cairo_restore(cr);
        cairo_stroke(cr);
    }
    else if (tool == TOOL_ARROW) {
        double angle = atan2(y2 - y1, x2 - x1);
        double arrow_len = 15.0 + current_size; 
        double arrow_angle = M_PI / 6.0;

        cairo_move_to(cr, x1, y1);
        cairo_line_to(cr, x2, y2);
        cairo_stroke(cr);

        cairo_move_to(cr, x2, y2);
        cairo_line_to(cr, x2 - arrow_len * cos(angle - arrow_angle),
                          y2 - arrow_len * sin(angle - arrow_angle));
        cairo_move_to(cr, x2, y2);
        cairo_line_to(cr, x2 - arrow_len * cos(angle + arrow_angle),
                          y2 - arrow_len * sin(angle + arrow_angle));
        cairo_stroke(cr);
    }
}

static gboolean configure_event_cb(GtkWidget *widget, GdkEventConfigure *event, gpointer data) {
    if (!surface) {
        surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, canvas_width, canvas_height);
        clear_surface();
    }
    return TRUE;
}

static void clear_surface(void) {
    if (!surface) return;
    cairo_t *cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_destroy(cr);
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    if (!surface) return FALSE;
    
    cairo_save(cr);
    cairo_scale(cr, zoom_level, zoom_level);
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_paint(cr);

    if (is_drawing && current_tool != TOOL_PEN) {
        draw_shape(cr, current_tool, start_x, start_y, end_x, end_y);
    }
    cairo_restore(cr);
    return FALSE;
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->button == GDK_BUTTON_PRIMARY && surface) {
        push_undo();

        is_drawing = TRUE;
        double wx = event->x / zoom_level;
        double wy = event->y / zoom_level;

        start_x = wx;
        start_y = wy;
        last_x = wx;
        last_y = wy;
        end_x = wx;
        end_y = wy;
    }
    return TRUE;
}

static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    if (!is_drawing || !surface) return TRUE;

    double wx = event->x / zoom_level;
    double wy = event->y / zoom_level;

    if (current_tool == TOOL_PEN) {
        cairo_t *cr = cairo_create(surface);
        cairo_set_source_rgba(cr, current_color.red, current_color.green, current_color.blue, current_color.alpha);
        cairo_set_line_width(cr, current_size);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        
        cairo_move_to(cr, last_x, last_y);
        cairo_line_to(cr, wx, wy);
        cairo_stroke(cr);
        cairo_destroy(cr);

        last_x = wx;
        last_y = wy;
        gtk_widget_queue_draw(widget);
    } 
    else {
        end_x = wx;
        end_y = wy;
        gtk_widget_queue_draw(widget);
    }
    return TRUE;
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->button == GDK_BUTTON_PRIMARY && is_drawing) {
        is_drawing = FALSE;
        is_modified = TRUE;
        end_x = event->x / zoom_level;
        end_y = event->y / zoom_level;

        if (current_tool == TOOL_REDACT) {
            int redact_iterations = 10;
            for (int i=0;i<redact_iterations;i++)
                apply_redact(surface, start_x, start_y, end_x, end_y);
            gtk_widget_queue_draw(widget);
        }
        else if (current_tool != TOOL_PEN) {
            cairo_t *cr = cairo_create(surface);
            draw_shape(cr, current_tool, start_x, start_y, end_x, end_y);
            cairo_destroy(cr);
            gtk_widget_queue_draw(widget);
        }
    }
    return TRUE;
}

static gboolean on_scroll_event(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
    if (event->state & GDK_CONTROL_MASK) {
        if (event->direction == GDK_SCROLL_UP) {
            zoom_level *= 1.1;
        } else if (event->direction == GDK_SCROLL_DOWN) {
            zoom_level /= 1.1;
        }
        update_drawing_area_size();
        return TRUE;
    }
    return FALSE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    if (event->keyval == GDK_KEY_Escape && is_drawing) {
        if (undo_stack) {
            cairo_surface_t *saved = (cairo_surface_t *)undo_stack->data;
            if (surface) cairo_surface_destroy(surface);
            surface = copy_surface(saved);
            
            cairo_surface_destroy(saved);
            undo_stack = g_list_delete_link(undo_stack, undo_stack);
        }
        
        is_drawing = FALSE;
        gtk_widget_queue_draw(drawing_area);
        return TRUE;
    }
    return FALSE;
}

static void on_new_file(GtkWidget *w, gpointer data) {
    free_stack(&undo_stack);
    free_stack(&redo_stack);
    
    canvas_width = 800;
    canvas_height = 600;
    
    if (surface) cairo_surface_destroy(surface);
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, canvas_width, canvas_height);
    clear_surface();
    
    is_modified = FALSE;
    zoom_level = 1.0;
    update_drawing_area_size();
}


static gchar *make_timestamped_name ()
{
    time_t    now = time (NULL);
    struct tm tm;
    localtime_r (&now, &tm);
    return g_strdup_printf ("annotation-%02d-%02d-%04d_%02d-%02d.png",
                            tm.tm_mday,
                            tm.tm_mon + 1,
                            tm.tm_year + 1900,
                            tm.tm_hour,
                            tm.tm_min);
}

static gboolean perform_save ()
{
    GtkWidget *dialog = gtk_file_chooser_dialog_new ("Save Drawing",
                                          GTK_WINDOW (window),
                                          GTK_FILE_CHOOSER_ACTION_SAVE,
                                          "_Cancel", GTK_RESPONSE_CANCEL,
                                          "_Save",   GTK_RESPONSE_ACCEPT,
                                          NULL);

    gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);

    gchar *default_name = make_timestamped_name ();
    gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), default_name);
    g_free (default_name);

    gboolean saved = FALSE;
    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
        cairo_surface_write_to_png (surface, filename);
        g_free (filename);
        is_modified = FALSE;
        saved = TRUE;
    }

    gtk_widget_destroy (dialog);
    return saved;
}
static void on_save_file(GtkWidget *w, gpointer data) {
    perform_save();
}

static gboolean on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
    if (!is_modified) return FALSE; /* Allow close if not modified */

    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_NONE,
                                               "You have unsaved changes. Do you want to save before closing?");
    
    gtk_dialog_add_button(GTK_DIALOG(dialog), "Close without Saving", GTK_RESPONSE_NO);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "_Save", GTK_RESPONSE_YES);

    int result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (result == GTK_RESPONSE_YES) {
        if (perform_save()) {
            return FALSE; /* Close */
        } else {
            return TRUE; /* Cancel close if save failed or got cancelled */
        }
    } else if (result == GTK_RESPONSE_NO) {
        return FALSE; /* Close */
    }
    
    return TRUE; /* Cancel close */
}

static void on_quit_menu(GtkWidget *w, gpointer data) {
    gtk_window_close(GTK_WINDOW(window));
}

static void on_open_file(GtkWidget *w, gpointer data) {
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Open Image",
                                                    GTK_WINDOW(window),
                                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Open", GTK_RESPONSE_ACCEPT,
                                                    NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        free_stack(&undo_stack);
        free_stack(&redo_stack);
        load_image_to_surface(filename);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void on_about(GtkWidget *w, gpointer data) {
    gtk_show_about_dialog(GTK_WINDOW(window),
                          "program-name", "Crayons",
                          "version", "1.1",
                          "comments", "Easy to use Image Annotator tool written in GTK\n(c) theonlyasdk 2025-26",
                          NULL);
}

static void load_image_to_surface(const char *filename) {
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filename, &error);
    
    if (!pixbuf) {
        char err_str[256];
        snprintf(err_str, sizeof(err_str), "Error loading file: %s\n", filename);
        show_error(GTK_WINDOW(window), err_str);
        g_printerr("%s", err_str);
        g_error_free(error);
        return;
    }

    canvas_width = gdk_pixbuf_get_width(pixbuf);
    canvas_height = gdk_pixbuf_get_height(pixbuf);

    if (surface) cairo_surface_destroy(surface);
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, canvas_width, canvas_height);
                                                    
    cairo_t *cr = cairo_create(surface);
    gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    
    g_object_unref(pixbuf);
    
    is_modified = FALSE;
    zoom_level = 1.0;
    update_drawing_area_size();
}

static void on_tool_clicked(GtkToolButton *btn, gpointer data) {
    current_tool = GPOINTER_TO_INT(data);
}

static void on_color_set(GtkColorButton *widget, gpointer data) {
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(widget), &current_color);
}

static void on_size_changed(GtkSpinButton *spin, gpointer data) {
    current_size = gtk_spin_button_get_value(spin);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Crayons");
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 700);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(window, "delete-event", G_CALLBACK(on_delete_event), NULL);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), NULL);

    GtkAccelGroup *accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    GtkWidget *menubar = gtk_menu_bar_new();
    
    GtkWidget *fileMenu = gtk_menu_new();
    GtkWidget *fileMi = gtk_menu_item_new_with_label("File");
    GtkWidget *newMi = gtk_menu_item_new_with_label("New");
    GtkWidget *openMi = gtk_menu_item_new_with_label("Open");
    GtkWidget *saveMi = gtk_menu_item_new_with_label("Save");
    GtkWidget *quitMi = gtk_menu_item_new_with_label("Quit");
    
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(fileMi), fileMenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), newMi);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), openMi);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), saveMi);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), quitMi);
    
    gtk_widget_add_accelerator(newMi, "activate", accel_group, GDK_KEY_n, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(openMi, "activate", accel_group, GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(saveMi, "activate", accel_group, GDK_KEY_s, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(quitMi, "activate", accel_group, GDK_KEY_q, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    g_signal_connect(newMi, "activate", G_CALLBACK(on_new_file), NULL);
    g_signal_connect(openMi, "activate", G_CALLBACK(on_open_file), NULL);
    g_signal_connect(saveMi, "activate", G_CALLBACK(on_save_file), NULL);
    g_signal_connect(quitMi, "activate", G_CALLBACK(on_quit_menu), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), fileMi);

    GtkWidget *editMenu = gtk_menu_new();
    GtkWidget *editMi = gtk_menu_item_new_with_label("Edit");
    GtkWidget *undoMi = gtk_menu_item_new_with_label("Undo");
    GtkWidget *redoMi = gtk_menu_item_new_with_label("Redo");

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(editMi), editMenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(editMenu), undoMi);
    gtk_menu_shell_append(GTK_MENU_SHELL(editMenu), redoMi);
    
    gtk_widget_add_accelerator(undoMi, "activate", accel_group, GDK_KEY_z, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(redoMi, "activate", accel_group, GDK_KEY_y, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    g_signal_connect(undoMi, "activate", G_CALLBACK(on_undo), NULL);
    g_signal_connect(redoMi, "activate", G_CALLBACK(on_redo), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), editMi);

    GtkWidget *viewMenu = gtk_menu_new();
    GtkWidget *viewMi = gtk_menu_item_new_with_label("View");
    GtkWidget *zoomInMi = gtk_menu_item_new_with_label("Zoom In");
    GtkWidget *zoomOutMi = gtk_menu_item_new_with_label("Zoom Out");

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(viewMi), viewMenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(viewMenu), zoomInMi);
    gtk_menu_shell_append(GTK_MENU_SHELL(viewMenu), zoomOutMi);

    gtk_widget_add_accelerator(zoomInMi, "activate", accel_group, GDK_KEY_equal, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(zoomOutMi, "activate", accel_group, GDK_KEY_minus, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    g_signal_connect(zoomInMi, "activate", G_CALLBACK(on_zoom_in), NULL);
    g_signal_connect(zoomOutMi, "activate", G_CALLBACK(on_zoom_out), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), viewMi);

    GtkWidget *helpMenu = gtk_menu_new();
    GtkWidget *helpMi = gtk_menu_item_new_with_label("Help");
    GtkWidget *aboutMi = gtk_menu_item_new_with_label("About");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(helpMi), helpMenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(helpMenu), aboutMi);
    g_signal_connect(aboutMi, "activate", G_CALLBACK(on_about), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), helpMi);

    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);

    GtkWidget *toolbar = gtk_toolbar_new();
    gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_BOTH_HORIZ);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

    GtkToolItem *openTb = gtk_tool_button_new_from_stock(GTK_STOCK_OPEN);
    gtk_tool_item_set_tooltip_text(openTb, "Open an image...");
    g_signal_connect(openTb, "clicked", G_CALLBACK(on_open_file), NULL);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), openTb, -1);

    GtkToolItem *saveTb = gtk_tool_button_new_from_stock(GTK_STOCK_SAVE);
    gtk_tool_item_set_tooltip_text(saveTb, "Save to...");
    g_signal_connect(saveTb, "clicked", G_CALLBACK(on_save_file), NULL);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), saveTb, -1);

    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_separator_tool_item_new(), -1);

    GtkToolItem *penTb = gtk_radio_tool_button_new(NULL);
    gtk_tool_button_set_label(GTK_TOOL_BUTTON(penTb), "Pen");
    g_signal_connect(penTb, "clicked", G_CALLBACK(on_tool_clicked), GINT_TO_POINTER(TOOL_PEN));
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), penTb, -1);
    
    GSList *group = gtk_radio_tool_button_get_group(GTK_RADIO_TOOL_BUTTON(penTb));
    
    GtkToolItem *rectTb = gtk_radio_tool_button_new(group);
    gtk_tool_button_set_label(GTK_TOOL_BUTTON(rectTb), "Rect");
    g_signal_connect(rectTb, "clicked", G_CALLBACK(on_tool_clicked), GINT_TO_POINTER(TOOL_RECT));
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), rectTb, -1);

    group = gtk_radio_tool_button_get_group(GTK_RADIO_TOOL_BUTTON(rectTb));

    GtkToolItem *ellipseTb = gtk_radio_tool_button_new(group);
    gtk_tool_button_set_label(GTK_TOOL_BUTTON(ellipseTb), "Circle");
    g_signal_connect(ellipseTb, "clicked", G_CALLBACK(on_tool_clicked), GINT_TO_POINTER(TOOL_ELLIPSE));
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), ellipseTb, -1);

    group = gtk_radio_tool_button_get_group(GTK_RADIO_TOOL_BUTTON(ellipseTb));

    GtkToolItem *arrowTb = gtk_radio_tool_button_new(group);
    gtk_tool_button_set_label(GTK_TOOL_BUTTON(arrowTb), "Arrow");
    g_signal_connect(arrowTb, "clicked", G_CALLBACK(on_tool_clicked), GINT_TO_POINTER(TOOL_ARROW));
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), arrowTb, -1);

    group = gtk_radio_tool_button_get_group(GTK_RADIO_TOOL_BUTTON(arrowTb));
    
    GtkToolItem *redactTb = gtk_radio_tool_button_new(group);
    gtk_tool_button_set_label(GTK_TOOL_BUTTON(redactTb), "Redact");
    gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(redactTb), "gtk-strikethrough");
    gtk_tool_item_set_tooltip_text(redactTb, "Redact Tool (Jitter Filter)");
    g_signal_connect(redactTb, "clicked", G_CALLBACK(on_tool_clicked), GINT_TO_POINTER(TOOL_REDACT));
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), redactTb, -1);

    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_separator_tool_item_new(), -1);

    GtkToolItem *colorItem = gtk_tool_item_new();
    GtkWidget *colorBtn = gtk_color_button_new_with_rgba(&current_color);
    gtk_widget_set_tooltip_text(GTK_WIDGET(colorBtn), "Select pen color");
    g_signal_connect(colorBtn, "color-set", G_CALLBACK(on_color_set), NULL);
    gtk_container_add(GTK_CONTAINER(colorItem), colorBtn);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), colorItem, -1);

    GtkToolItem *sepItem = gtk_separator_tool_item_new();
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), sepItem, -1);

    GtkToolItem *sizeItem = gtk_tool_item_new();
    GtkWidget *sizeBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *sizeLabel = gtk_label_new("Size:");
    GtkWidget *spin = gtk_spin_button_new_with_range(1.0, 50.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), current_size);
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_size_changed), NULL);
    
    gtk_box_pack_start(GTK_BOX(sizeBox), sizeLabel, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(sizeBox), spin, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(sizeItem), sizeBox);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), sizeItem, -1);

    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);

    drawing_area = gtk_drawing_area_new();
    gtk_container_add(GTK_CONTAINER(scrolled_window), drawing_area);

    g_signal_connect(drawing_area, "draw", G_CALLBACK(on_draw), NULL);
    g_signal_connect(drawing_area, "configure-event", G_CALLBACK(configure_event_cb), NULL);
    g_signal_connect(drawing_area, "button-press-event", G_CALLBACK(on_button_press), NULL);
    g_signal_connect(drawing_area, "motion-notify-event", G_CALLBACK(on_motion_notify), NULL);
    g_signal_connect(drawing_area, "button-release-event", G_CALLBACK(on_button_release), NULL);
    g_signal_connect(drawing_area, "scroll-event", G_CALLBACK(on_scroll_event), NULL);

    gtk_widget_set_events(drawing_area, gtk_widget_get_events(drawing_area)
                                      | GDK_BUTTON_PRESS_MASK
                                      | GDK_POINTER_MOTION_MASK
                                      | GDK_BUTTON_RELEASE_MASK
                                      | GDK_SCROLL_MASK);

    gtk_widget_show_all(window);

    if (argc > 1) {
        while(gtk_events_pending()) gtk_main_iteration();
        load_image_to_surface(argv[1]);
    } else {
        on_new_file(NULL, NULL);
    }

    gtk_main();

    return 0;
}
