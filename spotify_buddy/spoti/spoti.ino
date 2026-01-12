#include <TJpg_Decoder.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <vector>
#include <memory>

TFT_eSPI tft = TFT_eSPI();

const char* WIFI_SSID = ; // Wifi
const char* WIFI_PASS =; // PASSWORD
const char* PROXY_URL =; // URL

const int ALBUM_X = 10;
const int ALBUM_Y = 10;
const int ALBUM_W = 130;
const int ALBUM_H = 130;

int TITLE_X, TITLE_Y, ARTIST_Y;
int PROG_X, PROG_Y, PROG_W, PROG_H;
int TIME_Y;

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= tft.height()) return 0;
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

struct songDetails {
  int durationMs;
  String album;
  String artist;
  String song;
  String Id;
};

void formatTime(long ms, char* buf, size_t bufSize) {
  if (ms < 0) ms = 0;
  int s = ms / 1000;
  int m = s / 60;
  s = s % 60;
  snprintf(buf, bufSize, "%02d:%02d", m, s);
}

void drawWrappedCenteredText(const String &text, int centerX, int startY, int maxWidth, int font, int lineHeight, int maxLines) {

  std::vector<String> words;
  int idx = 0;
  while (idx < (int)text.length()) {
    int sp = text.indexOf(' ', idx);
    if (sp == -1) sp = text.length();
    String w = text.substring(idx, sp);
    if (w.length() > 0) words.push_back(w);
    idx = sp + 1;
  }

  String line = "";
  int drawnLines = 0;
  int y = startY;

  for (size_t i = 0; i < words.size(); ++i) {
    String test = line.length() ? (line + " " + words[i]) : words[i];
    int tw = tft.textWidth(test, font);

    if (tw <= maxWidth) {
      line = test;
    } else {
      if (line.length()) {
        tft.drawCentreString(line, centerX, y, font);
        y += lineHeight;
        drawnLines++;
        if (drawnLines >= maxLines) return;
      }

      line = words[i];
      if (tft.textWidth(line, font) > maxWidth) {

        String truncated = line;
        while (truncated.length() && tft.textWidth(truncated + "...", font) > maxWidth) {
          truncated.remove(truncated.length() - 1);
        }
        truncated += "...";
        tft.drawCentreString(truncated, centerX, y, font);
        y += lineHeight;
        drawnLines++;
        if (drawnLines >= maxLines) return;
        line = "";
      }
    }
  }

  if (line.length() && drawnLines < maxLines) {
    tft.drawCentreString(line, centerX, y, font);
  }
}

class SpotConn {
public:
  SpotConn() {
    client = std::make_unique<WiFiClientSecure>();
    client->setInsecure();
    accessTokenSet = false;
    tokenStartTime = 0;
    tokenExpireTime = 0;
    currentSongPositionMs = 0;
    lastSongId = "";
    isPlaying = true;
  }

  bool fetchTokenFromProxy(const char* proxyUrl) {
    HTTPClient http;
    http.begin(proxyUrl);
    int code = http.GET();
    if (code != 200) {
      Serial.print("Proxy token fetch failed, code: ");
      Serial.println(code);
      Serial.println(http.getString());
      http.end();
      return false;
    }

    String payload = http.getString();
    http.end();

    DynamicJsonDocument doc(2048);
    auto err = deserializeJson(doc, payload);
    if (err) {
      Serial.print("Proxy JSON parse error: ");
      Serial.println(err.c_str());
      return false;
    }

    if (!doc.containsKey("access_token")) {
      Serial.println("Proxy response lacks access_token");
      return false;
    }

    accessToken = String((const char*)doc["access_token"]);
    accessTokenSet = true;
    tokenStartTime = millis();
    tokenExpireTime = (int)(doc["expires_in"] | 3600);

    Serial.println("Got access token from proxy");
    return true;
  }

  bool downloadFile(const String &url, const char* path) {
    Serial.print("Downloading image: ");
    Serial.println(url);

    WiFiClientSecure *clientSecure = new WiFiClientSecure();
    clientSecure->setInsecure();
    HTTPClient http;

    if (!http.begin(*clientSecure, url)) {
      Serial.println("http.begin failed");
      delete clientSecure;
      return false;
    }

    int code = http.GET();
    if (code != 200) {
      Serial.print("Image download HTTP code: ");
      Serial.println(code);
      Serial.println(http.getString());
      http.end();
      delete clientSecure;
      return false;
    }

 
    File f = SPIFFS.open(path, FILE_WRITE);
    if (!f) {
      Serial.println("SPIFFS open for write failed");
      http.end();
      delete clientSecure;
      return false;
    }

    int contentLength = http.getSize();
    Serial.print("Content-Length: ");
    Serial.println(contentLength);

    WiFiClient * stream = http.getStreamPtr();
    const size_t bufSize = 512;
    uint8_t buffer[bufSize];
    int total = 0;
    unsigned long lastReport = millis();

    while (http.connected()) {
      size_t available = stream->available();
      if (available) {
        int toRead = available;
        if (toRead > (int)bufSize) toRead = bufSize;
        int c = stream->readBytes(buffer, toRead);
        if (c > 0) {
          f.write(buffer, c);
          total += c;
        }
      } else {

        delay(10);
      }

      if (contentLength > 0 && total >= contentLength) break;

      if (millis() - lastReport > 5000) {
        Serial.print("Downloaded bytes so far: ");
        Serial.println(total);
        lastReport = millis();
      }
    }

    f.close();
    http.end();
    delete clientSecure;

    Serial.print("Download finished, bytes: ");
    Serial.println(total);

    File fcheck = SPIFFS.open(path, FILE_READ);
    if (!fcheck) {
      Serial.println("Could not open saved file for check");
      return false;
    }
    size_t sz = fcheck.size();
    fcheck.close();

    Serial.print("Saved file size: ");
    Serial.println(sz);

    if (sz < 1000) {
      Serial.println("File suspiciously small — likely failed download or not an image");
      return false;
    }

    return true;
  }

  bool getTrackInfo() {
    if (!accessTokenSet) return false;

    WiFiClientSecure clientLocal;
    clientLocal.setInsecure();
    HTTPClient https;

    https.begin(clientLocal, "https://api.spotify.com/v1/me/player/currently-playing");
    https.addHeader("Authorization", "Bearer " + accessToken);
    int code = https.GET();

    if (code == 204) {
      https.end();
      return false;
    }
    if (code != 200) {
      Serial.print("Error getting track info: ");
      Serial.println(code);
      Serial.println(https.getString());
      https.end();
      return false;
    }

    String body = https.getString();
    https.end();

    StaticJsonDocument<20000> doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
      Serial.print("JSON parse error: ");
      Serial.println(err.c_str());
      return false;
    }

    long progress_ms = doc["progress_ms"] | 0;
    long duration_ms = doc["item"]["duration_ms"] | 0;

    const char* title  = doc["item"]["name"] | "Unknown";
    const char* artist = doc["item"]["artists"][0]["name"] | "Unknown";
    const char* album  = doc["item"]["album"]["name"] | "";
    const char* imageUrl = nullptr;

    if (doc["item"]["album"]["images"].is<JsonArray>()) {
      JsonArray imgs = doc["item"]["album"]["images"].as<JsonArray>();
      if (imgs.size() > 0) {
        int idx = min((int)imgs.size() - 1, 1);
        imageUrl = imgs[idx]["url"];
        if (!imageUrl) imageUrl = imgs[0]["url"];
      }
    }

    bool playing = true;
    if (doc.containsKey("is_playing")) {
      playing = doc["is_playing"] | true;
    }

    const char* uri = doc["item"]["uri"] | "";
    String songId = String(uri);
    if (songId.startsWith("spotify:track:")) {
      songId = songId.substring(String("spotify:track:").length());
    }

    currentSong.song   = String(title);
    currentSong.artist = String(artist);
    currentSong.album  = String(album);
    currentSong.Id     = songId;
    currentSong.durationMs = (int)duration_ms;
    currentSongPositionMs = (float)progress_ms;

    isPlaying = playing;

    if (currentSong.Id != lastSongId) {
      lastSongId = currentSong.Id;
      if (SPIFFS.exists("/albumArt.jpg")) SPIFFS.remove("/albumArt.jpg");
      if (imageUrl) {
        bool ok = downloadFile(String(imageUrl), "/albumArt.jpg");
        Serial.print("Image load was: ");
        Serial.println(ok);
      }
      fullRefresh = true;
    } else {

      if (isPlaying != lastIsPlaying) fullRefresh = true;
      else fullRefresh = false;
    }

    drawScreen(fullRefresh);

    lastSongPositionMs = currentSongPositionMs;
    lastIsPlaying = isPlaying;

    return true;
  }

  void drawScreen(bool fullRefresh) {
    int screenW = tft.width();
    int screenH = tft.height();

    if (fullRefresh) {
      tft.fillScreen(TFT_BLACK);

      // Albumart
      if (SPIFFS.exists("/albumArt.jpg")) {
        TJpgDec.setSwapBytes(true);
        TJpgDec.setJpgScale(2);
        TJpgDec.drawFsJpg(ALBUM_X, ALBUM_Y, "/albumArt.jpg");
      } else {
        tft.fillRect(ALBUM_X, ALBUM_Y, ALBUM_W, ALBUM_H, TFT_DARKGREY);
      }

      int centerX = TITLE_X + (screenW - TITLE_X) / 2;
      int textAreaW = screenW - TITLE_X - 8;

      int clearTop = ALBUM_Y - 4;
      int clearLeft = TITLE_X - 2;
      int clearW = textAreaW + 6;
      int clearH = ALBUM_H + 56;

      tft.fillRect(clearLeft, clearTop, clearW, clearH, TFT_BLACK);

      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      drawWrappedCenteredText(currentSong.song, centerX, TITLE_Y, textAreaW, 4, 20, 2);


      tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      drawWrappedCenteredText(currentSong.artist, centerX, ARTIST_Y, textAreaW, 2, 14, 2);
    } else {

      tft.fillRect(PROG_X - 6, PROG_Y - 8, PROG_W + 12, (TIME_Y - PROG_Y) + 40, TFT_BLACK);
    }

    int filled = (currentSong.durationMs > 0) ? (int)((currentSongPositionMs / currentSong.durationMs) * PROG_W) : 0;
    tft.drawRoundRect(PROG_X, PROG_Y, PROG_W, PROG_H, 4, TFT_WHITE);
    if (filled > 0)
      tft.fillRoundRect(PROG_X + 2, PROG_Y + 2, max(2, filled - 4), PROG_H - 4, 3, TFT_GREEN);


    char tbuf[32], dbuf[32];
    formatTime((long)currentSongPositionMs, tbuf, sizeof(tbuf));
    formatTime((long)currentSong.durationMs, dbuf, sizeof(dbuf));

    char timeStr[64];
    snprintf(timeStr, sizeof(timeStr), "%s / %s", tbuf, dbuf);

    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString(String(timeStr), PROG_X + PROG_W / 2, TIME_Y, 2);

    if (!isPlaying) {
      tft.setTextSize(1);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawCentreString("PAUSED", PROG_X + PROG_W / 2, PROG_Y - 16, 2);
    }
  }


  bool accessTokenSet = false;
  long tokenStartTime = 0;
  int tokenExpireTime = 0;

  songDetails currentSong;
  float currentSongPositionMs = 0;
  float lastSongPositionMs = 0;
  bool isPlaying = true;

private:
  std::unique_ptr<WiFiClientSecure> client;
  HTTPClient https;
  String accessToken;
  String lastSongId = "";
  bool fullRefresh = true;
  bool lastIsPlaying = true;
};

SpotConn spotifyConnection;

long refreshLoop = 0;

void setup() {
  Serial.begin(115200);
  delay(200);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS failed");
    while (1) yield();
  }

  tft.begin();
  tft.setRotation(1);

  tft.fillScreen(TFT_RED);
  delay(500);
  tft.fillScreen(TFT_BLUE);
  delay(500);
  tft.fillScreen(TFT_BLACK);

  int sw = tft.width();
  int sh = tft.height();

  TITLE_X = ALBUM_X + ALBUM_W + 18;
  TITLE_Y = ALBUM_Y + 8;
  ARTIST_Y = TITLE_Y + 50;

  PROG_W = sw - 16;
  PROG_H = 12;
  PROG_X = (sw - PROG_W) / 2;
  PROG_Y = ALBUM_Y + ALBUM_H + 48;
  TIME_Y = PROG_Y + PROG_H + 8;

  TJpgDec.setJpgScale(4);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  int cnt = 0;
  while (WiFi.status() != WL_CONNECTED && cnt < 60) {
    delay(300);
    Serial.print(".");
    cnt++;
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi failed");
    tft.setTextColor(TFT_RED);
    tft.drawString("WiFi failed", 10, 10, 2);
    return;
  }

  Serial.print("Connected to WiFi. IP: ");
  Serial.println(WiFi.localIP());
  tft.setTextColor(TFT_WHITE);
  tft.drawString("IP: " + WiFi.localIP().toString(), 10, 10, 2);
  tft.drawString("Using proxy", 10, 28, 2);
}

void loop() {
  static int lastSecondShown = -1;
  static unsigned long lastTick = millis();


  if (!spotifyConnection.accessTokenSet) {
    if (!spotifyConnection.fetchTokenFromProxy(PROXY_URL)) {
      delay(2000);
      return;
    }
    spotifyConnection.getTrackInfo();
    refreshLoop = millis();
  }

  if ((millis() - spotifyConnection.tokenStartTime) / 1000 > spotifyConnection.tokenExpireTime - 60) {
    spotifyConnection.fetchTokenFromProxy(PROXY_URL);
  }

  unsigned long now = millis();


  if (spotifyConnection.isPlaying) {
    spotifyConnection.currentSongPositionMs += (now - lastTick);
  }
  lastTick = now;


  if (now - refreshLoop > 1500) {
    spotifyConnection.getTrackInfo();
    refreshLoop = now;
  }

  int currentSecond = spotifyConnection.currentSongPositionMs / 1000;
  if (currentSecond != lastSecondShown) {
    spotifyConnection.drawScreen(false);
    lastSecondShown = currentSecond;
  }
}
