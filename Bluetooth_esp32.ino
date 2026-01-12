#include "AudioTools.h"
#include "BluetoothA2DPSink.h"
#include <math.h>


const int PIN_PCM_XSMT = 21;
const int ADC_PIN = 34;
const int PIN_STATUS_PAUSE = 32;

const int vuPins[10] = { 4, 16, 12, 13, 18, 19, 23, 25, 26, 27 };

const int LED_YELLOW_1 = 19;
const int LED_YELLOW_2 = 23;
const int LED_RED_1    = 25;
const int LED_RED_2    = 26;
const int LED_RED_3    = 27;


I2SStream i2s;
BluetoothA2DPSink a2dp_sink(i2s);

bool btConnected = false;
bool btPlaying   = false;
esp_a2d_connection_state_t lastConnState = ESP_A2D_CONNECTION_STATE_DISCONNECTED;

bool isFirstPlay = true; 

void update_dac_mute() {
  if (btConnected && btPlaying) {
    digitalWrite(PIN_PCM_XSMT, HIGH);
  } else {
    digitalWrite(PIN_PCM_XSMT, LOW);
  }
}

void on_connection_state(esp_a2d_connection_state_t state, void *obj) {
  bool isConnectedNow = (state == ESP_A2D_CONNECTION_STATE_CONNECTED);

  if (isConnectedNow && !btConnected) {
    a2dp_sink.set_volume(0);
    isFirstPlay = true; 
    Serial.println("BT Ansluten -> Volym satt till 0.");
  }

  btConnected = isConnectedNow;
  lastConnState = state;
  update_dac_mute();
}

void on_audio_state(esp_a2d_audio_state_t state, void *obj) {
  btPlaying = (state == ESP_A2D_AUDIO_STATE_STARTED);
  
  if (btPlaying && isFirstPlay) {
    a2dp_sink.set_volume(0);

    delay(150); 
    
    isFirstPlay = false; 
    Serial.println("Musik startad -> Peak hanterad (tyst start).");
  }

  update_dac_mute();
}

void on_volume_change(int vol) {
  if (isFirstPlay && vol > 0) {
    Serial.printf("Telefon försökte sätta volym %d - Tvingar ner till 0!\n", vol);
    a2dp_sink.set_volume(0);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PIN_PCM_XSMT, OUTPUT);
  digitalWrite(PIN_PCM_XSMT, LOW);

  for (int i = 0; i < 10; i++) {
    pinMode(vuPins[i], OUTPUT);
    digitalWrite(vuPins[i], LOW);
  }

  pinMode(PIN_STATUS_PAUSE, OUTPUT);
  digitalWrite(PIN_STATUS_PAUSE, LOW);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  auto cfg = i2s.defaultConfig(TX_MODE);
  cfg.pin_bck  = 14; 
  cfg.pin_ws   = 15; 
  cfg.pin_data = 22; 
  i2s.begin(cfg);

  a2dp_sink.set_on_connection_state_changed(on_connection_state, nullptr);
  a2dp_sink.set_on_audio_state_changed(on_audio_state, nullptr);
  a2dp_sink.set_on_volumechange(on_volume_change);

  a2dp_sink.start("BasTuna 3000");
  
  Serial.println("Klar: BasTuna 3000 är redo att paras!");
}

void setVuLevel(int level) {
  if (level < 0) level = 0;
  if (level > 10) level = 10;
  for (int i = 0; i < 10; i++) {
    digitalWrite(vuPins[i], (i < level) ? HIGH : LOW);
  }
}

void loop() {
  int raw = analogRead(ADC_PIN);

  if (!btConnected) {
    setVuLevel(0);
    digitalWrite(PIN_STATUS_PAUSE, LOW);

    if (lastConnState == ESP_A2D_CONNECTION_STATE_CONNECTING) {
      digitalWrite(LED_YELLOW_1, HIGH);
      digitalWrite(LED_YELLOW_2, HIGH);
      digitalWrite(LED_RED_1,    LOW);
      digitalWrite(LED_RED_2,    LOW);
      digitalWrite(LED_RED_3,    LOW);
    } else {
      digitalWrite(LED_YELLOW_1, LOW);
      digitalWrite(LED_YELLOW_2, LOW);
      digitalWrite(LED_RED_1,    HIGH);
      digitalWrite(LED_RED_2,    HIGH);
      digitalWrite(LED_RED_3,    HIGH);
    }
    delay(50);
    return;
  }

  if (btConnected && !btPlaying) {
    setVuLevel(0);
    digitalWrite(LED_YELLOW_1, LOW);
    digitalWrite(LED_YELLOW_2, LOW);
    digitalWrite(LED_RED_1,    LOW);
    digitalWrite(LED_RED_2,    LOW);
    digitalWrite(LED_RED_3,    LOW);
    digitalWrite(PIN_STATUS_PAUSE, HIGH);
    delay(20);
    return;
  }

  digitalWrite(PIN_STATUS_PAUSE, HIGH);

  static float dc = 50.0f;
  dc = dc * 0.98f + raw * 0.02f;
  float ac = fabsf((float)raw - dc);
  
  static float acSmooth = 0.0f;
  acSmooth = acSmooth * 0.8f + ac * 0.2f;

  const float GATE_OPEN_LEVEL  = 27.0f;
  const float GATE_CLOSE_LEVEL = 16.0f;

  static bool    gateOpen    = false;
  static uint8_t loudFrames  = 0;
  static uint8_t quietFrames = 0;

  if (!gateOpen) {
    if (acSmooth > GATE_OPEN_LEVEL) {
      loudFrames++;
      if (loudFrames > 5) {
        gateOpen    = true;
        quietFrames = 0;
      }
    } else {
      loudFrames = 0;
    }
    if (!gateOpen) {
      setVuLevel(0);
      delay(10);
      return;
    }
  } else {
    if (acSmooth < GATE_CLOSE_LEVEL) {
      quietFrames++;
      if (quietFrames > 15) { 
        gateOpen    = false;
        loudFrames  = 0;
        setVuLevel(0);
        delay(10);
        return;
      }
    } else {
      quietFrames = 0;
    }
  }

  const float MAX_AC = 170.0f; 
  float norm = acSmooth / MAX_AC;
  if (norm > 1.0f) norm = 1.0f;
  if (norm < 0.0f) norm = 0.0f;
  norm = powf(norm, 0.8f);
  int level = (int)(norm * 10.0f + 0.5f);
  setVuLevel(level);
  delay(10);
}