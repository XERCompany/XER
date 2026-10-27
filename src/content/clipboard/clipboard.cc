/**
 * This file is part of XER, Clipboard subsystem.
 * Copyright (C) 2026 XER Runtime Team
 */
#include "content/clipboard/clipboard.h"

#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <Cocoa/Cocoa.h>
#else
#include <X11/Xlib.h>
#include <gtk/gtk.h>
#endif

namespace content::clipboard {

Clipboard& Clipboard::Instance() {
  static Clipboard instance;
  return instance;
}

bool Clipboard::Initialize() {
  if (initialized_) {
    return true;
  }
  
  // Get the shared thread pool from async_io
  thread_pool_ = &AsyncIO::IO::IOThreadPool::GetSharedInstance();
  
#ifdef _WIN32
  // Windows clipboard is available by default
  initialized_ = true;
#elif defined(__APPLE__)
  // macOS clipboard is available by default
  initialized_ = true;
#else
  // Linux: Initialize GTK for clipboard support
  if (!gtk_init_check(nullptr, nullptr)) {
    return false;
  }
  initialized_ = true;
#endif
  
  return initialized_;
}

void Clipboard::Shutdown() {
  if (!initialized_) {
    return;
  }
  
  observers_.clear();
  initialized_ = false;
}

bool Clipboard::SetText(const std::string& text) {
  if (!initialized_) {
    return false;
  }
  
#ifdef _WIN32
  if (!OpenClipboard(nullptr)) {
    return false;
  }
  
  EmptyClipboard();
  
  HGLOBAL hglb = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
  if (hglb == nullptr) {
    CloseClipboard();
    return false;
  }
  
  char* lptstr = static_cast<char*>(GlobalLock(hglb));
  memcpy(lptstr, text.c_str(), text.size() + 1);
  GlobalUnlock(hglb);
  
  SetClipboardData(CF_TEXT, hglb);
  CloseClipboard();
  
  NotifyObservers(ClipboardData(ClipboardFormat::kText, text));
  return true;
  
#elif defined(__APPLE__)
  NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
  [pasteboard clearContents];
  
  NSString* ns_string = [NSString stringWithUTF8String:text.c_str()];
  [pasteboard setString:ns_string forType:NSPasteboardTypeString];
  
  NotifyObservers(ClipboardData(ClipboardFormat::kText, text));
  return true;
  
#else
  GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  gtk_clipboard_set_text(clipboard, text.c_str(), -1);
  
  NotifyObservers(ClipboardData(ClipboardFormat::kText, text));
  return true;
#endif
}

bool Clipboard::GetText(std::string* text) {
  if (!initialized_ || text == nullptr) {
    return false;
  }
  
#ifdef _WIN32
  if (!IsClipboardFormatAvailable(CF_TEXT)) {
    return false;
  }
  
  if (!OpenClipboard(nullptr)) {
    return false;
  }
  
  HGLOBAL hglb = GetClipboardData(CF_TEXT);
  if (hglb == nullptr) {
    CloseClipboard();
    return false;
  }
  
  char* lptstr = static_cast<char*>(GlobalLock(hglb));
  if (lptstr != nullptr) {
    *text = lptstr;
    GlobalUnlock(hglb);
  }
  
  CloseClipboard();
  return lptstr != nullptr;
  
#elif defined(__APPLE__)
  NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
  NSString* ns_string = [pasteboard stringForType:NSPasteboardTypeString];
  
  if (ns_string != nil) {
    *text = [ns_string UTF8String];
    return true;
  }
  return false;
  
#else
  GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  gchar* gtk_text = gtk_clipboard_wait_for_text(clipboard);
  
  if (gtk_text != nullptr) {
    *text = gtk_text;
    g_free(gtk_text);
    return true;
  }
  return false;
#endif
}

bool Clipboard::SetHTML(const std::string& html) {
  if (!initialized_) {
    return false;
  }
  
#ifdef _WIN32
  // Register HTML format if not already registered
  static UINT cf_html = RegisterClipboardFormatA("HTML Format");
  
  if (!OpenClipboard(nullptr)) {
    return false;
  }
  
  EmptyClipboard();
  
  HGLOBAL hglb = GlobalAlloc(GMEM_MOVEABLE, html.size() + 1);
  if (hglb == nullptr) {
    CloseClipboard();
    return false;
  }
  
  char* lptstr = static_cast<char*>(GlobalLock(hglb));
  memcpy(lptstr, html.c_str(), html.size() + 1);
  GlobalUnlock(hglb);
  
  SetClipboardData(cf_html, hglb);
  CloseClipboard();
  
  NotifyObservers(ClipboardData(ClipboardFormat::kHTML, html));
  return true;
  
#elif defined(__APPLE__)
  NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
  [pasteboard clearContents];
  
  NSString* ns_string = [NSString stringWithUTF8String:html.c_str()];
  [pasteboard setString:ns_string forType:NSPasteboardTypeHTML];
  
  NotifyObservers(ClipboardData(ClipboardFormat::kHTML, html));
  return true;
  
#else
  // Linux HTML clipboard support via GTK
  GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  GtkTargetList* target_list = gtk_target_list_new(nullptr, 0);
  gtk_target_list_add_text_targets(target_list, 0);
  
  GtkSelectionData* selection_data = gtk_selection_data_new(
      GDK_SELECTION_CLIPBOARD,
      gdk_atom_intern("text/html", FALSE),
      8,
      (const guchar*)html.c_str(),
      html.size());
  
  gtk_clipboard_set_with_data(clipboard,
                                target_list->table,
                                target_list->n_targets,
                                [](GtkClipboard*, GtkSelectionData* data, guint, gpointer user_data) {
                                  const std::string* html_str = static_cast<std::string*>(user_data);
                                  gtk_selection_data_set_text(data, html_str->c_str(), html_str->size());
                                },
                                [](gpointer user_data) {
                                  delete static_cast<std::string*>(user_data);
                                },
                                new std::string(html));
  
  gtk_target_list_unref(target_list);
  gtk_selection_data_free(selection_data);
  
  NotifyObservers(ClipboardData(ClipboardFormat::kHTML, html));
  return true;
#endif
}

bool Clipboard::GetHTML(std::string* html) {
  if (!initialized_ || html == nullptr) {
    return false;
  }
  
#ifdef _WIN32
  static UINT cf_html = RegisterClipboardFormatA("HTML Format");
  
  if (!IsClipboardFormatAvailable(cf_html)) {
    return false;
  }
  
  if (!OpenClipboard(nullptr)) {
    return false;
  }
  
  HGLOBAL hglb = GetClipboardData(cf_html);
  if (hglb == nullptr) {
    CloseClipboard();
    return false;
  }
  
  char* lptstr = static_cast<char*>(GlobalLock(hglb));
  if (lptstr != nullptr) {
    *html = lptstr;
    GlobalUnlock(hglb);
  }
  
  CloseClipboard();
  return lptstr != nullptr;
  
#elif defined(__APPLE__)
  NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
  NSString* ns_string = [pasteboard stringForType:NSPasteboardTypeHTML];
  
  if (ns_string != nil) {
    *html = [ns_string UTF8String];
    return true;
  }
  return false;
  
#else
  GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  // GTK doesn't provide direct HTML access, would need custom implementation
  return false;
#endif
}

bool Clipboard::SetImage(const std::vector<uint8_t>& image_data, const std::string& mime_type) {
  if (!initialized_) {
    return false;
  }
  
#ifdef _WIN32
  if (!OpenClipboard(nullptr)) {
    return false;
  }
  
  EmptyClipboard();
  
  // Create a DIB from image data (simplified - assumes PNG)
  HGLOBAL hglb = GlobalAlloc(GMEM_MOVEABLE, image_data.size());
  if (hglb == nullptr) {
    CloseClipboard();
    return false;
  }
  
  void* data = GlobalLock(hglb);
  memcpy(data, image_data.data(), image_data.size());
  GlobalUnlock(hglb);
  
  SetClipboardData(CF_DIB, hglb);
  CloseClipboard();
  
  ClipboardData data(ClipboardFormat::kImage, "");
  data.mime_type = mime_type;
  NotifyObservers(data);
  return true;
  
#elif defined(__APPLE__)
  NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
  [pasteboard clearContents];
  
  NSData* ns_data = [NSData dataWithBytes:image_data.data() length:image_data.size()];
  [pasteboard setData:ns_data forType:NSPasteboardTypePNG];
  
  ClipboardData data(ClipboardFormat::kImage, "");
  data.mime_type = mime_type;
  NotifyObservers(data);
  return true;
  
#else
  // Linux image clipboard support
  GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  GdkPixbuf* pixbuf = gdk_pixbuf_new_from_data(
      image_data.data(),
      GDK_COLORSPACE_RGB,
      FALSE,
      8,
      100,  // width (would need to parse image)
      100,  // height
      300,  // rowstride
      nullptr,
      nullptr);
  
  if (pixbuf) {
    gtk_clipboard_set_image(clipboard, pixbuf);
    g_object_unref(pixbuf);
    
    ClipboardData data(ClipboardFormat::kImage, "");
    data.mime_type = mime_type;
    NotifyObservers(data);
    return true;
  }
  return false;
#endif
}

bool Clipboard::GetImage(std::vector<uint8_t>* image_data, std::string* mime_type) {
  if (!initialized_ || image_data == nullptr) {
    return false;
  }
  
#ifdef _WIN32
  if (!IsClipboardFormatAvailable(CF_DIB)) {
    return false;
  }
  
  if (!OpenClipboard(nullptr)) {
    return false;
  }
  
  HGLOBAL hglb = GetClipboardData(CF_DIB);
  if (hglb == nullptr) {
    CloseClipboard();
    return false;
  }
  
  SIZE_T size = GlobalSize(hglb);
  void* data = GlobalLock(hglb);
  if (data != nullptr) {
    image_data->resize(size);
    memcpy(image_data->data(), data, size);
    GlobalUnlock(hglb);
  }
  
  CloseClipboard();
  if (mime_type) *mime_type = "image/bmp";
  return data != nullptr;
  
#elif defined(__APPLE__)
  NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
  NSData* ns_data = [pasteboard dataForType:NSPasteboardTypePNG];
  
  if (ns_data != nil) {
    image_data->resize([ns_data length]);
    memcpy(image_data->data(), [ns_data bytes], [ns_data length]);
    if (mime_type) *mime_type = "image/png";
    return true;
  }
  return false;
  
#else
  GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  GdkPixbuf* pixbuf = gtk_clipboard_wait_for_image(clipboard);
  
  if (pixbuf) {
    // Convert pixbuf to bytes (simplified)
    g_object_unref(pixbuf);
    if (mime_type) *mime_type = "image/png";
    return true;
  }
  return false;
#endif
}

bool Clipboard::SetFiles(const std::vector<std::string>& file_paths) {
  if (!initialized_ || file_paths.empty()) {
    return false;
  }
  
#ifdef _WIN32
  if (!OpenClipboard(nullptr)) {
    return false;
  }
  
  EmptyClipboard();
  
  // Create HDROP format for file list
  size_t total_size = sizeof(DROPFILES);
  for (const auto& path : file_paths) {
    total_size += path.size() + 1;
  }
  total_size += 1;  // Final null terminator
  
  HGLOBAL hglb = GlobalAlloc(GMEM_MOVEABLE, total_size);
  if (hglb == nullptr) {
    CloseClipboard();
    return false;
  }
  
  DROPFILES* drop_files = static_cast<DROPFILES*>(GlobalLock(hglb));
  drop_files->pFiles = sizeof(DROPFILES);
  drop_files->pt = {0, 0};
  drop_files->fNC = FALSE;
  drop_files->fWide = TRUE;
  
  char* file_list = reinterpret_cast<char*>(drop_files) + sizeof(DROPFILES);
  for (const auto& path : file_paths) {
    memcpy(file_list, path.c_str(), path.size() + 1);
    file_list += path.size() + 1;
  }
  *file_list = '\0';
  
  GlobalUnlock(hglb);
  SetClipboardData(CF_HDROP, hglb);
  CloseClipboard();
  
  return true;
  
#elif defined(__APPLE__)
  NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
  [pasteboard clearContents];
  
  NSMutableArray* file_urls = [NSMutableArray array];
  for (const auto& path : file_paths) {
    NSString* ns_path = [NSString stringWithUTF8String:path.c_str()];
    NSURL* url = [NSURL fileURLWithPath:ns_path];
    [file_urls addObject:url];
  }
  
  [pasteboard writeObjects:file_urls];
  return true;
  
#else
  // Linux file clipboard via GTK
  GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  gchar* uri_list = g_strjoinv("\n", const_cast<gchar**>(file_paths.data()));
  gtk_clipboard_set_text(clipboard, uri_list, -1);
  g_free(uri_list);
  return true;
#endif
}

bool Clipboard::GetFiles(std::vector<std::string>* file_paths) {
  if (!initialized_ || file_paths == nullptr) {
    return false;
  }
  
#ifdef _WIN32
  if (!IsClipboardFormatAvailable(CF_HDROP)) {
    return false;
  }
  
  if (!OpenClipboard(nullptr)) {
    return false;
  }
  
  HDROP hdrop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
  if (hdrop == nullptr) {
    CloseClipboard();
    return false;
  }
  
  UINT file_count = DragQueryFile(hdrop, 0xFFFFFFFF, nullptr, 0);
  file_paths->reserve(file_count);
  
  for (UINT i = 0; i < file_count; ++i) {
    UINT length = DragQueryFile(hdrop, i, nullptr, 0);
    std::string path(length, '\0');
    DragQueryFile(hdrop, i, &path[0], length + 1);
    file_paths->push_back(path);
  }
  
  CloseClipboard();
  return true;
  
#elif defined(__APPLE__)
  NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
  NSArray* file_urls = [pasteboard readObjectsForClasses:@[[NSURL class]] options:nil];
  
  if (file_urls != nil) {
    for (NSURL* url in file_urls) {
      if ([url isFileURL]) {
        file_paths->push_back([[url path] UTF8String]);
      }
    }
    return !file_paths->empty();
  }
  return false;
  
#else
  GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  gchar* uri_list = gtk_clipboard_wait_for_text(clipboard);
  
  if (uri_list != nullptr) {
    // Parse URI list (simplified)
    gchar** uris = g_strsplit(uri_list, "\n", -1);
    for (int i = 0; uris[i] != nullptr; ++i) {
      if (strlen(uris[i]) > 0) {
        file_paths->push_back(uris[i]);
      }
    }
    g_strfreev(uris);
    g_free(uri_list);
    return !file_paths->empty();
  }
  return false;
#endif
}

bool Clipboard::SetCustom(const std::string& data, const std::string& mime_type) {
  if (!initialized_) {
    return false;
  }
  
  ClipboardData clip_data(ClipboardFormat::kCustom, data);
  clip_data.mime_type = mime_type;
  
#ifdef _WIN32
  UINT format = RegisterClipboardFormatA(mime_type.c_str());
  
  if (!OpenClipboard(nullptr)) {
    return false;
  }
  
  EmptyClipboard();
  
  HGLOBAL hglb = GlobalAlloc(GMEM_MOVEABLE, data.size() + 1);
  if (hglb == nullptr) {
    CloseClipboard();
    return false;
  }
  
  char* lptstr = static_cast<char*>(GlobalLock(hglb));
  memcpy(lptstr, data.c_str(), data.size() + 1);
  GlobalUnlock(hglb);
  
  SetClipboardData(format, hglb);
  CloseClipboard();
  
  NotifyObservers(clip_data);
  return true;
  
#else
  // Custom format support would be platform-specific
  // For now, store as text
  return SetText(data);
#endif
}

bool Clipboard::GetCustom(const std::string& mime_type, std::string* data) {
  if (!initialized_ || data == nullptr) {
    return false;
  }
  
#ifdef _WIN32
  UINT format = RegisterClipboardFormatA(mime_type.c_str());
  
  if (!IsClipboardFormatAvailable(format)) {
    return false;
  }
  
  if (!OpenClipboard(nullptr)) {
    return false;
  }
  
  HGLOBAL hglb = GetClipboardData(format);
  if (hglb == nullptr) {
    CloseClipboard();
    return false;
  }
  
  char* lptstr = static_cast<char*>(GlobalLock(hglb));
  if (lptstr != nullptr) {
    *data = lptstr;
    GlobalUnlock(hglb);
  }
  
  CloseClipboard();
  return lptstr != nullptr;
  
#else
  // Fall back to text for custom formats on non-Windows
  return GetText(data);
#endif
}

bool Clipboard::HasFormat(ClipboardFormat format) {
  if (!initialized_) {
    return false;
  }
  
#ifdef _WIN32
  UINT cf_format = 0;
  switch (format) {
    case ClipboardFormat::kText:
      cf_format = CF_TEXT;
      break;
    case ClipboardFormat::kHTML:
      cf_format = RegisterClipboardFormatA("HTML Format");
      break;
    case ClipboardFormat::kImage:
      cf_format = CF_DIB;
      break;
    case ClipboardFormat::kFiles:
      cf_format = CF_HDROP;
      break;
    default:
      return false;
  }
  return IsClipboardFormatAvailable(cf_format) != FALSE;
  
#elif defined(__APPLE__)
  NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
  NSArray* types = [pasteboard types];
  
  NSString* target_type = nil;
  switch (format) {
    case ClipboardFormat::kText:
      target_type = NSPasteboardTypeString;
      break;
    case ClipboardFormat::kHTML:
      target_type = NSPasteboardTypeHTML;
      break;
    case ClipboardFormat::kImage:
      target_type = NSPasteboardTypePNG;
      break;
    default:
      return false;
  }
  
  return [types containsObject:target_type];
  
#else
  GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  switch (format) {
    case ClipboardFormat::kText:
      return gtk_clipboard_wait_is_text_available(clipboard);
    case ClipboardFormat::kImage:
      return gtk_clipboard_wait_is_image_available(clipboard);
    default:
      return false;
  }
#endif
}

bool Clipboard::Clear() {
  if (!initialized_) {
    return false;
  }
  
#ifdef _WIN32
  if (OpenClipboard(nullptr)) {
    EmptyClipboard();
    CloseClipboard();
    return true;
  }
  return false;
  
#elif defined(__APPLE__)
  NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
  [pasteboard clearContents];
  return true;
  
#else
  GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  gtk_clipboard_clear(clipboard);
  return true;
#endif
}

void Clipboard::AddObserver(ClipboardObserver observer) {
  observers_.push_back(observer);
}

void Clipboard::RemoveObserver(ClipboardObserver observer) {
  auto it = std::remove(observers_.begin(), observers_.end(), observer);
  observers_.erase(it, observers_.end());
}

std::vector<ClipboardFormat> Clipboard::GetAvailableFormats() {
  std::vector<ClipboardFormat> formats;
  
  if (!initialized_) {
    return formats;
  }
  
  if (HasFormat(ClipboardFormat::kText)) {
    formats.push_back(ClipboardFormat::kText);
  }
  if (HasFormat(ClipboardFormat::kHTML)) {
    formats.push_back(ClipboardFormat::kHTML);
  }
  if (HasFormat(ClipboardFormat::kImage)) {
    formats.push_back(ClipboardFormat::kImage);
  }
  if (HasFormat(ClipboardFormat::kFiles)) {
    formats.push_back(ClipboardFormat::kFiles);
  }
  
  return formats;
}

std::vector<ClipboardData> Clipboard::GetHistory() {
  // History is platform-specific and often not accessible
  // Return empty for now
  return std::vector<ClipboardData>();
}

void Clipboard::SetTextAsync(const std::string& text, std::function<void(bool)> callback) {
  if (thread_pool_) {
    thread_pool_->Enqueue([this, text, callback]() {
      bool success = SetText(text);
      callback(success);
    });
  } else {
    callback(SetText(text));
  }
}

void Clipboard::GetTextAsync(std::function<void(bool, const std::string&)> callback) {
  if (thread_pool_) {
    thread_pool_->Enqueue([this, callback]() {
      std::string text;
      bool success = GetText(&text);
      callback(success, text);
    });
  } else {
    std::string text;
    bool success = GetText(&text);
    callback(success, text);
  }
}

void Clipboard::NotifyObservers(const ClipboardData& data) {
  for (const auto& observer : observers_) {
    observer(data);
  }
}

}  // namespace content::clipboard
