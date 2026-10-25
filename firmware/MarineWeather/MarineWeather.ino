/*
  Marine Weather Display - shareable ESP32 prototype v0.5.3

  ESP32-WROOM-32 DevKit + GMT020-02-8P ST7789 240x320 TFT (landscape).
  Wiring: GND-GND, VCC-3V3, SCL-GPIO18, SDA-GPIO23,
          RST-GPIO22, DC-GPIO19, CS-GPIO5, BL-GPIO27.
  Measured BL draw for the original build: 2.4mA at 3.3V.
  Confirm compatibility before wiring a different display's BL to a GPIO.

  BOOT/GPIO0: short press when awake = next page; first press while asleep
  wakes without changing page. Hold BOOT >= 4 seconds while RUNNING
  to open the configuration portal (do NOT hold BOOT while powering up).
  Backlight timeout can be selected from the local setup page and persists
  across reboots: 15s, 30s, 1m, 2m, 5m, 10m, or always on.
  The device creates a 12-character WPA2 setup Wi-Fi password once on first
  setup and saves it in ESP32 Preferences. Each board generates its own.
  The same password is displayed again on every later visit to setup mode.
  Remembers the 8 most recently selected marine forecast zones in Preferences.
  Recent zones appear in the original editable zone field dropdown.
  Tap a saved zone to select it; press-and-hold a saved zone to delete it.
  Nearby-zone suggestions use NOAA marine-zone geographic centroids when online,
  with a small local fallback and a cached result for offline revisits.

  Libraries: Adafruit GFX Library, Adafruit ST7735 and ST7789 Library.
  Board: ESP32 Dev Module (classic ESP32, not ESP32-S3/C3).
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <math.h>

#define TFT_CS     5
#define TFT_DC     19
#define TFT_RST    22
#define TFT_SCLK   18
#define TFT_MOSI   23
#define TFT_BL     27
#define BOOT_PIN    0

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
WebServer configServer(80);
DNSServer captiveDns;
Preferences settings;

String wifiSSID;
String wifiPassword;
String marineZone = "ANZ331";
String setupSSID;
String setupPassword;
// Last-used forecast zones, newest first; persists in ESP32 NVS.
const int MAX_SAVED_ZONES = 8;
String savedZones[MAX_SAVED_ZONES];
int savedZoneCount = 0;
bool inSetup = false;
bool rebootPending = false;
unsigned long rebootRequestedAt = 0;

const unsigned long REFRESH_MS = 1800000UL;
const unsigned long RETRY_MS = 60000UL;
// User-configurable. Zero means the screen stays on indefinitely.
uint32_t screenTimeoutSeconds = 30;
unsigned long screenTimeoutMs = 30000UL;
const unsigned long DEBOUNCE_MS = 40UL;
const unsigned long LONG_PRESS_MS = 4000UL;

const uint16_t BG = 0x0843;
const uint16_t NAVY = 0x10A6;
const uint16_t CYAN = 0x07FF;
const uint16_t WHITE = 0xFFFF;
const uint16_t GRAY = 0xBDF7;
const uint16_t ORANGE = 0xFD20;

String names[4];
String bodies[4];
String warning;
int periodCount = 0;
int currentPage = 0;
unsigned long lastAttempt = 0;
unsigned long lastSuccess = 0;
bool updateFailed = false;

bool screenAwake = true;
unsigned long lastInteraction = 0;
bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long buttonDownAt = 0;
bool longPressHandled = false;

String escapedHtml(const String &value) {
  String out;
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value[i];
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else if (c == '\'') out += "&#39;";
    else out += c;
  }
  return out;
}

bool validTimeoutSeconds(uint32_t seconds) {
  return seconds == 0 || seconds == 15 || seconds == 30 ||
         seconds == 60 || seconds == 120 || seconds == 300 ||
         seconds == 600;
}

// Validate setup form values before converting to an integer.
bool validTimeoutFormValue(const String &value) {
  return value == "0" || value == "15" || value == "30" ||
         value == "60" || value == "120" || value == "300" ||
         value == "600";
}

String backlightOption(uint32_t seconds, const char *label) {
  String option = "<option value='" + String(seconds) + "'";
  if (screenTimeoutSeconds == seconds) option += " selected";
  return option + ">" + label + "</option>";
}

String normalizeZone(String zone) {
  zone.trim();
  zone.toUpperCase();
  return zone;
}

// NOAA coastal directories supported by this first version.
bool validZone(String zone) {
  zone = normalizeZone(zone);
  if (zone.length() != 6) return false;
  String prefix = zone.substring(0, 3);
  if (prefix != "ANZ" && prefix != "AMZ" &&
      prefix != "GMZ" && prefix != "PZZ") return false;
  for (int i = 3; i < 6; i++) {
    if (zone[i] < '0' || zone[i] > '9') return false;
  }
  return true;
}

// Maintain a small, deduplicated list of recently selected zone codes.
// This only updates RAM. Save to Preferences when the form is submitted.
void rememberZone(String zone) {
  zone = normalizeZone(zone);
  if (!validZone(zone)) return;
  String next[MAX_SAVED_ZONES];
  int count = 0;
  next[count++] = zone;
  for (int i = 0; i < savedZoneCount && count < MAX_SAVED_ZONES; i++) {
    if (savedZones[i] != zone) next[count++] = savedZones[i];
  }
  savedZoneCount = count;
  for (int i = 0; i < count; i++) savedZones[i] = next[i];
}

// Compact storage in NVS, e.g. "ANZ331,ANZ335,AMZ650".
String savedZonesCSV() {
  String out;
  for (int i = 0; i < savedZoneCount; i++) {
    if (i) out += ',';
    out += savedZones[i];
  }
  return out;
}

void loadSavedZones() {
  savedZoneCount = 0;
  if (!settings.begin("marinewx", true)) return;
  String csv = settings.getString("zone_hist", "");
  bool historyInitialized = settings.getBool("zone_hist_init", false);
  settings.end();
  // Keep the order stored in NVS (most recent first).
  int start = 0;
  while (start < (int)csv.length() && savedZoneCount < MAX_SAVED_ZONES) {
    int end = csv.indexOf(',', start);
    if (end < 0) end = csv.length();
    String zone = normalizeZone(csv.substring(start, end));
    if (validZone(zone)) {
      bool duplicate = false;
      for (int i = 0; i < savedZoneCount; i++) {
        if (savedZones[i] == zone) duplicate = true;
      }
      if (!duplicate) savedZones[savedZoneCount++] = zone;
    }
    start = end + 1;
  }
  // One-time migration: remember the existing zone from older firmware.
  // Never insert it again automatically after the user deletes it from history.
  if (!historyInitialized) {
    if (wifiSSID.length() > 0) rememberZone(marineZone);
    if (settings.begin("marinewx", false)) {
      settings.putString("zone_hist", savedZonesCSV());
      settings.putBool("zone_hist_init", true);
      settings.end();
    }
  }
}

// Remove one item from recent history without changing the active zone.
bool deleteSavedZone(String zone) {
  zone = normalizeZone(zone);
  if (!validZone(zone)) return false;
  int found = -1;
  for (int i = 0; i < savedZoneCount; ++i) {
    if (savedZones[i] == zone) { found = i; break; }
  }
  if (found < 0) return false;
  String updated;
  for (int i = 0; i < savedZoneCount; ++i) {
    if (i == found) continue;
    if (updated.length()) updated += ',';
    updated += savedZones[i];
  }
  if (!settings.begin("marinewx", false)) return false;
  size_t written = settings.putString("zone_hist", updated);
  settings.end();
  if (written != updated.length()) return false;
  for (int i = found; i < savedZoneCount - 1; ++i) {
    savedZones[i] = savedZones[i + 1];
  }
  --savedZoneCount;
  return true;
}

// Nearby marine forecast zones. NOAA publishes marine-zone geographic centroids
// (id,lat,lon) in its public NWS GIS FeatureServer. During configuration,
// we can download that list using the ESP32's EXISTING home Wi-Fi connection.
// This is optional: portal remains functional without Internet access.
// The returned nearest centroids are approximate geographic suggestions, not
// a guaranteed list of directly adjoining forecast zones.
// Live results are cached per selected zone in Preferences for offline reuse.
const int MAX_NEARBY_ZONES = 6;
const char *NWS_ZONES_ENDPOINT =
    "https://mapservices.weather.noaa.gov/static/rest/services/"
    "nws_reference_maps/nws_reference_map/FeatureServer/5/query?"
    "where=1%3D1&outFields=id%2Clat%2Clon&returnGeometry=false&f=json";

String storedNearKey(const String &zone) {
  return "near_" + zone;   // e.g. near_ANZ331, within Preferences 15-char limit
}

// Curated local suggestions verified against NWS marine-zone listings.
// Never fabricate 'nearby' zones based on numerical code similarity.
String localNearbyCSV(const String &zone) {
  if (zone == "ANZ331") return "ANZ332,ANZ335,ANZ340,ANZ237,ANZ350";
  if (zone == "ANZ332") return "ANZ331,ANZ340,ANZ237,ANZ235,ANZ256";
  if (zone == "ANZ335") return "ANZ331,ANZ338,ANZ345,ANZ355,ANZ353";
  if (zone == "ANZ340") return "ANZ332,ANZ331,ANZ350,ANZ237,ANZ256";
  if (zone == "ANZ338") return "ANZ335,ANZ355,ANZ345,ANZ353";
  if (zone == "ANZ350") return "ANZ340,ANZ353,ANZ256,ANZ331";
  if (zone == "ANZ353") return "ANZ350,ANZ355,ANZ345,ANZ335";
  if (zone == "ANZ355") return "ANZ353,ANZ338,ANZ335,ANZ345";
  if (zone == "ANZ232") return "ANZ233,ANZ234,ANZ255,ANZ254";
  if (zone == "ANZ233") return "ANZ232,ANZ234,ANZ235,ANZ255";
  if (zone == "ANZ234") return "ANZ233,ANZ235,ANZ236,ANZ232";
  if (zone == "ANZ235") return "ANZ236,ANZ237,ANZ234,ANZ233,ANZ256";
  if (zone == "ANZ236") return "ANZ235,ANZ237,ANZ234";
  if (zone == "ANZ237") return "ANZ235,ANZ236,ANZ256,ANZ332,ANZ340";
  if (zone == "ANZ255") return "ANZ232,ANZ233,ANZ254,ANZ256";
  if (zone == "ANZ256") return "ANZ237,ANZ235,ANZ340,ANZ350,ANZ255";
  return "";
}

String readNearbyCache(const String &zone) {
  if (!settings.begin("marinewx", true)) return "";
  String csv = settings.getString(storedNearKey(zone).c_str(), "");
  settings.end();
  return csv;
}

// Small JSON field readers for the narrowly defined ArcGIS query response.
// We intentionally request no polygon geometry or huge GeoJSON object.
String valueForJsonKey(const String &part, const char *key) {
  String token = String("\"") + key + "\"";
  int keyAt = part.indexOf(token);
  if (keyAt < 0) return "";
  int colon = part.indexOf(':', keyAt + token.length());
  if (colon < 0) return "";
  int i = colon + 1;
  while (i < (int)part.length() && part[i] == ' ') ++i;
  bool quoted = i < (int)part.length() && part[i] == '"';
  if (quoted) ++i;
  int end = i;
  while (end < (int)part.length()) {
    char c = part[end];
    if (quoted ? (c == '"') : (c == ',' || c == '}')) break;
    ++end;
  }
  String result = part.substring(i, end);
  result.trim();
  return result;
}

float nearbyDistanceKm(float lat1, float lon1, float lat2, float lon2) {
  // Haversine distance between NOAA zone centroids.
  const float rad = 0.01745329252f;
  float a = sinf((lat2-lat1)*rad/2);
  float b = sinf((lon2-lon1)*rad/2);
  float h = a*a + cosf(lat1*rad)*cosf(lat2*rad)*b*b;
  h = constrain(h, 0.0f, 1.0f);
  return 12742.0f * asinf(sqrtf(h));
}

// Returns comma-separated closest six codes. Empty string means unavailable.
// No JSON library is needed, keeping the original build dependencies intact.
String queryNearbyNOAA(const String &selected) {
  if (WiFi.status() != WL_CONNECTED) return "";
  WiFiClientSecure client;
  client.setInsecure();  // Same prototype TLS policy as the NOAA forecast feed.
  HTTPClient http;
  if (!http.begin(client, NWS_ZONES_ENDPOINT)) return "";
  http.setTimeout(10000);
  http.addHeader("User-Agent", "MarineWeatherESP32/0.5.2");
  int code = http.GET();
  if (code != 200) { http.end(); return ""; }
  String raw = http.getString();
  http.end();
  if (raw.indexOf("\"features\"") < 0 || raw.indexOf("\"attributes\"") < 0)
    return "";

  // Pass one: find selected zone's NOAA centroid.
  float centerLat = 0, centerLon = 0;
  bool found = false;
  int cursor = 0;
  while (true) {
    int start = raw.indexOf("\"attributes\"", cursor);
    if (start < 0) break;
    int end = raw.indexOf('}', start);
    if (end < 0) break;
    String part = raw.substring(start, end + 1);
    String candidate = normalizeZone(valueForJsonKey(part, "id"));
    if (candidate == selected) {
      centerLat = valueForJsonKey(part, "lat").toFloat();
      centerLon = valueForJsonKey(part, "lon").toFloat();
      found = (fabsf(centerLat) > 0.01f && fabsf(centerLon) > 0.01f);
      break;
    }
    cursor = end + 1;
  }
  if (!found) return "";

  // Pass two: nearest six marine-zone centroids (within 250 km of selected).
  float distances[MAX_NEARBY_ZONES];
  String chosen[MAX_NEARBY_ZONES];
  int count = 0;
  cursor = 0;
  while (true) {
    int start = raw.indexOf("\"attributes\"", cursor);
    if (start < 0) break;
    int end = raw.indexOf('}', start);
    if (end < 0) break;
    String part = raw.substring(start, end + 1);
    cursor = end + 1;
    String candidate = normalizeZone(valueForJsonKey(part, "id"));
    if (!validZone(candidate) || candidate == selected) continue;
    float lat = valueForJsonKey(part, "lat").toFloat();
    float lon = valueForJsonKey(part, "lon").toFloat();
    if (fabsf(lat) < 0.01f || fabsf(lon) < 0.01f) continue;
    float distance = nearbyDistanceKm(centerLat, centerLon, lat, lon);
    if (distance > 250.0f) continue;
    int pos = 0;
    while (pos < count && distances[pos] <= distance) ++pos;
    if (pos >= MAX_NEARBY_ZONES) continue;
    if (count < MAX_NEARBY_ZONES) ++count;
    for (int k=count-1; k>pos; --k) {
      distances[k] = distances[k-1];
      chosen[k] = chosen[k-1];
    }
    chosen[pos] = candidate;
    distances[pos] = distance;
  }
  String result;
  for (int i=0; i<count; ++i) {
    if (i) result += ',';
    result += chosen[i];
  }
  return result;
}

String nearResponseJson(String zone) {
  zone = normalizeZone(zone);
  if (!validZone(zone)) return "{\"zones\":[],\"source\":\"invalid\"}";
  String csv = readNearbyCache(zone);
  String source = "cached";
  if (csv.length() == 0) {
    // An online request is made only on cache miss, not every dropdown tap.
    // It may take several seconds. AP setup works even if this fails.
    csv = queryNearbyNOAA(zone);
    if (csv.length()) {
      source = "NOAA";
      if (settings.begin("marinewx", false)) {
        settings.putString(storedNearKey(zone).c_str(), csv);
        settings.end();
      }
    } else {
      csv = localNearbyCSV(zone);
      source = csv.length() ? "local" : "unavailable";
    }
  }
  String json = "{\"source\":\"" + source + "\",\"zones\":[";
  int start = 0;
  bool first = true;
  while (start < (int)csv.length()) {
    int end = csv.indexOf(',', start);
    if (end < 0) end = csv.length();
    String candidate = normalizeZone(csv.substring(start, end));
    if (validZone(candidate) && candidate != zone) {
      if (!first) json += ',';
      json += "\"";
      json += candidate;
      json += "\"";
      first = false;
    }
    start = end + 1;
  }
  json += "]}";
  return json;
}

String noaaURL() {
  String directory = marineZone.substring(0, 2);
  String filename = marineZone;
  directory.toLowerCase();
  filename.toLowerCase();
  return String("https://tgftp.nws.noaa.gov/data/forecasts/marine/coastal/") +
         directory + "/" + filename + ".txt";
}

// Startup splash for the 320 x 240 landscape TFT. No bitmap files needed.
// Keep it visible while joining Wi-Fi and downloading the NOAA forecast.
void drawBootScreen() {
  const uint16_t SPLASH_SKY = 0x08C9;
  const uint16_t SPLASH_SEA = 0x01D4;
  const uint16_t BOAT_HULL = 0xFFFF;
  const uint16_t BOAT_SHADOW = 0x4DD9;

  tft.fillScreen(SPLASH_SKY);
  tft.setTextWrap(false);

  // Clean, centered two-line title.
  tft.setTextSize(3);
  tft.setTextColor(WHITE);
  tft.setCursor(106, 19);  // 'MARINE' is 108px wide at size 3
  tft.print("MARINE");
  tft.setCursor(97, 55);   // 'WEATHER' is 126px wide at size 3
  tft.print("WEATHER");
  tft.drawFastHLine(75, 92, 170, CYAN);

  // Little sailboat: mast, two sails, and a tapered hull.
  tft.drawLine(160, 106, 160, 160, WHITE);
  tft.fillTriangle(155, 111, 155, 153, 130, 153, WHITE);
  tft.fillTriangle(165, 121, 165, 153, 188, 153, CYAN);
  tft.drawFastHLine(127, 157, 67, WHITE);
  tft.fillTriangle(116, 161, 204, 161, 193, 178, BOAT_HULL);
  tft.fillTriangle(116, 161, 193, 178, 128, 178, BOAT_HULL);
  tft.fillRect(131, 167, 56, 5, BOAT_SHADOW);
  tft.drawFastHLine(128, 178, 65, CYAN);

  // Smooth stylized rolling ocean waves.
  tft.fillRect(0, 181, 320, 59, SPLASH_SEA);
  for (int x = 0; x < 319; x += 2) {
    int y1 = 186 + (int)(4.0f * sinf(x * 0.105f));
    int y2 = 186 + (int)(4.0f * sinf((x + 2) * 0.105f));
    tft.drawLine(x, y1, x + 2, y2, CYAN);
    int z1 = 201 + (int)(3.0f * sinf(x * 0.080f + 1.3f));
    int z2 = 201 + (int)(3.0f * sinf((x + 2) * 0.080f + 1.3f));
    tft.drawLine(x, z1, x + 2, z2, BOAT_SHADOW);
  }

  tft.setTextSize(1);
  tft.setTextColor(WHITE);
  tft.setCursor(97, 222);
  tft.print("NOAA MARINE FORECAST");
}

void message(const String &heading, const String &subheading) {
  tft.fillScreen(BG);
  tft.setTextWrap(false);
  tft.setTextColor(CYAN);
  tft.setTextSize(2);
  tft.setCursor(14, 55);
  tft.print(heading);
  tft.setTextColor(WHITE);
  tft.setTextSize(1);
  tft.setCursor(14, 98);
  tft.print(subheading);
}

void printWrapped(String s, int x, int y,
                  int maxChars, int maxLines,
                  int size, uint16_t color) {
  s.trim();
  tft.setTextSize(size);
  tft.setTextColor(color);
  int pos = 0;
  for (int line = 0; line < maxLines && pos < (int)s.length(); line++) {
    int end = pos + maxChars;
    if (end >= (int)s.length()) end = s.length();
    else {
      int space = s.lastIndexOf(' ', end);
      if (space > pos + 3) end = space;
    }
    String part = s.substring(pos, end);
    if (line == maxLines - 1 && end < (int)s.length()) {
      if ((int)part.length() > maxChars - 3) part.remove(maxChars - 3);
      part += "...";
    }
    tft.setCursor(x, y + line * (size * 8 + 2));
    tft.print(part);
    pos = end;
    while (pos < (int)s.length() && s[pos] == ' ') pos++;
  }
}

String sentenceAt(const String &s, const String &key) {
  int pos = s.indexOf(key);
  if (pos < 0) return "";
  int end = s.indexOf('.', pos);
  if (end < 0) end = s.length();
  String result = s.substring(pos, end);
  result.trim();
  return result;
}

bool parseForecast(const String &raw) {
  int starts[5];
  int dataStarts[5];
  String labels[5];
  int n = 0, scan = 0;
  while (n < 5) {
    int p = raw.indexOf("\n.", scan);
    if (p < 0) break;
    scan = p + 2;
    int sep = raw.indexOf("...", p + 2);
    if (sep < 0 || sep - (p + 2) > 28) continue;
    String label = raw.substring(p + 2, sep);
    label.trim();
    bool valid = label.length() >= 3;
    for (int j = 0; j < (int)label.length(); j++) {
      char c = label[j];
      if (!((c >= 'A' && c <= 'Z') || c == ' ')) valid = false;
    }
    if (!valid) continue;
    starts[n] = p;
    dataStarts[n] = sep + 3;
    labels[n] = label;
    ++n;
  }
  if (n == 0) return false;
  int count = n < 4 ? n : 4;
  String newNames[4];
  String newBodies[4];
  for (int i = 0; i < count; i++) {
    int end = (i + 1 < n) ? starts[i + 1] :
              raw.indexOf("$$", dataStarts[i]);
    if (end < 0) end = raw.length();
    newNames[i] = labels[i];
    newBodies[i] = raw.substring(dataStarts[i], end);
    newBodies[i].replace("\r", " ");
    newBodies[i].replace("\n", " ");
    while (newBodies[i].indexOf("  ") >= 0) newBodies[i].replace("  ", " ");
    newBodies[i].trim();
  }
  String heading = raw.substring(0, starts[0]);
  heading.toUpperCase();
  String newWarning;
  const char* hazards[] = {
    "HURRICANE FORCE WIND WARNING", "STORM WARNING", "GALE WARNING",
    "SMALL CRAFT ADVISORY", "DENSE FOG ADVISORY"
  };
  for (int i = 0; i < 5; ++i) {
    if (heading.indexOf(hazards[i]) >= 0) {
      newWarning = hazards[i];
      break;
    }
  }
  for (int i = 0; i < count; ++i) {
    names[i] = newNames[i];
    bodies[i] = newBodies[i];
  }
  periodCount = count;
  warning = newWarning;
  if (currentPage >= periodCount) currentPage = 0;
  return true;
}

void drawForecast() {
  if (periodCount == 0) {
    message("NO FORECAST", "Check Wi-Fi or NOAA zone in setup");
    return;
  }
  String body = bodies[currentPage];
  String wind = body;
  int dot = wind.indexOf('.');
  if (dot >= 0) wind = wind.substring(0, dot);
  String seas = sentenceAt(body, "Seas ");
  if (seas.length() == 0) seas = "Not provided";
  // NOAA sometimes omits Wave Detail and may vary its capitalization.
  // Keep the bottom row visible even when NOAA does not provide wave detail.
  String upperBody = body;
  upperBody.toUpperCase();
  int wavePos = upperBody.indexOf("WAVE DETAIL:");
  String waves = "";
  if (wavePos >= 0) {
    int start = wavePos + 12;  // Length of "Wave Detail:"
    int end = body.indexOf('.', start);
    if (end < 0) end = body.length();
    waves = body.substring(start, end);
    waves.trim();
  }
  String details = dot >= 0 ? body.substring(dot + 1) : "";
  int p = details.indexOf("Seas ");
  if (p >= 0) {
    int e = details.indexOf('.', p);
    if (e < 0) e = details.length() - 1;
    details.remove(p, e - p + 1);
  }
  String upperDetails = details;
  upperDetails.toUpperCase();
  p = upperDetails.indexOf("WAVE DETAIL:");
  if (p >= 0) {
    int e = details.indexOf('.', p);
    if (e < 0) e = details.length() - 1;
    details.remove(p, e - p + 1);
  }
  details.trim();
  tft.fillScreen(BG);
  tft.setTextWrap(false);
  tft.fillRect(0, 0, 320, 24, NAVY);
  tft.setTextSize(1);
  tft.setTextColor(WHITE);
  tft.setCursor(10, 8);
  tft.print(names[currentPage]);
  tft.setTextColor(CYAN);
  tft.setCursor(286, 8);
  tft.print(currentPage + 1);
  tft.print("/");
  tft.print(periodCount);
  int y;
  if (warning.length() > 0) {
    tft.fillRoundRect(7, 29, 306, 19, 4, ORANGE);
    tft.setTextColor(ST77XX_BLACK);
    tft.setTextSize(1);
    tft.setCursor(13, 35);
    tft.print(warning);
    y = 55;
  } else {
    tft.drawFastHLine(8, 28, 304, CYAN);
    y = 34;
  }
  tft.setTextSize(1);
  tft.setTextColor(CYAN);
  tft.setCursor(10, y);
  tft.print("WIND");
  const int windLines = 4;
  printWrapped(wind, 10, y + 12, 24, windLines, 2, WHITE);
  int seasY = y + 12 + windLines * 18 + 5;
  tft.setTextSize(1);
  tft.setTextColor(CYAN);
  tft.setCursor(10, seasY);
  tft.print("SEAS");
  printWrapped(seas, 10, seasY + 13, 24, 3, 2, WHITE);
  int detailY = seasY + 71;
  // Reserve the last row for WAVE DETAIL on every forecast page.
  int waveTop = 221;
  int availableSpace = waveTop - detailY - 8;
  int availableLines = availableSpace >= 0 ? availableSpace / 10 + 1 : 0;
  if (availableLines > 0 && details.length() > 0) {
    printWrapped(details, 10, detailY, 49, availableLines, 1, GRAY);
  }
  // Show the WAVE DETAIL label even when NOAA omits the data.
  tft.drawFastHLine(8, 221, 304, CYAN);
  tft.setTextSize(1);
  tft.setTextColor(CYAN);
  tft.setCursor(10, 227);
  tft.print("WAVE DETAIL:");
  String waveText = waves.length() > 0 ? waves : "Not provided";
  // 38 chars from x=90 fit on a 320px screen at size 1.
  if (waveText.length() > 38) waveText = waveText.substring(0, 35) + "...";
  tft.setTextColor(waves.length() > 0 ? WHITE : GRAY);
  tft.setCursor(90, 227);
  tft.print(waveText);
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000UL) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool fetchForecast() {
  lastAttempt = millis();
  if (!connectWiFi()) {
    updateFailed = true;
    Serial.println("Wi-Fi unavailable");
    return false;
  }
  WiFiClientSecure client;
  // Prototype: HTTPS encryption but no certificate verification.
  client.setInsecure();
  HTTPClient http;
  String url = noaaURL();
  if (!http.begin(client, url)) {
    updateFailed = true;
    return false;
  }
  http.setTimeout(12000);
  http.addHeader("User-Agent", "MarineWeatherESP32/0.4");
  int code = http.GET();
  Serial.print("NOAA HTTP status: ");
  Serial.println(code);
  String raw;
  if (code == 200) raw = http.getString();
  http.end();
  if (code != 200 || raw.indexOf(marineZone + "-") < 0 || !parseForecast(raw)) {
    updateFailed = true;
    Serial.println("Missing or unsupported NOAA forecast for " + marineZone);
    return false;
  }
  lastSuccess = millis();
  updateFailed = false;
  Serial.println("NOAA forecast updated: " + marineZone);
  return true;
}

String setupPage(const String &error = "") {
  String html;
  html.reserve(6500);
  html += F("<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>Marine Weather Setup</title><style>"
            "*{box-sizing:border-box}body{margin:0;background:#061a2a;color:#edf8ff;"
            "font:16px system-ui,-apple-system,Arial,sans-serif;padding:25px 14px}"
            ".panel{max-width:450px;margin:auto;background:#0c2b40;border:1px solid #295572;"
            "border-radius:18px;padding:25px;box-shadow:0 12px 40px #0004}"
            "h1{font-size:23px;margin:8px 0 6px}.subtitle{color:#a4c9d9;margin:0 0 23px}"
            "label{display:block;font-weight:650;margin:15px 0 6px}"
            "input,select{width:100%;font:inherit;padding:13px;border:1px solid #62899a;"
            "border-radius:9px;background:#061b2a;color:white}"
            "button{width:100%;padding:14px;margin-top:23px;border:0;border-radius:10px;"
            "background:#43cbe2;color:#052033;font:700 17px system-ui}"
            ".note{font-size:13px;color:#b3c9d6;line-height:1.5}"
            ".error{background:#502b19;color:#ffe1ab;padding:10px;border-radius:8px}"
            ".tag{color:#6ce7f1;letter-spacing:.1em;font-weight:800;font-size:12px}"
            ".zonebox{display:flex;position:relative;gap:4px}.zonebox input{flex:1;min-width:0;}"
            ".zonebox .zone-toggle{width:53px;margin:0;padding:9px 0;background:#155574;"
            "border:1px solid #62899a;border-radius:9px;color:white;font-size:21px}"
            ".zone-menu{margin-top:4px;border:1px solid #62899a;border-radius:10px;"
            "background:#09273b;max-height:245px;overflow:auto;padding:7px}"
            ".zone-menu[hidden],.zone-group[hidden]{display:none}"
            ".zone-heading{color:#83dce9;font-size:11px;font-weight:750;"
            "letter-spacing:.08em;padding:8px 9px 2px}"
            ".zone-option{display:block;width:100%;text-align:left;background:transparent;"
            "margin:1px 0;padding:10px;border-radius:7px;color:white;font:inherit;"
            "font-size:16px;touch-action:pan-y;user-select:none;-webkit-user-select:none}"
            ".zone-option:focus,.zone-option:hover{background:#17475f}"
            ".zone-hint{font-size:12px;color:#a8c8d6;margin:7px 0}"
            ".zone-status{color:#85e8eb;font-size:13px;min-height:18px}"
            "</style></head><body><main class='panel'>"
            "<div class='tag'>NOAA MARINE WEATHER</div>"
            "<h1>Configure your display</h1>"
            "<p class='subtitle'>Settings are saved on the ESP32, not on a public website.</p>");
  if (error.length()) html += "<p class='error'>" + escapedHtml(error) + "</p>";
  html += F("<form action='/save' method='POST' autocomplete='off'>"
            "<label for='ssid'>Your 2.4 GHz Wi-Fi name</label>"
            "<input id='ssid' name='ssid' maxlength='32' required placeholder='Home network' value='");
  html += escapedHtml(wifiSSID);
  html += F("'><label for='password'>Wi-Fi password</label>"
            "<input id='password' name='password' type='password' maxlength='63' "
            "placeholder='Enter network password'>"
            "<p class='note'>If you're editing an existing setup, leaving the password "
            "blank keeps the saved password when the network name is unchanged. "
            "New open networks can also use a blank password.</p>");
  // One editable zone input. The arrow reveals saved and geographically
  // nearby zones within the same dropdown (not a second zone select box).
  html += F("<label for='zone'>NOAA marine forecast zone</label>"
            "<div class='zonebox'><input id='zone' name='zone' maxlength='6' "
            "pattern='[A-Za-z]{3}[0-9]{3}' autocomplete='off' required value='");
  html += escapedHtml(marineZone);
  html += F("' placeholder='ANZ331' aria-controls='zoneMenu' aria-expanded='false'>"
            "<button type='button' id='zoneToggle' class='zone-toggle' "
            "aria-label='Show saved and nearby marine zones' aria-controls='zoneMenu' "
            "aria-expanded='false'>&#9662;</button></div>"
            "<div id='zoneMenu' class='zone-menu' hidden>"
            "<div id='recentGroup' class='zone-group'");
  if (savedZoneCount == 0) html += F(" hidden");
  html += F("><div class='zone-heading'>PREVIOUSLY USED - HOLD TO DELETE</div>"
            "<div id='recentItems'>");
  for (int i = 0; i < savedZoneCount; i++) {
    html += "<button type='button' class='zone-option saved-zone' data-zone='" +
            escapedHtml(savedZones[i]) + "' aria-label='" +
            escapedHtml(savedZones[i]) + ", tap to select, hold to delete'>" +
            escapedHtml(savedZones[i]) + "</button>";
  }
  html += F("</div></div><div id='nearGroup' class='zone-group'>"
            "<div class='zone-heading'>NEARBY ZONES - BASED ON SELECTED ZONE</div>"
            "<div id='nearItems'></div>"
            "<p id='nearHint' class='zone-hint'>Finding nearby NOAA areas...</p>"
            "</div></div>"
            "<p class='zone-hint'>Tap the arrow for saved or nearby areas. "
            "Hold a previously used code for 1 second to delete it. "
            "You can also type a new zone code.</p>"
            "<p id='zoneStatus' class='zone-status' role='status' aria-live='polite'></p>"
            "<p class='note'>Nearby suggestions use NOAA marine-zone centroids "
            "(approximate distance, not necessarily shared boundaries). "
            "An Internet connection is needed to look up new regions; cached "
            "suggestions and some local examples work offline. "
            "<a href='https://www.weather.gov/marine/usamz' target='_blank' "
            "rel='noopener'>View NOAA's coastal zone map</a>.</p>"
            "<label for='timeout'>Backlight timeout</label>"
            "<select id='timeout' name='timeout' required>");
  html += backlightOption(15, "15 seconds");
  html += backlightOption(30, "30 seconds (default)");
  html += backlightOption(60, "1 minute");
  html += backlightOption(120, "2 minutes");
  html += backlightOption(300, "5 minutes");
  html += backlightOption(600, "10 minutes");
  html += backlightOption(0, "Always on");
  html += F("</select>"
            "<p class='note'>The timer resets when you press BOOT. "
            "The backlight stays on during setup. "
            "Your MarineWX setup password is generated once per ESP32 and "
            "is displayed on the screen whenever you reenter setup mode.</p>"
            "<button type='submit'>Save settings &amp; restart</button></form>"
            "<p class='note'>After saving, reconnect your phone to its normal Wi-Fi. "
            "To change settings later, hold BOOT for 4 seconds while running.</p>"
            "</main>");
  html += R"MWJS(
<script>
(() => {
  const field = document.getElementById('zone');
  const toggle = document.getElementById('zoneToggle');
  const menu = document.getElementById('zoneMenu');
  const recentItems = document.getElementById('recentItems');
  const recentGroup = document.getElementById('recentGroup');
  const nearItems = document.getElementById('nearItems');
  const nearHint = document.getElementById('nearHint');
  let nearZoneLoaded = '';
  let nearFetchToken = 0;
  let zoneTypingTimer = null;
  const status = document.getElementById('zoneStatus');
  let holdTimer = null;
  let longPressed = false;
  let moved = false;
  let startX = 0;
  let startY = 0;

  function openMenu(show) {
    menu.hidden = !show;
    field.setAttribute('aria-expanded', String(show));
    toggle.setAttribute('aria-expanded', String(show));
    if (show) loadNearby();
  }
  function choose(zone) {
    field.value = zone;
    status.textContent = 'Selected ' + zone + '. Press Save settings to apply.';
    nearZoneLoaded = '';
    openMenu(false);
  }
  async function loadNearby() {
    const code = field.value.trim().toUpperCase();
    if (!/^(ANZ|AMZ|GMZ|PZZ)\d{3}$/.test(code)) {
      nearItems.replaceChildren();
      nearHint.textContent = 'Enter a complete marine zone code to see nearby areas.';
      return;
    }
    if (code === nearZoneLoaded) return;
    const token = ++nearFetchToken;
    nearZoneLoaded = code;
    nearItems.replaceChildren();
    nearHint.textContent = 'Looking up nearby zones...';
    try {
      const response = await fetch('/nearby?zone=' + encodeURIComponent(code));
      if (!response.ok) throw new Error('Lookup failed');
      const info = await response.json();
      if (token !== nearFetchToken || field.value.trim().toUpperCase() !== code) return;
      nearItems.replaceChildren();
      for (const entry of info.zones || []) {
        if (recentItems.querySelector('[data-zone="' + entry + '"]')) continue;
        const option = document.createElement('button');
        option.type = 'button';
        option.className = 'zone-option';
        option.dataset.zone = entry;
        option.textContent = entry;
        option.addEventListener('click', () => choose(entry));
        nearItems.appendChild(option);
      }
      const count = nearItems.children.length;
      nearHint.textContent = count ?
        (info.source === 'local' ? 'Regional examples available offline - verify on NOAA map.' :
         info.source === 'cached' ? 'Previously downloaded nearby zones - verify on NOAA map.' :
         'Nearby NOAA zone centroids - verify on NOAA map.') :
        'No additional nearby zones found. Check the NOAA map.';
    } catch (_) {
      if (token === nearFetchToken) {
        nearZoneLoaded = '';
        nearHint.textContent = 'Could not look up nearby zones. Use NOAA map.';
      }
    }
  }
  async function deleteZone(zone, element) {
    if (!confirm('Remove ' + zone + ' from your previously used zones?')) return;
    try {
      const response = await fetch('/delete_zone', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'zone=' + encodeURIComponent(zone)
      });
      if (!response.ok) throw new Error('Could not remove saved zone');
      element.remove();
      if (!recentItems.querySelector('.saved-zone')) recentGroup.hidden = true;
      status.textContent = zone + ' removed from history. Active forecast unchanged.';
    } catch (err) {
      status.textContent = 'Could not delete zone. Try again.';
    }
  }
  function stopTimer() {
    if (holdTimer !== null) clearTimeout(holdTimer);
    holdTimer = null;
  }
  toggle.addEventListener('click', () => openMenu(menu.hidden));
  field.addEventListener('focus', () => openMenu(true));
  field.addEventListener('input', () => {
    field.value = field.value.toUpperCase();
    nearZoneLoaded = '';
    ++nearFetchToken;
    if (zoneTypingTimer !== null) clearTimeout(zoneTypingTimer);
    zoneTypingTimer = setTimeout(() => {
      openMenu(true);
    }, 650);
  });
  menu.querySelectorAll('.zone-option').forEach((item) => {
    const zone = item.dataset.zone;
    const saved = item.classList.contains('saved-zone');
    item.addEventListener('pointerdown', (event) => {
      if (event.button !== 0) return;
      longPressed = false;
      moved = false;
      startX = event.clientX;
      startY = event.clientY;
      stopTimer();
      if (saved) holdTimer = setTimeout(() => {
        longPressed = true;
        holdTimer = null;
        deleteZone(zone, item);
      }, 950);
    });
    item.addEventListener('pointermove', (event) => {
      if (Math.abs(event.clientX - startX) > 12 ||
          Math.abs(event.clientY - startY) > 12) {
        moved = true;
        stopTimer();
      }
    });
    item.addEventListener('pointerup', (event) => {
      const tapped = !longPressed && !moved && event.button === 0;
      stopTimer();
      if (tapped) choose(zone);
    });
    item.addEventListener('pointercancel', stopTimer);
    item.addEventListener('pointerleave', stopTimer);
    item.addEventListener('contextmenu', (event) => {
      if (!saved) return;
      event.preventDefault();
      stopTimer();
      if (!longPressed) {
        longPressed = true;
        deleteZone(zone, item);
      }
    });
    item.addEventListener('click', (event) => {
      // Keyboard selection. Pointer input is handled above.
      if (event.detail === 0) choose(zone);
    });
  });
  document.addEventListener('pointerdown', (event) => {
    if (!menu.contains(event.target) && !toggle.contains(event.target) &&
        event.target !== field) openMenu(false);
  });
})();
</script></body></html>
)MWJS";
  return html;
}

void drawSetupScreen() {
  tft.enableDisplay(true);
  digitalWrite(TFT_BL, HIGH);
  screenAwake = true;
  tft.fillScreen(BG);
  tft.setTextWrap(false);
  tft.setTextColor(CYAN);
  tft.setTextSize(2);
  tft.setCursor(10, 8);
  tft.print("SETUP MODE");
  tft.setTextSize(1);
  tft.setTextColor(GRAY);
  tft.setCursor(10, 40);
  tft.print("Connect phone to Wi-Fi:");
  tft.setTextSize(2);
  tft.setTextColor(WHITE);
  tft.setCursor(10, 57);
  tft.print(setupSSID);
  tft.setTextSize(1);
  tft.setTextColor(CYAN);
  tft.setCursor(10, 96);
  tft.print("NETWORK PASSWORD:");
  tft.setTextSize(2);
  tft.setTextColor(WHITE);
  tft.setCursor(10, 110);
  tft.print(setupPassword);
  tft.setTextSize(1);
  tft.setTextColor(CYAN);
  tft.setCursor(10, 157);
  tft.print("OPEN IN PHONE BROWSER:");
  tft.setTextColor(WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 174);
  tft.print("192.168.4.1");
  tft.setTextSize(1);
  tft.setTextColor(GRAY);
  tft.setCursor(10, 215);
  tft.print("Password saved on this ESP32");
}

// Create a random per-device setup password exactly once.
// ESP32 Preferences stores it in NVS (nonvolatile flash). A normal firmware
// upload or restart retains it; a full erase of flash/NVS will reset it.
bool loadOrCreateSetupPassword() {
  if (!settings.begin("marinewx", false)) return false;

  setupPassword = settings.getString("ap_pass", "");

  // Accept an existing valid password so future firmware updates do not
  // unexpectedly change credentials even if password length later changes.
  if (setupPassword.length() < 8 || setupPassword.length() > 63) {
    // With the Wi-Fi radio enabled, esp_random() uses hardware RNG entropy.
    // Exclude easily confused 0/O and 1/I characters.
    const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    setupPassword = "";
    for (int i = 0; i < 12; i++) {
      setupPassword += alphabet[esp_random() % (sizeof(alphabet) - 1)];
    }
    if (settings.putString("ap_pass", setupPassword) != setupPassword.length()) {
      settings.end();
      setupPassword = "";
      return false;
    }
    Serial.println("Generated and saved setup Wi-Fi password for this ESP32");
  } else {
    Serial.println("Reusing saved setup Wi-Fi password");
  }

  settings.end();
  return true;
}

void startSetupPortal() {
  if (inSetup) return;
  inSetup = true;
  setupSSID = "MarineWX-" + String((uint16_t)(ESP.getEfuseMac() & 0xFFFF), HEX);
  setupSSID.toUpperCase();

  // Start the Wi-Fi radio before generating the initial random password.
  WiFi.mode(WIFI_AP_STA);
  if (!loadOrCreateSetupPassword()) {
    Serial.println("ERROR: Unable to save setup Wi-Fi password");
    message("SETUP ERROR", "Cannot save setup password");
    inSetup = false;
    return;
  }

  bool ok = WiFi.softAP(setupSSID.c_str(), setupPassword.c_str());
  if (!ok) {
    Serial.println("ERROR: Setup AP could not be started");
    message("SETUP ERROR", "Cannot start setup Wi-Fi");
    inSetup = false;
    return;
  }
  Serial.println("Setup access point active");
  configServer.on("/", HTTP_GET, []() {
    configServer.send(200, "text/html; charset=utf-8", setupPage());
  });
  configServer.on("/nearby", HTTP_GET, []() {
    configServer.sendHeader("Cache-Control", "no-store");
    configServer.send(200, "application/json", nearResponseJson(configServer.arg("zone")));
  });
  // Delete only one zone from the local history; leave active NOAA zone alone.
  configServer.on("/delete_zone", HTTP_POST, []() {
    String zone = normalizeZone(configServer.arg("zone"));
    if (!validZone(zone)) {
      configServer.send(400, "text/plain", "Invalid zone code");
      return;
    }
    if (!deleteSavedZone(zone)) {
      configServer.send(404, "text/plain", "Zone not saved or could not persist change");
      return;
    }
    configServer.send(200, "application/json", "{\"ok\":true}");
  });
  configServer.on("/save", HTTP_POST, []() {
    String newSSID = configServer.arg("ssid");
    String newPassword = configServer.arg("password");
    String newZone = normalizeZone(configServer.arg("zone"));
    String timeoutValue = configServer.arg("timeout");
    if (newSSID.length() == 0 || newSSID.length() > 32 ||
        newPassword.length() > 63 || !validZone(newZone) ||
        !validTimeoutFormValue(timeoutValue)) {
      configServer.send(400, "text/html; charset=utf-8",
                        setupPage("Check the Wi-Fi name, password, zone code, and timeout."));
      return;
    }
    if (newPassword.length() == 0 && newSSID == wifiSSID) {
      newPassword = wifiPassword;
    }
    settings.begin("marinewx", false);
    settings.putString("ssid", newSSID);
    settings.putString("pass", newPassword);
    settings.putString("zone", newZone);
    rememberZone(newZone);  // Move the selected code to the top of recent zones.
    settings.putString("zone_hist", savedZonesCSV());
    settings.putUInt("timeout", (uint32_t)timeoutValue.toInt());
    settings.end();
    configServer.send(200, "text/html; charset=utf-8",
                      "<!doctype html><meta name='viewport' "
                      "content='width=device-width,initial-scale=1'>"
                      "<body style='font:18px system-ui;background:#082536;" 
                      "color:white;padding:32px'><h2>Saved!</h2>"
                      "<p>The ESP32 is restarting. Reconnect your phone to "
                      "your usual Wi-Fi. Your screen will show the forecast.</p></body>");
    rebootPending = true;
    rebootRequestedAt = millis();
  });
  configServer.onNotFound([]() {
    configServer.sendHeader("Location", "http://192.168.4.1/", true);
    configServer.send(302, "text/plain", "Open the Marine Weather setup page");
  });
  configServer.begin();
  captiveDns.start(53, "*", WiFi.softAPIP());
  drawSetupScreen();
}

void shortBootPress() {
  if (!screenAwake) {
    tft.enableDisplay(true);
    digitalWrite(TFT_BL, HIGH);
    screenAwake = true;
    drawForecast();
  } else if (periodCount > 0) {
    currentPage = (currentPage + 1) % periodCount;
    drawForecast();
  }
  lastInteraction = millis();
}

void checkBootButton() {
  bool reading = digitalRead(BOOT_PIN);
  if (reading != lastButtonReading) lastDebounceTime = millis();
  if (millis() - lastDebounceTime > DEBOUNCE_MS) {
    if (reading != stableButtonState) {
      stableButtonState = reading;
      if (stableButtonState == LOW) {
        buttonDownAt = millis();
        longPressHandled = false;
      } else if (!longPressHandled && !inSetup) {
        shortBootPress();
      }
    }
  }
  if (stableButtonState == LOW && !longPressHandled && !inSetup &&
      millis() - buttonDownAt >= LONG_PRESS_MS) {
    longPressHandled = true;
    startSetupPortal();
  }
  lastButtonReading = reading;
}

void setup() {
  Serial.begin(115200);
  pinMode(BOOT_PIN, INPUT_PULLUP);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(240, 320);
  tft.setRotation(1);
  tft.setTextWrap(false);
  drawBootScreen();
  delay(1000); // Let the splash be seen even on a fast Wi-Fi connection.
  settings.begin("marinewx", true);
  wifiSSID = settings.getString("ssid", "");
  wifiPassword = settings.getString("pass", "");
  marineZone = normalizeZone(settings.getString("zone", "ANZ331"));
  screenTimeoutSeconds = settings.getUInt("timeout", 30);
  settings.end();
  if (!validZone(marineZone)) marineZone = "ANZ331";
  loadSavedZones();
  if (!validTimeoutSeconds(screenTimeoutSeconds)) screenTimeoutSeconds = 30;
  screenTimeoutMs = screenTimeoutSeconds * 1000UL;
  lastInteraction = millis();

  if (wifiSSID.length() == 0) {
    startSetupPortal();
    return;
  }

  if (!connectWiFi()) {
    startSetupPortal();
    return;
  }
  fetchForecast();
  drawForecast();
  lastInteraction = millis();
}

void loop() {
  if (inSetup) {
    captiveDns.processNextRequest();
    configServer.handleClient();
    if (rebootPending && millis() - rebootRequestedAt >= 1800UL) {
      ESP.restart();
    }
    delay(2);
    return;
  }

  checkBootButton();
  if (inSetup) return;
  unsigned long now = millis();
  if (screenAwake && screenTimeoutMs > 0 &&
      now - lastInteraction >= screenTimeoutMs) {
    tft.enableDisplay(false);
    digitalWrite(TFT_BL, LOW);
    screenAwake = false;
  }
  unsigned long interval = updateFailed ? RETRY_MS : REFRESH_MS;
  if (now - lastAttempt >= interval) {
    fetchForecast();
    if (screenAwake) drawForecast();
  }
}
