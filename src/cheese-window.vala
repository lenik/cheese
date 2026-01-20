/*
 * Copyright © 2010 Yuvaraj Pandian T <yuvipanda@yuvi.in>
 * Copyright © 2010 daniel g. siegel <dgsiegel@gnome.org>
 * Copyright © 2008 Filippo Argiolas <filippo.argiolas@gmail.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

using Gtk;
using Gdk;
using GtkClutter;
using Clutter;
using Config;
using Eog;
using Gst;
using CanberraGtk;

const int FULLSCREEN_TIMEOUT_INTERVAL = 5 * 1000;
const uint EFFECTS_PER_PAGE = 9;

[GtkTemplate (ui = "/org/gnome/Cheese/cheese-main-window.ui")]
public class Cheese.MainWindow : Gtk.ApplicationWindow
{
    private const GLib.ActionEntry actions[] = {
        { "file-open", on_file_open },
        { "file-saveas", on_file_saveas },
        { "file-trash", on_file_trash },
        { "file-delete", on_file_delete },
        { "effects-next", on_effects_next },
        { "effects-previous", on_effects_previous }
    };

    private MediaMode current_mode;

    private Clutter.Script clutter_builder;

    private Gtk.Builder header_bar_ui = new Gtk.Builder.from_resource ("/org/gnome/Cheese/headerbar.ui");

    private Gtk.HeaderBar header_bar;

    private GLib.Settings settings;

    [GtkChild]
    private unowned GtkClutter.Embed viewport_widget;
    [GtkChild]
    private unowned Gtk.Widget main_vbox;
    private Eog.ThumbNav thumb_nav;
    private Cheese.ThumbView thumb_view;
    [GtkChild]
    private unowned Gtk.Box thumbnails_right;
    [GtkChild]
    private unowned Gtk.Box thumbnails_bottom;
    [GtkChild]
    private unowned Gtk.Widget leave_fullscreen_button_box;
    [GtkChild]
    private unowned Gtk.Button take_action_button;
    [GtkChild]
    private unowned Gtk.Image take_action_button_image;
    [GtkChild]
    private unowned Gtk.ToggleButton effects_toggle_button;
    [GtkChild]
    private unowned Gtk.Widget buttons_area;
    [GtkChild]
    private unowned Gtk.Button switch_camera_button;
    private Gtk.Menu thumbnail_popup;

    private Clutter.Stage viewport;
    private Clutter.Actor viewport_layout;
    private Clutter.Actor video_preview;
    private Clutter.BinLayout viewport_layout_manager;
    private Clutter.Text countdown_layer;
    private Clutter.Actor background_layer;
    private Clutter.Text error_layer;
    private Clutter.Text timeout_layer;
    private Clutter.Actor auto_shoot_indicator;

  private Clutter.Actor current_effects_grid;
  private uint current_effects_page = 0;
  private List<Clutter.Actor> effects_grids;

  private bool is_fullscreen;
  private bool is_borderless;
  private bool is_actionbar_visible = true;
  private bool is_thumbnailsbar_visible = true;
  private bool is_wide_mode;
  private bool is_recording;       /* Video Recording Flag */
  private bool is_bursting;
  private bool is_effects_selector_active;
  private bool action_cancelled;
    private bool was_maximized;
    private bool was_keep_above;

  private Cheese.Camera   camera;
  private Cheese.FileUtil fileutil;
  private Cheese.Flash    flash;

  private Cheese.EffectsManager    effects_manager;

  private Cheese.Effect selected_effect;

  private bool button_down = false;
  private bool button_move_or_resize;
  private double button_down_x;
  private double button_down_y;
  private int button_down_width;
  private int button_down_height;
  private int button_down_left;
  private int button_down_top;
  private int resize_edge; // 0=move, 1=top-left, 2=top-right, 3=bottom-left, 4=bottom-right (corners only)

  /**
   * Responses from the delete files confirmation dialog.
   *
   * @param SKIP skip a single file
   * @param SKIP_ALL skill all following files
   */
  enum DeleteResponse
  {
    SKIP = 1,
    SKIP_ALL = 2
  }

    public MainWindow (Gtk.Application application)
    {
        GLib.Object (application: application);

        header_bar = header_bar_ui.get_object ("header_bar") as Gtk.HeaderBar;
        header_bar.visible = true;
        this.set_titlebar (header_bar);
    }

    private void set_window_title (string title)
    {
        header_bar.set_title (title);
        this.set_title (title);
    }

    private double get_opacity ()
    {
      return this.opacity;
    }
  
    private void set_opacity (double opacity)
    {
      // Clamp opacity to valid range
      opacity = opacity.clamp (0.01, 1.0);
  
      // Set window opacity
      this.opacity = opacity;
  
      // Set opacity on Clutter actors
      uint8 clutter_opacity = (uint8)(opacity * 255.0);
      if (viewport != null) {
        viewport.opacity = clutter_opacity;
      }
      if (background_layer != null) {
        background_layer.opacity = clutter_opacity;
      }
      if (video_preview != null) {
        video_preview.opacity = clutter_opacity;
      }
      if (auto_shoot_indicator != null) {
        // Highlight the indicator based on the window opacity
        // Use sqrt(opacity) to create a softer highlight effect
        uint8 highlight = (uint8) (Math.sqrt(opacity) * 255.0);
        auto_shoot_indicator.opacity = highlight;
      }
      // Save to settings
      settings.set_double ("opacity", opacity);
    }
  
    private bool on_window_state_change_event (Gtk.Widget widget,
                                               Gdk.EventWindowState event)
    {
        was_maximized = (((event.new_window_state - event.changed_mask)
                          & Gdk.WindowState.MAXIMIZED) != 0);
        was_keep_above = (((event.new_window_state - event.changed_mask)
                          & Gdk.WindowState.ABOVE) != 0);

        window_state_event.disconnect (on_window_state_change_event);
        return false;
    }

    private void do_thumb_view_popup_menu ()
    {
        thumbnail_popup.popup_at_pointer (null);
    }

    private bool on_thumb_view_popup_menu ()
    {
        do_thumb_view_popup_menu ();

        return true;
    }

    /**
    * Popup a context menu when right-clicking on a thumbnail.
    *
    * @param iconview the thumbnail view that emitted the signal
    * @param event the event
    * @return false to allow further processing of the event, true to indicate
    * that the event was handled completely
    */
    public bool on_thumbnail_button_press_event (Gtk.Widget iconview,
                                                 Gdk.EventButton event)
    {
        Gtk.TreePath path;
        path = thumb_view.get_path_at_pos ((int) event.x, (int) event.y);

        if (path == null)
        {
            return false;
        }

        if (!thumb_view.path_is_selected (path))
        {
            thumb_view.unselect_all ();
            thumb_view.select_path (path);
            thumb_view.set_cursor (path, null, false);
        }

        if (event.type == Gdk.EventType.BUTTON_PRESS)
        {
            Gdk.Event* button_press = (Gdk.Event*)(event);

            if (button_press->triggers_context_menu ())
            {
                do_thumb_view_popup_menu ();
                return true;
            }
        }
        else if (event.type == Gdk.EventType.2BUTTON_PRESS)
        {
            on_file_open ();
            return true;
        }

        return false;
    }

  /**
   * Open an image associated with a thumbnail in the default application.
   */
  private void on_file_open ()
  {
    string filename, uri;

    Gdk.Screen screen;
    filename = thumb_view.get_selected_image ();

    if (filename == null)
      return;                     /* Nothing selected. */

    try
    {
      uri    = GLib.Filename.to_uri (filename);
      screen = this.get_screen ();
      Gtk.show_uri (screen, uri, Gtk.get_current_event_time ());
    }
    catch (Error err)
    {
      MessageDialog error_dialog = new MessageDialog (this,
                                                      Gtk.DialogFlags.MODAL |
                                                      Gtk.DialogFlags.DESTROY_WITH_PARENT,
                                                      Gtk.MessageType.ERROR,
                                                      Gtk.ButtonsType.OK,
                                                      _("Could not open %s"),
                                                      filename);

      error_dialog.run ();
      error_dialog.destroy ();
    }
  }

  /**
   * Delete the requested image or images in the thumbview from storage.
   *
   * A confirmation dialog is shown to the user before deleting any files.
   */
  private void on_file_delete ()
  {
    int response;
    int error_response;
    bool skip_all_errors = false;

    var files = thumb_view.get_selected_images_list ();
    var files_length = files.length ();

    var confirmation_dialog = new MessageDialog.with_markup (this,
      Gtk.DialogFlags.MODAL | Gtk.DialogFlags.DESTROY_WITH_PARENT,
      Gtk.MessageType.WARNING, Gtk.ButtonsType.NONE,
      GLib.ngettext("Are you sure you want to permanently delete the file?",
        "Are you sure you want to permanently delete %d files?",
        files_length), files_length);
    confirmation_dialog.add_button (_("_Cancel"), Gtk.ResponseType.CANCEL);
    confirmation_dialog.add_button (_("_Delete"), Gtk.ResponseType.ACCEPT);
    confirmation_dialog.format_secondary_text ("%s",
      GLib.ngettext("If you delete an item, it will be permanently lost",
        "If you delete the items, they will be permanently lost",
        files_length));

    response = confirmation_dialog.run ();
    if (response == Gtk.ResponseType.ACCEPT)
    {
      foreach (var file in files)
      {
        if (file == null)
          return;

        try
        {
          file.delete (null);
        }
        catch (Error err)
        {
          warning ("Unable to delete file: %s", err.message);

          if (!skip_all_errors) {
            var error_dialog = new MessageDialog (this,
              Gtk.DialogFlags.MODAL | Gtk.DialogFlags.DESTROY_WITH_PARENT,
              Gtk.MessageType.ERROR, Gtk.ButtonsType.NONE,
              _("Could not delete %s"), file.get_path ());

            error_dialog.add_button (_("_Cancel"), Gtk.ResponseType.CANCEL);
            error_dialog.add_button (_("Skip"), DeleteResponse.SKIP);
            error_dialog.add_button (_("Skip all"), DeleteResponse.SKIP_ALL);

            error_response = error_dialog.run ();
            if (error_response == DeleteResponse.SKIP_ALL) {
              skip_all_errors = true;
            } else if (error_response == Gtk.ResponseType.CANCEL) {
              break;
            }

            error_dialog.destroy ();
          }
        }
      }
    }
    confirmation_dialog.destroy ();
  }

  /**
   * Move the requested image in the thumbview to the trash.
   *
   * A confirmation dialog is shown to the user before moving the file.
   */
  private void on_file_trash ()
  {
    File file;

    GLib.List<GLib.File> files = thumb_view.get_selected_images_list ();

    for (int i = 0; i < files.length (); i++)
    {
      file = files<GLib.File>.nth (i).data;
      if (file == null)
        return;

      try
      {
        file.trash (null);
      }
      catch (Error err)
      {
        MessageDialog error_dialog = new MessageDialog (this,
                                                        Gtk.DialogFlags.MODAL |
                                                        Gtk.DialogFlags.DESTROY_WITH_PARENT,
                                                        Gtk.MessageType.ERROR,
                                                        Gtk.ButtonsType.OK,
                                                        _("Could not move %s to trash"),
                                                        file.get_path ());

        error_dialog.run ();
        error_dialog.destroy ();
      }
    }
  }

  /**
   * Save the selected file in the thumbview to an alternate storage location.
   *
   * A file chooser dialog is shown to the user, asking where the file should
   * be saved and the filename.
   */
  private void on_file_saveas ()
  {
    string            filename, basename;
    FileChooserDialog save_as_dialog;
    int               response;

    filename = thumb_view.get_selected_image ();
    if (filename == null)
      return;                    /* Nothing selected. */

    save_as_dialog = new FileChooserDialog (_("Save File"),
                                            this,
                                            Gtk.FileChooserAction.SAVE,
                                            _("_Cancel"), Gtk.ResponseType.CANCEL,
                                            _("Save"), Gtk.ResponseType.ACCEPT,
                                            null);

    save_as_dialog.do_overwrite_confirmation = true;
    basename                                 = GLib.Filename.display_basename (filename);
    save_as_dialog.set_current_name (basename);
    save_as_dialog.set_current_folder (GLib.Environment.get_home_dir ());

    response = save_as_dialog.run ();

    save_as_dialog.hide ();
    if (response == Gtk.ResponseType.ACCEPT)
    {
      string target_filename;
      target_filename = save_as_dialog.get_filename ();

      File src  = File.new_for_path (filename);
      File dest = File.new_for_path (target_filename);

      try
      {
        src.copy (dest, FileCopyFlags.OVERWRITE, null, null);
      }
      catch (Error err)
      {
        MessageDialog error_dialog = new MessageDialog (this,
                                                        Gtk.DialogFlags.MODAL |
                                                        Gtk.DialogFlags.DESTROY_WITH_PARENT,
                                                        Gtk.MessageType.ERROR,
                                                        Gtk.ButtonsType.OK,
                                                        _("Could not save %s"),
                                                        target_filename);

        error_dialog.run ();
        error_dialog.destroy ();
      }
    }
    save_as_dialog.destroy ();
  }

    /**
     * Toggle fullscreen mode.
     *
     * @param fullscreen whether the window should be fullscreen
     */
    public void set_fullscreen (bool fullscreen)
    {
        set_fullscreen_mode (fullscreen);
    }

    /**
     * Toggle borderless mode.
     *
     * @param borderless whether the window border should be visible
     */
     public void set_borderless (bool borderless)
     {
         set_borderless_mode (borderless);
     }

    /**
     * Toggle action bar mode.
     *
     * @param actionbar whether the window should be visible
     */
     public void set_actionbar (bool actionbar)
     {
         set_actionbar_visible (actionbar);
     }

    /**
     * Toggle thumbnails bar mode.
     *
     * @param thumbnailsbar whether the thumbnails bar should be visible
     */
     public void set_thumbnailsbar (bool thumbnailsbar)
     {
         set_thumbnailsbar_visible (thumbnailsbar);
     }
  
    /**
     * Make the media capture mode actions sensitive.
     */
    private void enable_mode_change ()
    {
        var mode = this.application.lookup_action ("mode") as SimpleAction;
        mode.set_enabled (true);
        var effects = this.application.lookup_action ("effects") as SimpleAction;
        effects.set_enabled (true);
        var preferences = this.application.lookup_action ("preferences") as SimpleAction;
        preferences.set_enabled (true);
    }

    /**
     * Make the media capture mode actions insensitive.
     */
    private void disable_mode_change ()
    {
        var mode = this.application.lookup_action ("mode") as SimpleAction;
        mode.set_enabled (false);
        var effects = this.application.lookup_action ("effects") as SimpleAction;
        effects.set_enabled (false);
        var preferences = this.application.lookup_action ("preferences") as SimpleAction;
        preferences.set_enabled (false);
    }

  /**
   * Set the capture resolution, based on the current capture mode.
   *
   * @param mode the current capture mode (photo, video or burst)
   */
  private void set_resolution(MediaMode mode)
  {
    if (camera == null)
      return;

    var formats = camera.get_video_formats ();

    if (formats == null)
      return;
    
    unowned Cheese.VideoFormat format;
    int width = 0;
    int height = 0;

    switch (mode)
    {
      case MediaMode.PHOTO:
      case MediaMode.BURST:
        width  = settings.get_int ("photo-x-resolution");
        height = settings.get_int ("photo-y-resolution");
        break;
      case MediaMode.VIDEO:
        width  = settings.get_int ("video-x-resolution");
        height = settings.get_int ("video-y-resolution");
        break;
    }

    for (int i = 0; i < formats.length (); i++)
    {
      format = formats<VideoFormat>.nth (i).data;
      if (width == format.width && height == format.height)
      {
        camera.set_video_format (format);
        break;
      }
    }
  }

  private TimeoutSource fullscreen_timeout;
  /**
   * Clear the fullscreen activity timeout.
   */
  private void clear_fullscreen_timeout ()
  {
    if (fullscreen_timeout != null)
    {
      fullscreen_timeout.destroy ();
      fullscreen_timeout = null;
    }
  }

  /**
   * Set the fullscreen timeout, for hiding the UI if there is no mouse
   * movement.
   */
  private void set_fullscreen_timeout ()
  {
    fullscreen_timeout = new TimeoutSource (FULLSCREEN_TIMEOUT_INTERVAL);
    fullscreen_timeout.attach (null);
    fullscreen_timeout.set_callback (() => {buttons_area.hide ();
                                            clear_fullscreen_timeout ();
                                            this.fullscreen ();
                                            return true; });
  }

    /**
     * Show the UI in fullscreen if there is any mouse activity.
     *
     * Start a new timeout at the end of every mouse pointer movement. All
     * timeouts will be cancelled, except one created during the last movement
     * event. Show() is called even if the button is not hidden.
     *
     * @param viewport the widget to check for mouse activity on
     * @param e the (unused) event
     */
    private bool fullscreen_motion_notify_callback (Gtk.Widget viewport,
                                                    EventMotion e)
    {
        clear_fullscreen_timeout ();
        this.unfullscreen ();
        this.maximize ();
        buttons_area.show ();
        set_fullscreen_timeout ();
        return true;
    }

  /**
   * Enable or disable fullscreen mode to the requested state.
   *
   * @param fullscreen_mode whether to enable or disable fullscreen mode
   */
  private void set_fullscreen_mode (bool fullscreen)
  {
    /* After the first time the window has been shown using this.show_all (),
     * the value of leave_fullscreen_button_box.no_show_all should be set to false
     * So that the next time leave_fullscreen_button_box.show_all () is called, the button is actually shown
     * FIXME: If this code can be made cleaner/clearer, please do */

    is_fullscreen = fullscreen;
    if (fullscreen)
    {
            window_state_event.connect (on_window_state_change_event);

      if (is_wide_mode)
      {
        thumbnails_right.hide ();
      }
      else
      {
        thumbnails_bottom.hide ();
      }
      leave_fullscreen_button_box.no_show_all = false;
      leave_fullscreen_button_box.show_all ();

      this.fullscreen ();
      viewport_widget.motion_notify_event.connect (fullscreen_motion_notify_callback);
      set_fullscreen_timeout ();
    }
    else
    {
      if (is_wide_mode)
      {
        thumbnails_right.show_all ();
      }
      else
      {
        thumbnails_bottom.show_all ();
      }
      leave_fullscreen_button_box.hide ();

      /* Stop timer so buttons_area does not get hidden after returning from
       * fullscreen mode */
      clear_fullscreen_timeout ();
      /* Show the buttons area anyway - it might've been hidden in fullscreen mode */
      buttons_area.show ();
      viewport_widget.motion_notify_event.disconnect (fullscreen_motion_notify_callback);
      this.unfullscreen ();

            if (was_maximized)
            {
                this.maximize ();
            }
            else
            {
                this.unmaximize ();
            }
    }
  }

    /**
   * Enable or disable window border mode to the requested state.
     *
     * @param visible whether to show or hide the action bar
     */
     private void set_borderless_mode (bool borderless)
     {
         is_borderless = borderless;
         if (borderless)
         {
             // hide border
            this.decorated = false;

            this.button_press_event.connect (on_button_press);
            this.button_release_event.connect (on_button_release);
            this.motion_notify_event.connect (on_motion_notify);
            this.key_press_event.connect (on_key_press);
            
            // Patch window size on startup to remove gaps (if camera is already set)
            if (camera != null) {
                // Use idle callback to ensure layout is complete
                GLib.Idle.add (() => {
                    GLib.debug("patching window size for preview on startup");
                    patch_window_size_for_preview ();
                    return false; // Don't repeat
                });
            }
         }
         else
         {
             // show border
            this.decorated = true;

            this.button_press_event.disconnect (on_button_press);
            this.button_release_event.disconnect (on_button_release);
            this.motion_notify_event.disconnect (on_motion_notify);
            this.key_press_event.disconnect (on_key_press);
         }
     }
  
    /**
     * Show or hide the action bar.
     *
     * @param visible whether to show or hide the action bar
     */
    private void set_actionbar_visible (bool visible)
    {
        is_actionbar_visible = visible;
        if (visible)
        {
            buttons_area.show ();
        }
        else
        {
            buttons_area.hide ();
        }
    }
 
    /**
     * Show or hide the action bar.
     *
     * @param visible whether to show or hide the action bar
     */
    public void set_thumbnailsbar_visible (bool visible)
    {
        is_thumbnailsbar_visible = visible;
        if (is_fullscreen)
            visible = false;
        if (visible)
        {
            if (is_wide_mode)
            {
                thumbnails_right.show ();
            }
            else
            {
                thumbnails_bottom.show ();
            }
        }
        else
        {
            if (is_wide_mode)
            {
                thumbnails_right.hide ();
            }
            else
            {
                thumbnails_bottom.hide ();
            }
        }
    }
 
  /**
   * Enable or disable wide mode to the requested state.
   *
   * @param wide_mode whether to enable or disable wide mode
   */
  public void set_wide_mode (bool wide_mode)
  {
    is_wide_mode = wide_mode;

    /* keep the viewport to its current size while rearranging the ui,
     * so that thumbview moves from right to bottom and viceversa
     * while the rest of the window stays unchanged */
    Gtk.Allocation alloc;
    viewport_widget.get_allocation (out alloc);
    viewport_widget.set_size_request (alloc.width, alloc.height);

    if (is_wide_mode)
    {
      thumb_view.set_vertical (true);
      thumb_nav.set_vertical (true);
      if (thumbnails_bottom.get_children () != null)
      {
        thumbnails_bottom.remove (thumb_nav);
      }
      thumbnails_right.add (thumb_nav);

            if (!is_fullscreen)
            {
                thumbnails_right.show_all ();
                thumbnails_bottom.hide ();
            }
    }
    else
    {
      thumb_view.set_vertical (false);
      thumb_nav.set_vertical (false);
      if (thumbnails_right.get_children () != null)
      {
        thumbnails_right.remove (thumb_nav);
      }
      thumbnails_bottom.add (thumb_nav);

            if (!is_fullscreen)
            {
                thumbnails_bottom.show_all ();
                thumbnails_right.hide ();
            }
    }

    /* handy trick to keep the window to the desired size while not
     * requesting a fixed one. This way the window is resized to its
     * natural size (particularly with the constraints imposed by the
     * viewport, see above) but can still be shrinked down */

    Gtk.Requisition req;
    this.get_preferred_size(out req, out req);
    this.resize (req.width, req.height);
    viewport_widget.set_size_request (-1, -1);
  }

  /**
   * Make sure that the layout manager manages the entire stage.
   *
   * @param actor unused
   * @param box unused
   * @param flags unused
   */
  public void on_stage_resize (Clutter.Actor           actor,
                               Clutter.ActorBox        box,
                               Clutter.AllocationFlags flags)
  {
    this.viewport_layout.set_size (viewport.width, viewport.height);
    this.background_layer.set_size (viewport.width, viewport.height);
    this.timeout_layer.set_position (video_preview.width/3 + viewport.width/2,
                                viewport.height-20);
    // Position auto-shoot indicator in bottom right corner of viewport
    auto_shoot_indicator.set_position (viewport.width - 25, viewport.height - 25);
  }

  /**
   * The method to call when the countdown is finished.
   */
  private void finish_countdown_callback ()
  {
    if (action_cancelled == false)
    {
      string file_name = fileutil.get_new_media_filename (this.current_mode);

      if (settings.get_boolean ("flash"))
      {
        this.flash.fire ();
      }
      CanberraGtk.play_for_widget (this.main_vbox, 0,
                                   Canberra.PROP_EVENT_ID, "camera-shutter",
                                   Canberra.PROP_MEDIA_ROLE, "event",
                                   Canberra.PROP_EVENT_DESCRIPTION, _("Shutter sound"),
                                   null);
      this.camera.take_photo (file_name);
    }

    if (current_mode == MediaMode.PHOTO)
    {
      enable_mode_change ();
    }
  }

  Countdown current_countdown;
  /**
   * Start to take a photo, starting a countdown if it is enabled.
   */
  public void take_photo ()
  {
    if (settings.get_boolean ("countdown"))
    {
      if (current_mode == MediaMode.PHOTO)
      {
        disable_mode_change ();
      }

      current_countdown = new Countdown (this.countdown_layer);
      current_countdown.start (finish_countdown_callback);
    }
    else
    {
      finish_countdown_callback ();
    }
  }

  private int  burst_count;
  private uint burst_callback_id;

  /**
   * Take a photo during burst mode, and increment the burst count.
   *
   * @return true if there are more photos to be taken in the current burst,
   * false otherwise
   */
  private bool burst_take_photo ()
  {
    if (is_bursting && burst_count < settings.get_int ("burst-repeat"))
    {
      this.take_photo ();
      burst_count++;
      return true;
    }
    else
    {
      toggle_photo_bursting (false);
      return false;
    }
  }

    /**
     * Cancel the current action (if any)
     */
    private bool cancel_running_action ()
    {
        if ((current_countdown != null && current_countdown.running)
            || is_bursting || is_recording)
        {
            action_cancelled = true;

            switch (current_mode)
            {
                case MediaMode.PHOTO:
                    current_countdown.stop ();
                    finish_countdown_callback ();
                    break;
                case MediaMode.BURST:
                    toggle_photo_bursting (false);
                    break;
                case MediaMode.VIDEO:
                    toggle_video_recording (false);
                    break;
            }

            action_cancelled = false;

            return true;
        }

        return false;
    }

    public void take_photo_background (string file_name) {
        this.camera.take_photo (file_name);
    }

    public void fire_flash (bool always, bool never) {
        if (! never)
        {
            bool enabled = always;
            if (! always)
            {
                enabled = settings.get_boolean ("flash");
            }
            if (enabled)
            {
                flash.fire ();
            }
        }
    }

    public void play_shutter_sound () {
        CanberraGtk.play_for_widget (this.main_vbox, 0,
            Canberra.PROP_EVENT_ID, "camera-shutter",
            Canberra.PROP_MEDIA_ROLE, "event",
            Canberra.PROP_EVENT_DESCRIPTION, _("Shutter sound"),
            null);
    }

  /**
   * Cancel the current activity if the escape key is pressed.
   *
   * @param event the key event, to check which key was pressed
   * @return false, to allow further processing of the event
   */
  private bool on_key_release (Gdk.EventKey event)
  {
    string key;

    key = Gdk.keyval_name (event.keyval);
    if (strcmp (key, "Escape") == 0)
    {
      if (cancel_running_action ())
      {
        return false;
      }
      else if (is_effects_selector_active)
      {
        effects_toggle_button.set_active (false);
      }
    }
    return false;
  }

  /**
   * Toggle whether video recording is active.
   *
   * @param is_start whether to start video recording
   */
  public void toggle_video_recording (bool is_start)
  {
    if (is_start)
    {
      camera.start_video_recording (fileutil.get_new_media_filename (this.current_mode));
      /* Will be called every 1 second while
       * update_timeout_layer returns true.
       */
      Timeout.add_seconds (1, update_timeout_layer);
      take_action_button.tooltip_text = _("Stop recording");
      take_action_button_image.set_from_icon_name ("media-playback-stop-symbolic", Gtk.IconSize.BUTTON);
      this.is_recording = true;
      this.disable_mode_change ();
    }
    else
    {
      camera.stop_video_recording ();
      /* The timeout_layer always shows the "00:00:00"
       * string when not recording, in order to notify
       * the user about two things:
       *   + The user is making use of the recording mode.
       *   + The user is currently not recording.
       */
      timeout_layer.text = "00:00:00";
      take_action_button.tooltip_text = _("Record a video");
      take_action_button_image.set_from_icon_name ("camera-web-symbolic", Gtk.IconSize.BUTTON);
      this.is_recording = false;
      this.enable_mode_change ();
    }
  }

  /**
   * Update the timeout layer displayed timer.
   *
   * @return false, if the source, Timeout.add_seconds (used
   * in the toogle_video_recording method), should be removed.
   */
  private bool update_timeout_layer ()
  {
    if (is_recording) {
      timeout_layer.text = camera.get_recorded_time ();
      return true;
    }
    else
      return false;
  }

  /**
   * Toggle whether photo bursting is active.
   *
   * @param is_start whether to start capturing a photo burst
   */
  public void toggle_photo_bursting (bool is_start)
  {
    if (is_start)
    {
      is_bursting = true;
      this.disable_mode_change ();
      // FIXME: Set the effects action to be inactive.
      take_action_button.tooltip_text = _("Stop taking pictures");
      burst_take_photo ();

      /* Use the countdown duration if it is greater than the burst delay, plus
       * about 500 ms for taking the photo. */
      var burst_delay = settings.get_int ("burst-delay");
      var countdown_duration = 500 + settings.get_int ("countdown-duration") * 1000;
      if ((burst_delay - countdown_duration) < 1000 && settings.get_boolean ("countdown"))
      {
        burst_callback_id = GLib.Timeout.add (countdown_duration, burst_take_photo);
      }
      else
      {
        burst_callback_id = GLib.Timeout.add (burst_delay, burst_take_photo);
      }
    }
    else
    {
      if (current_countdown != null && current_countdown.running)
        current_countdown.stop ();

      is_bursting = false;
      this.enable_mode_change ();
      take_action_button.tooltip_text = _("Take multiple photos");
      burst_count = 0;
      fileutil.reset_burst ();
      GLib.Source.remove (burst_callback_id);
    }
  }

    /**
     * Take a photo or burst of photos, or record a video, based on the current
     * capture mode.
     */
    public void shoot ()
    {
        switch (current_mode)
        {
            case MediaMode.PHOTO:
                take_photo ();
                break;
            case MediaMode.VIDEO:
                toggle_video_recording (!is_recording);
                break;
            case MediaMode.BURST:
                toggle_photo_bursting (!is_bursting);
                break;
            default:
                assert_not_reached ();
        }
    }

    /**
     * Show an error.
     *
     * @param error the error to display, or null to hide the error layer
     */
    public void show_error (string? error)
    {
        if (error != null)
        {
            current_effects_grid.hide ();
            video_preview.hide ();
            error_layer.text = error;
            error_layer.show ();
        }
        else
        {
            error_layer.hide ();

            if (is_effects_selector_active)
            {
                current_effects_grid.show ();
            }
            else
            {
                video_preview.show ();
            }
        }
    }

    /**
     * Toggle the display of the effect selector.
     *
     * @param effects whether effects should be enabled
     */
    public void set_effects (bool effects)
    {
        toggle_effects_selector (effects);
    }

  /**
   * Change the selected effect, as a new one was selected.
   *
   * @param tap unused
   * @param source the actor (with associated effect) that was selected
   */
  public void on_selected_effect_change (Clutter.TapAction tap,
                                         Clutter.Actor source)
  {
    /* Disable the effects selector after selecting an effect. */
    effects_toggle_button.set_active (false);

    selected_effect = source.get_data ("effect");
    camera.set_effect (selected_effect);
    settings.set_string ("selected-effect", selected_effect.name);
  }

    /**
     * Navigate back one page of effects.
     */
    private void on_effects_previous ()
    {
        if (is_previous_effects_page ())
        {
            activate_effects_page ((int)current_effects_page - 1);
        }
    }

    /**
     * Navigate forward one page of effects.
     */
    private void on_effects_next ()
    {
        if (is_next_effects_page ())
        {
            activate_effects_page ((int)current_effects_page + 1);
        }
    }

  /**
   * Switch to the supplied page of effects.
   *
   * @param number the effects page to switch to
   */
  private void activate_effects_page (int number)
  {
    if (!is_effects_selector_active)
      return;
    current_effects_page = number;
    if (viewport_layout.get_children ().index (current_effects_grid) != -1)
    {
      viewport_layout.remove_child (current_effects_grid);
    }
    current_effects_grid = effects_grids.nth_data (number);
    current_effects_grid.opacity = 0;
    viewport_layout.add_child (current_effects_grid);
    current_effects_grid.save_easing_state ();
    current_effects_grid.set_easing_mode (Clutter.AnimationMode.LINEAR);
    current_effects_grid.set_easing_duration (500);
    current_effects_grid.opacity = 255;
    current_effects_grid.restore_easing_state ();


    uint i = 0;
    foreach (var effect in effects_manager.effects)
    {
        uint page_nr = i / EFFECTS_PER_PAGE;
        if (page_nr == number)
        {
            if (!effect.is_preview_connected ())
            {
                Clutter.Actor texture = effect.get_data<Clutter.Actor> ("texture");
                camera.connect_effect_texture (effect, texture);
            }
            effect.enable_preview ();
        }
        else
        {
            if (effect.is_preview_connected ())
            {
                effect.disable_preview ();
            }
        }

	    i++;
    }

    setup_effects_page_switch_sensitivity ();
  }

    /**
     * Control the sensitivity of the effects page navigation buttons.
     */
    private void setup_effects_page_switch_sensitivity ()
    {
        var effects_next = this.lookup_action ("effects-next") as SimpleAction;
        var effects_previous = this.lookup_action ("effects-previous") as SimpleAction;

        effects_next.set_enabled (is_effects_selector_active
                                  && is_next_effects_page ());
        effects_previous.set_enabled (is_effects_selector_active
                                      && is_previous_effects_page ());
    }

    private bool is_next_effects_page ()
    {
        // Calculate the number of effects visible up to the current page.
        return (current_effects_page + 1) * EFFECTS_PER_PAGE < effects_manager.effects.length ();
    }

    private bool is_previous_effects_page ()
    {
        return current_effects_page != 0;
    }

    /**
     * Toggle the visibility of the effects selector.
     *
     * @param active whether the selector should be active
     */
    private void toggle_effects_selector (bool active)
    {
        is_effects_selector_active = active;

        if (effects_grids.length () == 0)
        {
            show_error (active ? _("No effects found") : null);
        }
        else if (active)
        {
            video_preview.hide ();
            current_effects_grid.show ();
            activate_effects_page ((int)current_effects_page);
        }
        else
        {
            current_effects_grid.hide ();
            video_preview.show ();
        }

        camera.toggle_effects_pipeline (active);
        setup_effects_page_switch_sensitivity ();
        update_header_bar_title ();
    }

  /**
   * Create the effects selector.
   */
  private void setup_effects_selector ()
  {
    if (current_effects_grid == null)
    {
      effects_manager = new EffectsManager ();
      effects_manager.load_effects ();

      /* Must initialize effects_grids before returning, as it is dereferenced later, bug 654671. */
      effects_grids = new List<Clutter.Actor> ();

      if (effects_manager.effects.length () == 0)
      {
        warning ("gnome-video-effects is not installed.");
        return;
      }

      foreach (var effect in effects_manager.effects)
      {
          Clutter.GridLayout grid_layout = new GridLayout ();
          var grid = new Clutter.Actor ();
          grid.set_layout_manager (grid_layout);
          effects_grids.append (grid);
          grid_layout.set_column_spacing (10);
          grid_layout.set_row_spacing (10);
      }

      uint i = 0;
      foreach (var effect in effects_manager.effects)
      {
        Clutter.Actor texture = new Clutter.Actor ();
        Clutter.BinLayout layout = new Clutter.BinLayout (Clutter.BinAlignment.CENTER,
                                                          Clutter.BinAlignment.CENTER);
        var box = new Clutter.Actor ();
        box.set_layout_manager (layout);
        Clutter.Text      text = new Clutter.Text ();
        var rect = new Clutter.Actor ();

        rect.opacity = 128;
        rect.background_color = Clutter.Color.from_string ("black");

        texture.content_gravity = Clutter.ContentGravity.RESIZE_ASPECT;
        box.add_child (texture);
        box.reactive = true;
        box.min_height = 40;
        box.min_width = 50;
        var tap = new Clutter.TapAction ();
        box.add_action (tap);
        tap.tap.connect (on_selected_effect_change);
        box.set_data ("effect", effect);
        effect.set_data ("texture", texture);

        text.text  = effect.name;
        text.color = Clutter.Color.from_string ("white");

        rect.height = text.height + 5;
        rect.x_align = Clutter.ActorAlign.FILL;
        rect.y_align = Clutter.ActorAlign.END;
        rect.x_expand = true;
        rect.y_expand = true;
        box.add_child (rect);

        text.x_align = Clutter.ActorAlign.CENTER;
        text.y_align = Clutter.ActorAlign.END;
        text.x_expand = true;
        text.y_expand = true;
        box.add_child (text);

        var grid_layout = effects_grids.nth_data (i / EFFECTS_PER_PAGE).layout_manager as GridLayout;
        grid_layout.attach (box, ((int)(i % EFFECTS_PER_PAGE)) % 3,
                            ((int)(i % EFFECTS_PER_PAGE)) / 3, 1, 1);

        i++;
      }

      setup_effects_page_switch_sensitivity ();
      current_effects_grid = effects_grids.nth_data (0);
    }
  }

    /**
     * Update the UI when the camera starts playing.
     */
    public void camera_state_change_playing ()
    {
        show_error (null);

        Effect effect = effects_manager.get_effect (settings.get_string ("selected-effect"));
        if (effect != null)
        {
            camera.set_effect (effect);
        }
    }

    /**
     * Report an error as the camerabin switched to the NULL state.
     */
    public void camera_state_change_null ()
    {
        cancel_running_action ();

        if (!error_layer.visible)
        {
            show_error (_("There was an error playing video from the webcam"));
        }
    }

    /**
     * Show or hide the auto-shoot indicator.
     *
     * @param visible true to show the indicator, false to hide it
     */
    public void set_auto_shoot_indicator_visible (bool visible)
    {
        if (visible) {
            auto_shoot_indicator.show();
            // Ensure canvas is redrawn
            var canvas = auto_shoot_indicator.get_content() as Clutter.Canvas;
            if (canvas != null) {
                canvas.invalidate();
            }
        } else {
            auto_shoot_indicator.hide();
        }
    }

  /**
   * Select next camera in list and activate it.
   */
  public void on_switch_camera_clicked ()
  {
      unowned Cheese.CameraDevice selected;
      unowned Cheese.CameraDevice next = null;
      GLib.GenericArray<unowned Cheese.CameraDevice> cameras;
      uint i;

      if (camera == null)
      {
          return;
      }

      selected = camera.get_selected_device ();

      if (selected == null)
      {
          return;
      }

      cameras = camera.get_camera_devices ();

      for (i = 0; i < cameras.length; i++)
      {
          next = cameras.get (i);

          if (next == selected)
          {
              break;
          }
      }

      if (i + 1 < cameras.length)
      {
          next = cameras.get (i + 1);
      }
      else
      {
          next = cameras.get (0);
      }

      if (next == selected)
      {
          /* Next is the same device.... */
          return;
      }

      camera.set_device (next);
      camera.switch_camera_device ();
  }

  /**
   * Set switch camera buttons visible state.
   */
  public void set_switch_camera_button_state ()
  {
      unowned Cheese.CameraDevice selected;
      GLib.GenericArray<unowned Cheese.CameraDevice> cameras;

      if (camera == null)
      {
          switch_camera_button.set_visible (false);
          return;
      }

      selected = camera.get_selected_device ();

      if (selected == null)
      {
          switch_camera_button.set_visible (false);
          return;
      }

      cameras = camera.get_camera_devices ();

      if (cameras.length > 1)
      {
         switch_camera_button.set_visible (true);
         return;
      }

      switch_camera_button.set_visible (false);
  }

  /**
   * Load the UI from the GtkBuilder description.
   */
  public void setup_ui ()
  {
        clutter_builder = new Clutter.Script ();
    fileutil        = new FileUtil ();
    flash           = new Flash (this);
    settings        = new GLib.Settings ("org.gnome.Cheese");

        var menu = application.get_menu_by_id ("thumbview-menu");
        thumbnail_popup = new Gtk.Menu.from_model (menu);
        
        application.set_accels_for_action ("app.quit", {"<Primary>q"});
        application.set_accels_for_action ("app.fullscreen", {"F11"});
        application.set_accels_for_action ("app.borderless", {"F4"});
        application.set_accels_for_action ("app.actionbar", {"F5"});
        application.set_accels_for_action ("app.thumbnails", {"F6"});
        application.set_accels_for_action ("app.follow-toggle", {"grave"});
        application.set_accels_for_action ("app.follow-faster", {"greater"});
        application.set_accels_for_action ("app.follow-slower", {"less"});
        application.set_accels_for_action ("app.follow-reset", {"equal"});
        application.set_accels_for_action ("win.file-open", {"<Primary>o"});
        application.set_accels_for_action ("win.file-saveas", {"<Primary>s"});
        application.set_accels_for_action ("win.file-trash", {"Delete"});
        application.set_accels_for_action ("win.file-delete", {"<Shift>Delete"});

        this.add_action_entries (actions, this);

        try
        {
            clutter_builder.load_from_resource ("/org/gnome/Cheese/cheese-viewport.json");
        }
        catch (Error err)
        {
            error ("Error: %s", err.message);
        }

        viewport = viewport_widget.get_stage () as Clutter.Stage;

        // Enable alpha compositing on the Clutter stage to support transparency
        // This is needed for opacity to work properly with video textures
        viewport.set_use_alpha (true);

        video_preview = clutter_builder.get_object ("video_preview") as Clutter.Actor;
        viewport_layout = clutter_builder.get_object ("viewport_layout") as Clutter.Actor;
        viewport_layout_manager = clutter_builder.get_object ("viewport_layout_manager") as Clutter.BinLayout;
        countdown_layer = clutter_builder.get_object ("countdown_layer") as Clutter.Text;
        background_layer = clutter_builder.get_object ("background") as Clutter.Actor;
        error_layer = clutter_builder.get_object ("error_layer") as Clutter.Text;
        timeout_layer = clutter_builder.get_object ("timeout_layer") as Clutter.Text;

    video_preview.request_mode      = Clutter.RequestMode.HEIGHT_FOR_WIDTH;
    viewport.add_child (background_layer);
    viewport_layout.set_layout_manager (viewport_layout_manager);

    viewport.add_child (viewport_layout);
    viewport.add_child (timeout_layer);

    // Create auto-shoot indicator (red dot) - test canvas sizing
    auto_shoot_indicator = new Clutter.Actor();
    int size = 16;
    auto_shoot_indicator.set_size(size, size); // Larger to accommodate border
    auto_shoot_indicator.hide(); // Initially hidden

    // Create canvas with explicit sizing for circular indicator
    var canvas = new Clutter.Canvas();
    canvas.set_size(size, size); // Explicit canvas size
    auto_shoot_indicator.set_content(canvas);

    // Draw red circle with black border
    canvas.draw.connect((cr, width, height) => {
        double center_x = width / 2.0;
        double center_y = height / 2.0;

        // clear the canvas
        cr.set_source_rgba(0.0, 0.0, 0.0, 0.0);
        cr.rectangle(0, 0, width, height);
        cr.fill();

        // Draw red interior circle
        cr.set_source_rgba(1.0, 0.0, 0.0, 1.0);
        cr.arc(center_x, center_y, width/2.0 - 3, 0, 2 * Math.PI);
        cr.fill();

        return true;
    });
    canvas.invalidate();

    viewport.add_child (auto_shoot_indicator);

    viewport.allocation_changed.connect (on_stage_resize);

    thumb_view = new Cheese.ThumbView ();
    thumb_nav  = new Eog.ThumbNav (thumb_view, false);
        thumbnail_popup.attach_to_widget (thumb_view, null);
        thumb_view.popup_menu.connect (on_thumb_view_popup_menu);

        Gtk.CssProvider css;
        try
        {
            var file = File.new_for_uri ("resource:///org/gnome/Cheese/cheese.css");
            css = new Gtk.CssProvider ();
            css.load_from_file (file);
        }
        catch (Error e)
        {
            // TODO: Use parsing-error signal.
            error ("Error parsing CSS: %s\n", e.message);
        }

    Gtk.StyleContext.add_provider_for_screen (screen, css, STYLE_PROVIDER_PRIORITY_USER);

    thumb_view.button_press_event.connect (on_thumbnail_button_press_event);

    switch_camera_button.clicked.connect (on_switch_camera_clicked);

    /* needed for the sizing tricks in set_wide_mode (allocation is 0
     * if the widget is not realized */
    viewport_widget.realize ();

    viewport_widget.scroll_event.connect (on_viewport_scroll);

    set_wide_mode (false);

    setup_effects_selector ();

    this.key_release_event.connect (on_key_release);

    this.delete_event.connect (on_window_delete);
  }

  public void restore_window_state (bool resize_window = true)
  {
    int left = settings.get_int ("window-left");
    int top = settings.get_int ("window-top");
    
    // Only restore position if it's not at the default (0,0) and within reasonable bounds
    if (left != 0 || top != 0) {
        // Check if position is within screen bounds (basic check)
        if (left >= -1000 && left <= 10000 && top >= -1000 && top <= 10000) {
            this.move (left, top);
        }
    }
    
    if (resize_window) {
        // Restore window size and position
        int width = settings.get_int ("window-width");
        int height = settings.get_int ("window-height");
        
        // Clamp size to valid range
        width = width.clamp (10, 4096);
        height = height.clamp (10, 4096);
        
        this.resize (width, height);
    }

    // Restore opacity
    double opacity = settings.get_double ("opacity");
    set_opacity (opacity);
  }

  public void resize (int width, int height)
  {
    //  stackdump();
    base.resize(width, height);
  }

  public void save_window_state ()
  {
    // Save window size, position and maximized state
    bool maximized = this.is_maximized;
    settings.set_boolean ("window-maximized", maximized);

    int width, height;
    this.get_size (out width, out height);

    // Clamp values to valid GSettings range
    width = width.clamp (10, 4096);
    height = height.clamp (10, 4096);

    settings.set_int ("window-width", width);
    settings.set_int ("window-height", height);

    // Save window position
    int left, top;
    this.get_position (out left, out top);
    settings.set_int ("window-left", left);
    settings.set_int ("window-top", top);

    // Save opacity
    double opacity = get_opacity ();
    settings.set_double ("opacity", opacity);
  }

  private bool on_window_delete (Gdk.Event event)
  {
    // Save final window state before the window is destroyed
    save_window_state ();

    // Return false to allow the window to be destroyed
    return false;
  }

    public Clutter.Actor get_video_preview ()
    {
        return video_preview;
    }

  /**
   * Setup the thumbview thumbnail monitors.
   */
  public void start_thumbview_monitors ()
  {
    thumb_view.start_monitoring_video_path (fileutil.get_video_path ());
    thumb_view.start_monitoring_photo_path (fileutil.get_photo_path ());
  }

    /**
     * Set the current media mode (photo, video or burst).
     *
     * @param mode the media mode to set
     */
    public void set_current_mode (MediaMode mode)
    {
        current_mode = mode;

        set_resolution (current_mode);
        update_header_bar_title ();
        timeout_layer.hide ();

        switch (current_mode)
        {
            case MediaMode.PHOTO:
                take_action_button.tooltip_text = _("Take a photo using a webcam");
                break;

            case MediaMode.VIDEO:
                take_action_button.tooltip_text = _("Record a video using a webcam");
                timeout_layer.text = "00:00:00";
                timeout_layer.show ();
                break;

            case MediaMode.BURST:
                take_action_button.tooltip_text = _("Take multiple photos using a webcam");
                break;
        }
    }

     /**
     * Set the header bar title.
     */
    private void update_header_bar_title ()
    {
        if (is_effects_selector_active)
        {
            set_window_title (_("Choose an Effect"));
        }
        else
        {
            switch (current_mode)
            {
                case MediaMode.PHOTO:
                    set_window_title (_("Take a Photo"));
                    break;

                case MediaMode.VIDEO:
                    set_window_title (_("Record a Video"));
                    break;

                case MediaMode.BURST:
                    set_window_title (_("Take Multiple Photos"));
                    break;
            }
        }
    }
    /**
     * Set the camera.
     *
     * @param camera the camera to set
     */
    public void set_camera (Camera camera)
    {
        this.camera = camera;
        set_switch_camera_button_state ();
        
        // If in borderless mode, patch window size on startup to remove gaps
        if (is_borderless) {
            // Use idle callback to ensure layout is complete
            GLib.Idle.add (() => {
                patch_window_size_for_preview ();
                return false; // Don't repeat
            });
        }
     }

    private bool on_button_press (Gtk.Widget widget,
                                  Gdk.EventButton event)
    {
        if (event.type != Gdk.EventType.BUTTON_PRESS)
            return false;

        switch (event.button) {
            case 1:
                button_move_or_resize = true;
                resize_edge = 0; // move
                break;
            case 3:
                button_move_or_resize = false;
                get_size (out button_down_width, out button_down_height);
                get_position (out button_down_left, out button_down_top);
                
                // Detect which corner/edge we're resizing from
                int width, height;
                get_size (out width, out height);
                
                double mouse_x = event.x;
                double mouse_y = event.y;
                
                // Calculate relative position (0.0 to 1.0)
                double rel_x = mouse_x / width;
                double rel_y = mouse_y / height;
                
                // Always use corner detection - no edge detection
                // Use relative position to determine which corner
                
                // Determine corner based on relative position
                // Split window into 4 quadrants
                if (rel_x < 0.5 && rel_y < 0.5) {
                    // Top-left quadrant
                    resize_edge = 1; // top-left
                } else if (rel_x >= 0.5 && rel_y < 0.5) {
                    // Top-right quadrant
                    resize_edge = 2; // top-right
                } else if (rel_x < 0.5 && rel_y >= 0.5) {
                    // Bottom-left quadrant
                    resize_edge = 3; // bottom-left
                } else {
                    // Bottom-right quadrant (rel_x >= 0.5 && rel_y >= 0.5)
                    resize_edge = 4; // bottom-right
                }
                break;
            default:
                return false;
        }

        button_down = true;
        button_down_x = event.x;
        button_down_y = event.y;
        return true;
    }

    private bool on_button_release (Gtk.Widget widget,
                                    Gdk.EventButton event)
    {
        if (button_down) {
            button_down = false;
            
            // If we were resizing (not moving), patch the window size to remove gaps
            if (!button_move_or_resize && resize_edge != 0) {
                patch_window_size_for_preview ();
            }
            
            return true;
        } else {
            return false;
        }
    }

    private string input_source_to_name (Gdk.InputSource source)
    {
        switch (source) {
            case Gdk.InputSource.MOUSE:
                return "MOUSE";
            case Gdk.InputSource.PEN:
                return "PEN";
            case Gdk.InputSource.ERASER:
                return "ERASER";
            case Gdk.InputSource.CURSOR:
                return "CURSOR";
            case Gdk.InputSource.KEYBOARD:
                return "KEYBOARD";
            case Gdk.InputSource.TOUCHSCREEN:
                return "TOUCHSCREEN";
            case Gdk.InputSource.TOUCHPAD:
                return "TOUCHPAD";
            case Gdk.InputSource.TABLET_PAD:
                return "TABLET_PAD";
            default:
                return "UNKNOWN(%d)".printf((int)source);
        }
    }

    private bool on_viewport_scroll (Gtk.Widget widget,
                                     Gdk.EventScroll event)
    {
        bool alt_pressed = (event.state & Gdk.ModifierType.MOD1_MASK) != 0;
        bool ctrl_pressed = (event.state & Gdk.ModifierType.CONTROL_MASK) != 0;
        bool shift_pressed = (event.state & Gdk.ModifierType.SHIFT_MASK) != 0;
        bool win_pressed = (event.state & Gdk.ModifierType.SUPER_MASK) != 0;

        // Check if middle mouse button is pressed (button 2)
        bool middle_button_pressed = (event.state & Gdk.ModifierType.BUTTON2_MASK) != 0;

        int mode = (win_pressed ? 0x1000 : 0)
                | (alt_pressed ? 0x0100 : 0)
                | (ctrl_pressed ? 0x0010 : 0)
                | (shift_pressed ? 0x0001 : 0);

        switch (mode) {
            case 0x1000: // win
            case 0x0110: // alt + ctrl
                mode = 'h'; // hue
                break;
            case 0x0100: // alt
                mode = 's'; // saturation
                break;
            case 0x0010: // ctrl
                mode = 'c'; // contrast
                break;
            case 0x0011: // ctrl + shift
                mode = 'l'; // lux
                break;
            case 0x0001: // shift
                mode = 'b'; // brightness
                break;
            default:
                if (middle_button_pressed) {
                    mode = 'z'; // zoom
                } else {
                    mode = 'o'; // opacity
                    if (!is_borderless)
                        return false;
                }
                break;
        }

        // Check the source device type to distinguish mouse wheel from trackpad
        unowned Gdk.Device? source_device = event.get_source_device ();
        
        Gdk.InputSource source = Gdk.InputSource.MOUSE;
        if (source_device != null) {
            source = source_device.get_source ();
        }
        
        double value_step = 0.01;
        double direction = 1.0;
        double current_value;
        switch (mode) {
            case 'h':
                current_value = settings.get_double ("hue");
                break;
            case 's':
                current_value = settings.get_double ("saturation");
                direction = -1.0;
                break;
            case 'c':
                current_value = settings.get_double ("contrast");
                direction = -1.0;
                break;
            case 'b':
                current_value = settings.get_double ("brightness");
                direction = -1.0;
                break;
            case 'l':
                current_value = settings.get_double ("lux");
                direction = -1.0;
                break;
            case 'z':
                current_value = settings.get_double ("canvas-zoom");
                value_step = 0.05; // Larger step for zoom
                break;
            default:
                current_value = get_opacity ();
                break;
        }

        double new_value = current_value;
        
        switch (source) {
            case Gdk.InputSource.TOUCHPAD:
            case 7:
                value_step = 0.01;
                if (event.direction == Gdk.ScrollDirection.SMOOTH) {
                    double _delta_x, delta_y;
                    if (event.get_scroll_deltas (out _delta_x, out delta_y)) {
                        // Use vertical delta (delta_y) for value change
                        if (delta_y < 0)
                            new_value = current_value - value_step * direction;
                        else
                            new_value = current_value + value_step * direction;
                        break;
                    }
                }
                return false;
            default:
                if (event.direction == Gdk.ScrollDirection.UP) {
                    new_value = current_value - value_step * direction;
                    break;
                } else if (event.direction == Gdk.ScrollDirection.DOWN) {
                    new_value = current_value + value_step * direction;
                    break;
                }
                return false;
        }

        switch (mode) {
            case 'h':
                new_value = new_value.clamp (-1.0, 1.0);
                camera.set_balance_property ("hue", new_value);
                settings.set_double ("hue", new_value);
                GLib.debug("hue: %.2f", new_value);
                break;

            case 's':
                new_value = new_value.clamp (0.0, 2.0);
                camera.set_balance_property ("saturation", new_value);
                settings.set_double ("saturation", new_value);
                GLib.debug("saturation: %.2f", new_value);
                break;

            case 'c':
                new_value = new_value.clamp (0.0, 1.99);
                camera.set_balance_property ("contrast", new_value);
                settings.set_double ("contrast", new_value);
                GLib.debug("contrast: %.2f", new_value);
                break;

            case 'b':
                new_value = new_value.clamp (-1.0, 1.0);
                camera.set_balance_property ("brightness", new_value);
                settings.set_double ("brightness", new_value);
                GLib.debug("brightness: %.2f", new_value);
                break;

            case 'l':
                new_value = new_value.clamp (-10.0, 10.0);
                camera.set_lux (new_value);
                settings.set_double ("lux", new_value);
                GLib.debug("lux: %.2f", new_value);
                break;

            case 'z':
                new_value = new_value.clamp (0.1, 10.0);
                camera.set_canvas_zoom (new_value);
                settings.set_double ("canvas-zoom", new_value);
                GLib.debug("canvas: zoom %.2f", new_value);
                break;
            
            default:
            case 'o':
                // Set opacity using the setter method (which handles clamping, Clutter actors, and saving)
                set_opacity (new_value);

                GLib.debug("opacity: %.2f", new_value);
                break;
        }
        return true;
    }
    
    /**
     * Adjust window size after resize to remove gaps around preview frame
     */
    private void patch_window_size_for_preview ()
    {
        // Get current window size
        int current_width, current_height;
        get_size (out current_width, out current_height);
        int current_left, current_top;
        get_position (out current_left, out current_top);
        
        // Get camera format aspect ratio as reference
        double camera_aspect_ratio = 16.0 / 9.0; // default
        int camera_format_width = 0;
        int camera_format_height = 0;
        
        if (camera != null) {
            unowned Cheese.VideoFormat format = camera.get_current_video_format ();
            if (format != null && format.height > 0) {
                camera_format_width = format.width;
                camera_format_height = format.height;
                camera_aspect_ratio = (double)format.width / (double)format.height;
            }
        }
        
        // Get the actual viewport/preview area size (where content is displayed)
        int viewport_available_width = 0;
        int viewport_available_height = 0;
        
        if (viewport_widget != null) {
            Gtk.Allocation alloc;
            viewport_widget.get_allocation (out alloc);
            viewport_available_width = alloc.width;
            viewport_available_height = alloc.height;
        }
        
        // Calculate the actual content size that fits in the viewport while maintaining camera aspect ratio
        // This is the largest rectangle with camera_aspect_ratio that fits in viewport_available_width x viewport_available_height
        int preview_width = 0;
        int preview_height = 0;
        double aspect_ratio = camera_aspect_ratio;
        
        if (viewport_available_width > 0 && viewport_available_height > 0) {
            // Calculate what the content size would be if we fit it to the viewport
            double viewport_aspect = (double)viewport_available_width / (double)viewport_available_height;
            
            if (viewport_aspect > camera_aspect_ratio) {
                // Viewport is wider - height is the limiting factor
                preview_height = viewport_available_height;
                preview_width = (int)(preview_height * camera_aspect_ratio);
            } else {
                // Viewport is taller - width is the limiting factor
                preview_width = viewport_available_width;
                preview_height = (int)(preview_width / camera_aspect_ratio);
            }
        } else {
            // Fallback: use camera format size directly if viewport not available
            preview_width = camera_format_width;
            preview_height = camera_format_height;
            aspect_ratio = camera_aspect_ratio;
        }
        
        // Use the actual viewport allocation (accounts for any spacing automatically)
        int viewport_current_width = viewport_available_width;
        int viewport_current_height = viewport_available_height;
        
        if (viewport_current_width < 1) viewport_current_width = 1;
        if (viewport_current_height < 1) viewport_current_height = 1;
        
        // Calculate ideal viewport size to match preview aspect ratio
        // The preview_width/height already fit the viewport with correct aspect ratio
        // So we use those directly as the target viewport size
        int viewport_target_width = preview_width;
        int viewport_target_height = preview_height;
        
        // Calculate target window size by adding back UI elements
        // Get the actual allocations to account for any spacing
        int actual_header_height = 0;
        int actual_actionbar_height = 0;
        int actual_thumbnails_width = 0;
        int actual_thumbnails_height = 0;
        
        if (header_bar != null && header_bar.visible && !is_borderless) {
            Gtk.Allocation header_alloc;
            header_bar.get_allocation (out header_alloc);
            actual_header_height = header_alloc.height;
        }
        
        // Get actionbar height including parent container (accounts for spacing)
        if (buttons_area != null && buttons_area.visible && is_actionbar_visible) {
            Gtk.Allocation actionbar_alloc;
            buttons_area.get_allocation (out actionbar_alloc);
            actual_actionbar_height = actionbar_alloc.height;
            
            // Check parent container for spacing
            Gtk.Widget? parent = buttons_area.get_parent ();
            if (parent != null) {
                Gtk.Allocation parent_alloc;
                parent.get_allocation (out parent_alloc);
                if (parent_alloc.height > actionbar_alloc.height) {
                    actual_actionbar_height = parent_alloc.height;
                }
            }
        }
        
        if (is_thumbnailsbar_visible && !is_fullscreen) {
            if (is_wide_mode && thumbnails_right != null && thumbnails_right.visible) {
                Gtk.Allocation thumb_alloc;
                thumbnails_right.get_allocation (out thumb_alloc);
                actual_thumbnails_width = thumb_alloc.width;
            } else if (!is_wide_mode && thumbnails_bottom != null && thumbnails_bottom.visible) {
                Gtk.Allocation thumb_alloc;
                thumbnails_bottom.get_allocation (out thumb_alloc);
                actual_thumbnails_height = thumb_alloc.height;
            }
        }
        
        // Calculate target window size
        int target_width = viewport_target_width + actual_thumbnails_width;
        int target_height = viewport_target_height + actual_header_height + actual_actionbar_height + actual_thumbnails_height;
        
        // Only resize if there's a significant difference (more than 2 pixels)
        if ((target_width - current_width).abs () > 2 || (target_height - current_height).abs () > 2) {
            // Calculate position adjustment to keep the resize corner fixed
            int new_left = current_left;
            int new_top = current_top;
            
            switch (resize_edge) {
                case 1: // top-left
                    new_left = current_left + (current_width - target_width);
                    new_top = current_top + (current_height - target_height);
                    break;
                case 2: // top-right
                    new_top = current_top + (current_height - target_height);
                    break;
                case 3: // bottom-left
                    new_left = current_left + (current_width - target_width);
                    break;
                case 4: // bottom-right - no position change
                    break;
                default:
                    // Should not happen
                    break;
            }
            
            if (new_left != current_left || new_top != current_top) {
                move (new_left, new_top);
            }
            resize (target_width, target_height);
        }
    }
    
    private bool on_motion_notify (Gtk.Widget widget,
                                   Gdk.EventMotion event)
    {
        if (! button_down)
            return false;

        int dx = (int) (event.x - button_down_x);
        int dy = (int) (event.y - button_down_y);

        if (button_move_or_resize) {
            // Dragging the window
            int left, top;
            get_position (out left, out top);
            int new_left = left + dx;
            int new_top = top + dy;
            move (new_left, new_top);
        } else {
            // Resizing the window - allow free resizing, patch on release
            int mouse_width = button_down_width;
            int mouse_height = button_down_height;
            
            // Calculate new size based on which corner is being resized (corners only)
            switch (resize_edge) {
                case 1: // top-left
                    mouse_width = button_down_width - dx;
                    mouse_height = button_down_height - dy;
                    break;
                case 2: // top-right
                    mouse_width = button_down_width + dx;
                    mouse_height = button_down_height - dy;
                    break;
                case 3: // bottom-left
                    mouse_width = button_down_width - dx;
                    mouse_height = button_down_height + dy;
                    break;
                case 4: // bottom-right
                    mouse_width = button_down_width + dx;
                    mouse_height = button_down_height + dy;
                    break;
                default:
                    // Should not happen, but default to bottom-right
                    mouse_width = button_down_width + dx;
                    mouse_height = button_down_height + dy;
                    break;
            }

            const int min_width = 64;
            const int min_height = 36;

            // Enforce minimum size
            if (mouse_width < min_width) mouse_width = min_width;
            if (mouse_height < min_height) mouse_height = min_height;

            int new_left = button_down_left;
            int new_top = button_down_top;

            // Calculate position adjustment based on which corner is being resized
            switch (resize_edge) {
                case 1: // top-left
                    new_left = button_down_left + (button_down_width - mouse_width);
                    new_top = button_down_top + (button_down_height - mouse_height);
                    break;
                case 2: // top-right
                    new_top = button_down_top + (button_down_height - mouse_height);
                    break;
                case 3: // bottom-left
                    new_left = button_down_left + (button_down_width - mouse_width);
                    break;
                case 4: // bottom-right
                    // No position change
                    break;
                default:
                    // Should not happen
                    break;
            }

            // Apply resize immediately (no aspect ratio patching during drag)
            int current_left, current_top;
            get_position (out current_left, out current_top);
            
            if (new_left != current_left || new_top != current_top) {
                move (new_left, new_top);
            }
            resize (mouse_width, mouse_height);
        }
        return true;
    }

    private bool on_key_press (Gdk.EventKey event)
    {
        // Shift+F1: Show keyboard shortcuts window
        if ((event.state & Gdk.ModifierType.SHIFT_MASK) != 0 && event.keyval == Gdk.Key.F1) {
            var app = GLib.Application.get_default() as Cheese.Application;
            if (app != null) {
                app.on_shortcuts();
            }
            return true;
        }

        // F2: Toggle auto-shoot
        if (event.keyval == Gdk.Key.F2 && (event.state & Gdk.ModifierType.MODIFIER_MASK) == 0) {
            var app = GLib.Application.get_default() as Cheese.Application;
            if (app != null) {
                app.toggle_auto_shoot();
            }
            return true;
        }

        // Ctrl+0 or Alt+C: Reset brightness, contrast, hue, saturation, lux, canvas, and opacity
        bool alt_pressed = (event.state & Gdk.ModifierType.MOD1_MASK) != 0;
        bool ctrl_pressed = (event.state & Gdk.ModifierType.CONTROL_MASK) != 0;
        bool shift_pressed = (event.state & Gdk.ModifierType.SHIFT_MASK) != 0;
        bool no_modifier = (event.state & Gdk.ModifierType.MODIFIER_MASK) == 0;

        uint keyval = event.keyval;
        uint keyval_lower = Gdk.keyval_to_lower (event.keyval);
 
        if ((alt_pressed && keyval == 'c') || (ctrl_pressed && keyval == '0')) {
            if (camera == null)
                return false;
            
            // Reset to default values
            double default_opacity = 1.0;

            double default_contrast = 1.0;
            double default_hue = 0.0;
            double default_saturation = 1.0;
            double default_brightness = 0.0;
            
            double default_canvas_zoom = 1.0;
            double default_canvas_rotation = 0.0;
            double default_canvas_pan_x = 0.0;
            double default_canvas_pan_y = 0.0;
            
            double default_lux = 0.0;
            double default_lux_black = 0.0;
            double default_lux_shadow = 0.0;
            double default_lux_midtone = 0.0;
            double default_lux_highlight = 0.0;
            double default_lux_white = 0.0;
            double default_lux_red = 0.0;
            double default_lux_orange = 0.0;
            double default_lux_yellow = 0.0;
            double default_lux_green = 0.0;
            double default_lux_cyan = 0.0;
            double default_lux_blue = 0.0;
            double default_lux_magenta = 0.0;
            double default_lux_color_breadth = 0.3;
            
            double default_fft_blur = 0.0;
            double default_fft_sharp = 0.0;
            
            bool canvas_before_balance = camera.get_canvas_before_balance ();

            // Apply to camera
            camera.set_balance_property ("brightness", default_brightness);
            camera.set_balance_property ("contrast", default_contrast);
            camera.set_balance_property ("hue", default_hue);
            camera.set_balance_property ("saturation", default_saturation);
            
            if (canvas_before_balance) {
                camera.set_canvas_zoom (default_canvas_zoom);
                camera.set_canvas_rotation (default_canvas_rotation);
                camera.set_canvas_pan_x (default_canvas_pan_x);
                camera.set_canvas_pan_y (default_canvas_pan_y);
            } else {
                // canvas after fft for diagnostic
                // so don't reset it
            }

            camera.set_lux (default_lux);
            camera.set_lux_black (default_lux_black);
            camera.set_lux_shadow (default_lux_shadow);
            camera.set_lux_midtone (default_lux_midtone);
            camera.set_lux_highlight (default_lux_highlight);
            camera.set_lux_white (default_lux_white);
            camera.set_lux_red (default_lux_red);
            camera.set_lux_orange (default_lux_orange);
            camera.set_lux_yellow (default_lux_yellow);
            camera.set_lux_green (default_lux_green);
            camera.set_lux_cyan (default_lux_cyan);
            camera.set_lux_blue (default_lux_blue);
            camera.set_lux_magenta (default_lux_magenta);
            camera.set_lux_color_breadth (default_lux_color_breadth);
            camera.set_fft_blur (default_fft_blur);
            camera.set_fft_sharp (default_fft_sharp);
            
            // Save to settings
            settings.set_double ("brightness", default_brightness);
            settings.set_double ("contrast", default_contrast);
            settings.set_double ("hue", default_hue);
            settings.set_double ("saturation", default_saturation);
            
            settings.set_double ("canvas-zoom", default_canvas_zoom);
            settings.set_double ("canvas-rotation", default_canvas_rotation);
            settings.set_double ("canvas-pan-x", default_canvas_pan_x);
            settings.set_double ("canvas-pan-y", default_canvas_pan_y);

            settings.set_double ("lux", default_lux);
            settings.set_double ("lux-black", default_lux_black);
            settings.set_double ("lux-shadow", default_lux_shadow);
            settings.set_double ("lux-midtone", default_lux_midtone);
            settings.set_double ("lux-highlight", default_lux_highlight);
            settings.set_double ("lux-white", default_lux_white);
            settings.set_double ("lux-orange", default_lux_orange);
            settings.set_double ("lux-red", default_lux_red);
            settings.set_double ("lux-yellow", default_lux_yellow);
            settings.set_double ("lux-green", default_lux_green);
            settings.set_double ("lux-cyan", default_lux_cyan);
            settings.set_double ("lux-blue", default_lux_blue);
            settings.set_double ("lux-magenta", default_lux_magenta);
            settings.set_double ("lux-color-breadth", default_lux_color_breadth);

            settings.set_double ("fft-blur", default_fft_blur);
            settings.set_double ("fft-sharp", default_fft_sharp);
            
            // Reset opacity
            set_opacity (default_opacity);
            return true;
        }
        
        if (camera == null)
            return false;
        
        double step = 0.01;
        double current_value;
        double new_value;
        
        int key_class = 0; // c for canvas, l for lux, f for fft

        switch (keyval_lower) {
            case Gdk.Key.Left:
                if (ctrl_pressed) {
                    // Rotate clockwise 90 degrees
                    current_value = settings.get_double ("canvas-rotation");
                    new_value = current_value + 90.0;
                    // Normalize to -360 to 360 range
                    while (new_value > 360.0) new_value -= 360.0;
                    camera.set_canvas_rotation (new_value);
                    settings.set_double ("canvas-rotation", new_value);
                    key_class = 'c';
                    break;
                } else if (alt_pressed) {
                    // Rotate clockwise 2.5 degrees
                    current_value = settings.get_double ("canvas-rotation");
                    new_value = current_value + 2.5;
                    // Normalize to -360 to 360 range
                    while (new_value > 360.0) new_value -= 360.0;
                    camera.set_canvas_rotation (new_value);
                    settings.set_double ("canvas-rotation", new_value);
                    key_class = 'c';
                    break;
                } else if (no_modifier) {
                    // Pan left (move view right, so pan_x decreases)
                    current_value = settings.get_double ("canvas-pan-x");
                    new_value = current_value - 10.0;
                    camera.set_canvas_pan_x (new_value);
                    settings.set_double ("canvas-pan-x", new_value);
                    key_class = 'c';
                    break;
                }
                break;

            case Gdk.Key.Right:
                if (ctrl_pressed) {
                    // Rotate counter-clockwise 90 degrees
                    current_value = settings.get_double ("canvas-rotation");
                    new_value = current_value - 90.0;
                    // Normalize to -360 to 360 range
                    while (new_value < -360.0) new_value += 360.0;
                    camera.set_canvas_rotation (new_value);
                    settings.set_double ("canvas-rotation", new_value);
                    key_class = 'c';
                    break;
                } else if (alt_pressed) {
                    // Rotate counter-clockwise 2.5 degrees
                    current_value = settings.get_double ("canvas-rotation");
                    new_value = current_value - 2.5;
                    // Normalize to -360 to 360 range
                    while (new_value < -360.0) new_value += 360.0;
                    camera.set_canvas_rotation (new_value);
                    settings.set_double ("canvas-rotation", new_value);
                    key_class = 'c';
                    break;
                } else if (no_modifier) {
                    // Pan right (move view left, so pan_x increases)
                    current_value = settings.get_double ("canvas-pan-x");
                    new_value = current_value + 10.0;
                    camera.set_canvas_pan_x (new_value);
                    settings.set_double ("canvas-pan-x", new_value);
                    key_class = 'c';
                    break;
                }
                break;

            case Gdk.Key.Up:
                if (shift_pressed) {
                    // Zoom in (shift + up)
                    current_value = settings.get_double ("canvas-zoom");
                    new_value = current_value + 0.05;
                    new_value = new_value.clamp (0.1, 10.0);
                    camera.set_canvas_zoom (new_value);
                    settings.set_double ("canvas-zoom", new_value);
                    key_class = 'c';
                    break;
                } else if (no_modifier) {
                    // Pan up (move view down, so pan_y decreases)
                    current_value = settings.get_double ("canvas-pan-y");
                    new_value = current_value - 10.0;
                    camera.set_canvas_pan_y (new_value);
                    settings.set_double ("canvas-pan-y", new_value);
                    key_class = 'c';
                    break;
                }
                break;

            case Gdk.Key.Down:
                if (shift_pressed) {
                    // Zoom out (shift + down)
                    current_value = settings.get_double ("canvas-zoom");
                    new_value = current_value - 0.05;
                    new_value = new_value.clamp (0.1, 10.0);
                    camera.set_canvas_zoom (new_value);
                    settings.set_double ("canvas-zoom", new_value);
                    key_class = 'c';
                    break;
                } else if (no_modifier) {
                    // Pan down (move view up, so pan_y increases)
                    current_value = settings.get_double ("canvas-pan-y");
                    new_value = current_value + 10.0;
                    camera.set_canvas_pan_y (new_value);
                    settings.set_double ("canvas-pan-y", new_value);
                    key_class = 'c';
                    break;
                }
                break;

            case Gdk.Key.Home:
                if (shift_pressed) {
                    // Reset zoom (shift + home)
                    current_value = settings.get_double ("canvas-zoom");
                    new_value = 1.0;
                    camera.set_canvas_zoom (new_value);
                    settings.set_double ("canvas-zoom", new_value);
                    key_class = 'c';
                    break;
                }
                break;

            case 't':
            case 'T':
                // Toggle canvas position between before balance and after lux
                if (no_modifier) {
                    camera.toggle_canvas_position ();
                    GLib.debug ("Canvas position toggled: %s",
                        camera.get_canvas_before_balance () ? "before balance" : "after fft");
                    return true;
                }
                break;

            case '1': // black: 1+ increase, !- decrease, ctrl+1 reset
                if (ctrl_pressed) {
                    current_value = settings.get_double ("lux-black");
                    new_value = 0.0;
                    camera.set_lux_black (new_value);
                    settings.set_double ("lux-black", new_value);
                    key_class = 'l';
                    break;
                }
                current_value = settings.get_double ("lux-black");
                new_value = shift_pressed ? current_value - step : current_value + step;
                new_value = new_value.clamp (-2.0, 2.0);
                camera.set_lux_black (new_value);
                settings.set_double ("lux-black", new_value);
                key_class = 'l';
                break;
            case '!':
                current_value = settings.get_double ("lux-black");
                new_value = current_value - step;
                new_value = new_value.clamp (-2.0, 2.0);
                camera.set_lux_black (new_value);
                settings.set_double ("lux-black", new_value);
                key_class = 'l';
                break;
            case '2': // shadow: 2+ increase, @- decrease, ctrl+2 reset
                if (ctrl_pressed) {
                    current_value = settings.get_double ("lux-shadow");
                    new_value = 0.0;
                    camera.set_lux_shadow (new_value);
                    settings.set_double ("lux-shadow", new_value);
                    key_class = 'l';
                    break;
                }
                current_value = settings.get_double ("lux-shadow");
                new_value = shift_pressed ? current_value - step : current_value + step;
                new_value = new_value.clamp (-2.0, 2.0);
                camera.set_lux_shadow (new_value);
                settings.set_double ("lux-shadow", new_value);
                key_class = 'l';
                break;
            case '@':
                current_value = settings.get_double ("lux-shadow");
                new_value = current_value - step;
                new_value = new_value.clamp (-2.0, 2.0);
                camera.set_lux_shadow (new_value);
                settings.set_double ("lux-shadow", new_value);
                key_class = 'l';
                break;
            case '3': // midtone: 3+ increase, #- decrease, ctrl+3 reset
                if (ctrl_pressed) {
                    current_value = settings.get_double ("lux-midtone");
                    new_value = 0.0;
                    camera.set_lux_midtone (new_value);
                    settings.set_double ("lux-midtone", new_value);
                    key_class = 'l';
                    break;
                }
                current_value = settings.get_double ("lux-midtone");
                new_value = shift_pressed ? current_value - step : current_value + step;
                new_value = new_value.clamp (-2.0, 2.0);
                camera.set_lux_midtone (new_value);
                settings.set_double ("lux-midtone", new_value);
                key_class = 'l';
                break;
            case '#':
                current_value = settings.get_double ("lux-midtone");
                new_value = current_value - step;
                new_value = new_value.clamp (-2.0, 2.0);
                camera.set_lux_midtone (new_value);
                settings.set_double ("lux-midtone", new_value);
                key_class = 'l';
                break;
            case '4': // highlight: 4+ increase, $- decrease, ctrl+4 reset
                if (ctrl_pressed) {
                    current_value = settings.get_double ("lux-highlight");
                    new_value = 0.0;
                    camera.set_lux_highlight (new_value);
                    settings.set_double ("lux-highlight", new_value);
                    key_class = 'l';
                    break;
                }
                current_value = settings.get_double ("lux-highlight");
                new_value = shift_pressed ? current_value - step : current_value + step;
                new_value = new_value.clamp (-2.0, 2.0);
                camera.set_lux_highlight (new_value);
                settings.set_double ("lux-highlight", new_value);
                key_class = 'l';
                break;
            case '$':
                current_value = settings.get_double ("lux-highlight");
                new_value = current_value - step;
                new_value = new_value.clamp (-2.0, 2.0);
                camera.set_lux_highlight (new_value);
                settings.set_double ("lux-highlight", new_value);
                key_class = 'l';
                break;
            case '5': // white: 5+ increase, %- decrease, ctrl+5 reset
                if (ctrl_pressed) {
                    current_value = settings.get_double ("lux-white");
                    new_value = 0.0;
                    camera.set_lux_white (new_value);
                    settings.set_double ("lux-white", new_value);
                    key_class = 'l';
                    break;
                }
                current_value = settings.get_double ("lux-white");
                new_value = shift_pressed ? current_value - step : current_value + step;
                new_value = new_value.clamp (-2.0, 2.0);
                camera.set_lux_white (new_value);
                settings.set_double ("lux-white", new_value);
                key_class = 'l';
                break;
            case '%':
                current_value = settings.get_double ("lux-white");
                new_value = current_value - step;
                new_value = new_value.clamp (-2.0, 2.0);
                camera.set_lux_white (new_value);
                settings.set_double ("lux-white", new_value);
                key_class = 'l';
                break;

            case 'r': // red: r+ increase, R- decrease
                current_value = settings.get_double ("lux-red");
                new_value = shift_pressed ? current_value - step : current_value + step;
                new_value = new_value.clamp (-2.0, 2.0);
                camera.set_lux_red (new_value);
                settings.set_double ("lux-red", new_value);
                key_class = 'L';
                break;
            case 'o': // orange: o+ increase, O- decrease
                current_value = settings.get_double ("lux-orange");
                new_value = shift_pressed ? current_value - step : current_value + step;
                new_value = new_value.clamp (-2.0, 2.0);
                camera.set_lux_orange (new_value);
                settings.set_double ("lux-orange", new_value);
                key_class = 'L';
                break;
            case 'y': // yellow: y+ increase, Y- decrease
                current_value = settings.get_double ("lux-yellow");
                new_value = shift_pressed ? current_value - step : current_value + step;
                new_value = new_value.clamp (-2.0, 2.0);
                camera.set_lux_yellow (new_value);
                settings.set_double ("lux-yellow", new_value);
                key_class = 'L';
                break;
            case 'g': // green: g+ increase, G- decrease
                current_value = settings.get_double ("lux-green");
                new_value = shift_pressed ? current_value - step : current_value + step;
                new_value = new_value.clamp (-2.0, 2.0);
                camera.set_lux_green (new_value);
                settings.set_double ("lux-green", new_value);
                key_class = 'L';
                break;
            case 'c': // cyan: c+ increase, C- decrease
                current_value = settings.get_double ("lux-cyan");
                new_value = shift_pressed ? current_value - step : current_value + step;
                new_value = new_value.clamp (-2.0, 2.0);
                camera.set_lux_cyan (new_value);
                settings.set_double ("lux-cyan", new_value);
                key_class = 'L';
                break;
            case 'b': // blue: b+ increase, B- decrease (note: blur uses different key)
                current_value = settings.get_double ("lux-blue");
                new_value = shift_pressed ? current_value - step : current_value + step;
                new_value = new_value.clamp (-2.0, 2.0);
                camera.set_lux_blue (new_value);
                settings.set_double ("lux-blue", new_value);
                key_class = 'L';
                break;
            case 'm': // magenta: m+ increase, M- decrease
                current_value = settings.get_double ("lux-magenta");
                new_value = shift_pressed ? current_value - step : current_value + step;
                new_value = new_value.clamp (-2.0, 2.0);
                camera.set_lux_magenta (new_value);
                settings.set_double ("lux-magenta", new_value);
                key_class = 'L';
                break;

            case Gdk.Key.minus: // exposure: - decrease, = increase
                if (!ctrl_pressed && !alt_pressed && !shift_pressed) {
                    current_value = settings.get_double ("lux");
                    new_value = current_value - step;
                    new_value = new_value.clamp (-10.0, 10.0);
                    camera.set_lux (new_value);
                    settings.set_double ("lux", new_value);
                    key_class = 'l';
                    break;
                }
                // Fall through to contrast if shift is pressed (_)
                break;

            case Gdk.Key.equal: // exposure: = increase
                if (!ctrl_pressed && !alt_pressed && !shift_pressed) {
                    current_value = settings.get_double ("lux");
                    new_value = current_value + step;
                    new_value = new_value.clamp (-10.0, 10.0);
                    camera.set_lux (new_value);
                    settings.set_double ("lux", new_value);
                    key_class = 'l';
                    break;
                }
                // Fall through to contrast if shift is pressed (+)
                break;

            case Gdk.Key.underscore: // contrast: _ decrease
                if (shift_pressed && keyval_lower == '_') {
                    current_value = settings.get_double ("contrast");
                    new_value = current_value - step;
                    new_value = new_value.clamp (0.0, 1.99);
                    camera.set_balance_property ("contrast", new_value);
                    settings.set_double ("contrast", new_value);
                    key_class = 'b';
                    break;
                }
                break;

            case Gdk.Key.plus: // contrast: + increase
                if (shift_pressed && keyval_lower == '+') {
                    current_value = settings.get_double ("contrast");
                    new_value = current_value + step;
                    new_value = new_value.clamp (0.0, 1.99);
                    camera.set_balance_property ("contrast", new_value);
                    settings.set_double ("contrast", new_value);
                    key_class = 'b';
                    break;
                }
                break;

            case Gdk.Key.BackSpace: // reset exposure, shift+backspace reset contrast
                if (shift_pressed) {
                    // Reset contrast
                    current_value = settings.get_double ("contrast");
                    new_value = 1.0;
                    camera.set_balance_property ("contrast", new_value);
                    settings.set_double ("contrast", new_value);
                    key_class = 'b';
                    break;
                } else {
                    // Reset exposure
                    current_value = settings.get_double ("lux");
                    new_value = 0.0;
                    camera.set_lux (new_value);
                    settings.set_double ("lux", new_value);
                    key_class = 'l';
                    break;
                }

            case '[': // luminance: [ decrease
                current_value = settings.get_double ("lux-midtone");
                new_value = current_value - step;
                new_value = new_value.clamp (-2.0, 2.0);
                camera.set_lux_midtone (new_value);
                settings.set_double ("lux-midtone", new_value);
                key_class = 'l';
                break;
            case ']': // luminance: ] increase
                current_value = settings.get_double ("lux-midtone");
                new_value = current_value + step;
                new_value = new_value.clamp (-2.0, 2.0);
                camera.set_lux_midtone (new_value);
                settings.set_double ("lux-midtone", new_value);
                key_class = 'l';
                break;
            case '\\': // reset luminance
                current_value = settings.get_double ("lux-midtone");
                new_value = 0.0;
                camera.set_lux_midtone (new_value);
                settings.set_double ("lux-midtone", new_value);
                key_class = 'l';
                break;
            case '{': // saturation: { decrease
                current_value = settings.get_double ("saturation");
                new_value = current_value - step;
                new_value = new_value.clamp (0.0, 2.0);
                camera.set_balance_property ("saturation", new_value);
                settings.set_double ("saturation", new_value);
                key_class = 'b';
                break;
            case '}': // saturation: } increase
                current_value = settings.get_double ("saturation");
                new_value = current_value + step;
                new_value = new_value.clamp (0.0, 2.0);
                camera.set_balance_property ("saturation", new_value);
                settings.set_double ("saturation", new_value);
                key_class = 'b';
                break;
            case '|': // reset saturation
                current_value = settings.get_double ("saturation");
                new_value = 1.0;
                camera.set_balance_property ("saturation", new_value);
                settings.set_double ("saturation", new_value);
                key_class = 'b';
                break;
            case '<': // hue: < decrease
                current_value = settings.get_double ("hue");
                new_value = current_value - step;
                new_value = new_value.clamp (-1.0, 1.0);
                camera.set_balance_property ("hue", new_value);
                settings.set_double ("hue", new_value);
                key_class = 'b';
                break;
            case '>': // hue: > increase
                current_value = settings.get_double ("hue");
                new_value = current_value + step;
                new_value = new_value.clamp (-1.0, 1.0);
                camera.set_balance_property ("hue", new_value);
                settings.set_double ("hue", new_value);
                key_class = 'b';
                break;
            case '?': // reset hue
                current_value = settings.get_double ("hue");
                new_value = 0.0;
                camera.set_balance_property ("hue", new_value);
                settings.set_double ("hue", new_value);
                key_class = 'b';
                break;
            case '7': // blur/sharp: 7 decreases blur or increases sharp, shift-7 decreases sharp or increases blur, ctrl-7 reset
                if (ctrl_pressed) {
                    // Reset both blur and sharp
                    camera.set_fft_blur (0.0);
                    settings.set_double ("fft-blur", 0.0);
                    camera.set_fft_sharp (0.0);
                    settings.set_double ("fft-sharp", 0.0);
                    key_class = 'f';
                    break;
                }
                double blur_value = settings.get_double ("fft-blur");
                double sharp_value = settings.get_double ("fft-sharp");

                if (shift_pressed) {
                    // shift-7: decrease sharp, or if sharp <= 0, increase blur
                    if (sharp_value > 0) {
                        sharp_value = (sharp_value - step).clamp (0, 10.0);
                        camera.set_fft_sharp (sharp_value);
                        settings.set_double ("fft-sharp", sharp_value);
                    } else {
                        blur_value = (blur_value + step).clamp (0, 10.0);
                        camera.set_fft_blur (blur_value);
                        settings.set_double ("fft-blur", blur_value);
                    }
                } else {
                    // 7: decrease blur, or if blur <= 0, increase sharp
                    if (blur_value > 0) {
                        blur_value = (blur_value - step).clamp (0, 10.0);
                        camera.set_fft_blur (blur_value);
                        settings.set_double ("fft-blur", blur_value);
                    } else {
                        sharp_value = (sharp_value + step).clamp (0, 10.0);
                        camera.set_fft_sharp (sharp_value);
                        settings.set_double ("fft-sharp", sharp_value);
                    }
                }
                key_class = 'f';
                break;
            case 'h':
                select_effect_by_name("Flip");
                return true;
            case 'v':
                select_effect_by_name("Flip Vertical");
                return true;
        }
        switch (key_class) {
            case 'b':
                GLib.debug("balance: contrast %.2f hue %.2f saturation %.2f brightness %.2f",
                    settings.get_double ("contrast"),
                    settings.get_double ("hue"),
                    settings.get_double ("saturation"),
                    settings.get_double ("brightness"));
                return true;
            case 'c':
                GLib.debug("canvas: zoom %.2f, rotation %.2f, panx %.2f, pan_y %.2f",
                    settings.get_double ("canvas-zoom"),
                    settings.get_double ("canvas-rotation"),
                    settings.get_double ("canvas-pan-x"),
                    settings.get_double ("canvas-pan-y"));
                return true;
            case 'l':
                GLib.debug("lux: [%.2f] black %.2f shadow %.2f mid %.2f highlight %.2f white %.2f",
                    settings.get_double ("lux"),
                    settings.get_double ("lux-black"),
                    settings.get_double ("lux-shadow"),
                    settings.get_double ("lux-midtone"),
                    settings.get_double ("lux-highlight"),
                    settings.get_double ("lux-white")
                    );
                return true;
            case 'L':
                GLib.debug("lux: red %.2f orange %.2f yellow %.2f green %.2f cyan %.2f blue %.2f magenta %.2f",
                    settings.get_double ("lux-red"),
                    settings.get_double ("lux-orange"),
                    settings.get_double ("lux-yellow"),
                    settings.get_double ("lux-green"),
                    settings.get_double ("lux-cyan"),
                    settings.get_double ("lux-blue"),
                    settings.get_double ("lux-magenta"));
                return true;
            case 'f':
                GLib.debug("fft: blur %.3f, sharp %.3f", 
                    settings.get_double ("fft-blur"),
                    settings.get_double ("fft-sharp"));
                return true;
        }
        return false;
    }
    
    private void select_effect_by_name(string name) {
        if (selected_effect.name == name) {
            // toggle off
            name = "No Effect";
        }
        Effect effect = effects_manager.get_effect (name);
        if (effect!= null) {
            this.selected_effect = effect;
            camera.set_effect (selected_effect);
            settings.set_string ("selected-effect", selected_effect.name);
        }
    }

}
