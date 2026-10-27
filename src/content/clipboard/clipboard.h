/**
 * This file is part of XER, Clipboard subsystem.
 * Copyright (C) 2026 XER Runtime Team
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "async_io/io_thread_pool.h"

namespace content::clipboard {

/**
 * ClipboardFormat defines the type of data stored in clipboard.
 */
enum class ClipboardFormat {
  kText,           // Plain text (UTF-8)
  kHTML,           // HTML formatted text
  kRTF,            // Rich Text Format
  kImage,          // Image data (PNG, JPEG, etc.)
  kFiles,          // File paths
  kCustom          // Custom format
};

/**
 * ClipboardData represents a single clipboard item with format and content.
 */
struct ClipboardData {
  ClipboardFormat format;
  std::string content;
  std::string mime_type;  // For custom formats
  
  ClipboardData() : format(ClipboardFormat::kText) {}
  explicit ClipboardData(ClipboardFormat fmt, const std::string& data)
      : format(fmt), content(data) {}
};

/**
 * ClipboardObserver is a callback interface for clipboard change notifications.
 */
using ClipboardObserver = std::function<void(const ClipboardData&)>;

/**
 * Clipboard provides cross-platform clipboard operations.
 * Integrates with XER's async infrastructure for non-blocking operations.
 */
class Clipboard {
public:
  static Clipboard& Instance();
  
  // Prevent copying
  Clipboard(const Clipboard&) = delete;
  Clipboard& operator=(const Clipboard&) = delete;
  
  /**
   * Initialize the clipboard subsystem.
   * Must be called before any other operations.
   */
  bool Initialize();
  
  /**
   * Shutdown the clipboard subsystem.
   */
  void Shutdown();
  
  /**
   * Check if clipboard is initialized.
   */
  bool IsInitialized() const { return initialized_; }
  
  /**
   * Set clipboard content with specified format.
   * @param data The clipboard data to set
   * @return true on success
   */
  bool SetText(const std::string& text);
  bool SetHTML(const std::string& html);
  bool SetImage(const std::vector<uint8_t>& image_data, const std::string& mime_type);
  bool SetFiles(const std::vector<std::string>& file_paths);
  bool SetCustom(const std::string& data, const std::string& mime_type);
  
  /**
   * Get clipboard content.
   * @param format The format to retrieve
   * @param data Output parameter for the retrieved data
   * @return true on success
   */
  bool GetText(std::string* text);
  bool GetHTML(std::string* html);
  bool GetImage(std::vector<uint8_t>* image_data, std::string* mime_type);
  bool GetFiles(std::vector<std::string>* file_paths);
  bool GetCustom(const std::string& mime_type, std::string* data);
  
  /**
   * Check if clipboard has data of a specific format.
   */
  bool HasFormat(ClipboardFormat format);
  
  /**
   * Clear the clipboard.
   */
  bool Clear();
  
  /**
   * Register an observer for clipboard changes.
   * The observer will be called whenever clipboard content changes.
   */
  void AddObserver(ClipboardObserver observer);
  
  /**
   * Remove a previously registered observer.
   */
  void RemoveObserver(ClipboardObserver observer);
  
  /**
   * Get available formats in clipboard.
   */
  std::vector<ClipboardFormat> GetAvailableFormats();
  
  /**
   * Get clipboard history (if supported by platform).
   */
  std::vector<ClipboardData> GetHistory();
  
  /**
   * Async operations using thread pool.
   */
  void SetTextAsync(const std::string& text, std::function<void(bool)> callback);
  void GetTextAsync(std::function<void(bool, const std::string&)> callback);

private:
  Clipboard() = default;
  ~Clipboard() = default;
  
  void NotifyObservers(const ClipboardData& data);
  
  bool initialized_ = false;
  std::vector<ClipboardObserver> observers_;
  AsyncIO::IO::IOThreadPool* thread_pool_ = nullptr;
};

}  // namespace content::clipboard
