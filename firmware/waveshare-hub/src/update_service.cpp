#include "update_service.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_http_client.h>
#include <esp_heap_caps.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <stdarg.h>
#include <strings.h>
#include <time.h>
#include <sys/time.h>
#include <freertos/idf_additions.h>
#include <new>

#include "build_version.h"
#include "plants_ota_installer.h"
#include "update_policy.h"

namespace espplants_update {
namespace {

using namespace espplants_update_policy;

constexpr char kPlantDataPartition[] = "plantdata";
constexpr char kNetworkNamespace[] = "espnet";
constexpr char kGithubReleasesUrl[] =
    "https://api.github.com/repos/bcarriveau/esp-plants/releases?per_page=10";
constexpr char kGithubReleasePrefix[] =
    "https://github.com/bcarriveau/esp-plants/releases/download/";
constexpr char kManifestAssetName[] = "esp-plants-waveshare.manifest.json";
constexpr uint32_t kDailyCheckSeconds = 24UL * 60UL * 60UL;
constexpr uint32_t kFailedCheckRetryMs = 60UL * 60UL * 1000UL;
constexpr uint32_t kWifiReconnectMs = 30UL * 1000UL;
constexpr uint32_t kPortalConnectTimeoutMs = 20UL * 1000UL;
constexpr uint32_t kPortalSuccessHoldMs = 10UL * 1000UL;
// Match the proven Aircraft Radar network/update timing. Automatic release
// checks are deliberately kept out of the noisy boot/network-settle window.
constexpr uint32_t kInitialAutoCheckDelayMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kTimeSyncRetryMs = 15UL * 1000UL;
constexpr uint32_t kTimePersistIntervalSeconds = 6UL * 60UL * 60UL;
constexpr uint32_t kTimeSeedMinEpoch = 1704067200UL;  // 2024-01-01 UTC
constexpr uint32_t kTimeSeedMaxEpoch = 4102444800UL;  // 2100-01-01 UTC
constexpr char kTimeSeedKey[] = "last_ntp";
constexpr uint32_t kSaneEpoch = kTimeSeedMinEpoch;
constexpr uint32_t kMetadataTimeoutMs = 15000UL;
constexpr uint32_t kMetadataIdleTimeoutMs = 10000UL;
constexpr size_t kMaxReleaseListBytes = 96U * 1024U;
constexpr uint8_t kMaxMetadataRedirects = 3;
constexpr char kUserAgent[] = "ESP-PLANTS-Waveshare-Updater/1";

constexpr char kEmbeddedBuildId[] = ESP_PLANTS_WAVESHARE_BUILD_ID;
constexpr char kEmbeddedHardwareId[] = ESP_PLANTS_WAVESHARE_HARDWARE_ID;
#ifdef ESP_PLANTS_DISTRIBUTION_BUILD
constexpr char kEmbeddedDistributionMarker[] = ESP_PLANTS_DISTRIBUTION_MARKER;
#endif

Preferences networkPreferences;
DNSServer dnsServer;
WebServer portalServer(80);

String savedSsid;
String savedPassword;
String pendingSsid;
String pendingPassword;
String lastAttemptSsid;
String portalResultMessage;

enum class PortalAttemptState : uint8_t {
  Idle = 0,
  Testing,
  Failed,
  Succeeded,
};
PortalAttemptState portalAttemptState = PortalAttemptState::Idle;

char connectedSsid[33] = "--";
char wifiAddressText[20] = "--";
char setupSsidText[33] = "--";
char setupPasswordText[33] = "--";
char latestVersionText[32] = "--";
char statusTextBuffer[160] = "Wi-Fi not configured";

espplants_ota_installer::Release pendingRelease{};

volatile bool setupPortalRunning = false;
bool portalConnectionPending = false;
uint32_t portalAttemptStartedMs = 0;
uint32_t portalStopAtMs = 0;
volatile bool checkTaskRunning = false;
volatile bool installTaskRunning = false;
volatile bool manualCheckRequested = false;
volatile bool installRequested = false;
volatile bool hasUpdate = false;
volatile int installProgressPct = 0;

bool initialized = false;
bool sawWifiConnected = false;
bool reconnectSuppressed = false;
bool autoCheckedThisBoot = false;
bool waitingForClockNotice = false;
uint32_t bootStartedMs = 0;
uint32_t lastReconnectAttemptMs = 0;
uint32_t nextFailedCheckMs = 0;
uint32_t lastCheckEpoch = 0;

portMUX_TYPE timeMux = portMUX_INITIALIZER_UNLOCKED;
bool ntpSynchronized = false;
bool timePersistPending = false;
uint32_t pendingNtpEpoch = 0;
uint32_t lastPersistedNtpEpoch = 0;
uint32_t lastTimeSyncKickMs = 0;

void setStatus(const char *format, ...) {
  char text[sizeof(statusTextBuffer)]{};
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  strncpy(statusTextBuffer, text, sizeof(statusTextBuffer) - 1);
  statusTextBuffer[sizeof(statusTextBuffer) - 1] = '\0';
  Serial.printf("[update] %s\n", statusTextBuffer);
}

bool timeEpochSane(uint32_t epoch) {
  return epoch >= kTimeSeedMinEpoch && epoch <= kTimeSeedMaxEpoch;
}

bool systemTimeUsable() {
  const time_t current = time(nullptr);
  return current >= 0 && static_cast<uint64_t>(current) <= UINT32_MAX &&
         timeEpochSane(static_cast<uint32_t>(current));
}

bool timeSynchronizedThisBoot() {
  portENTER_CRITICAL(&timeMux);
  const bool synchronized = ntpSynchronized;
  portEXIT_CRITICAL(&timeMux);
  return synchronized;
}

bool secureTimeReady() {
  // A seeded clock is useful for bootstrapping TLS, but update metadata does
  // not run until SNTP has positively synchronized during this boot.
  return timeSynchronizedThisBoot() && systemTimeUsable();
}

void noteTimeSyncKick() {
  portENTER_CRITICAL(&timeMux);
  lastTimeSyncKickMs = millis();
  portEXIT_CRITICAL(&timeMux);
}

uint32_t lastTimeSyncKick() {
  portENTER_CRITICAL(&timeMux);
  const uint32_t kickedAt = lastTimeSyncKickMs;
  portEXIT_CRITICAL(&timeMux);
  return kickedAt;
}

void onTimeSynchronized(struct timeval *tv) {
  if (!tv || tv->tv_sec < 0 || static_cast<uint64_t>(tv->tv_sec) > UINT32_MAX) return;
  const uint32_t epoch = static_cast<uint32_t>(tv->tv_sec);
  if (!timeEpochSane(epoch)) return;

  portENTER_CRITICAL(&timeMux);
  ntpSynchronized = true;
  pendingNtpEpoch = epoch;
  timePersistPending = true;
  portEXIT_CRITICAL(&timeMux);
}

void configureTimeSync(const char *reason) {
  noteTimeSyncKick();
  sntp_set_time_sync_notification_cb(onTimeSynchronized);
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  Serial.printf("[time] SNTP started%s%s\n", reason ? ": " : "",
                reason ? reason : "");
}

void restoreTimeSeed() {
  if (!networkPreferences.isKey(kTimeSeedKey)) {
    Serial.println("[time] seed: no saved NTP epoch yet");
    return;
  }
  const uint32_t epoch = networkPreferences.getULong(kTimeSeedKey, 0);
  if (!timeEpochSane(epoch)) {
    Serial.println("[time] seed: saved NTP epoch is outside sanity bounds");
    return;
  }

  portENTER_CRITICAL(&timeMux);
  lastPersistedNtpEpoch = epoch;
  portEXIT_CRITICAL(&timeMux);

  if (systemTimeUsable()) {
    Serial.println("[time] seed: system clock already usable");
    return;
  }

  struct timeval seededTime{};
  seededTime.tv_sec = static_cast<time_t>(epoch);
  if (settimeofday(&seededTime, nullptr) != 0) {
    Serial.println("[time] seed: settimeofday failed");
    return;
  }
  Serial.printf("[time] seed: restored last successful NTP epoch %lu\n",
                static_cast<unsigned long>(epoch));
}

bool shouldPersistTime(uint32_t epoch) {
  portENTER_CRITICAL(&timeMux);
  const uint32_t previous = lastPersistedNtpEpoch;
  portEXIT_CRITICAL(&timeMux);
  if (previous == 0) return true;
  const uint32_t delta = epoch >= previous ? epoch - previous : previous - epoch;
  return delta >= kTimePersistIntervalSeconds;
}

void persistTimeSeed(uint32_t epoch) {
  if (!timeEpochSane(epoch) || !shouldPersistTime(epoch)) return;
  const size_t written = networkPreferences.putULong(kTimeSeedKey, epoch);
  if (written != sizeof(uint32_t)) {
    Serial.println("[time] seed: NVS write failed");
    return;
  }
  portENTER_CRITICAL(&timeMux);
  lastPersistedNtpEpoch = epoch;
  portEXIT_CRITICAL(&timeMux);
  Serial.printf("[time] seed: saved NTP epoch %lu\n",
                static_cast<unsigned long>(epoch));
}

void serviceTimeSync(bool connected) {
  uint32_t synchronizedEpoch = 0;
  bool persistPending = false;
  portENTER_CRITICAL(&timeMux);
  if (timePersistPending) {
    synchronizedEpoch = pendingNtpEpoch;
    timePersistPending = false;
    persistPending = true;
  }
  portEXIT_CRITICAL(&timeMux);

  if (persistPending) {
    persistTimeSeed(synchronizedEpoch);
    Serial.printf("[time] SNTP synchronized epoch=%lu\n",
                  static_cast<unsigned long>(synchronizedEpoch));
  }

  if (!connected || secureTimeReady()) return;
  const uint32_t lastKick = lastTimeSyncKick();
  if (lastKick != 0 && millis() - lastKick < kTimeSyncRetryMs) return;
  configureTimeSync("background retry");
}

String htmlEscape(const String &value) {
  String out;
  out.reserve(value.length() + 16);
  for (size_t i = 0; i < value.length(); ++i) {
    switch (value[i]) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += value[i]; break;
    }
  }
  return out;
}

String setupPage(const String &message = String(), bool error = false) {
  String html;
  html.reserve(7600);
  html += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>ESP PLANTS Wi-Fi</title><style>");
  html += F("body{font-family:system-ui;background:#101814;color:#e5ece7;margin:0;padding:20px}");
  html += F(".card{max-width:560px;margin:auto;background:#18231d;padding:22px;border-radius:18px}");
  html += F("h1{margin-top:0;color:#e5ece7}label{display:block;margin:15px 0 6px;color:#b7c8bc}");
  html += F("select,input,button{width:100%;box-sizing:border-box;font-size:17px;padding:13px;border-radius:10px;border:1px solid #405348}");
  html += F("select,input{background:#131c17;color:#e5ece7}button{margin-top:20px;background:#3f7a4e;color:white;font-weight:700}");
  html += F(".msg{padding:12px;background:#244f39;border-radius:10px}.msg.error{background:#5a2d2a;border:1px solid #a85b50;color:#ffe7e2}.small{color:#9db5a5;font-size:14px}</style></head><body><div class='card'>");
  html += F("<h1>ESP PLANTS</h1><p>Choose the Wi-Fi network this display should use for software updates.</p>");
  if (message.length()) {
    html += error ? F("<p class='msg error'>") : F("<p class='msg'>");
    html += htmlEscape(message);
    html += F("</p>");
  }
  html += F("<form method='POST' action='/save'><label>Wi-Fi network</label><select name='ssid' required>");

  const int found = WiFi.scanNetworks(false, true);
  if (found <= 0) {
    html += F("<option value=''>No networks found - refresh and try again</option>");
  } else {
    for (int i = 0; i < found; ++i) {
      const String ssid = WiFi.SSID(i);
      if (!ssid.length()) continue;
      html += F("<option value=\"");
      html += htmlEscape(ssid);
      html += F("\"");
      if ((lastAttemptSsid.length() && ssid == lastAttemptSsid) ||
          (!lastAttemptSsid.length() && ssid == savedSsid)) html += F(" selected");
      html += F(">");
      html += htmlEscape(ssid);
      html += F("  (");
      html += String(WiFi.RSSI(i));
      html += F(" dBm)</option>");
    }
  }
  WiFi.scanDelete();

  html += F("</select><label>Password</label><input type='password' name='password' autocomplete='current-password'>");
  html += F("<button type='submit'>CONNECT ESP PLANTS</button></form>");
  html += F("<p class='small'>The previous known-good Wi-Fi is kept until the new network successfully connects.</p>");
  html += F("</div></body></html>");
  return html;
}

void redirectToPortal() {
  portalServer.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
  portalServer.send(302, "text/plain", "");
}

String portalStatusPage() {
  if (portalAttemptState == PortalAttemptState::Failed) {
    return setupPage(portalResultMessage.length() ? portalResultMessage
                                                  : String("Connection failed. Check the password and try again."),
                     true);
  }

  String html;
  html.reserve(2600);
  html += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  if (portalAttemptState == PortalAttemptState::Testing) {
    html += F("<meta http-equiv='refresh' content='2;url=/status'>");
  }
  html += F("<title>ESP PLANTS Wi-Fi</title><style>body{font-family:system-ui;background:#101814;color:#e5ece7;margin:0;padding:20px}.card{max-width:560px;margin:auto;background:#18231d;padding:22px;border-radius:18px}h1{margin-top:0}.ok{color:#a5e0b2}.small{color:#9db5a5}a{display:inline-block;margin-top:18px;padding:12px 16px;background:#244f39;color:#fff;text-decoration:none;border-radius:10px}</style></head><body><div class='card'><h1>ESP PLANTS</h1>");
  if (portalAttemptState == PortalAttemptState::Succeeded) {
    html += F("<h2 class='ok'>Wi-Fi connected and saved.</h2><p>ESP PLANTS is now connected to <b>");
    html += htmlEscape(savedSsid);
    html += F("</b>.</p><p class='small'>You can close this page and return to the display.</p>");
  } else {
    html += F("<h2>Testing Wi-Fi connection...</h2><p>Trying <b>");
    html += htmlEscape(pendingSsid);
    html += F("</b>. This page will update automatically.</p><p class='small'>The previous known-good Wi-Fi is not changed unless this test succeeds.</p><a href='/status'>CHECK NOW</a>");
  }
  html += F("</div></body></html>");
  return html;
}

void configurePortalRoutes() {
  portalServer.on("/", HTTP_GET, []() {
    if (portalAttemptState == PortalAttemptState::Failed)
      portalServer.send(200, "text/html", setupPage(portalResultMessage, true));
    else
      portalServer.send(200, "text/html", setupPage());
  });
  portalServer.on("/status", HTTP_GET, []() {
    portalServer.send(200, "text/html", portalStatusPage());
  });
  portalServer.on("/save", HTTP_POST, []() {
    const String ssid = portalServer.arg("ssid");
    const String password = portalServer.arg("password");
    if (!ssid.length()) {
      portalServer.send(400, "text/html", setupPage("Choose a Wi-Fi network first.", true));
      return;
    }

    pendingSsid = ssid;
    pendingPassword = password;
    lastAttemptSsid = ssid;
    portalResultMessage = "";
    portalAttemptState = PortalAttemptState::Testing;
    portalConnectionPending = true;
    portalAttemptStartedMs = millis();
    portalStopAtMs = 0;
    setStatus("Testing Wi-Fi %s...", pendingSsid.c_str());
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(pendingSsid.c_str(), pendingPassword.c_str());
    lastReconnectAttemptMs = millis();
    portalServer.send(200, "text/html", portalStatusPage());
  });

  portalServer.on("/generate_204", HTTP_ANY, redirectToPortal);
  portalServer.on("/gen_204", HTTP_ANY, redirectToPortal);
  portalServer.on("/hotspot-detect.html", HTTP_ANY, redirectToPortal);
  portalServer.on("/library/test/success.html", HTTP_ANY, redirectToPortal);
  portalServer.on("/ncsi.txt", HTTP_ANY, redirectToPortal);
  portalServer.on("/connecttest.txt", HTTP_ANY, redirectToPortal);
  portalServer.onNotFound(redirectToPortal);
}

void failPendingWifi(const char *reason) {
  if (!portalConnectionPending) return;
  portalConnectionPending = false;
  portalAttemptState = PortalAttemptState::Failed;
  portalAttemptStartedMs = 0;
  portalStopAtMs = 0;
  portalResultMessage = reason && reason[0]
      ? String(reason)
      : String("Connection failed. Check the password and try again.");
  pendingPassword = "";
  WiFi.disconnect(false, false);
  if (savedSsid.length()) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(savedSsid.c_str(), savedPassword.c_str());
    lastReconnectAttemptMs = millis();
  }
  setStatus("Wi-Fi test failed - check password and try again");
  Serial.printf("[wifi] setup test failed for \"%s\": %s\n",
                lastAttemptSsid.c_str(), portalResultMessage.c_str());
}

void commitPendingWifi() {
  if (!portalConnectionPending || !pendingSsid.length()) return;
  savedSsid = pendingSsid;
  savedPassword = pendingPassword;
  networkPreferences.putString("ssid", savedSsid);
  networkPreferences.putString("pass", savedPassword);
  pendingSsid = "";
  pendingPassword = "";
  portalResultMessage = "Connected and saved.";
  portalAttemptState = PortalAttemptState::Succeeded;
  portalConnectionPending = false;
  portalAttemptStartedMs = 0;
  portalStopAtMs = millis() + kPortalSuccessHoldMs;
  Serial.printf("[wifi] verified and saved network \"%s\"\n", savedSsid.c_str());
}

void stopSetupPortal() {
  if (!setupPortalRunning) return;
  portalServer.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  setupPortalRunning = false;
  portalConnectionPending = false;
  portalAttemptStartedMs = 0;
  portalStopAtMs = 0;
  portalAttemptState = PortalAttemptState::Idle;
  portalResultMessage = "";
  lastAttemptSsid = "";
  if (WiFi.status() == WL_CONNECTED) WiFi.mode(WIFI_STA);
  Serial.println("[wifi] setup portal stopped");
}

void beginStationConnection(bool restartRadio = false) {
  if (!savedSsid.length()) return;

  WiFi.setAutoReconnect(false);
  if (restartRadio && !setupPortalRunning) {
    // Same cold-start station-radio bring-up used by Aircraft Radar to clear
    // stale TLS/socket state before the first secure request.
    Serial.println("[wifi] startup hard station-radio bring-up");
    WiFi.disconnect(true, false);
    delay(250);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
  } else {
    WiFi.mode(setupPortalRunning ? WIFI_AP_STA : WIFI_STA);
    WiFi.disconnect(false, false);
    delay(100);
    if (!setupPortalRunning) WiFi.setSleep(false);
  }
  WiFi.setAutoReconnect(true);
  WiFi.begin(savedSsid.c_str(), savedPassword.c_str());
  lastReconnectAttemptMs = millis();
  setStatus("Connecting to Wi-Fi...");
  Serial.printf("[wifi] connecting to \"%s\"\n", savedSsid.c_str());
}

bool metadataHostAllowed(const char *url) {
  constexpr char prefix[] = "https://";
  if (!url || strncmp(url, prefix, sizeof(prefix) - 1U) != 0) return false;
  const char *authority = url + sizeof(prefix) - 1U;
  const char *slash = strchr(authority, '/');
  if (!slash || slash == authority) return false;
  char host[96]{};
  const size_t length = static_cast<size_t>(slash - authority);
  if (length >= sizeof(host) || memchr(authority, '@', length) ||
      memchr(authority, ':', length)) return false;
  memcpy(host, authority, length);
  host[length] = 0;
  for (size_t i = 0; i < length; ++i) host[i] = asciiLower(host[i]);
  return strcmp(host, "api.github.com") == 0 || allowedReleaseHost(host);
}


struct MetadataHeaderState {
  size_t totalBytes = 0;
  bool invalid = false;
  char location[kMaxRedirectUrlLength + 1U]{};
};

struct MetadataWorkspace {
  MetadataHeaderState headers;
  char currentUrl[kMaxRedirectUrlLength + 1U]{};
};

static_assert(sizeof(MetadataWorkspace) <= 9U * 1024U,
              "Metadata HTTPS workspace exceeded bounded PSRAM budget");

class MetadataWorkspaceGuard {
 public:
  MetadataWorkspaceGuard() {
    workspace_ = static_cast<MetadataWorkspace *>(heap_caps_malloc(
        sizeof(MetadataWorkspace), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (workspace_) new (workspace_) MetadataWorkspace{};
  }

  ~MetadataWorkspaceGuard() {
    if (workspace_) {
      workspace_->~MetadataWorkspace();
      heap_caps_free(workspace_);
    }
  }

  MetadataWorkspace *get() const { return workspace_; }

 private:
  MetadataWorkspace *workspace_ = nullptr;
};

void logHttpsMemory(const char *stage) {
  Serial.printf(
      "[update] HTTPS memory %s: internal=%u largest=%u psram=%u\n",
      stage ? stage : "?",
      static_cast<unsigned>(heap_caps_get_free_size(
          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      static_cast<unsigned>(heap_caps_get_largest_free_block(
          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      static_cast<unsigned>(ESP.getFreePsram()));
}

esp_err_t metadataEventHandler(esp_http_client_event_t *event) {
  if (!event || !event->user_data) return ESP_OK;
  MetadataHeaderState &state = *static_cast<MetadataHeaderState *>(event->user_data);
  if (event->event_id != HTTP_EVENT_ON_HEADER || !event->header_key || !event->header_value) {
    return state.invalid ? ESP_FAIL : ESP_OK;
  }
  size_t updated = state.totalBytes;
  if (!accumulateHeaderBytes(state.totalBytes, strlen(event->header_key),
                             strlen(event->header_value), updated)) {
    state.invalid = true;
    return ESP_FAIL;
  }
  state.totalBytes = updated;
  if (strcasecmp(event->header_key, "Location") == 0) {
    const char *value = event->header_value;
    while (*value == ' ' || *value == '\t') ++value;
    if (!redirectUrlLengthValid(value)) {
      state.invalid = true;
      return ESP_FAIL;
    }
    snprintf(state.location, sizeof(state.location), "%s", value);
  }
  return ESP_OK;
}
bool httpsGetText(const char *initialUrl, const char *accept, size_t maximumBytes,
                  String &body, int &statusCode) {
  body = "";
  statusCode = -1;
  if (!initialUrl || !metadataHostAllowed(initialUrl)) return false;

  MetadataWorkspaceGuard workspaceGuard;
  MetadataWorkspace *workspace = workspaceGuard.get();
  if (!workspace) {
    setStatus("GitHub HTTPS PSRAM workspace allocation failed");
    return false;
  }
  snprintf(workspace->currentUrl, sizeof(workspace->currentUrl), "%s", initialUrl);

  for (uint8_t redirect = 0; redirect <= kMaxMetadataRedirects; ++redirect) {
    if (!metadataHostAllowed(workspace->currentUrl)) return false;

    workspace->headers = MetadataHeaderState{};
    const size_t currentUrlLength = strlen(workspace->currentUrl);
    const size_t transmitBufferBytes =
        httpTransmitBufferBytes(currentUrlLength);
    if (transmitBufferBytes == 0) return false;

    esp_http_client_config_t config{};
    config.url = workspace->currentUrl;
    config.user_agent = kUserAgent;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = static_cast<int>(kMetadataTimeoutMs);
    config.disable_auto_redirect = true;
    config.max_redirection_count = 0;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    config.buffer_size = 2048;
    config.buffer_size_tx = static_cast<int>(transmitBufferBytes);
    config.keep_alive_enable = false;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.skip_cert_common_name_check = false;
    config.event_handler = metadataEventHandler;
    config.user_data = &workspace->headers;

    logHttpsMemory("before TLS");
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    if (accept) esp_http_client_set_header(client, "Accept", accept);
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_http_client_set_header(client, "Connection", "close");

    bool opened = false;
    const esp_err_t openResult = esp_http_client_open(client, 0);
    if (openResult != ESP_OK) {
      logHttpsMemory("TLS failed");
      esp_http_client_cleanup(client);
      return false;
    }
    opened = true;
    logHttpsMemory("TLS connected");

    const int64_t length = esp_http_client_fetch_headers(client);
    if (length < 0 || workspace->headers.invalid) {
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return false;
    }
    statusCode = esp_http_client_get_status_code(client);

    const bool redirectStatus = statusCode == 301 || statusCode == 302 ||
                                statusCode == 303 || statusCode == 307 ||
                                statusCode == 308;
    if (redirectStatus) {
      if (redirect >= kMaxMetadataRedirects ||
          !workspace->headers.location[0] ||
          !metadataHostAllowed(workspace->headers.location) ||
          strlen(workspace->headers.location) >= sizeof(workspace->currentUrl)) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
      }
      snprintf(workspace->currentUrl, sizeof(workspace->currentUrl), "%s",
               workspace->headers.location);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      continue;
    }

    if (statusCode != 200) {
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return false;
    }

    if (length > 0 && static_cast<uint64_t>(length) > maximumBytes) {
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return false;
    }

    body.reserve(length > 0 ? static_cast<size_t>(length) + 1U : 2048U);
    uint8_t buffer[1024];
    uint32_t lastDataMs = millis();
    bool ok = true;
    while (!esp_http_client_is_complete_data_received(client)) {
      const int got = esp_http_client_read(
          client, reinterpret_cast<char *>(buffer), sizeof(buffer));
      if (got > 0) {
        if (body.length() + static_cast<size_t>(got) > maximumBytes) {
          ok = false;
          break;
        }
        body.concat(reinterpret_cast<const char *>(buffer),
                    static_cast<unsigned int>(got));
        lastDataMs = millis();
      } else if (got == 0) {
        if (esp_http_client_is_complete_data_received(client)) break;
        if (millis() - lastDataMs >= kMetadataIdleTimeoutMs) {
          ok = false;
          break;
        }
        delay(10);
      } else {
        const int socketError = esp_http_client_get_errno(client);
        if (socketError == EAGAIN || socketError == EWOULDBLOCK ||
            socketError == ETIMEDOUT) {
          if (millis() - lastDataMs < kMetadataIdleTimeoutMs) continue;
        }
        ok = false;
        break;
      }
    }
    const bool complete = esp_http_client_is_complete_data_received(client);
    if (opened) esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok && complete;
  }
  return false;
}

String normalizedVersion(const char *value) {
  String version = value ? String(value) : String();
  version.trim();
  if (version.startsWith("v") || version.startsWith("V")) version.remove(0, 1);
  return version;
}

struct SemVersion {
  int major = -1;
  int minor = -1;
  int patch = -1;
  String pre;
};

bool parseVersion(const String &input, SemVersion &out) {
  String v = normalizedVersion(input.c_str());
  const int plus = v.indexOf('+');
  if (plus >= 0) v = v.substring(0, plus);
  const int dash = v.indexOf('-');
  String core = dash >= 0 ? v.substring(0, dash) : v;
  out.pre = dash >= 0 ? v.substring(dash + 1) : String();
  const int dot1 = core.indexOf('.');
  const int dot2 = dot1 >= 0 ? core.indexOf('.', dot1 + 1) : -1;
  if (dot1 <= 0 || dot2 <= dot1 + 1 || dot2 >= static_cast<int>(core.length()) - 1) return false;
  const String a = core.substring(0, dot1);
  const String b = core.substring(dot1 + 1, dot2);
  const String c = core.substring(dot2 + 1);
  for (size_t i = 0; i < a.length(); ++i) if (!isDigit(a[i])) return false;
  for (size_t i = 0; i < b.length(); ++i) if (!isDigit(b[i])) return false;
  for (size_t i = 0; i < c.length(); ++i) if (!isDigit(c[i])) return false;
  out.major = a.toInt();
  out.minor = b.toInt();
  out.patch = c.toInt();
  return true;
}

bool numericIdentifier(const String &s) {
  if (!s.length()) return false;
  for (size_t i = 0; i < s.length(); ++i) if (!isDigit(s[i])) return false;
  return true;
}

int comparePrerelease(const String &a, const String &b) {
  if (!a.length() && !b.length()) return 0;
  if (!a.length()) return 1;
  if (!b.length()) return -1;
  int aPos = 0;
  int bPos = 0;
  while (aPos <= static_cast<int>(a.length()) && bPos <= static_cast<int>(b.length())) {
    const int aDot = a.indexOf('.', aPos);
    const int bDot = b.indexOf('.', bPos);
    const String ai = a.substring(aPos, aDot < 0 ? a.length() : aDot);
    const String bi = b.substring(bPos, bDot < 0 ? b.length() : bDot);
    const bool an = numericIdentifier(ai);
    const bool bn = numericIdentifier(bi);
    int result = 0;
    if (an && bn) {
      const long av = ai.toInt();
      const long bv = bi.toInt();
      if (av < bv) result = -1;
      else if (av > bv) result = 1;
    } else if (an != bn) {
      result = an ? -1 : 1;
    } else {
      result = ai.compareTo(bi);
      if (result < 0) result = -1;
      else if (result > 0) result = 1;
    }
    if (result) return result;
    const bool aDone = aDot < 0;
    const bool bDone = bDot < 0;
    if (aDone || bDone) {
      if (aDone && bDone) return 0;
      return aDone ? -1 : 1;
    }
    aPos = aDot + 1;
    bPos = bDot + 1;
  }
  return 0;
}

int compareVersions(const String &a, const String &b) {
  SemVersion av;
  SemVersion bv;
  if (!parseVersion(a, av) || !parseVersion(b, bv)) return a.compareTo(b);
  if (av.major != bv.major) return av.major < bv.major ? -1 : 1;
  if (av.minor != bv.minor) return av.minor < bv.minor ? -1 : 1;
  if (av.patch != bv.patch) return av.patch < bv.patch ? -1 : 1;
  return comparePrerelease(av.pre, bv.pre);
}

bool hexToBytes(const char *hex, uint8_t *out, size_t bytes) {
  if (!hex || !out || strlen(hex) != bytes * 2U) return false;
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < bytes; ++i) {
    const int hi = nibble(hex[i * 2U]);
    const int lo = nibble(hex[i * 2U + 1U]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

void rememberCheckTime() {
  const time_t now = time(nullptr);
  if (now > static_cast<time_t>(kSaneEpoch)) {
    lastCheckEpoch = static_cast<uint32_t>(now);
    networkPreferences.putULong("last_chk", lastCheckEpoch);
  }
  autoCheckedThisBoot = true;
}

bool makeManifestUrl(const String &tag, char *destination, size_t capacity) {
  if (!tagValid(tag.c_str()) || !destination || capacity == 0) return false;
  const int written = snprintf(destination, capacity, "%s%s/%s",
                               kGithubReleasePrefix, tag.c_str(), kManifestAssetName);
  return written > 0 && static_cast<size_t>(written) < capacity;
}

bool parseManifest(const String &body, const String &githubTag,
                   espplants_ota_installer::Release &release,
                   String &versionOut, String &assetOut) {
  if (body.length() > kMaxManifestBytes) {
    setStatus("Release manifest exceeds the supported size");
    return false;
  }
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, body);
  if (error) {
    setStatus("Release manifest is invalid JSON");
    return false;
  }

  const uint32_t schema = doc["schema"] | 0U;
  const String product = doc["product"] | "";
  const String hardware = doc["hardware"] | "";
  const String channel = doc["channel"] | "";
  const String version = doc["version"] | "";
  const String tag = doc["tag"] | "";
  const String buildId = doc["build_id"] | "";
  const String asset = doc["asset"] | "";
  const uint32_t packageSize = doc["package_size"] | 0U;
  String packageSha = doc["package_sha256"] | "";
  const uint32_t firmwareSize = doc["firmware_size"] | 0U;
  String firmwareSha = doc["firmware_sha256"] | "";
  const uint32_t minimumUpdater = doc["min_updater"] | 0U;
  packageSha.toLowerCase();
  firmwareSha.toLowerCase();

  if (schema != kManifestSchema || product != ESP_PLANTS_WAVESHARE_PRODUCT_ID ||
      hardware != ESP_PLANTS_WAVESHARE_HARDWARE_ID ||
      channel != ESP_PLANTS_WAVESHARE_RELEASE_CHANNEL ||
      minimumUpdater > ESP_PLANTS_WAVESHARE_UPDATER_VERSION) {
    setStatus("Release manifest is incompatible with this ESP PLANTS display");
    return false;
  }
  if (tag != githubTag || normalizedVersion(tag.c_str()) != normalizedVersion(version.c_str())) {
    setStatus("Release tag and manifest version do not match");
    return false;
  }
  SemVersion parsed;
  if (!parseVersion(version, parsed) || !tagValid(tag.c_str()) ||
      !assetNameValid(asset.c_str()) ||
      !boundedPrintableAscii(buildId.c_str(), kMaxBuildIdLength) ||
      !packageLayoutValid(packageSize, firmwareSize) ||
      !lowerHexDigest(packageSha.c_str()) || !lowerHexDigest(firmwareSha.c_str())) {
    setStatus("Release manifest contains invalid package metadata");
    return false;
  }

  espplants_ota_installer::Release candidate{};
  snprintf(candidate.tag, sizeof(candidate.tag), "%s", tag.c_str());
  snprintf(candidate.asset, sizeof(candidate.asset), "%s", asset.c_str());
  snprintf(candidate.buildId, sizeof(candidate.buildId), "%s", buildId.c_str());
  candidate.packageSize = packageSize;
  candidate.firmwareSize = firmwareSize;
  if (!hexToBytes(packageSha.c_str(), candidate.packageSha256,
                  sizeof(candidate.packageSha256)) ||
      !hexToBytes(firmwareSha.c_str(), candidate.firmwareSha256,
                  sizeof(candidate.firmwareSha256))) {
    setStatus("Release SHA-256 metadata is invalid");
    return false;
  }

  release = candidate;
  versionOut = normalizedVersion(version.c_str());
  assetOut = asset;
  return true;
}

bool checkGithubRelease() {
  hasUpdate = false;
  pendingRelease = espplants_ota_installer::Release{};

  String releasesBody;
  int code = 0;
  if (!httpsGetText(kGithubReleasesUrl, "application/vnd.github+json",
                    kMaxReleaseListBytes, releasesBody, code)) {
    if (code == 403) setStatus("GitHub rate limit reached; try later");
    else setStatus("Secure GitHub check failed (HTTP %d)", code);
    return false;
  }

  JsonDocument releases;
  const DeserializationError releasesError = deserializeJson(releases, releasesBody);
  if (releasesError || !releases.is<JsonArray>()) {
    setStatus("GitHub returned an invalid release list");
    return false;
  }

  bool sawManifestRelease = false;
  bool manifestTransportFailed = false;
  bool manifestRejected = false;

  for (JsonObject githubRelease : releases.as<JsonArray>()) {
    if (githubRelease["draft"] | false) continue;
    const String tag = githubRelease["tag_name"] | "";
    if (!tagValid(tag.c_str())) continue;

    bool hasManifestAsset = false;
    for (JsonObject asset : githubRelease["assets"].as<JsonArray>()) {
      if (String(asset["name"] | "") == kManifestAssetName) {
        hasManifestAsset = true;
        break;
      }
    }
    if (!hasManifestAsset) continue;
    sawManifestRelease = true;

    char manifestUrl[512]{};
    if (!makeManifestUrl(tag, manifestUrl, sizeof(manifestUrl))) continue;
    String manifestBody;
    if (!httpsGetText(manifestUrl, "application/octet-stream", kMaxManifestBytes,
                      manifestBody, code)) {
      manifestTransportFailed = true;
      continue;
    }

    espplants_ota_installer::Release candidate{};
    String candidateVersion;
    String candidateAsset;
    if (!parseManifest(manifestBody, tag, candidate, candidateVersion, candidateAsset)) {
      manifestRejected = true;
      continue;
    }

    bool matchingPackageFound = false;
    for (JsonObject asset : githubRelease["assets"].as<JsonArray>()) {
      const String name = asset["name"] | "";
      const uint32_t size = asset["size"] | 0U;
      if (name == candidateAsset && size == candidate.packageSize) {
        matchingPackageFound = true;
        break;
      }
    }
    if (!matchingPackageFound) continue;

    pendingRelease = candidate;
    snprintf(latestVersionText, sizeof(latestVersionText), "%s", candidateVersion.c_str());
    const int comparison = compareVersions(String(ESP_PLANTS_WAVESHARE_VERSION), candidateVersion);
    if (comparison >= 0) {
      hasUpdate = false;
      setStatus("Up to date: v%s", ESP_PLANTS_WAVESHARE_VERSION);
    } else {
      hasUpdate = true;
      setStatus("Verified update available: v%s", latestVersionText);
    }
    rememberCheckTime();
    return true;
  }

  if (manifestTransportFailed) {
    setStatus("Secure GitHub release manifest download failed; will retry later");
    return false;
  }
  if (manifestRejected) {
    setStatus("Published Waveshare release manifest was rejected; nothing installed");
    return false;
  }

  snprintf(latestVersionText, sizeof(latestVersionText), "%s", ESP_PLANTS_WAVESHARE_VERSION);
  setStatus(sawManifestRelease
                ? "No compatible newer Waveshare release is available"
                : "No Waveshare update release is published yet");
  rememberCheckTime();
  return true;
}

void checkTask(void *) {
  setStatus("Checking GitHub securely for updates...");
  const bool ok = checkGithubRelease();
  if (!ok) nextFailedCheckMs = millis() + kFailedCheckRetryMs;
  checkTaskRunning = false;
  vTaskDeleteWithCaps(xTaskGetCurrentTaskHandle());
}

bool startCheckTask(const char *failureMessage) {
  // Certificate verification needs scarce internal DRAM. Put the disposable
  // metadata task stack in PSRAM so MbedTLS has room for RSA/X.509 work.
  // The OTA install task intentionally stays on an internal stack because
  // PSRAM may be unavailable while flash writes are in progress.
  const BaseType_t result = xTaskCreateWithCaps(
      checkTask, "espplants-update-check", 16384, nullptr, 1, nullptr,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (result == pdPASS) return true;
  checkTaskRunning = false;
  setStatus("%s", failureMessage);
  return false;
}

void installProgress(uint32_t receivedBytes, uint32_t packageBytes) {
  if (!packageBytes) return;
  const uint32_t percent = (receivedBytes * 100ULL) / packageBytes;
  installProgressPct = static_cast<int>(percent > 100U ? 100U : percent);
}

void installTask(void *) {
  installProgressPct = 0;
  setStatus("Downloading and verifying .plantsota package...");
  char message[160]{};
  const espplants_ota_installer::Result result =
      espplants_ota_installer::install(pendingRelease, installProgress,
                                       message, sizeof(message));
  if (result == espplants_ota_installer::Result::RESTART_PENDING) {
    installProgressPct = 100;
    setStatus("%s", message);
  } else {
    setStatus("Update rejected safely: %s", message[0] ? message : "unknown error");
  }
  installTaskRunning = false;
  vTaskDelete(nullptr);
}

bool shouldAutoCheck() {
  if (!wifiConnected() || !secureTimeReady() || checkTaskRunning || installTaskRunning) return false;
  if (espplants_ota_installer::restartState() != espplants_ota_installer::RestartState::IDLE) return false;
  if (millis() - bootStartedMs < kInitialAutoCheckDelayMs) return false;
  if (nextFailedCheckMs && static_cast<int32_t>(millis() - nextFailedCheckMs) < 0) return false;

  const time_t now = time(nullptr);
  if (lastCheckEpoch > kSaneEpoch) {
    if (static_cast<uint32_t>(now) - lastCheckEpoch >= kDailyCheckSeconds) return true;
    autoCheckedThisBoot = true;
    return false;
  }
  return !autoCheckedThisBoot;
}

}  // namespace

void begin() {
  if (initialized) return;
  initialized = true;
  bootStartedMs = millis();

  Serial.printf("[update] firmware=%s build=%s hardware=%s channel=%s updater=%u\n",
                ESP_PLANTS_WAVESHARE_VERSION, kEmbeddedBuildId,
                kEmbeddedHardwareId, ESP_PLANTS_WAVESHARE_RELEASE_CHANNEL,
                ESP_PLANTS_WAVESHARE_UPDATER_VERSION);
#ifdef ESP_PLANTS_DISTRIBUTION_BUILD
  Serial.printf("[update] public distribution marker=%s\n", kEmbeddedDistributionMarker);
#else
  Serial.println("[update] private/development build; public package generation is disabled");
#endif

  WiFi.persistent(false);
  if (!networkPreferences.begin(kNetworkNamespace, false, kPlantDataPartition)) {
    setStatus("Could not open persistent network settings");
    return;
  }
  savedSsid = networkPreferences.getString("ssid", "");
  savedPassword = networkPreferences.getString("pass", "");
  lastCheckEpoch = networkPreferences.getULong("last_chk", 0);

  sntp_set_time_sync_notification_cb(onTimeSynchronized);
  restoreTimeSeed();

  const uint64_t mac = ESP.getEfuseMac();
  snprintf(setupSsidText, sizeof(setupSsidText), "ESP-PLANTS-%04X",
           static_cast<unsigned>(mac & 0xffffu));
  snprintf(setupPasswordText, sizeof(setupPasswordText), "plants-%06X",
           static_cast<unsigned>(mac & 0xffffffu));

  configurePortalRoutes();
  if (savedSsid.length()) {
    const esp_reset_reason_t reason = esp_reset_reason();
    const bool coldRadioReset =
        reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT;
    beginStationConnection(coldRadioReset);
  } else {
    setStatus("Wi-Fi not configured - use SET UP WI-FI");
  }
}

void service() {
  if (!initialized) return;

  espplants_ota_installer::serviceRestart();
  if (espplants_ota_installer::restartState() == espplants_ota_installer::RestartState::FAILED) {
    char restartMessage[160]{};
    espplants_ota_installer::copyRestartMessage(restartMessage, sizeof(restartMessage));
    if (restartMessage[0] && strcmp(statusTextBuffer, restartMessage) != 0) setStatus("%s", restartMessage);
  }

  if (setupPortalRunning) {
    dnsServer.processNextRequest();
    portalServer.handleClient();
  }

  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !sawWifiConnected) {
    sawWifiConnected = true;
    strncpy(connectedSsid, WiFi.SSID().c_str(), sizeof(connectedSsid) - 1);
    strncpy(wifiAddressText, WiFi.localIP().toString().c_str(), sizeof(wifiAddressText) - 1);
    setStatus("Wi-Fi connected: %s", connectedSsid);
    Serial.printf("[wifi] IP %s\n", wifiAddressText);
    configureTimeSync("Wi-Fi connected");
    waitingForClockNotice = false;
  } else if (!connected && sawWifiConnected) {
    sawWifiConnected = false;
    strncpy(wifiAddressText, "--", sizeof(wifiAddressText) - 1);
    setStatus(reconnectSuppressed ? "Wi-Fi disconnected until you reconnect or reboot"
                                  : "Wi-Fi disconnected; reconnecting...");
  }

  if (connected && setupPortalRunning && portalConnectionPending &&
      WiFi.SSID() == pendingSsid) {
    commitPendingWifi();
    strncpy(connectedSsid, WiFi.SSID().c_str(), sizeof(connectedSsid) - 1);
    strncpy(wifiAddressText, WiFi.localIP().toString().c_str(), sizeof(wifiAddressText) - 1);
    setStatus("Wi-Fi saved: %s", connectedSsid);
  }

  if (setupPortalRunning && portalConnectionPending) {
    const wl_status_t wifiState = WiFi.status();
    if (wifiState == WL_CONNECT_FAILED) {
      failPendingWifi("Could not connect. Check the Wi-Fi password and try again.");
    } else if (wifiState == WL_NO_SSID_AVAIL) {
      failPendingWifi("That Wi-Fi network is no longer available. Choose a network and try again.");
    } else if (portalAttemptStartedMs &&
               millis() - portalAttemptStartedMs >= kPortalConnectTimeoutMs) {
      failPendingWifi("Connection timed out. Check the password and signal, then try again.");
    }
  }

  if (setupPortalRunning && portalStopAtMs &&
      static_cast<int32_t>(millis() - portalStopAtMs) >= 0) {
    stopSetupPortal();
  }

  if (!connected && savedSsid.length() && !setupPortalRunning &&
      !reconnectSuppressed &&
      millis() - lastReconnectAttemptMs >= kWifiReconnectMs) {
    beginStationConnection();
  }

  serviceTimeSync(connected);

  if (manualCheckRequested && connected && !secureTimeReady()) {
    if (!waitingForClockNotice) {
      waitingForClockNotice = true;
      setStatus("Waiting for confirmed SNTP time before secure GitHub check...");
    }
  }

  if (manualCheckRequested && connected && secureTimeReady() &&
      !checkTaskRunning && !installTaskRunning &&
      espplants_ota_installer::restartState() == espplants_ota_installer::RestartState::IDLE) {
    manualCheckRequested = false;
    waitingForClockNotice = false;
    checkTaskRunning = true;
    startCheckTask("Could not start update-check task");
  } else if (shouldAutoCheck()) {
    autoCheckedThisBoot = true;
    checkTaskRunning = true;
    startCheckTask("Could not start automatic update-check task");
  }

  if (installRequested && connected && secureTimeReady() && hasUpdate &&
      !checkTaskRunning && !installTaskRunning &&
      espplants_ota_installer::restartState() == espplants_ota_installer::RestartState::IDLE) {
    installRequested = false;
    installTaskRunning = true;
    if (xTaskCreate(installTask, "espplants-update-install", 16384, nullptr, 1, nullptr) != pdPASS) {
      installTaskRunning = false;
      setStatus("Could not start OTA install task");
    }
  }
}

void startWifiSetup() {
  if (!initialized || setupPortalRunning) return;
  reconnectSuppressed = false;
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(setupSsidText, setupPasswordText)) {
    setStatus("Could not start Wi-Fi setup hotspot");
    return;
  }
  delay(50);
  dnsServer.start(53, "*", WiFi.softAPIP());
  portalServer.begin();
  setupPortalRunning = true;
  portalConnectionPending = false;
  portalAttemptState = PortalAttemptState::Idle;
  portalAttemptStartedMs = 0;
  portalStopAtMs = 0;
  portalResultMessage = "";
  lastAttemptSsid = "";
  setStatus("Phone setup hotspot is ready - scan QR to connect");
  Serial.printf("[wifi] setup AP \"%s\" password \"%s\" at %s\n",
                setupSsidText, setupPasswordText,
                WiFi.softAPIP().toString().c_str());
}

void disconnectWifi() {
  if (!initialized) return;
  if (installTaskRunning) {
    setStatus("Cannot disconnect Wi-Fi during an update install");
    return;
  }
  if (setupPortalRunning) stopSetupPortal();
  reconnectSuppressed = true;
  manualCheckRequested = false;
  waitingForClockNotice = false;
  WiFi.disconnect(true, false);
  sawWifiConnected = false;
  strncpy(connectedSsid, "--", sizeof(connectedSsid) - 1);
  connectedSsid[sizeof(connectedSsid) - 1] = '\0';
  strncpy(wifiAddressText, "--", sizeof(wifiAddressText) - 1);
  wifiAddressText[sizeof(wifiAddressText) - 1] = '\0';
  setStatus("Wi-Fi disconnected until you reconnect or reboot");
  Serial.println("[wifi] user disconnected station; saved credentials retained");
}

void reconnectWifi() {
  if (!initialized) return;
  if (!savedSsid.length()) {
    setStatus("No saved Wi-Fi network; use SET UP / CHANGE WI-FI");
    return;
  }
  if (installTaskRunning) return;
  reconnectSuppressed = false;
  beginStationConnection();
}

void forgetWifi() {
  if (!initialized) return;
  if (installTaskRunning) {
    setStatus("Cannot forget Wi-Fi during an update install");
    return;
  }
  if (setupPortalRunning) stopSetupPortal();
  reconnectSuppressed = true;
  manualCheckRequested = false;
  waitingForClockNotice = false;
  hasUpdate = false;
  pendingRelease = espplants_ota_installer::Release{};
  snprintf(latestVersionText, sizeof(latestVersionText), "--");
  networkPreferences.remove("ssid");
  networkPreferences.remove("pass");
  savedSsid = "";
  savedPassword = "";
  pendingSsid = "";
  pendingPassword = "";
  WiFi.disconnect(true, false);
  sawWifiConnected = false;
  strncpy(connectedSsid, "--", sizeof(connectedSsid) - 1);
  connectedSsid[sizeof(connectedSsid) - 1] = '\0';
  strncpy(wifiAddressText, "--", sizeof(wifiAddressText) - 1);
  wifiAddressText[sizeof(wifiAddressText) - 1] = '\0';
  setStatus("Saved Wi-Fi forgotten; ESP PLANTS remains available offline");
  Serial.println("[wifi] saved Wi-Fi credentials erased by user");
}

void requestCheck() {
  if (!initialized) return;
  if (!wifiConnected()) {
    setStatus("Connect ESP PLANTS to Wi-Fi first");
    return;
  }
  if (checkTaskRunning || installTaskRunning ||
      espplants_ota_installer::restartState() != espplants_ota_installer::RestartState::IDLE) return;
  manualCheckRequested = true;
  waitingForClockNotice = false;
}

void requestInstall() {
  if (!initialized) return;
  if (!wifiConnected()) {
    setStatus("Wi-Fi disconnected; update not started");
    return;
  }
  if (!secureTimeReady()) {
    setStatus("Secure update is waiting for confirmed SNTP time sync");
    return;
  }
  if (!hasUpdate) {
    setStatus("No newer verified update is ready");
    return;
  }
  if (checkTaskRunning || installTaskRunning ||
      espplants_ota_installer::restartState() != espplants_ota_installer::RestartState::IDLE) return;
  installRequested = true;
}

bool wifiConfigured() { return savedSsid.length() > 0; }
bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }
bool setupPortalActive() { return setupPortalRunning; }
bool wifiReconnectSuppressed() { return reconnectSuppressed; }
const char *wifiSsid() {
  return wifiConnected() ? connectedSsid : (savedSsid.length() ? savedSsid.c_str() : "--");
}
const char *wifiAddress() { return wifiAddressText; }
const char *setupSsid() { return setupSsidText; }
const char *setupPassword() { return setupPasswordText; }
bool checking() { return checkTaskRunning; }
bool installing() { return installTaskRunning; }
bool updateAvailable() { return hasUpdate; }
int updateProgress() { return installProgressPct; }
const char *currentVersion() { return ESP_PLANTS_WAVESHARE_VERSION; }
const char *latestVersion() { return latestVersionText; }
const char *statusText() { return statusTextBuffer; }

}  // namespace espplants_update
