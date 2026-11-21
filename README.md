# Float Browser 🌊🍾

**Float** is an ultra-lightweight, high-performance web browser written in native **C++** using the Windows API and WebView2 Native COM SDK.

It is designed to give you the speed and visual aesthetics of Google Chrome and Microsoft Edge, while consuming **only ~150 MB of RAM** (compared to ~1,000 MB on Chrome/Edge), saving more than **85%** of system resources!

---

## ✨ Features
- **Ultra-Lightweight**: Written in pure C++ with zero bloat and minimal background processes.
- **Chrome / Edge Modern UI**: Rounded tab strips, animated tabs, capsule address bar (Omnibox), and crisp SVG icons.
- **Built-in Instant Translation**: Google Translate injection directly on any webpage at the click of a button.
- **Dedicated History Page**: Clean Chrome-styled `Float History` page to search, manage, and clear browsing records.
- **Custom App Identity**: Includes high-resolution bottle-in-the-sea app icons (`.ico` and `.png`).
- **Resource Benchmark Tool**: Comes with `compare_resources.ps1` to compare Float's RAM usage with Chrome, Firefox, and Edge in real-time.

---

## 🚀 How to Build & Run

### Prerequisites
- Windows 10 / 11
- MinGW-w64 (GCC with C++17 support)
- Microsoft Edge WebView2 Runtime (pre-installed on Windows 10/11)

### Build
Run `build.bat` in the project directory:
```cmd
build.bat
```
This compiles `Float.exe` statically with all Windows resources and links `WebView2Loader.dll`.

---

## 📂 Project Structure
- `main.cpp`: Core C++ Win32 application and WebView2 controller.
- `chrome_ui.html`: Chrome-inspired navigation toolbar and tab strip.
- `history.html`: Dedicated browsing history dashboard.
- `resource.rc` / `manifest.xml`: Windows icon and DPI-awareness manifest.
- `compare_resources.ps1`: Real-time browser RAM benchmark script.
- `build.bat`: One-click compile script.
