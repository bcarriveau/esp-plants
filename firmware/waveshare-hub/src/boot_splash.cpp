#include "boot_splash.h"

#include <lvgl.h>
#include <stdint.h>

namespace espplants_boot_splash {
namespace {

// The same fixed layout is scaled from 800x480 to the 7B's 1024x600.
constexpr int32_t kDesignWidth = 800;
constexpr int32_t kDesignHeight = 480;
constexpr uint32_t kSplashDurationMs = 6500;

lv_obj_t *splash = nullptr;
lv_obj_t *stem = nullptr;
lv_obj_t *leaves[4]{};
lv_obj_t *activityFill = nullptr;
lv_obj_t *statusDot[3]{};
lv_obj_t *statusValue[3]{};
lv_timer_t *dismissTimer = nullptr;
lv_coord_t displayWidth = 800;
lv_coord_t displayHeight = 480;
lv_coord_t stemBottom = 0;

lv_color_t okColor;
lv_color_t busyColor;
lv_color_t idleColor;

enum class WifiState : uint8_t {
  Checking, NotConfigured, Connecting, Connected, Disconnected
};
enum class H2State : uint8_t { Checking, Connecting, Linked, ZigbeeReady };
enum class SystemState : uint8_t { Starting, Ready };

WifiState drawnWifi = WifiState::Checking;
H2State drawnH2 = H2State::Checking;
SystemState drawnSystem = SystemState::Starting;

lv_coord_t x(int32_t n) {
  return static_cast<lv_coord_t>(n * displayWidth / kDesignWidth);
}
lv_coord_t y(int32_t n) {
  return static_cast<lv_coord_t>(n * displayHeight / kDesignHeight);
}

lv_obj_t *shape(lv_obj_t *parent, int32_t left, int32_t top,
                int32_t width, int32_t height,
                lv_color_t color, lv_coord_t radius) {
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_set_pos(obj, x(left), y(top));
  lv_obj_set_size(obj, x(width), y(height));
  lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  return obj;
}

lv_obj_t *makeLabel(lv_obj_t *parent, int32_t left, int32_t top,
                    int32_t width, int32_t height, const char *text,
                    const lv_font_t *font, lv_color_t color,
                    lv_text_align_t alignment = LV_TEXT_ALIGN_CENTER) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_pos(label, x(left), y(top));
  lv_obj_set_size(label, x(width), y(height));
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_align(label, alignment, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
  lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  return label;
}

void drawStatus(int index, const char *text, lv_color_t color) {
  if (!statusDot[index] || !statusValue[index]) return;
  lv_label_set_text(statusValue[index], text);
  lv_obj_set_style_text_color(statusValue[index], color, LV_PART_MAIN);
  lv_obj_set_style_bg_color(statusDot[index], color, LV_PART_MAIN);
}

void setStemHeight(void *obj, int32_t height) {
  lv_obj_t *o = static_cast<lv_obj_t *>(obj);
  lv_obj_set_height(o, static_cast<lv_coord_t>(height));
  lv_obj_set_y(o, stemBottom - static_cast<lv_coord_t>(height));
}

void setOpacity(void *obj, int32_t opacity) {
  lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj),
                       static_cast<lv_opa_t>(opacity), LV_PART_MAIN);
}

void setFillWidth(void *obj, int32_t width) {
  lv_obj_set_width(static_cast<lv_obj_t *>(obj),
                   static_cast<lv_coord_t>(width));
}

void dismiss(lv_timer_t *timer) {
  // Invoked on LVGL's timer context. No UI pointers are retained afterward.
  if (stem) lv_anim_del(stem, nullptr);
  for (lv_obj_t *leaf : leaves) {
    if (leaf) lv_anim_del(leaf, nullptr);
  }
  if (activityFill) lv_anim_del(activityFill, nullptr);
  if (splash) lv_obj_del(splash);
  splash = stem = activityFill = nullptr;
  for (lv_obj_t *&leaf : leaves) leaf = nullptr;
  for (lv_obj_t *&obj : statusDot) obj = nullptr;
  for (lv_obj_t *&obj : statusValue) obj = nullptr;
  dismissTimer = nullptr;
  lv_timer_del(timer);
}

void fadeLeaf(lv_obj_t *leaf, uint32_t delayMs) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, leaf);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_time(&a, 780);
  lv_anim_set_delay(&a, delayMs);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&a, setOpacity);
  lv_anim_start(&a);
}

}  // namespace

void show(const char *deviceName, bool lightAppearance) {
  if (splash) return;
  lv_disp_t *display = lv_disp_get_default();
  if (!display) return;
  displayWidth = lv_disp_get_hor_res(display);
  displayHeight = lv_disp_get_ver_res(display);
  if (displayWidth <= 0 || displayHeight <= 0) return;

  const lv_color_t background = lv_color_hex(lightAppearance ? 0xEFF5EE : 0x08120F);
  const lv_color_t cardColor = lv_color_hex(lightAppearance ? 0xDFE9DE : 0x13231C);
  const lv_color_t titleColor = lv_color_hex(lightAppearance ? 0x193C27 : 0xEAF5EC);
  const lv_color_t mutedColor = lv_color_hex(lightAppearance ? 0x536D5A : 0xB0C7B8);
  const lv_color_t green = lv_color_hex(lightAppearance ? 0x1C934B : 0x6FE175);
  const lv_color_t soil = lv_color_hex(lightAppearance ? 0x937354 : 0x72513A);
  const lv_color_t track = lv_color_hex(lightAppearance ? 0xB9CDBB : 0x2A4034);
  okColor = green;
  busyColor = lv_color_hex(lightAppearance ? 0xA66B08 : 0xF1C46E);
  idleColor = mutedColor;
  drawnWifi = WifiState::Checking;
  drawnH2 = H2State::Checking;
  drawnSystem = SystemState::Starting;

  // One temporary top-layer overlay; EEZ continues to own the Home page.
  splash = lv_obj_create(lv_layer_sys());
  lv_obj_set_pos(splash, 0, 0);
  lv_obj_set_size(splash, displayWidth, displayHeight);
  lv_obj_set_style_bg_color(splash, background, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(splash, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(splash, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(splash, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(splash, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(splash, 0, LV_PART_MAIN);
  lv_obj_clear_flag(splash, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(splash, LV_OBJ_FLAG_CLICKABLE);

  const bool largerScreen = displayWidth > 800;
  makeLabel(splash, 45, 32, 710, 50,
            deviceName && deviceName[0] ? deviceName : "ESP PLANTS",
            largerScreen ? &lv_font_montserrat_48 : &lv_font_montserrat_36,
            titleColor);
  makeLabel(splash, 70, 92, 660, 30, "GROWING TOGETHER",
            largerScreen ? &lv_font_montserrat_22 : &lv_font_montserrat_18,
            mutedColor);

  const char *headings[3] = {"WI-FI", "H2 / ZIGBEE", "SYSTEM"};
  const int32_t colLeft[3] = {46, 289, 532};
  const lv_font_t *statusFont = largerScreen
                                    ? &lv_font_montserrat_22
                                    : &lv_font_montserrat_16;
  for (int i = 0; i < 3; ++i) {
    shape(splash, colLeft[i], 144, 222, 82, cardColor, y(12));
    statusDot[i] = shape(splash, colLeft[i] + 16, 165, 12, 12,
                         idleColor, LV_RADIUS_CIRCLE);
    makeLabel(splash, colLeft[i] + 36, 158, 173, 27,
              headings[i],
              largerScreen ? &lv_font_montserrat_22 : &lv_font_montserrat_18,
              titleColor,
              LV_TEXT_ALIGN_LEFT);
    statusValue[i] = makeLabel(splash, colLeft[i] + 13, 190, 196, 28,
                               i == 2 ? "STARTING..." : "CHECKING...",
                               statusFont, i == 2 ? busyColor : idleColor);
  }
  lv_obj_set_style_bg_color(statusDot[2], busyColor, LV_PART_MAIN);

  // The plant grows over a narrow line of soil. All shapes are tiny objects
  // rather than decoded pictures or a video, minimizing 7B RGB traffic.
  shape(splash, 260, 370, 280, 6, soil, LV_RADIUS_CIRCLE);
  shape(splash, 320, 377, 160, 5, soil, LV_RADIUS_CIRCLE);
  stemBottom = y(370);
  stem = shape(splash, 396, 367, 8, 3, green, LV_RADIUS_CIRCLE);
  leaves[0] = shape(splash, 347, 326, 49, 22, green, LV_RADIUS_CIRCLE);
  leaves[1] = shape(splash, 405, 308, 53, 24, green, LV_RADIUS_CIRCLE);
  leaves[2] = shape(splash, 365, 278, 31, 17, green, LV_RADIUS_CIRCLE);
  leaves[3] = shape(splash, 405, 263, 34, 18, green, LV_RADIUS_CIRCLE);
  for (lv_obj_t *leaf : leaves) {
    lv_obj_set_style_opa(leaf, LV_OPA_TRANSP, LV_PART_MAIN);
  }

  makeLabel(splash, 110, 395, 580, 26, "STARTING ESP PLANTS...",
            largerScreen ? &lv_font_montserrat_20 : &lv_font_montserrat_16,
            mutedColor);
  shape(splash, 190, 436, 420, 10, track, LV_RADIUS_CIRCLE);
  activityFill = shape(splash, 190, 436, 2, 10, green, LV_RADIUS_CIRCLE);
}

void animateAndDismiss() {
  if (!splash || dismissTimer) return;
  // Independent of PlantLink/Wi-Fi: the Home page is never held hostage by
  // an offline device or missing Zigbee sensor.
  dismissTimer = lv_timer_create(dismiss, kSplashDurationMs, nullptr);
  if (!dismissTimer) {
    lv_obj_del(splash);
    splash = stem = activityFill = nullptr;
    for (lv_obj_t *&leaf : leaves) leaf = nullptr;
    for (lv_obj_t *&obj : statusDot) obj = nullptr;
    for (lv_obj_t *&obj : statusValue) obj = nullptr;
    return;
  }

  lv_anim_t grow;
  lv_anim_init(&grow);
  lv_anim_set_var(&grow, stem);
  lv_anim_set_values(&grow, y(3), y(112));
  lv_anim_set_time(&grow, 4300);
  lv_anim_set_path_cb(&grow, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&grow, setStemHeight);
  lv_anim_start(&grow);
  fadeLeaf(leaves[0], 600);
  fadeLeaf(leaves[1], 1650);
  fadeLeaf(leaves[2], 2700);
  fadeLeaf(leaves[3], 3500);

  lv_anim_t activity;
  lv_anim_init(&activity);
  lv_anim_set_var(&activity, activityFill);
  lv_anim_set_values(&activity, x(2), x(420));
  lv_anim_set_time(&activity, 5850);
  lv_anim_set_path_cb(&activity, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&activity, setFillWidth);
  lv_anim_start(&activity);
}

bool update(bool h2Linked, bool zigbeeReady,
            bool wifiConfigured, bool wifiConnected,
            bool wifiReconnectSuppressed, bool systemReady) {
  if (!splash) return false;

  const WifiState wifi = !wifiConfigured ? WifiState::NotConfigured
                        : wifiConnected ? WifiState::Connected
                        : wifiReconnectSuppressed ? WifiState::Disconnected
                        : WifiState::Connecting;
  const H2State h2 = !h2Linked ? H2State::Connecting
                     : zigbeeReady ? H2State::ZigbeeReady
                     : H2State::Linked;
  const SystemState system = systemReady ? SystemState::Ready
                                           : SystemState::Starting;

  if (wifi != drawnWifi) {
    drawnWifi = wifi;
    switch (wifi) {
      case WifiState::Checking: break;
      case WifiState::NotConfigured: drawStatus(0, "NOT CONFIGURED", idleColor); break;
      case WifiState::Connecting: drawStatus(0, "CONNECTING...", busyColor); break;
      case WifiState::Connected: drawStatus(0, "CONNECTED", okColor); break;
      case WifiState::Disconnected: drawStatus(0, "DISCONNECTED", idleColor); break;
    }
  }
  if (h2 != drawnH2) {
    drawnH2 = h2;
    switch (h2) {
      case H2State::Checking: break;
      case H2State::Connecting: drawStatus(1, "CONNECTING...", busyColor); break;
      case H2State::Linked: drawStatus(1, "H2 ONLINE", okColor); break;
      case H2State::ZigbeeReady: drawStatus(1, "ZIGBEE READY", okColor); break;
    }
  }
  if (system != drawnSystem) {
    drawnSystem = system;
    if (system == SystemState::Ready) drawStatus(2, "READY", okColor);
  }
  return true;
}

}  // namespace espplants_boot_splash
