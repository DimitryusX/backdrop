#include "app.hpp"

#include "config.hpp"
#include "i18n.hpp"
#include "rotator.hpp"
#include "service.hpp"
#include "storage.hpp"

#include <adwaita.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace backdrop {
namespace {

struct UiState {
  Config config;
  std::unique_ptr<Rotator> rotator;
  GtkWindow* window = nullptr;
  GtkBox* gallery = nullptr;
  GtkListBox* folder_list = nullptr;
  GtkFlowBox* image_grid = nullptr;
  GtkWidget* empty_state = nullptr;
  GtkSpinButton* interval = nullptr;
  GtkSwitch* shuffle = nullptr;
  GtkLabel* status = nullptr;
  GtkButton* toggle_btn = nullptr;
  GtkButton* next_btn = nullptr;
  bool holding = false;
  AdwApplication* app = nullptr;
};

UiState* state_from_app(AdwApplication* app) {
  return static_cast<UiState*>(g_object_get_data(G_OBJECT(app), "backdrop-state"));
}

void set_status(UiState* st, const std::string& text) {
  if (st->status != nullptr) {
    gtk_label_set_text(st->status, text.c_str());
  }
}

gboolean idle_set_status(gpointer data) {
  auto* pair = static_cast<std::pair<UiState*, std::string>*>(data);
  set_status(pair->first, pair->second);
  delete pair;
  return G_SOURCE_REMOVE;
}

void queue_status(UiState* st, const std::string& text) {
  g_idle_add(idle_set_status, new std::pair<UiState*, std::string>(st, text));
}

void sync_config_from_ui(UiState* st) {
  if (st->interval != nullptr) {
    st->config.interval_minutes = gtk_spin_button_get_value(st->interval);
  }
  if (st->shuffle != nullptr) {
    st->config.shuffle = gtk_switch_get_active(st->shuffle);
  }
}

void persist(UiState* st) {
  sync_config_from_ui(st);
  st->config.save();
}

void hold_app(UiState* st) {
  if (!st->holding && st->app != nullptr) {
    g_application_hold(G_APPLICATION(st->app));
    st->holding = true;
  }
}

void release_app(UiState* st) {
  if (st->holding && st->app != nullptr) {
    g_application_release(G_APPLICATION(st->app));
    st->holding = false;
  }
}

void rebuild_path_list(UiState* st);
void sync_playback_controls(UiState* st);
void set_toggle_running(UiState* st, bool running);
void stop_rotation(UiState* st);

void ensure_ui_css() {
  static bool loaded = false;
  if (loaded) {
    return;
  }
  loaded = true;

  GtkCssProvider* provider = gtk_css_provider_new();
  const char* css =
      ".wallpaper-tile {"
      "  border-radius: 12px;"
      "  padding: 0;"
      "}"
      ".wallpaper-thumb {"
      "  border-radius: 12px;"
      "  background-color: alpha(@window_fg_color, 0.06);"
      "}"
      ".wallpaper-delete {"
      "  opacity: 0;"
      "  transition: opacity 120ms ease;"
      "}"
      ".wallpaper-tile:hover .wallpaper-delete,"
      ".wallpaper-tile:selected .wallpaper-delete {"
      "  opacity: 1;"
      "}"
      ".folder-row:hover .wallpaper-delete {"
      "  opacity: 1;"
      "}";
  gtk_css_provider_load_from_string(provider, css);

  GdkDisplay* display = gdk_display_get_default();
  if (display != nullptr) {
    gtk_style_context_add_provider_for_display(
        display, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  }
  g_object_unref(provider);
}

bool is_configured_source(const Config& config, const std::string& path) {
  if (std::find(config.paths.begin(), config.paths.end(), path) != config.paths.end()) {
    return true;
  }
  return config.find_import_by_path(path) != nullptr;
}

bool is_directory_source(const std::string& path) {
  std::error_code ec;
  return fs::is_directory(path, ec);
}

GtkWidget* make_thumbnail(const fs::path& path, int width, int height) {
  GError* error = nullptr;
  GdkPixbuf* pixbuf =
      gdk_pixbuf_new_from_file_at_scale(path.c_str(), width, height, TRUE, &error);
  if (pixbuf == nullptr) {
    if (error != nullptr) {
      g_error_free(error);
    }
    GtkWidget* fallback = gtk_image_new_from_icon_name("image-x-generic-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(fallback), 40);
    gtk_widget_set_size_request(fallback, width, height);
    gtk_widget_add_css_class(fallback, "wallpaper-thumb");
    return fallback;
  }

  const int w = gdk_pixbuf_get_width(pixbuf);
  const int h = gdk_pixbuf_get_height(pixbuf);
  const int stride = gdk_pixbuf_get_rowstride(pixbuf);
  const gsize nbytes = static_cast<gsize>(stride) * static_cast<gsize>(h);
  GBytes* bytes = g_bytes_new(gdk_pixbuf_get_pixels(pixbuf), nbytes);
  const GdkMemoryFormat format =
      gdk_pixbuf_get_has_alpha(pixbuf) ? GDK_MEMORY_R8G8B8A8 : GDK_MEMORY_R8G8B8;
  GdkTexture* texture = gdk_memory_texture_new(w, h, format, bytes, static_cast<gsize>(stride));
  g_bytes_unref(bytes);
  g_object_unref(pixbuf);

  GtkWidget* picture = gtk_picture_new_for_paintable(GDK_PAINTABLE(texture));
  g_object_unref(texture);
  gtk_widget_set_size_request(picture, width, height);
  gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_COVER);
  gtk_widget_add_css_class(picture, "wallpaper-thumb");
  return picture;
}

void clear_list_box(GtkListBox* list) {
  if (list == nullptr) {
    return;
  }
  GtkListBoxRow* row = nullptr;
  while ((row = gtk_list_box_get_row_at_index(list, 0)) != nullptr) {
    gtk_list_box_remove(list, GTK_WIDGET(row));
  }
}

void clear_flow_box(GtkFlowBox* flow) {
  if (flow == nullptr) {
    return;
  }
  GtkWidget* child = gtk_widget_get_first_child(GTK_WIDGET(flow));
  while (child != nullptr) {
    GtkWidget* next = gtk_widget_get_next_sibling(child);
    gtk_flow_box_remove(flow, child);
    child = next;
  }
}

bool rotation_active(UiState* st) {
  if (service::available() && service::is_active()) {
    return true;
  }
  return st->rotator && st->rotator->running();
}

void notify_backend_config(UiState* st) {
  if (service::available() && service::is_active()) {
    std::string err;
    if (!service::reload_config(&err)) {
      set_status(st, err.empty() ? _("Failed to update background service") : err);
    }
    return;
  }
  if (st->rotator && st->rotator->running()) {
    st->rotator->configure(st->config);
  }
}

void after_paths_changed(UiState* st) {
  rebuild_path_list(st);
  persist(st);
  const auto images = st->config.image_files();
  if (images.empty()) {
    if (rotation_active(st) || st->config.running) {
      stop_rotation(st);
    }
    set_status(st, _("Add images or a folder"));
  } else {
    set_status(st, _("Images found: ") + std::to_string(images.size()));
    notify_backend_config(st);
  }
  sync_playback_controls(st);
}

void on_remove_path(GtkButton*, gpointer user_data) {
  auto* payload = static_cast<std::pair<UiState*, std::string>*>(user_data);
  UiState* st = payload->first;
  const std::string& path = payload->second;
  remove_managed_path(path);
  st->config.remove_path_references(path);
  after_paths_changed(st);
}

void free_path_payload(gpointer data, GClosure*) {
  delete static_cast<std::pair<UiState*, std::string>*>(data);
}

void append_folder_row(UiState* st, const std::string& path) {
  GtkWidget* list_row = gtk_list_box_row_new();
  gtk_widget_add_css_class(list_row, "folder-row");

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_top(box, 8);
  gtk_widget_set_margin_bottom(box, 8);
  gtk_widget_set_margin_start(box, 12);
  gtk_widget_set_margin_end(box, 8);

  GtkWidget* icon = gtk_image_new_from_icon_name("folder-symbolic");
  gtk_image_set_pixel_size(GTK_IMAGE(icon), 24);

  GtkWidget* text_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_hexpand(text_col, TRUE);
  GtkWidget* title = gtk_label_new(_("Folder"));
  gtk_widget_set_halign(title, GTK_ALIGN_START);
  GtkWidget* subtitle = gtk_label_new(path.c_str());
  gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
  gtk_label_set_ellipsize(GTK_LABEL(subtitle), PANGO_ELLIPSIZE_MIDDLE);
  gtk_widget_add_css_class(subtitle, "dim-label");
  gtk_box_append(GTK_BOX(text_col), title);
  gtk_box_append(GTK_BOX(text_col), subtitle);

  GtkWidget* remove = gtk_button_new_from_icon_name("user-trash-symbolic");
  gtk_widget_add_css_class(remove, "flat");
  gtk_widget_add_css_class(remove, "wallpaper-delete");
  gtk_widget_set_valign(remove, GTK_ALIGN_CENTER);
  gtk_widget_set_tooltip_text(remove, _("Remove folder from list"));
  auto* payload = new std::pair<UiState*, std::string>(st, path);
  g_signal_connect_data(remove, "clicked", G_CALLBACK(on_remove_path), payload, free_path_payload,
                        static_cast<GConnectFlags>(0));

  gtk_box_append(GTK_BOX(box), icon);
  gtk_box_append(GTK_BOX(box), text_col);
  gtk_box_append(GTK_BOX(box), remove);
  gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(list_row), box);
  gtk_list_box_append(st->folder_list, list_row);
}

void append_image_tile(UiState* st, const fs::path& image_path) {
  constexpr int kWidth = 140;
  constexpr int kHeight = 90;

  const std::string path = image_path.string();
  const bool removable = is_configured_source(st->config, path) || is_managed_path(path);

  GtkWidget* overlay = gtk_overlay_new();
  gtk_widget_add_css_class(overlay, "wallpaper-tile");
  gtk_widget_set_tooltip_text(overlay, path.c_str());

  GtkWidget* thumb = make_thumbnail(image_path, kWidth, kHeight);
  gtk_overlay_set_child(GTK_OVERLAY(overlay), thumb);

  if (removable) {
    GtkWidget* remove = gtk_button_new_from_icon_name("user-trash-symbolic");
    gtk_widget_add_css_class(remove, "circular");
    gtk_widget_add_css_class(remove, "destructive-action");
    gtk_widget_add_css_class(remove, "wallpaper-delete");
    gtk_widget_set_halign(remove, GTK_ALIGN_END);
    gtk_widget_set_valign(remove, GTK_ALIGN_START);
    gtk_widget_set_margin_top(remove, 6);
    gtk_widget_set_margin_end(remove, 6);
    gtk_widget_set_tooltip_text(remove, _("Remove from list"));
    auto* payload = new std::pair<UiState*, std::string>(st, path);
    g_signal_connect_data(remove, "clicked", G_CALLBACK(on_remove_path), payload, free_path_payload,
                          static_cast<GConnectFlags>(0));
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), remove);
  }

  gtk_flow_box_append(st->image_grid, overlay);
}

void rebuild_path_list(UiState* st) {
  if (st->image_grid == nullptr || st->folder_list == nullptr) {
    return;
  }

  ensure_ui_css();
  clear_list_box(st->folder_list);
  clear_flow_box(st->image_grid);

  std::vector<std::string> folders;
  for (const auto& path : st->config.paths) {
    if (is_directory_source(path)) {
      folders.push_back(path);
    }
  }

  const auto images = st->config.image_files();
  const bool empty = folders.empty() && images.empty();

  if (st->empty_state != nullptr) {
    gtk_widget_set_visible(st->empty_state, empty);
  }
  gtk_widget_set_visible(GTK_WIDGET(st->image_grid), !empty);
  gtk_widget_set_visible(GTK_WIDGET(st->folder_list), !folders.empty());

  if (empty) {
    return;
  }

  for (const auto& folder : folders) {
    append_folder_row(st, folder);
  }

  constexpr std::size_t kMaxThumbs = 200;
  const std::size_t shown = std::min(images.size(), kMaxThumbs);
  for (std::size_t i = 0; i < shown; ++i) {
    append_image_tile(st, images[i]);
  }

  if (images.size() > kMaxThumbs) {
    const std::string text =
        "… +" + std::to_string(images.size() - kMaxThumbs);
    GtkWidget* more = gtk_label_new(text.c_str());
    gtk_widget_add_css_class(more, "dim-label");
    gtk_widget_set_margin_top(more, 12);
    gtk_widget_set_margin_bottom(more, 12);
    gtk_flow_box_append(st->image_grid, more);
  }
}

void sync_playback_controls(UiState* st) {
  const bool has_images = !st->config.image_files().empty();
  if (st->toggle_btn != nullptr) {
    gtk_widget_set_sensitive(GTK_WIDGET(st->toggle_btn), has_images);
    if (!has_images) {
      set_toggle_running(st, false);
    }
  }
  if (st->next_btn != nullptr) {
    gtk_widget_set_sensitive(GTK_WIDGET(st->next_btn), has_images);
  }
}

void set_toggle_running(UiState* st, bool running) {
  if (st->toggle_btn == nullptr) {
    return;
  }
  if (running) {
    gtk_button_set_label(st->toggle_btn, _("Stop"));
    gtk_widget_remove_css_class(GTK_WIDGET(st->toggle_btn), "suggested-action");
    gtk_widget_add_css_class(GTK_WIDGET(st->toggle_btn), "destructive-action");
  } else {
    gtk_button_set_label(st->toggle_btn, _("Start"));
    gtk_widget_remove_css_class(GTK_WIDGET(st->toggle_btn), "destructive-action");
    gtk_widget_add_css_class(GTK_WIDGET(st->toggle_btn), "suggested-action");
  }
}

void start_rotation(UiState* st, bool persist_cfg) {
  sync_config_from_ui(st);
  if (st->config.image_files().empty()) {
    set_status(st, _("No images. Add files or a folder."));
    return;
  }

  st->config.running = true;
  st->config.save();
  (void)persist_cfg;

  // Prefer systemd user service so rotation survives closing the window / reboot.
  if (service::available()) {
    std::string err;
    const std::string exe = service::self_executable();
    if (service::ensure_unit(exe, &err) && service::enable_now(&err)) {
      // Stop any leftover in-process rotator from older sessions.
      if (st->rotator && st->rotator->running()) {
        st->rotator->stop();
      }
      release_app(st);
      set_toggle_running(st, true);
      set_status(st, _("Background service started (keeps running after you close the window)"));
      return;
    }
    set_status(st, std::string("systemd: ") + (err.empty() ? _("error") : err) +
                       _(" — falling back to in-process mode"));
  }

  if (!st->rotator->start(st->config)) {
    st->config.running = false;
    st->config.save();
    return;
  }
  hold_app(st);
  set_toggle_running(st, true);
  set_status(st, _("Running in the app process (closing the window may stop rotation)"));
}

void stop_rotation(UiState* st) {
  if (service::available()) {
    std::string err;
    service::disable_now(&err);
  }
  if (st->rotator && st->rotator->running()) {
    st->rotator->stop();
  }
  st->config.running = false;
  st->config.save();
  release_app(st);
  set_toggle_running(st, false);
  set_status(st, _("Stopped"));
}

void on_settings_changed(GtkSpinButton*, gpointer user_data) {
  auto* st = static_cast<UiState*>(user_data);
  persist(st);
  notify_backend_config(st);
}

gboolean on_shuffle_set(GtkSwitch*, gboolean state, gpointer user_data) {
  auto* st = static_cast<UiState*>(user_data);
  st->config.shuffle = state;
  st->config.save();
  notify_backend_config(st);
  return FALSE;
}

void on_files_chosen(GObject* source, GAsyncResult* result, gpointer user_data) {
  auto* st = static_cast<UiState*>(user_data);
  GError* error = nullptr;
  GListModel* files = gtk_file_dialog_open_multiple_finish(GTK_FILE_DIALOG(source), result, &error);
  if (error != nullptr) {
    g_error_free(error);
    return;
  }
  if (files == nullptr) {
    return;
  }

  int imported = 0;
  int failed = 0;
  int duplicates = 0;
  const guint n = g_list_model_get_n_items(files);
  for (guint i = 0; i < n; ++i) {
    auto* file = static_cast<GFile*>(g_list_model_get_item(files, i));
    char* path = g_file_get_path(file);
    if (path != nullptr) {
      std::string err;
      const ImportResult result = import_image(path, st->config, &err);
      g_free(path);
      if (result.path.empty() || result.sha256.empty()) {
        ++failed;
      } else if (result.duplicate) {
        ++duplicates;
        st->config.add_or_update_import(result.path.string(), result.sha256);
      } else {
        st->config.add_or_update_import(result.path.string(), result.sha256);
        ++imported;
      }
    }
    g_object_unref(file);
  }
  g_object_unref(files);
  after_paths_changed(st);
  if (failed > 0 || duplicates > 0) {
    char buf[192];
    std::snprintf(buf, sizeof(buf), _("Imported: %d, duplicates: %d, failed: %d"), imported,
                  duplicates, failed);
    set_status(st, buf);
  } else if (imported > 0) {
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  _("Imported images: %d → ~/.local/share/backdrop/wallpapers"), imported);
    set_status(st, buf);
  }
}

void on_add_files(GtkButton*, gpointer user_data) {
  auto* st = static_cast<UiState*>(user_data);
  GtkFileDialog* dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, _("Import images"));

  GtkFileFilter* images = gtk_file_filter_new();
  gtk_file_filter_set_name(images, _("Images"));
  for (const char* mime : {"image/jpeg", "image/png", "image/webp", "image/bmp", "image/gif",
                           "image/jxl", "image/svg+xml"}) {
    gtk_file_filter_add_mime_type(images, mime);
  }
  GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
  g_list_store_append(filters, images);
  gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
  g_object_unref(filters);
  g_object_unref(images);

  gtk_file_dialog_open_multiple(dialog, st->window, nullptr, on_files_chosen, st);
  g_object_unref(dialog);
}

void on_folder_chosen(GObject* source, GAsyncResult* result, gpointer user_data) {
  auto* st = static_cast<UiState*>(user_data);
  GError* error = nullptr;
  GFile* folder = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source), result, &error);
  if (error != nullptr) {
    g_error_free(error);
    return;
  }
  if (folder == nullptr) {
    return;
  }
  char* path = g_file_get_path(folder);
  if (path != nullptr) {
    std::string p(path);
    if (std::find(st->config.paths.begin(), st->config.paths.end(), p) == st->config.paths.end()) {
      st->config.paths.push_back(p);
    }
    g_free(path);
  }
  g_object_unref(folder);
  after_paths_changed(st);
}

void on_add_folder(GtkButton*, gpointer user_data) {
  auto* st = static_cast<UiState*>(user_data);
  GtkFileDialog* dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, _("Select folder"));
  gtk_file_dialog_select_folder(dialog, st->window, nullptr, on_folder_chosen, st);
  g_object_unref(dialog);
}

void on_clear(GtkButton*, gpointer user_data) {
  auto* st = static_cast<UiState*>(user_data);
  for (const auto& entry : st->config.imports) {
    remove_managed_path(entry.path);
  }
  for (const auto& path : st->config.paths) {
    remove_managed_path(path);
  }
  st->config.paths.clear();
  st->config.imports.clear();
  purge_data_dir();
  after_paths_changed(st);
}

void on_toggle(GtkButton*, gpointer user_data) {
  auto* st = static_cast<UiState*>(user_data);
  if (rotation_active(st)) {
    stop_rotation(st);
  } else {
    start_rotation(st, true);
  }
}

void on_next(GtkButton*, gpointer user_data) {
  auto* st = static_cast<UiState*>(user_data);
  sync_config_from_ui(st);
  st->config.save();
  if (st->config.image_files().empty()) {
    set_status(st, _("No images"));
    return;
  }
  if (service::available() && service::is_active()) {
    std::string err;
    if (!service::request_next(&err)) {
      set_status(st, err.empty() ? _("Failed to switch wallpaper") : err);
      return;
    }
    set_status(st, _("Requested next wallpaper"));
    return;
  }
  if (!st->rotator->running()) {
    st->rotator->configure(st->config);
  }
  st->rotator->next();
}

gboolean on_close_request(GtkWindow*, gpointer user_data) {
  auto* st = static_cast<UiState*>(user_data);
  st->window = nullptr;
  st->gallery = nullptr;
  st->folder_list = nullptr;
  st->image_grid = nullptr;
  st->empty_state = nullptr;
  st->interval = nullptr;
  st->shuffle = nullptr;
  st->status = nullptr;
  st->toggle_btn = nullptr;
  st->next_btn = nullptr;
  return FALSE;
}

GtkWidget* build_window(UiState* st) {
  ensure_ui_css();

  AdwApplicationWindow* window =
      ADW_APPLICATION_WINDOW(adw_application_window_new(GTK_APPLICATION(st->app)));
  gtk_window_set_title(GTK_WINDOW(window), "Backdrop");
  gtk_window_set_default_size(GTK_WINDOW(window), 520, 680);
  g_signal_connect(window, "close-request", G_CALLBACK(on_close_request), st);
  st->window = GTK_WINDOW(window);

  AdwToolbarView* toolbar = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
  AdwHeaderBar* header = ADW_HEADER_BAR(adw_header_bar_new());
  adw_header_bar_set_show_title(header, FALSE);
  GtkWidget* title = gtk_label_new("Backdrop");
  gtk_widget_add_css_class(title, "heading");
  gtk_widget_set_margin_start(title, 6);
  adw_header_bar_pack_start(header, title);
  adw_toolbar_view_add_top_bar(toolbar, GTK_WIDGET(header));

  GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
  gtk_widget_set_margin_top(root, 16);
  gtk_widget_set_margin_bottom(root, 16);
  gtk_widget_set_margin_start(root, 16);
  gtk_widget_set_margin_end(root, 16);

  GtkWidget* sources_label = gtk_label_new(_("Wallpapers"));
  gtk_widget_set_halign(sources_label, GTK_ALIGN_START);
  gtk_widget_add_css_class(sources_label, "heading");
  gtk_box_append(GTK_BOX(root), sources_label);

  st->gallery = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 8));

  st->folder_list = GTK_LIST_BOX(gtk_list_box_new());
  gtk_list_box_set_selection_mode(st->folder_list, GTK_SELECTION_NONE);
  gtk_widget_add_css_class(GTK_WIDGET(st->folder_list), "boxed-list");
  gtk_box_append(st->gallery, GTK_WIDGET(st->folder_list));

  st->image_grid = GTK_FLOW_BOX(gtk_flow_box_new());
  gtk_flow_box_set_selection_mode(st->image_grid, GTK_SELECTION_NONE);
  gtk_flow_box_set_min_children_per_line(st->image_grid, 2);
  gtk_flow_box_set_max_children_per_line(st->image_grid, 4);
  gtk_flow_box_set_homogeneous(st->image_grid, TRUE);
  gtk_flow_box_set_column_spacing(st->image_grid, 10);
  gtk_flow_box_set_row_spacing(st->image_grid, 10);
  gtk_widget_set_hexpand(GTK_WIDGET(st->image_grid), TRUE);
  gtk_box_append(st->gallery, GTK_WIDGET(st->image_grid));

  st->empty_state = gtk_label_new(_("No images\nImport files or add a folder"));
  gtk_label_set_justify(GTK_LABEL(st->empty_state), GTK_JUSTIFY_CENTER);
  gtk_label_set_wrap(GTK_LABEL(st->empty_state), TRUE);
  gtk_widget_set_halign(st->empty_state, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(st->empty_state, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand(st->empty_state, TRUE);
  gtk_widget_set_vexpand(st->empty_state, TRUE);
  gtk_widget_add_css_class(st->empty_state, "dim-label");
  gtk_widget_set_visible(st->empty_state, FALSE);
  gtk_box_append(st->gallery, st->empty_state);

  GtkWidget* list_scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_scroll), GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(list_scroll), 320);
  gtk_widget_set_vexpand(list_scroll, TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(list_scroll), GTK_WIDGET(st->gallery));
  gtk_box_append(GTK_BOX(root), list_scroll);
  rebuild_path_list(st);

  GtkWidget* params_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
  gtk_box_set_homogeneous(GTK_BOX(params_row), TRUE);

  // Left column: sources
  GtkWidget* left_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget* sources_heading = gtk_label_new(_("Sources"));
  gtk_widget_set_halign(sources_heading, GTK_ALIGN_START);
  gtk_widget_add_css_class(sources_heading, "heading");
  gtk_box_append(GTK_BOX(left_col), sources_heading);

  GtkWidget* add_files = gtk_button_new_with_label(_("Import"));
  gtk_widget_set_hexpand(add_files, TRUE);
  g_signal_connect(add_files, "clicked", G_CALLBACK(on_add_files), st);
  gtk_widget_set_tooltip_text(add_files, _("Copy images into ~/.local/share/backdrop/wallpapers"));
  gtk_box_append(GTK_BOX(left_col), add_files);

  GtkWidget* add_folder = gtk_button_new_with_label(_("Folder"));
  gtk_widget_set_hexpand(add_folder, TRUE);
  g_signal_connect(add_folder, "clicked", G_CALLBACK(on_add_folder), st);
  gtk_widget_set_tooltip_text(add_folder, _("Link a folder (no copying)"));
  gtk_box_append(GTK_BOX(left_col), add_folder);

  GtkWidget* clear_btn = gtk_button_new_with_label(_("Clear"));
  gtk_widget_set_hexpand(clear_btn, TRUE);
  gtk_widget_add_css_class(clear_btn, "destructive-action");
  g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear), st);
  gtk_box_append(GTK_BOX(left_col), clear_btn);

  // Right column: rotation settings + controls
  GtkWidget* right_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget* rotation_heading = gtk_label_new(_("Rotation"));
  gtk_widget_set_halign(rotation_heading, GTK_ALIGN_START);
  gtk_widget_add_css_class(rotation_heading, "heading");
  gtk_box_append(GTK_BOX(right_col), rotation_heading);

  GtkWidget* interval_label = gtk_label_new(_("Interval"));
  gtk_widget_set_halign(interval_label, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(right_col), interval_label);

  GtkWidget* interval_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_valign(interval_row, GTK_ALIGN_CENTER);
  st->interval = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0.5, 1440.0, 0.5));
  gtk_spin_button_set_digits(st->interval, 1);
  gtk_spin_button_set_value(st->interval, st->config.interval_minutes);
  gtk_widget_set_hexpand(GTK_WIDGET(st->interval), TRUE);
  g_signal_connect(st->interval, "value-changed", G_CALLBACK(on_settings_changed), st);
  gtk_box_append(GTK_BOX(interval_row), GTK_WIDGET(st->interval));
  gtk_box_append(GTK_BOX(interval_row), gtk_label_new(_("minutes")));
  gtk_box_append(GTK_BOX(right_col), interval_row);

  GtkWidget* shuffle_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* shuffle_text = gtk_label_new(_("Shuffle"));
  gtk_widget_set_halign(shuffle_text, GTK_ALIGN_START);
  gtk_widget_set_hexpand(shuffle_text, TRUE);
  st->shuffle = GTK_SWITCH(gtk_switch_new());
  gtk_switch_set_active(st->shuffle, st->config.shuffle);
  gtk_widget_set_valign(GTK_WIDGET(st->shuffle), GTK_ALIGN_CENTER);
  g_signal_connect(st->shuffle, "state-set", G_CALLBACK(on_shuffle_set), st);
  gtk_box_append(GTK_BOX(shuffle_row), shuffle_text);
  gtk_box_append(GTK_BOX(shuffle_row), GTK_WIDGET(st->shuffle));
  gtk_box_append(GTK_BOX(right_col), shuffle_row);

  GtkWidget* controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  st->toggle_btn = GTK_BUTTON(gtk_button_new_with_label(_("Start")));
  gtk_widget_add_css_class(GTK_WIDGET(st->toggle_btn), "suggested-action");
  gtk_widget_set_hexpand(GTK_WIDGET(st->toggle_btn), TRUE);
  g_signal_connect(st->toggle_btn, "clicked", G_CALLBACK(on_toggle), st);
  st->next_btn = GTK_BUTTON(gtk_button_new_with_label(_("Next")));
  g_signal_connect(st->next_btn, "clicked", G_CALLBACK(on_next), st);
  gtk_box_append(GTK_BOX(controls), GTK_WIDGET(st->toggle_btn));
  gtk_box_append(GTK_BOX(controls), GTK_WIDGET(st->next_btn));
  gtk_box_append(GTK_BOX(right_col), controls);

  gtk_box_append(GTK_BOX(params_row), left_col);
  gtk_box_append(GTK_BOX(params_row), right_col);
  gtk_box_append(GTK_BOX(root), params_row);

  st->status = GTK_LABEL(gtk_label_new(_("Ready")));
  gtk_widget_set_hexpand(GTK_WIDGET(st->status), TRUE);
  gtk_widget_set_halign(GTK_WIDGET(st->status), GTK_ALIGN_FILL);
  gtk_label_set_xalign(st->status, 0.0f);
  gtk_label_set_wrap(st->status, TRUE);
  gtk_label_set_wrap_mode(st->status, PANGO_WRAP_WORD_CHAR);
  gtk_widget_add_css_class(GTK_WIDGET(st->status), "dim-label");
  gtk_box_append(GTK_BOX(root), GTK_WIDGET(st->status));

  const auto images = st->config.image_files();
  if (images.empty()) {
    set_status(st, _("Add images or a folder"));
  } else {
    set_status(st, _("Images found: ") + std::to_string(images.size()));
  }
  sync_playback_controls(st);

  adw_toolbar_view_set_content(toolbar, root);
  adw_application_window_set_content(window, GTK_WIDGET(toolbar));

  if (rotation_active(st) || st->config.running) {
    set_toggle_running(st, rotation_active(st) || st->config.running);
  }
  if (service::available() && service::is_active()) {
    set_status(st, _("Background service is active"));
  }

  return GTK_WIDGET(window);
}

void on_activate(GApplication* app, gpointer) {
  auto* st = state_from_app(ADW_APPLICATION(app));
  if (st->window == nullptr) {
    GtkWidget* window = build_window(st);
    // Resume via systemd (or in-process fallback) if config says we should be running.
    if (st->config.running && !st->config.image_files().empty() && !rotation_active(st)) {
      start_rotation(st, false);
    } else if (rotation_active(st)) {
      set_toggle_running(st, true);
    }
    gtk_window_present(GTK_WINDOW(window));
  } else {
    gtk_window_present(st->window);
  }
}

}  // namespace

int run_ui(int argc, char** argv) {
  adw_init();

  auto* st = new UiState();
  st->config = Config::load();
  st->rotator = std::make_unique<Rotator>(
      [st](const fs::path& p) { queue_status(st, _("Now: ") + p.filename().string()); },
      [st](const std::string& m) { queue_status(st, m); });

  AdwApplication* app =
      adw_application_new("io.nexol.Backdrop", G_APPLICATION_DEFAULT_FLAGS);
  st->app = app;
  g_object_set_data_full(G_OBJECT(app), "backdrop-state", st, [](gpointer data) {
    auto* state = static_cast<UiState*>(data);
    if (state->rotator) {
      state->rotator->stop();
    }
    delete state;
  });

  g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
  const int code = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return code;
}

}  // namespace backdrop
