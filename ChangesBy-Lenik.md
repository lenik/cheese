# Changes by Lenik (pub.lenik@bodz.net)

This document summarizes all modifications made by Lenik to the Cheese project in the past year (March 2025 - April 2025).

## Overview

Lenik contributed 19+ commits between March 11, 2025 and April 2025, adding significant new features to support a "mini UI mode" for Cheese, along with auto-shooting capabilities, cursor following functionality, window resizing improvements with aspect ratio preservation, and various improvements and bug fixes.

## Major Features Added

### 1. Mini UI Mode Support

**Commit:** `5fa40d18` (March 11, 2025)

Added support for a minimal UI mode with the following command-line options:
- `--borderless` / `-b`: Start in borderless mode (no window decorations)
- `--noactionbar` / `-n`: Start with action bar (buttons) toggled off
- `--nothumbnails` / `-m`: Start with thumbnails view toggled off

**Files Modified:**
- `data/headerbar.ui`: Added UI definitions for borderless mode
- `data/shortcuts.ui`: Added keyboard shortcuts documentation
- `src/cheese-application.vala`: Added command-line option handling and action entries
- `src/cheese-window.vala`: Implemented borderless, actionbar, and thumbnails visibility controls

**Keyboard Shortcuts Added:**
- `F4`: Toggle borderless mode
- `F5`: Toggle action bar visibility
- `F6`: Toggle thumbnails visibility

### 2. Window Cursor Following Feature

**Commits:** 
- `d27528ef` (March 15, 2025): Initial implementation
- `0dadd709` (March 27, 2025): Monitor geometry restriction
- `b3b1c214` (April 1, 2025): Runtime interval adjustment

Added the ability to make the window position follow the cursor on the screen, useful for mini-UI mode where the window can be resized very small (e.g., 64x48 pixels).

**Command-line Options:**
- `--follow` / `-c`: Enable cursor following
- `--follow-interval` / `-C INTERVAL`: Adjust refresh interval (default: 1000ms)
- `--follow-dx` / `-x PIXELS`: X-offset of window top-left to cursor (default: 10)
- `--follow-dy` / `-y PIXELS`: Y-offset of window top-left to cursor (default: 10)

**Runtime Controls:**
- Keyboard accelerators `<`, `>`, `=` to adjust following speed at runtime
- The window is restricted to stay within the current monitor geometry
- Window position updates only when cursor is outside the window

**Files Modified:**
- `src/cheese-application.vala`: Implemented cursor following logic with timer-based updates

### 3. Auto-Shoot Feature

**Commits:**
- `fb6991b8` (March 17, 2025): Initial implementation
- `fddafca5` (March 17, 2025): Directory creation with parents
- `1d32d378` (March 20, 2025): Error checking for directory creation

Added automatic photo capture functionality that takes photos repeatedly at specified intervals.

**Command-line Options:**
- `--auto` / `-a`: Enable auto-shoot mode
- `--save-dir` / `-o PATH`: Specify directory to save captured photos
- `--shoot-interval` / `-i INTERVAL`: Specify shoot interval in milliseconds (default: 1000ms)

**Features:**
- Automatically creates save directory with parent directories if they don't exist
- Proper error handling if directory creation fails
- Uses current user settings for flash and sound preferences

**Files Modified:**
- `src/cheese-application.vala`: Added auto-shoot timer and logic
- `src/cheese-window.vala`: Integrated with photo capture system

### 4. Window Size Configuration

**Commit:** `23b5efc6` (March 17, 2025)

Added ability to specify initial window size via command-line.

**Command-line Options:**
- `--width` / `-W WIDTH`: Specify window width in pixels
- `--height` / `-H HEIGHT`: Specify window height in pixels

**Files Modified:**
- `src/cheese-application.vala`: Added option parsing and window resizing

### 5. Topmost Window Option

**Commit:** `b50bbd65` (March 12, 2025)

Added option to keep the main window always on top.

**Command-line Options:**
- `--topmost` / `-t`: Keep the main window above other windows

**Files Modified:**
- `src/cheese-application.vala`: Added option handling
- `src/cheese-window.vala`: Implemented `set_keep_above()` call

### 6. Borderless UI Enhancements

**Commits:**
- `f0355241` (March 15, 2025): Window movement and resizing
- `2c79ddf0` (March 15, 2025): Keyboard shortcuts for effects
- `50ab0bb2` (March 31, 2025): Minimum size enforcement
- `02f9af64` (April 2025): Aspect ratio preservation during resize
- `8947c044` (April 2025): Debug rectangle removal
- `d5651f2a` (April 2025): Debug print removal

**Mouse Controls:**
- Left mouse button: Move the window
- Right mouse button: Resize the window from any corner (with minimum width/height enforcement)
  - Corner-based resizing: Window is divided into 4 quadrants for corner detection
  - Aspect ratio preservation: Window automatically adjusts to maintain camera's aspect ratio
  - Gap-free preview: Window size is patched to remove gaps around the preview frame

**Keyboard Shortcuts:**
- `h`: Select Flip effect
- `v`: Select Flip-Vertical effect

**Features:**
- **Aspect Ratio Preservation**: When resizing in borderless mode, the window automatically adjusts to maintain the camera's aspect ratio, ensuring the preview fills the available space without gaps
- **Startup Gap Removal**: Window size is automatically patched on startup in borderless mode to remove initial gaps
- **Corner-based Resizing**: Simplified resizing to use only corners (top-left, top-right, bottom-left, bottom-right) for more predictable behavior
- **UI Element Awareness**: Resize calculations account for header bar, action bar, and thumbnails to ensure accurate viewport sizing

**Files Modified:**
- `src/cheese-window.vala`: Implemented mouse event handlers for window manipulation, aspect ratio preservation, and startup patching

## Library Enhancements

### File Utility Functions

**Commit:** `a2271de2` (March 17, 2025)

Added setter functions to allow changing photo and video save paths at runtime.

**New Functions:**
- `cheese_fileutil_set_photo_path()`: Set the photo save directory
- `cheese_fileutil_set_video_path()`: Set the video save directory

**Files Modified:**
- `libcheese/cheese-fileutil.c`: Implemented setter functions
- `libcheese/cheese-fileutil.h`: Added function declarations
- `src/vapi/cheese-common.vapi`: Added Vala bindings

## Bug Fixes

### 1. Command-line Option Checking

**Commit:** `b59d5e5b` (March 17, 2025)

Fixed incorrect checking of command-line options. The `options_dict` only contains options without arguments, so options with arguments need to be checked differently.

**Files Modified:**
- `src/cheese-application.vala`: Fixed option presence checking logic

### 2. Command-line Help Text

**Commit:** `acb501b3` (March 12, 2025)

Fixed and improved command-line help text descriptions.

**Files Modified:**
- `src/cheese-application.vala`: Updated help text strings

### 3. Incompatible Pointer Types Warning

**Commit:** `81f2ee02` (March 11, 2025)

Fixed compiler warning about incompatible pointer types in flash handling code.

**Files Modified:**
- `libcheese/cheese-flash.c`: Added proper type cast to `GTK_WIDGET`

## Documentation

### Author Attribution

**Commit:** `48a5ba94` (March 17, 2025)

Added Lenik to the AUTHORS file.

**Files Modified:**
- `AUTHORS`: Added "Lenik Xie <lenik@bodz.net>" to contributors list

## Technical Details

### Implementation Highlights

1. **Timer-based Updates**: Both cursor following and auto-shoot features use GLib's `Timeout.add()` for periodic updates
2. **Monitor Geometry Awareness**: Cursor following respects monitor boundaries to prevent windows from moving off-screen
3. **Directory Management**: Auto-shoot feature automatically creates save directories with parent directories as needed
4. **State Management**: All new features integrate with Cheese's existing action system for consistent UI behavior

### Code Statistics

- **Total Commits**: 19+
- **Files Modified**: 
  - `src/cheese-application.vala`: Major additions (cursor following, auto-shoot, mini-UI options)
  - `src/cheese-window.vala`: Window manipulation, UI controls, and aspect ratio preservation
  - `libcheese/cheese-fileutil.c/h`: File utility enhancements
  - `data/shortcuts.ui`: Keyboard shortcuts documentation
  - `data/headerbar.ui`: UI definitions
  - `AUTHORS`: Contributor attribution

## Use Cases

These modifications enable several new use cases:

1. **Minimal Overlay Mode**: Run Cheese in a small, borderless window that follows the cursor, perfect for always-visible webcam monitoring
2. **Automated Photo Capture**: Set up automatic photo capture for time-lapse or surveillance applications
3. **Custom Window Sizes**: Launch Cheese with specific dimensions for integration into custom workflows
4. **Always-on-Top Monitoring**: Keep the webcam window visible above all other applications

## Future Considerations

The implementation provides a solid foundation for:
- Additional window positioning modes
- More sophisticated auto-shoot scheduling
- Enhanced mini-UI customization options
- Integration with external automation tools

---

**Document Generated**: Based on git log analysis of commits from March 11, 2025 to April 2025  
**Author**: Lenik <pub.lenik@bodz.net>  
**Total Contribution Period**: ~4+ weeks of active development

