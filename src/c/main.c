#include <pebble.h>
#include <stdlib.h>
#include <string.h>

#define NUM_SCENES 7
#define NUM_JEEP_FRAMES 4
#define TERRAIN_SAMPLES 25
/* Terrain tables are authored against the 200x228 emery art. */
#define TERRAIN_SRC_W 200
#define TERRAIN_SRC_H 228
#ifdef PBL_ROUND
#define JEEP_W 80
#define JEEP_H 52
#else
#define JEEP_W 64
#define JEEP_H 40
#endif
#define DRIVE_TICK_MS 80
#define PERSIST_SETTINGS 1
#define PERSIST_LAST_DRIVE_HOUR 2

typedef enum {
  TOD_NIGHT = 0,
  TOD_MORNING,
  TOD_DAY,
  TOD_EVENING
} TimeOfDay;

typedef struct {
  GColor text_color;
  bool show_date;
  bool animations_enabled;
  bool vibrate_on_drive;
  bool use_bold_time;
  uint8_t scene_mode;   /* 0 = daily, 1 = fixed */
  uint8_t fixed_scene;  /* 0..6 */
  uint8_t drive_minute; /* 0..59 */
} Settings;

static Window *s_window;
static Layer *s_canvas;
static TextLayer *s_time_shadow;
static TextLayer *s_time_layer;
static TextLayer *s_date_shadow;
static TextLayer *s_date_layer;

static GBitmap *s_scene_bitmap;
static GBitmap *s_jeep_frames[NUM_JEEP_FRAMES];
static int s_loaded_scene = -1;

static Settings s_settings;
static char s_time_buf[16];
static char s_date_buf[32];

static bool s_driving;
static int16_t s_jeep_x;
static uint8_t s_jeep_frame;
static AppTimer *s_drive_timer;
static int s_last_drive_hour = -1;
static int s_bounds_w;
static int s_bounds_h;
/* Exact on-screen rect used for the scene bitmap (1:1 blit, never scaled). */
static GRect s_scene_dest;

/* Terrain Y (road surface) sampled across asset width 200, for each scene. */
static const uint8_t s_terrain[NUM_SCENES][TERRAIN_SAMPLES] = {
  /* desert */   {213,214,214,215,220,222,204,200,201,205,203,202,204,212,208,202,201,202,219,218,213,212,216,211,213},
  /* forest */   {195,195,204,208,216,202,201,202,204,221,207,206,202,214,218,206,203,207,204,203,203,199,200,195,199},
  /* mountain */ {200,201,200,201,198,199,199,199,200,201,200,200,200,200,199,200,200,199,198,199,201,199,199,200,199},
  /* beach */    {200,198,199,200,201,200,200,199,201,201,200,201,199,200,198,200,199,197,198,201,200,198,199,200,202},
  /* snow */     {190,190,186,188,192,191,180,177,179,180,168,167,170,166,164,164,168,173,185,188,186,188,192,193,192},
  /* farm */     {200,200,200,201,201,202,202,202,202,202,201,201,202,202,201,200,201,202,202,202,202,201,201,200,201},
  /* moab */     {168,170,172,178,185,188,170,168,172,171,173,173,173,172,172,171,179,186,170,178,185,180,185,188,184}
};

static const uint32_t s_scene_ids[NUM_SCENES] = {
  RESOURCE_ID_SCENE_DESERT,
  RESOURCE_ID_SCENE_FOREST,
  RESOURCE_ID_SCENE_MOUNTAIN,
  RESOURCE_ID_SCENE_BEACH,
  RESOURCE_ID_SCENE_SNOW,
  RESOURCE_ID_SCENE_FARM,
  RESOURCE_ID_SCENE_MOAB
};

static const char *s_scene_names[NUM_SCENES] = {
  "Desert Canyon",
  "Forest Trail",
  "Mountain Pass",
  "Beach Dunes",
  "Snowy Alpine",
  "Farm Country",
  "Moab Slickrock"
};

static void settings_set_defaults(void) {
  s_settings.text_color = GColorWhite;
  s_settings.show_date = true;
  s_settings.animations_enabled = true;
  s_settings.vibrate_on_drive = false;
  s_settings.use_bold_time = true;
  s_settings.scene_mode = 0;
  s_settings.fixed_scene = 0;
  s_settings.drive_minute = 0;
}

static void settings_load(void) {
  settings_set_defaults();
  if (persist_exists(PERSIST_SETTINGS)) {
    persist_read_data(PERSIST_SETTINGS, &s_settings, sizeof(s_settings));
  }
  if (persist_exists(PERSIST_LAST_DRIVE_HOUR)) {
    s_last_drive_hour = persist_read_int(PERSIST_LAST_DRIVE_HOUR);
  }
}

static void settings_save(void) {
  persist_write_data(PERSIST_SETTINGS, &s_settings, sizeof(s_settings));
  persist_write_int(PERSIST_LAST_DRIVE_HOUR, s_last_drive_hour);
}

static int day_of_year(const struct tm *t) {
  static const int mdays[] = {0,31,59,90,120,151,181,212,243,273,304,334};
  int doy = mdays[t->tm_mon] + t->tm_mday;
  int leap = ((t->tm_year + 1900) % 4 == 0);
  if (leap && t->tm_mon > 1) {
    doy++;
  }
  return doy;
}

static int current_scene_index(const struct tm *t) {
  if (s_settings.scene_mode == 1) {
    return s_settings.fixed_scene % NUM_SCENES;
  }
  return day_of_year(t) % NUM_SCENES;
}

static TimeOfDay time_of_day_from_hour(int hour) {
  if (hour >= 5 && hour <= 8) {
    return TOD_MORNING;
  }
  if (hour >= 9 && hour <= 16) {
    return TOD_DAY;
  }
  if (hour >= 17 && hour <= 20) {
    return TOD_EVENING;
  }
  return TOD_NIGHT;
}

static void ensure_scene_loaded(int scene) {
  if (scene == s_loaded_scene && s_scene_bitmap) {
    return;
  }
  if (s_scene_bitmap) {
    gbitmap_destroy(s_scene_bitmap);
    s_scene_bitmap = NULL;
  }
  s_scene_bitmap = gbitmap_create_with_resource(s_scene_ids[scene]);
  s_loaded_scene = scene;
}

/*
 * Pebble does NOT scale bitmaps in graphics_draw_bitmap_in_rect — a larger
 * dest rect tiles. Always blit 1:1 using the bitmap's native size.
 */
static GRect scene_dest_for_bitmap(GRect bounds, GBitmap *bmp) {
  if (!bmp) {
    return bounds;
  }
  GRect b = gbitmap_get_bounds(bmp);
  int ox = (bounds.size.w - b.size.w) / 2;
  int oy = (bounds.size.h - b.size.h) / 2;
  return GRect(ox, oy, b.size.w, b.size.h);
}

static int terrain_y_at(int scene, int x) {
  GRect dest = s_scene_dest;
  if (dest.size.w <= 1) {
    return s_bounds_h * 3 / 4;
  }
  int local_x = x - dest.origin.x;
  if (local_x < 0) local_x = 0;
  if (local_x >= dest.size.w) local_x = dest.size.w - 1;

  /* Terrain samples are in original 200px-wide space. */
  int src_x = (local_x * (TERRAIN_SRC_W - 1)) / (dest.size.w > 1 ? dest.size.w - 1 : 1);
  int idx = (src_x * (TERRAIN_SAMPLES - 1)) / (TERRAIN_SRC_W - 1);
  int next = idx + 1;
  if (next >= TERRAIN_SAMPLES) next = TERRAIN_SAMPLES - 1;
  int span = TERRAIN_SRC_W - 1;
  int frac = (src_x * (TERRAIN_SAMPLES - 1)) % (span > 0 ? span : 1);
  int y0 = s_terrain[scene][idx];
  int y1 = s_terrain[scene][next];
  int src_y = y0 + ((y1 - y0) * frac) / (span > 0 ? span : 1);

  /* Map 228-tall source samples onto the drawn scene height. */
  return dest.origin.y + (src_y * dest.size.h) / TERRAIN_SRC_H;
}

static void draw_sky_overlay(GContext *ctx, GRect bounds, TimeOfDay tod) {
  if (tod == TOD_DAY) {
    return;
  }

  const int bands = 6;
  int sky_h = bounds.size.h * 42 / 100;
  int band_h = sky_h / bands;
  GColor colors[6];

  switch (tod) {
    case TOD_MORNING:
      colors[0] = GColorPictonBlue;
      colors[1] = GColorCeleste;
      colors[2] = GColorChromeYellow;
      colors[3] = GColorMelon;
      colors[4] = GColorSunsetOrange;
      colors[5] = GColorRajah;
      break;
    case TOD_EVENING:
      colors[0] = GColorOxfordBlue;
      colors[1] = GColorIndigo;
      colors[2] = GColorPurple;
      colors[3] = GColorFashionMagenta;
      colors[4] = GColorOrange;
      colors[5] = GColorChromeYellow;
      break;
    case TOD_NIGHT:
    default:
      colors[0] = GColorBlack;
      colors[1] = GColorBlack;
      colors[2] = GColorOxfordBlue;
      colors[3] = GColorDukeBlue;
      colors[4] = GColorBlueMoon;
      colors[5] = GColorCobaltBlue;
      break;
  }

  for (int i = 0; i < bands; i++) {
    graphics_context_set_fill_color(ctx, colors[i]);
    graphics_fill_rect(ctx, GRect(0, i * band_h, bounds.size.w, band_h + 1), 0, GCornerNone);
  }

  /* Celestial body */
  graphics_context_set_fill_color(ctx, tod == TOD_NIGHT ? GColorWhite : GColorIcterine);
  int cx = bounds.size.w * (tod == TOD_NIGHT ? 72 : 28) / 100;
  int cy = sky_h / 3;
  int r = (tod == TOD_NIGHT) ? 8 : 10;
  graphics_fill_circle(ctx, GPoint(cx, cy), r);

  if (tod == TOD_NIGHT) {
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_pixel(ctx, GPoint(bounds.size.w / 5, sky_h / 5));
    graphics_draw_pixel(ctx, GPoint(bounds.size.w * 2 / 5, sky_h / 8));
    graphics_draw_pixel(ctx, GPoint(bounds.size.w * 3 / 5, sky_h / 4));
    graphics_draw_pixel(ctx, GPoint(bounds.size.w * 4 / 5, sky_h / 6));
    graphics_draw_pixel(ctx, GPoint(bounds.size.w / 2, sky_h / 3));
  }
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  s_bounds_w = bounds.size.w;
  s_bounds_h = bounds.size.h;

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int scene = current_scene_index(t);
  TimeOfDay tod = time_of_day_from_hour(t->tm_hour);

  ensure_scene_loaded(scene);

  s_scene_dest = scene_dest_for_bitmap(bounds, s_scene_bitmap);

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  if (s_scene_bitmap) {
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    /* Dest size MUST equal bitmap size or Pebble will tile. */
    graphics_draw_bitmap_in_rect(ctx, s_scene_bitmap, s_scene_dest);
  }

  draw_sky_overlay(ctx, bounds, tod);

  if (s_driving && s_jeep_frames[s_jeep_frame % NUM_JEEP_FRAMES]) {
    GRect jeep_bounds = gbitmap_get_bounds(s_jeep_frames[s_jeep_frame % NUM_JEEP_FRAMES]);
    int jeep_w = jeep_bounds.size.w;
    int jeep_h = jeep_bounds.size.h;
    int road = terrain_y_at(scene, s_jeep_x + jeep_w / 2);
    int jeep_y = road - jeep_h + (jeep_h / 10);
    jeep_y += ((s_jeep_frame % 2) ? 1 : 0);
    if (jeep_y < 0) jeep_y = 0;
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, s_jeep_frames[s_jeep_frame % NUM_JEEP_FRAMES],
                                 GRect(s_jeep_x, jeep_y, jeep_w, jeep_h));
  }
}

static void update_clock(struct tm *t) {
  if (clock_is_24h_style()) {
    strftime(s_time_buf, sizeof(s_time_buf), "%H:%M", t);
  } else {
    strftime(s_time_buf, sizeof(s_time_buf), "%I:%M", t);
    /* strip leading zero */
    if (s_time_buf[0] == '0') {
      memmove(s_time_buf, s_time_buf + 1, strlen(s_time_buf));
    }
  }
  text_layer_set_text(s_time_shadow, s_time_buf);
  text_layer_set_text(s_time_layer, s_time_buf);

  if (s_settings.show_date) {
    strftime(s_date_buf, sizeof(s_date_buf), "%a %b %e", t);
    text_layer_set_text(s_date_shadow, s_date_buf);
    text_layer_set_text(s_date_layer, s_date_buf);
    layer_set_hidden(text_layer_get_layer(s_date_shadow), false);
    layer_set_hidden(text_layer_get_layer(s_date_layer), false);
  } else {
    layer_set_hidden(text_layer_get_layer(s_date_shadow), true);
    layer_set_hidden(text_layer_get_layer(s_date_layer), true);
  }

  TimeOfDay tod = time_of_day_from_hour(t->tm_hour);
  GColor text = s_settings.text_color;
  if (gcolor_equal(text, GColorClear)) {
    text = GColorWhite;
  }
  /* Morning: prefer dark text on bright sky when using the default white. */
  if (tod == TOD_MORNING && gcolor_equal(s_settings.text_color, GColorWhite)) {
    text = GColorBlack;
  }
  GColor shadow = gcolor_equal(text, GColorBlack) ? GColorWhite : GColorBlack;
  text_layer_set_text_color(s_time_shadow, shadow);
  text_layer_set_text_color(s_time_layer, text);
  text_layer_set_text_color(s_date_shadow, shadow);
  text_layer_set_text_color(s_date_layer, text);

  GFont time_font = fonts_get_system_font(s_settings.use_bold_time ? FONT_KEY_BITHAM_42_BOLD
                                                                     : FONT_KEY_BITHAM_42_LIGHT);
  text_layer_set_font(s_time_shadow, time_font);
  text_layer_set_font(s_time_layer, time_font);
}

static void drive_timer_cb(void *context) {
  s_drive_timer = NULL;
  if (!s_driving) {
    return;
  }
  s_jeep_x += 4;
  s_jeep_frame = (s_jeep_frame + 1) % NUM_JEEP_FRAMES;
  layer_mark_dirty(s_canvas);

  if (s_jeep_x > s_bounds_w + JEEP_W) {
    s_driving = false;
    return;
  }
  s_drive_timer = app_timer_register(DRIVE_TICK_MS, drive_timer_cb, NULL);
}

static void start_drive(struct tm *t) {
  if (!s_settings.animations_enabled) {
    return;
  }
  if (s_driving) {
    return;
  }
  int hour_key = t->tm_yday * 24 + t->tm_hour;
  if (hour_key == s_last_drive_hour) {
    return;
  }
  s_last_drive_hour = hour_key;
  settings_save();

  s_driving = true;
  s_jeep_x = -JEEP_W;
  s_jeep_frame = 0;
  if (s_settings.vibrate_on_drive) {
    vibes_short_pulse();
  }
  if (s_drive_timer) {
    app_timer_cancel(s_drive_timer);
  }
  s_drive_timer = app_timer_register(DRIVE_TICK_MS, drive_timer_cb, NULL);
  layer_mark_dirty(s_canvas);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_clock(tick_time);
  layer_mark_dirty(s_canvas);

  if ((units_changed & MINUTE_UNIT) && tick_time->tm_min == s_settings.drive_minute) {
    start_drive(tick_time);
  }
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;

  t = dict_find(iter, MESSAGE_KEY_TextColor);
  if (t) {
    s_settings.text_color = GColorFromHEX(t->value->int32);
  }
  t = dict_find(iter, MESSAGE_KEY_ShowDate);
  if (t) {
    s_settings.show_date = t->value->int32 == 1;
  }
  t = dict_find(iter, MESSAGE_KEY_AnimationsEnabled);
  if (t) {
    s_settings.animations_enabled = t->value->int32 == 1;
  }
  t = dict_find(iter, MESSAGE_KEY_VibrateOnDrive);
  if (t) {
    s_settings.vibrate_on_drive = t->value->int32 == 1;
  }
  t = dict_find(iter, MESSAGE_KEY_UseBoldTime);
  if (t) {
    s_settings.use_bold_time = t->value->int32 == 1;
  }
  t = dict_find(iter, MESSAGE_KEY_SceneMode);
  if (t) {
    /* Clay select may send string or int depending on version */
    if (t->type == TUPLE_CSTRING) {
      s_settings.scene_mode = (uint8_t)atoi(t->value->cstring);
    } else {
      s_settings.scene_mode = (uint8_t)t->value->int32;
    }
  }
  t = dict_find(iter, MESSAGE_KEY_FixedScene);
  if (t) {
    if (t->type == TUPLE_CSTRING) {
      s_settings.fixed_scene = (uint8_t)atoi(t->value->cstring);
    } else {
      s_settings.fixed_scene = (uint8_t)t->value->int32;
    }
  }
  t = dict_find(iter, MESSAGE_KEY_DriveMinute);
  if (t) {
    s_settings.drive_minute = (uint8_t)t->value->int32;
  }

  settings_save();

  time_t now = time(NULL);
  update_clock(localtime(&now));
  layer_mark_dirty(s_canvas);
  APP_LOG(APP_LOG_LEVEL_INFO, "Settings updated. Scene mode %d", s_settings.scene_mode);
}

static void apply_text_layout(GRect bounds) {
#ifdef PBL_ROUND
  int time_y = bounds.size.h * 12 / 100;
  int date_y = time_y + 46;
  int time_x = 18;
  int date_x = 24;
  int time_w = bounds.size.w - 36;
  int date_w = bounds.size.w - 48;
#else
  int time_y = 8;
  int date_y = 54;
  int time_x = 0;
  int date_x = 0;
  int time_w = bounds.size.w;
  int date_w = bounds.size.w;
#endif
  text_layer_set_text_alignment(s_time_shadow, GTextAlignmentCenter);
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  text_layer_set_text_alignment(s_date_shadow, GTextAlignmentCenter);
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  layer_set_frame(text_layer_get_layer(s_time_shadow), GRect(time_x + 1, time_y + 1, time_w, 50));
  layer_set_frame(text_layer_get_layer(s_time_layer), GRect(time_x, time_y, time_w, 50));
  layer_set_frame(text_layer_get_layer(s_date_shadow), GRect(date_x + 1, date_y + 1, date_w, 24));
  layer_set_frame(text_layer_get_layer(s_date_layer), GRect(date_x, date_y, date_w, 24));
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  s_bounds_w = bounds.size.w;
  s_bounds_h = bounds.size.h;

  s_canvas = layer_create(bounds);
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);

  s_time_shadow = text_layer_create(GRect(1, 9, bounds.size.w, 50));
  text_layer_set_background_color(s_time_shadow, GColorClear);
  text_layer_set_text_color(s_time_shadow, GColorBlack);
  text_layer_set_font(s_time_shadow, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  layer_add_child(root, text_layer_get_layer(s_time_shadow));

  s_time_layer = text_layer_create(GRect(0, 8, bounds.size.w, 50));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorWhite);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  layer_add_child(root, text_layer_get_layer(s_time_layer));

  s_date_shadow = text_layer_create(GRect(1, 55, bounds.size.w, 24));
  text_layer_set_background_color(s_date_shadow, GColorClear);
  text_layer_set_text_color(s_date_shadow, GColorBlack);
  text_layer_set_font(s_date_shadow, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  layer_add_child(root, text_layer_get_layer(s_date_shadow));

  s_date_layer = text_layer_create(GRect(0, 54, bounds.size.w, 24));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, GColorWhite);
  text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  layer_add_child(root, text_layer_get_layer(s_date_layer));

  apply_text_layout(bounds);

  const uint32_t jeep_ids[NUM_JEEP_FRAMES] = {
    RESOURCE_ID_JEEP_0, RESOURCE_ID_JEEP_1, RESOURCE_ID_JEEP_2, RESOURCE_ID_JEEP_3
  };
  for (int i = 0; i < NUM_JEEP_FRAMES; i++) {
    s_jeep_frames[i] = gbitmap_create_with_resource(jeep_ids[i]);
  }

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  ensure_scene_loaded(current_scene_index(t));
  update_clock(t);
  APP_LOG(APP_LOG_LEVEL_INFO, "Trail: %s", s_scene_names[current_scene_index(t)]);

  /* First launch: show a welcome drive so the face isn't static. */
  if (!persist_exists(PERSIST_LAST_DRIVE_HOUR) && s_settings.animations_enabled) {
    s_last_drive_hour = -1;
    start_drive(t);
  }
}

static void window_unload(Window *window) {
  if (s_drive_timer) {
    app_timer_cancel(s_drive_timer);
    s_drive_timer = NULL;
  }
  layer_destroy(s_canvas);
  text_layer_destroy(s_time_shadow);
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_shadow);
  text_layer_destroy(s_date_layer);
  if (s_scene_bitmap) {
    gbitmap_destroy(s_scene_bitmap);
    s_scene_bitmap = NULL;
  }
  for (int i = 0; i < NUM_JEEP_FRAMES; i++) {
    if (s_jeep_frames[i]) {
      gbitmap_destroy(s_jeep_frames[i]);
      s_jeep_frames[i] = NULL;
    }
  }
}

static void init(void) {
  settings_load();

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  app_message_register_inbox_received(inbox_received);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
}

static void deinit(void) {
  settings_save();
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
