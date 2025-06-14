/* cheese-common.vapi generated by vapigen, do not modify. */

[CCode (cprefix = "Cheese", lower_case_cprefix = "cheese_")]
namespace Cheese
{
  [CCode (cheader_filename = "cheese.h")]
  public static bool init([CCode (array_length_cname = "argc", array_length_pos = 0.5)] ref unowned string[]? argv);

  [CCode (cheader_filename = "cheese-gtk.h")]
  public static bool gtk_init([CCode (array_length_cname = "argc", array_length_pos = 0.5)] ref unowned string[]? argv);

  [CCode (cheader_filename = "cheese-effect.h")]
  public class Effect : GLib.Object
  {
    [CCode (has_construct_function = false)]
    public Effect (string name, string pipeline_desc);
    public unowned string get_name ();
    public unowned string get_pipeline_desc ();
    public string name {get;}
    public string pipeline_desc {get;}
    [NoAccessorMethod]
    public Gst.Element control_valve {get; set;}

    public void enable_preview();
    public void disable_preview();
    public bool is_preview_connected();

    public static Cheese.Effect load_from_file (string fname);
    public static GLib.List<Cheese.Effect> load_effects ();
  }

  [CCode (cheader_filename = "cheese-camera.h")]
  public class Camera : GLib.Object
  {
    [CCode (has_construct_function = false)]
    public Camera (Clutter.Actor video_texture, string camera_device_node, int x_resolution, int y_resolution);
    public bool                        get_balance_property_range (string property, double min, double max, double def);
    public GLib.GenericArray<unowned Cheese.CameraDevice> get_camera_devices ();
    public unowned Cheese.VideoFormat  get_current_video_format ();
    public int                         get_num_camera_devices ();
    public unowned Cheese.CameraDevice get_selected_device ();
    public GLib.List<unowned Cheese.VideoFormat> get_video_formats ();
    public bool                        has_camera ();
    public void                        play ();
    public void                        set_balance_property (string property, double value);
    public void                        set_device (Cheese.CameraDevice device);
    public void                        set_effect (Cheese.Effect effect);
    public void                        toggle_effects_pipeline (bool active);
    public void                        connect_effect_texture (Cheese.Effect effect, Clutter.Actor texture);
    public void                        set_video_format (Cheese.VideoFormat format);
    public void                        setup (Cheese.CameraDevice? device = null) throws GLib.Error;
    public void                        start_video_recording (string filename);
    public void                        stop ();
    public void                        stop_video_recording ();
    public bool                        switch_camera_device ();
    public bool                        take_photo (string filename);
    public bool                        take_photo_pixbuf ();
    public string                      get_recorded_time ();
    [NoAccessorMethod]
    public string device_node {owned get; set;}
    [NoAccessorMethod]
    public Cheese.VideoFormat format {owned get; set;}
    [NoAccessorMethod]
    public void *video_texture {get; set;}
    [NoAccessorMethod]
    public uint num_camera_devices {get;}
    public virtual signal void photo_saved ();
    public virtual signal void photo_taken (Gdk.Pixbuf pixbuf);
    public virtual signal void video_saved ();
    public virtual signal void state_flags_changed (Gst.State new_state);
  }
  [CCode (cheader_filename = "cheese-camera-device.h")]
  public class CameraDevice : GLib.Object, GLib.Initable
  {
    [CCode (has_construct_function = false)]
    public CameraDevice (string uuid, string device_node, string name, int v4lapi_version) throws GLib.Error;
    public Cheese.VideoFormat get_best_format ();
    public Gst.Caps get_caps_for_format (Cheese.VideoFormat format);
    public GLib.List<unowned Cheese.VideoFormat> get_format_list ();
    public unowned string             get_name ();
    public unowned string             get_path ();
    public Gst.Element                get_src ();
    [NoAccessorMethod]
    public Gst.Device device {get; construct;}
    [NoAccessorMethod]
    public string name {get;}
    [NoAccessorMethod]
    public string path {get;}
  }

  [CCode (cheader_filename = "cheese-camera-device-monitor.h")]
  public class CameraDeviceMonitor : GLib.Object
  {
    [CCode (has_construct_function = false)]
    public CameraDeviceMonitor ();
    public void                coldplug ();
    public virtual signal void added (Gst.Device device);
    public virtual signal void removed (Gst.Device device);
  }


  [CCode (cheader_filename = "cheese-fileutil.h")]
  public class FileUtil  : GLib.Object
  {
    [CCode (cname = "cheese_fileutil_new", has_construct_function = false)]
    public FileUtil ();
    [CCode (cname = "cheese_fileutil_get_new_media_filename")]
    public string get_new_media_filename (Cheese.MediaMode mode);
    [CCode (cname = "cheese_fileutil_get_photo_path")]
    public unowned string get_photo_path ();
    [CCode (cname = "cheese_fileutil_set_photo_path")]
    public unowned string set_photo_path (string path);
    [CCode (cname = "cheese_fileutil_get_video_path")]
    public unowned string get_video_path ();
    [CCode (cname = "cheese_fileutil_set_video_path")]
    public unowned string set_video_path (string path);
    [CCode (cname = "cheese_fileutil_reset_burst")]
    public void reset_burst ();
  }

  [CCode (cheader_filename = "cheese-flash.h")]
  public class Flash : Gtk.Window
  {
    [CCode (has_construct_function = false)]
    public Flash (Gtk.Widget parent);
    public void fire ();
  }

  [Compact]
  [CCode (type_id = "CHEESE_TYPE_VIDEO_FORMAT", cheader_filename = "cheese-camera-device.h", copy_function = "g_boxed_copy", free_function = "g_boxed_free")]
  public class VideoFormat
  {
    public int height;
    public int width;
  }
  [CCode (cprefix = "CHEESE_MEDIA_MODE_", has_type_id = false, cheader_filename = "cheese-fileutil.h")]
  public enum MediaMode
  {
    PHOTO,
    VIDEO,
    BURST
  }
  [CCode (cprefix = "CHEESE_WIDGET_STATE_", cheader_filename = "cheese-fileutil.h")]
  public enum WidgetState
  {
    NONE,
    READY,
    ERROR
  }
  [CCode (cheader_filename = "cheese-fileutil.h")]
  public const string PHOTO_NAME_SUFFIX;
  [CCode (cheader_filename = "cheese-fileutil.h")]
  public const string VIDEO_NAME_SUFFIX;
}
