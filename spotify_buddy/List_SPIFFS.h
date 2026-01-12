/***************************************************************************************
** Function name:           listSPIFFS
** Description:             Listing SPIFFS files (ESP32-compatible)
***************************************************************************************/
void listSPIFFS(void) {
  Serial.println(F("\r\nListing SPIFFS files:"));

  // Open root directory
  File root = SPIFFS.open("/");

  if (!root) {
    Serial.println(F("Failed to open SPIFFS root"));
    return;
  }

  static const char line[] PROGMEM =  "=================================================";
  Serial.println(FPSTR(line));
  Serial.println(F("  File name                              Size"));
  Serial.println(FPSTR(line));

  File file = root.openNextFile();
  while (file) {
    String fileName = String(file.name());
    Serial.print(fileName);
    int spaces = 33 - fileName.length(); // Tabulate nicely
    if (spaces < 1) spaces = 1;
    while (spaces--) Serial.print(" ");

    String fileSize = String(file.size());
    spaces = 10 - fileSize.length(); // Tabulate nicely
    if (spaces < 1) spaces = 1;
    while (spaces--) Serial.print(" ");
    Serial.println(fileSize + " bytes");

    file = root.openNextFile();
  }

  Serial.println(FPSTR(line));
  Serial.println();
  delay(1000);
}
