#include "boot_splash.h"
#include "boot_splash_assets.h"

#include <lvgl.h>
#include <stdint.h>

namespace espplants_boot_splash {
namespace {

constexpr uint32_t kSplashDurationMs = 6500U;
constexpr uint32_t kGrowDurationMs = 4300U;
constexpr uint16_t kZoomMin = 14U;
constexpr uint16_t kZoomFull = 256U;

lv_obj_t *splash = nullptr;
lv_obj_t *plant = nullptr;
lv_obj_t *progress = nullptr;
lv_obj_t *valueLabels[3]{};
lv_timer_t *animTimer = nullptr;
uint32_t splashStartTick = 0;
uint32_t lastPhotoStep = UINT32_MAX;

lv_color_t okColor;
lv_color_t busyColor;
lv_color_t idleColor;

enum class WifiState : uint8_t { Checking, NotConfigured, Connecting, Connected, Disconnected };
enum class LinkState : uint8_t { Checking, Connecting, Linked, ZigbeeReady };
enum class SystemState : uint8_t { Starting, Ready };
WifiState drawnWifi = WifiState::Checking;
LinkState drawnLink = LinkState::Checking;
SystemState drawnSystem = SystemState::Starting;

const lv_img_dsc_t *backgroundImage() {
#ifdef ESP_PLANTS_WAVESHARE_7B
  return &espplants_boot_background_7b;
#else
  return &espplants_boot_background_7;
#endif
}

const lv_img_dsc_t *plantImage() {
#ifdef ESP_PLANTS_WAVESHARE_7B
  return &espplants_boot_sprout_7b;
#else
  return &espplants_boot_sprout_7;
#endif
}

lv_coord_t designX(int value) {
  return static_cast<lv_coord_t>(static_cast<int32_t>(value) *
                                 lv_disp_get_hor_res(lv_disp_get_default()) / 1619);
}
lv_coord_t designY(int value) {
  return static_cast<lv_coord_t>(static_cast<int32_t>(value) *
                                 lv_disp_get_ver_res(lv_disp_get_default()) / 971);
}

void statusValue(int index, const char *text, lv_color_t color) {
  lv_obj_t *label = valueLabels[index];
  if (!label) return;
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
}

void tick(lv_timer_t *) {
  if (!splash) return;
  const uint32_t elapsed = lv_tick_elaps(splashStartTick);
  if (progress) {
    const uint32_t bounded = elapsed >= kSplashDurationMs
        ? kSplashDurationMs : elapsed;
    lv_bar_set_value(progress,
                     static_cast<int32_t>(bounded * 1000U / kSplashDurationMs),
                     LV_ANIM_OFF);
  }
  if (plant) {
    // Limit photo resampling to 10 steps per second to protect 7B RGB throughput.
    const uint32_t growMs = elapsed >= kGrowDurationMs ? kGrowDurationMs : elapsed;
    const uint32_t step = growMs / 100U;
    if (step != lastPhotoStep) {
      lastPhotoStep = step;
      const uint16_t zoom = static_cast<uint16_t>(
          kZoomMin + (kZoomFull - kZoomMin) * growMs / kGrowDurationMs);
      lv_img_set_zoom(plant, zoom);
    }
  }
}

}  // namespace

void show(const char *, bool) {
  if (splash) return;
  lv_disp_t *display = lv_disp_get_default();
  if (!display) return;
  const lv_coord_t width = lv_disp_get_hor_res(display);
  const lv_coord_t height = lv_disp_get_ver_res(display);
  if (width <= 0 || height <= 0) return;

  drawnWifi = WifiState::Checking;
  drawnLink = LinkState::Checking;
  drawnSystem = SystemState::Starting;
  lastPhotoStep = UINT32_MAX;
  okColor = lv_color_hex(0x78F17A);
  busyColor = lv_color_hex(0xFFCB64);
  idleColor = lv_color_hex(0xD7E3DD);

  // System layer renders above the EEZ top layer even while new screens load.
  splash = lv_obj_create(lv_layer_sys());
  lv_obj_set_pos(splash, 0, 0);
  lv_obj_set_size(splash, width, height);
  lv_obj_set_style_pad_all(splash, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(splash, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(splash, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(splash, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(splash, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(splash, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(splash, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *art = lv_img_create(splash);
  lv_img_set_src(art, backgroundImage());
  lv_obj_set_pos(art, 0, 0);
  lv_obj_clear_flag(art, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  plant = lv_img_create(splash);
  lv_img_set_src(plant, plantImage());
  lv_obj_set_pos(plant, designX(555), designY(523));
  lv_img_set_pivot(plant, lv_obj_get_width(plant) / 2,
                         lv_obj_get_height(plant));
  lv_img_set_zoom(plant, kZoomMin);
  lv_obj_clear_flag(plant, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  const int statusPosX[3] = {236, 690, 1160};
  const lv_font_t *font = width >= 1000 ? &lv_font_montserrat_20
                                          : &lv_font_montserrat_16;
  for (int i = 0; i < 3; ++i) {
    valueLabels[i] = lv_label_create(splash);
    lv_label_set_text(valueLabels[i], i == 2 ? "STARTING..." : "CHECKING...");
    lv_label_set_long_mode(valueLabels[i], LV_LABEL_LONG_CLIP);
    lv_obj_set_size(valueLabels[i], designX(i == 2 ? 300 : 320), designY(51));
    lv_obj_set_pos(valueLabels[i], designX(statusPosX[i]), designY(449));
    lv_obj_set_style_text_font(valueLabels[i], font, LV_PART_MAIN);
    lv_obj_set_style_text_color(valueLabels[i], idleColor, LV_PART_MAIN);
    lv_obj_set_style_text_align(valueLabels[i], LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_clear_flag(valueLabels[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  }

  progress = lv_bar_create(splash);
  lv_obj_set_pos(progress, designX(205), designY(825));
  lv_obj_set_size(progress, designX(1182), designY(43));
  lv_bar_set_range(progress, 0, 1000);
  lv_bar_set_value(progress, 0, LV_ANIM_OFF);
  lv_obj_set_style_radius(progress, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(progress, lv_color_hex(0x0D1E1B), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(progress, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(progress, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(progress, lv_color_hex(0x709D80), LV_PART_MAIN);
  lv_obj_set_style_pad_all(progress, designY(5), LV_PART_MAIN);
  lv_obj_set_style_radius(progress, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(progress, lv_color_hex(0x57E865), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_color(progress, lv_color_hex(0xB2FF6D), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(progress, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(progress, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_clear_flag(progress, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

void startAnimation() {
  if (!splash || animTimer) return;
  splashStartTick = lv_tick_get();
  animTimer = lv_timer_create(tick, 100U, nullptr);
  if (!animTimer) {
    dismiss();
    return;
  }
  tick(nullptr);
}

bool active() { return splash != nullptr; }

bool readyToOpenDashboard() {
  return splash && animTimer &&
         lv_tick_elaps(splashStartTick) >= kSplashDurationMs;
}

void dismiss() {
  if (animTimer) lv_timer_del(animTimer);
  animTimer = nullptr;
  if (splash) lv_obj_del(splash);
  splash = plant = progress = nullptr;
  for (lv_obj_t *&label : valueLabels) label = nullptr;
}

bool update(bool h2Linked, bool zigbeeReady,
            bool wifiConfigured, bool wifiConnected,
            bool wifiReconnectSuppressed, bool systemReady) {
  if (!splash) return false;
  const WifiState wifi = !wifiConfigured ? WifiState::NotConfigured
                        : wifiConnected ? WifiState::Connected
                        : wifiReconnectSuppressed ? WifiState::Disconnected
                        : WifiState::Connecting;
  const LinkState link = !h2Linked ? LinkState::Connecting
                         : zigbeeReady ? LinkState::ZigbeeReady
                         : LinkState::Linked;
  const SystemState system = systemReady ? SystemState::Ready
                                          : SystemState::Starting;
  if (wifi != drawnWifi) {
    drawnWifi = wifi;
    switch (wifi) {
      case WifiState::Checking: break;
      case WifiState::NotConfigured: statusValue(0, "NOT CONFIGURED", busyColor); break;
      case WifiState::Connecting: statusValue(0, "CONNECTING...", busyColor); break;
      case WifiState::Connected: statusValue(0, "CONNECTED", okColor); break;
      case WifiState::Disconnected: statusValue(0, "DISCONNECTED", idleColor); break;
    }
  }
  if (link != drawnLink) {
    drawnLink = link;
    switch (link) {
      case LinkState::Checking: break;
      case LinkState::Connecting: statusValue(1, "CONNECTING...", busyColor); break;
      case LinkState::Linked: statusValue(1, "LINK ONLINE", okColor); break;
      case LinkState::ZigbeeReady: statusValue(1, "ZIGBEE READY", okColor); break;
    }
  }
  if (system != drawnSystem) {
    drawnSystem = system;
    statusValue(2, system == SystemState::Ready ? "READY" : "STARTING...",
                system == SystemState::Ready ? okColor : busyColor);
  }
  return true;
}

}  // namespace espplants_boot_splash
