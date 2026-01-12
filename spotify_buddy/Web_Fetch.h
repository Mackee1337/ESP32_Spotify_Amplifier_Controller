// Fetch a file from the URL given and save it in SPIFFS
// Return true if a web fetch was needed and succeeded, false otherwise
bool getFile(const String &url, const String &filename) {

  // If it exists then no need to fetch it
  if (SPIFFS.exists(filename.c_str())) {
    Serial.println("Found " + filename);
    return true; // already present (treat as success)
  }

  Serial.println("Downloading "  + filename + " from " + url);

  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return false;
  }

  // Use WiFiClientSecure on ESP32
  WiFiClientSecure client;
  client.setInsecure(); // Accept all certs (OK for testing)

  HTTPClient http;
  // begin with a secure client instance
  if (!http.begin(client, url)) {
    Serial.println("[HTTP] begin failed");
    return false;
  }

  Serial.print("[HTTP] GET...\n");
  int httpCode = http.GET();
  if (httpCode <= 0) {
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    http.end();
    return false;
  }

  if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND) {
    File f = SPIFFS.open(filename.c_str(), FILE_WRITE);
    if (!f) {
      Serial.println("file open failed");
      http.end();
      return false;
    }

    int total = http.getSize();
    Serial.printf("[HTTP] GET... code: %d, size: %d\n", httpCode, total);

    // Buffer for reading
    uint8_t buff[256];
    WiFiClient * stream = http.getStreamPtr();

    // Read data from server and write to file
    while (http.connected() && (total > 0 || total == -1)) {
      size_t size = stream->available();
      if (size) {
        int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
        if (c > 0) {
          f.write(buff, c);
        }
        if (total > 0) total -= c;
      } else {
        delay(1);
      }
      yield();
    }
    Serial.println();
    Serial.println("[HTTP] connection closed or file end.");
    f.close();
    http.end();
    return true; // download ok
  } else {
    Serial.printf("[HTTP] GET... failed, response: %d\n", httpCode);
    http.end();
    return false;
  }
}
