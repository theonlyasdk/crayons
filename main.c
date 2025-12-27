#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

/* --- Global State --- */
static cairo_surface_t *surface = NULL;
static GtkWidget *window = NULL;
static GtkWidget *drawing_area = NULL;

typedef enum {
    TOOL_SELECT,
    TOOL_PEN
} ToolType;

static ToolType current_tool = TOOL_PEN;
static double last_x = 0;
static double last_y = 0;
static gboolean is_drawing = FALSE;

// Selection Rectangle State
static GdkRectangle rect = {0, 0, 0, 0};
static int start_x = 0;
static int start_y = 0;

/* --- Forward Declarations --- */
static void clear_surface(void);
static void load_image_to_surface(const char *filename);
static void on_tool_change_check(GtkCheckMenuItem *elem, gpointer data);

/* --- Helper Functions --- */

void normalize_rect(GdkRectangle *r) {
    if (r->width < 0) {
        r->x += r->width;
        r->width = -r->width;
    }
    if (r->height < 0) {
        r->y += r->height;
        r->height = -r->height;
    }
}

static gboolean configure_event_cb(GtkWidget *widget, GdkEventConfigure *event, gpointer data) {
    if (surface) {
        cairo_surface_t *new_surface = gdk_window_create_similar_surface(
            gtk_widget_get_window(widget), CAIRO_CONTENT_COLOR,
            gtk_widget_get_allocated_width(widget),
            gtk_widget_get_allocated_height(widget));

        cairo_t *cr = cairo_create(new_surface);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        cairo_set_source_surface(cr, surface, 0, 0);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        surface = new_surface;
    } else {
        surface = gdk_window_create_similar_surface(
            gtk_widget_get_window(widget), CAIRO_CONTENT_COLOR,
            gtk_widget_get_allocated_width(widget),
            gtk_widget_get_allocated_height(widget));
        clear_surface();
    }
    return TRUE;
}

static void clear_surface(void) {
    cairo_t *cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_destroy(cr);
}

/* --- Drawing Callbacks --- */

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_paint(cr);

    if (current_tool == TOOL_SELECT && is_drawing) {
        GdkRectangle draw_rect = rect;
        normalize_rect(&draw_rect);

        cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
        cairo_set_line_width(cr, 2.0);
        cairo_set_dash(cr, (double[]){4.0, 4.0}, 2, 0);
        cairo_rectangle(cr, draw_rect.x, draw_rect.y, draw_rect.width, draw_rect.height);
        cairo_stroke(cr);
    }
    return FALSE;
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->button == GDK_BUTTON_PRIMARY && surface) {
        is_drawing = TRUE;
        start_x = event->x;
        start_y = event->y;
        last_x = event->x;
        last_y = event->y;

        if (current_tool == TOOL_SELECT) {
            rect.x = start_x;
            rect.y = start_y;
            rect.width = 0;
            rect.height = 0;
        }
    }
    return TRUE;
}

static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    if (!is_drawing || !surface) return TRUE;

    if (current_tool == TOOL_PEN) {
        cairo_t *cr = cairo_create(surface);
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_set_line_width(cr, 3.0);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        
        cairo_move_to(cr, last_x, last_y);
        cairo_line_to(cr, event->x, event->y);
        cairo_stroke(cr);
        cairo_destroy(cr);

        last_x = event->x;
        last_y = event->y;
        gtk_widget_queue_draw(widget);
    } 
    else if (current_tool == TOOL_SELECT) {
        rect.width = event->x - start_x;
        rect.height = event->y - start_y;
        gtk_widget_queue_draw(widget);
    }
    return TRUE;
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->button == GDK_BUTTON_PRIMARY && is_drawing) {
        is_drawing = FALSE;
        if (current_tool == TOOL_SELECT) {
            normalize_rect(&rect);
            g_print("Selection: x=%d, y=%d, w=%d, h=%d\n", 
                    rect.x, rect.y, rect.width, rect.height);
            gtk_widget_queue_draw(widget);
        }
    }
    return TRUE;
}

/* --- Menu Actions --- */

static void on_new_file(GtkWidget *w, gpointer data) {
    clear_surface();
    gtk_widget_queue_draw(drawing_area);
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
        load_image_to_surface(filename);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void on_save_file(GtkWidget *w, gpointer data) {
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Save Drawing",
                                         GTK_WINDOW(window),
                                         GTK_FILE_CHOOSER_ACTION_SAVE,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Save", GTK_RESPONSE_ACCEPT,
                                         NULL);
    
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "drawing.png");

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        cairo_surface_write_to_png(surface, filename);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void on_about(GtkWidget *w, gpointer data) {
    gtk_show_about_dialog(GTK_WINDOW(window),
                          "program-name", "Crayons",
                          "version", "1.0",
                          "comments", "A simple image annotation creator written in GTK.\n(c) theonlyasdk 2025-26",
                          NULL);
}

static void load_image_to_surface(const char *filename) {
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filename, &error);
    
    if (!pixbuf) {
        g_printerr("Error loading file: %s\n", error->message);
        g_error_free(error);
        return;
    }

    int w = gdk_pixbuf_get_width(pixbuf);
    int h = gdk_pixbuf_get_height(pixbuf);

    gtk_window_resize(GTK_WINDOW(window), w, h + 30);

    if (surface) cairo_surface_destroy(surface);
    surface = gdk_window_create_similar_surface(gtk_widget_get_window(drawing_area),
                                                CAIRO_CONTENT_COLOR, w, h);
                                                
    cairo_t *cr = cairo_create(surface);
    gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    
    g_object_unref(pixbuf);
    gtk_widget_queue_draw(drawing_area);
}

// Tool change callback (Called when Pen radio button state changes)
static void on_tool_change_check(GtkCheckMenuItem *elem, gpointer data) {
    if (gtk_check_menu_item_get_active(elem)) {
        current_tool = TOOL_PEN;
    } else {
        current_tool = TOOL_SELECT;
    }
}

/* --- Main --- */

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Crayons");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    /* --- Menubar Setup --- */
    GtkWidget *menubar = gtk_menu_bar_new();
    
    // File Menu
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
    
    g_signal_connect(newMi, "activate", G_CALLBACK(on_new_file), NULL);
    g_signal_connect(openMi, "activate", G_CALLBACK(on_open_file), NULL);
    g_signal_connect(saveMi, "activate", G_CALLBACK(on_save_file), NULL);
    g_signal_connect(quitMi, "activate", G_CALLBACK(gtk_main_quit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), fileMi);

    // Tools Menu
    GtkWidget *toolsMenu = gtk_menu_new();
    GtkWidget *toolsMi = gtk_menu_item_new_with_label("Tools");
    
    GSList *group = NULL;
    GtkWidget *penMi = gtk_radio_menu_item_new_with_label(group, "Pen");
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(penMi));
    GtkWidget *selectMi = gtk_radio_menu_item_new_with_label(group, "Select Rectangle");
    
    // Connect the tool change signal properly
    g_signal_connect(penMi, "toggled", G_CALLBACK(on_tool_change_check), NULL);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(toolsMi), toolsMenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(toolsMenu), penMi);
    gtk_menu_shell_append(GTK_MENU_SHELL(toolsMenu), selectMi);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), toolsMi);

    // Help Menu
    GtkWidget *helpMenu = gtk_menu_new();
    GtkWidget *helpMi = gtk_menu_item_new_with_label("Help");
    GtkWidget *aboutMi = gtk_menu_item_new_with_label("About");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(helpMi), helpMenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(helpMenu), aboutMi);
    g_signal_connect(aboutMi, "activate", G_CALLBACK(on_about), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), helpMi);

    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);

    /* --- Drawing Area Setup --- */
    drawing_area = gtk_drawing_area_new();
    gtk_box_pack_start(GTK_BOX(vbox), drawing_area, TRUE, TRUE, 0);

    g_signal_connect(drawing_area, "draw", G_CALLBACK(on_draw), NULL);
    g_signal_connect(drawing_area, "configure-event", G_CALLBACK(configure_event_cb), NULL);
    g_signal_connect(drawing_area, "button-press-event", G_CALLBACK(on_button_press), NULL);
    g_signal_connect(drawing_area, "motion-notify-event", G_CALLBACK(on_motion_notify), NULL);
    g_signal_connect(drawing_area, "button-release-event", G_CALLBACK(on_button_release), NULL);

    gtk_widget_set_events(drawing_area, gtk_widget_get_events(drawing_area)
                                      | GDK_BUTTON_PRESS_MASK
                                      | GDK_POINTER_MOTION_MASK
                                      | GDK_BUTTON_RELEASE_MASK);

    gtk_widget_show_all(window);

    // Handle CLI arguments
    if (argc > 1) {
        while(gtk_events_pending()) gtk_main_iteration();
        load_image_to_surface(argv[1]);
    }

    gtk_main();

    return 0;
}
