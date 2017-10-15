#include <FS.h>                   //this needs to be first, or it all crashes and burns...
// #define BLYNK_DEBUG           // Comment this out to disable debug and save space
#define BLYNK_PRINT Serial    // Comment this out to disable prints and save space
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ArduinoOTA.h>

#define FASTLED_ALLOW_INTERRUPTS 0
#include <FastLED.h>

// How many leds in your strip?
#define BRIGHTNESS  64              // Default brightness.
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
#define NUM_LANTERNS 4              // Number of Individual Lanterns
#define NUM_LEDS_PER_LANTERN 32     // Number of LEDs per Lantern
                                    // 28 gives 4 row on TP tube, at 8 LEDs per row.
#define START_LEDS 2                // How many LEDs to NOT count from the start
#define NUM_COLUMNS 8
#define NUM_ROWS (NUM_LEDS_PER_LANTERN/NUM_COLUMNS)
#define FRAMES_PER_SECOND 30        // LED update frequency
#define WINK_SKIP 5                 // How many LEDs to skip to reduce overall brightness
                                    // While retaining sufficient brightness levels for smooth transition

// For led chips like Neopixels, which have a data line, ground, and power, you just
// need to define DATA_PIN.  For led chipsets that are SPI based (four wires - data, clock,
// ground, and power), like the LPD8806, define both DATA_PIN and CLOCK_PIN
#define DATA_PIN D8

// #define HTTP_UPDATER  // Comment this out to disable HTTP updates (for better security)

//for LED status
#include <Ticker.h>
Ticker ticker;

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

char blynk_token[34] = "BLYNK_TOKEN";

bool shouldSaveConfig = false; //flag for saving data

// #include <BlynkSimpleEsp8266_SSL.h>
#include <BlynkSimpleEsp8266.h>
#include <WidgetRTC.h>

WidgetRTC rtc;

const char hostOTA[] = "HalloweenLantern";
const char passOTA[] = "striper";

const char blynkServer[] = "home.lightningflash.net";
// const char blynkServer[] = "blynk.dyn-cms.org";
// const char blynkServer[] = "192.168.1.10";
// #define BLYNK_CUSTOM_FINGERPRINT "14:D6:6C:28:9B:A2:A1:6D:08:70:75:01:8A:02:D1:1A:C2:14:F3:CB" // ensure no invisible characters in the fingerprint
// #define BLYNK_CUSTOM_FINGERPRINT "BC:70:D1:AA:B7:5B:F3:8C:62:EB:A6:90:A0:4A:31:18:19:7C:6A:B8:44:F1:7C:9C:ED:C3:E2:DB:64:B2:49:F9" // SHA256
// #define BLYNK_CUSTOM_FINGERPRINT "FA:0C:A4:C7:D0:EC:9B:CD:FF:15:42:DB:FD:28:67:15:50:95:7A:E5" // SHA1

#define BLYNK_CUSTOM_FINGERPRINT "14:D6:6C:28:9B:A2:A1:6D:08:70:75:01:8A:02:D1:1A:C2:14:F3:CB" // SHA1 of PEM format home.lightningflash.net
// Fingerprint command: openssl x509 -in certs/home_lightningflash_net.crt.pem -text -noout -fingerprint
// CRT to PEM format conversion: openssl x509 -in home_lightningflash_net.crt -out home_lightningflash_net.crt.pe

// const char blynkServer[] = "blynk.dyn-cms.org";

#ifdef HTTP_UPDATER
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;
#endif

// Define the array of leds
// CRGB leds[NUM_LEDS];
// CRGBArray<NUM_LEDS> leds;
CRGBArray<(NUM_LEDS_PER_LANTERN*NUM_LANTERNS)+START_LEDS> leds;
// CRGBSet* leds[NUM_LANTERNS];
CRGB lColor = CRGB::Red;
CRGB winkColor[NUM_LANTERNS];

enum Mode :uint8_t {CANDLES, FADESTRIPE, BLACK, SIREN};
Mode displayMode = CANDLES;       // 0 = Candles/Flame
                            // 1 = Solid, Selectable Color
                            // 2 = Black (off)
                            // 3 = Rotating Siren effect

Mode newDisplayMode = CANDLES;

BlynkTimer utilTimer;
BlynkTimer candleTimer;
BlynkTimer fadestripTimer;
BlynkTimer blankTimer;
BlynkTimer sirenTimer;

uint16_t dotSpeed = 4;
uint8_t fadeSpeed = 0;
uint8_t fadeAmount = 1;
uint8_t winkFadeAmount = 1;
uint8_t brightness = BRIGHTNESS;
uint8_t ledSkipStep = WINK_SKIP;
uint8_t fireColorLow = 130;
uint8_t fireColorHigh = 155;
uint8_t winkSpeed = 4;
uint8_t winkOutChance = 30;
uint8_t winkInChance = 5;
uint8_t inciteFlameDelay = 80;

uint8_t sirenDelay = 10;
uint8_t sirenShiftAmount = 5;
uint8_t sirenFold = 0;

uint8_t disconnected = 1;

uint8_t hue = 0;

uint8_t winks[NUM_LANTERNS][2]; // [0] Lantern current dimmness (255=black, 0=full)
                                // [1]=0 => Stable Light
                                // [1]=1 => Winking Out
                                // [1]=2 => Stable Dark
                                // [1]=3 => Winking In


CRGBPalette16 firePalette = HeatColors_p;

void slide1();
void slide2();
void updateLEDs();
void fadeLEDs();
void dotmove();
uint16_t lanternPos(uint8_t lantern_num, uint16_t pos_on_lantern);

void winkLanterns();
void winkSelect();
void watchdogPrint();
void winkEntropy();
void drawWinkingLanterns();
void inciteFlame();
void blankDisplay();

void disableFlame();
void enableFlame();
void disableDotmove();
void enableDotmove();
void disableBlankDisplay();
void enableBlankDisplay();
void disableSirenDisplay();
void enableSirenDisplay();
void sirenDraw();

void modeSelect(Mode newMode, Mode &oldMode);

void reconnectWifi();
void onDisconnected(const WiFiEventStationModeDisconnected& event);
uint8_t bl_connecting = 0;
void bl_reconnect();

// Flames
uint8_t winkTimer = candleTimer.setInterval(winkSpeed, winkLanterns);
uint8_t winkSelectTimer = candleTimer.setInterval(200UL, winkSelect);
uint8_t winkDrawTimer = candleTimer.setInterval(50UL, drawWinkingLanterns);
uint8_t winkFlameTimer = candleTimer.setInterval(inciteFlameDelay, inciteFlame);

// Fading Dots
uint8_t dotSpeedTimer = fadestripTimer.setInterval(dotSpeed, dotmove);
uint8_t fadeSpeedTimer = fadestripTimer.setInterval(fadeSpeed, fadeLEDs);

// Blank
uint8_t blankDrawTimer = blankTimer.setInterval(1000UL, blankDisplay);

uint8_t sirenDrawTimer = sirenTimer.setInterval(sirenDelay, sirenDraw);

// Utility
uint8_t watchDogTimer = utilTimer.setInterval(500UL, watchdogPrint);
uint8_t entropyTimer = utilTimer.setInterval(5000UL, winkEntropy);

void tick() {
  //toggle state
  int state = digitalRead(BUILTIN_LED);  // get the current state of GPIO1 pin
  digitalWrite(BUILTIN_LED, !state);     // set pin to the opposite state

  leds[0] = CRGB::Blue;
  leds[0].fadeToBlackBy(255*(!state));
  FastLED.show();

}

void saveConfigCallback () {  //callback notifying us of the need to save config
  Serial.println("Should save config");
  shouldSaveConfig = true;
  ticker.attach(0.2, tick);  // led toggle faster
}

void setup() {

  Serial.begin(115200);
  Serial.println();

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, ((NUM_LEDS_PER_LANTERN*NUM_LANTERNS)+START_LEDS)).setCorrection( TypicalLEDStrip );
  FastLED.setBrightness(  BRIGHTNESS );

  leds[0] = CRGB::Red;
  leds[1] = CRGB::Red;
  FastLED.show();

  //set led pin as output
  pinMode(BUILTIN_LED, OUTPUT);
  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);

  //SPIFFS.format();    //clean FS, for testing
  Serial.println("Mounting FS...");    //read configuration from FS json

  if (SPIFFS.begin()) {
    Serial.println("Mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("Reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("Opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(blynk_token, json["blynk_token"]);

        } else {
          Serial.println("Failed to load json config");
        }
      }
    }
  } else {
    Serial.println("Failed to mount FS");
  }
  //end read

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_blynk_token("blynk", "blynk token", blynk_token, 33);   // was 32 length

  Serial.println(blynk_token);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  wifiManager.setSaveConfigCallback(saveConfigCallback);   //set config save notify callback

  //set static ip
  // this is for connecting to Office router not GargoyleTest but it can be changed in AP mode at 192.168.4.1
  //wifiManager.setSTAStaticIPConfig(IPAddress(192,168,10,111), IPAddress(192,168,10,90), IPAddress(255,255,255,0));

  wifiManager.addParameter(&custom_blynk_token);   //add all your parameters here

  // wifiManager.resetSettings();  //reset settings - for testing

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep, in seconds
  wifiManager.setTimeout(600);   // 10 minutes to enter data and then Wemos resets to try again.

  //fetches ssid and pass and tries to connect, if it does not connect it starts an access point with the specified name
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("HalloweenLanternController", "booberry3833")) {
    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }
  Serial.println("Connected Network :)");   //if you get here you have connected to the WiFi
  ticker.detach();
  //turn LED off
  digitalWrite(BUILTIN_LED, HIGH);

  strcpy(blynk_token, custom_blynk_token.getValue());    //read updated parameters

  if (shouldSaveConfig) {      //save the custom parameters to FS
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["blynk_token"] = blynk_token;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("Failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  WiFi.setAutoReconnect( true );

  Serial.println("local ip");
  Serial.println(WiFi.localIP());

  ArduinoOTA.setHostname(hostOTA);
  ArduinoOTA.setPassword(passOTA);
  MDNS.addService("arduino", "tcp", 8266);

  ArduinoOTA.onStart([]() {
    //Serial.println("Start");
  });

  ArduinoOTA.onEnd([]() {
    //Serial.println("\nEnd");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    //Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    //Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();

  // MDNS.addService("http", "tcp", 80);

  #ifdef HTTP_UPDATER
  httpUpdater.setup(&httpServer);
  httpServer.begin();
  Serial.printf("HTTPUpdateServer ready! Open http://%s.local/update in your browser\n", hostOTA);
  #endif

  Serial.println("OTA: Ready");
  //END OTA Setup

  // leds[1] = CRGB::Blue;
  FastLED.show();

  // Blynk.config(blynk_token, blynkServer, 8441, BLYNK_CUSTOM_FINGERPRINT);
  Blynk.config(blynk_token, blynkServer);
  Blynk.connect();

  leds[1] = CRGB::Black;
  FastLED.show();

  // Initialize flame colors
  inciteFlame();

  candleTimer.disable(winkTimer);
  candleTimer.disable(winkSelectTimer);
  candleTimer.disable(winkDrawTimer);
  candleTimer.disable(winkFlameTimer);

  fadestripTimer.disable(dotSpeedTimer);
  fadestripTimer.disable(fadeSpeedTimer);

  blankTimer.disable(blankDrawTimer);

  sirenTimer.disable(sirenDrawTimer);

  // utilTimer.disable(watchDogTimer);

  // Serial.print("winkTimer: ");
  // Serial.println(winkTimer);
  //
  // Serial.print("winkSelectTimer: ");
  // Serial.println(winkSelectTimer);
  //
  // Serial.print("winkDrawTimer: ");
  // Serial.println(winkDrawTimer);
  //
  // Serial.print("winkFlameTimer: ");
  // Serial.println(winkFlameTimer);
  //
  // Serial.print("dotSpeedTimer: ");
  // Serial.println(dotSpeedTimer);
  //
  // Serial.print("fadeSpeedTimer: ");
  // Serial.println(fadeSpeedTimer);
  //
  // Serial.print("blankDrawTimer: ");
  // Serial.println(blankDrawTimer);
  //
  // Serial.print("watchDogTimer: ");
  // Serial.println(watchDogTimer);

  // Start drawing LEDs
  utilTimer.setInterval(1000/FRAMES_PER_SECOND, updateLEDs);

  setSyncInterval(10*60); // Sync interval in seconds (10 minutes)

  modeSelect(displayMode, displayMode);
}

void disableFlame() {
  // Serial.println("disableFlame()");
  FastLED.clear();
  candleTimer.disable(winkTimer);
  candleTimer.disable(winkSelectTimer);
  candleTimer.disable(winkDrawTimer);
  candleTimer.disable(winkFlameTimer);
  // Serial.print("Disabled Timers: ");
  // Serial.print(winkTimer);
  // Serial.print(winkSelectTimer);
  // Serial.print(winkDrawTimer);
  // Serial.print(winkFlameTimer);
  // Serial.println();
}

void enableFlame() {
  // Serial.println("enableFlame()");
  FastLED.clear();
  candleTimer.enable(winkTimer);
  candleTimer.enable(winkSelectTimer);
  candleTimer.enable(winkDrawTimer);
  candleTimer.enable(winkFlameTimer);
  // Serial.print("Enabled Timers: ");
  // Serial.print(winkTimer);
  // Serial.print(winkSelectTimer);
  // Serial.print(winkDrawTimer);
  // Serial.print(winkFlameTimer);
  // Serial.println();
}


void disableDotmove() {
  // Serial.println("disableDotmove()");
  FastLED.clear();
  fadestripTimer.disable(dotSpeedTimer);
  fadestripTimer.disable(fadeSpeedTimer);
  // Serial.print("Disabled Timers: ");
  // Serial.print(dotSpeedTimer);
  // Serial.print(fadeSpeedTimer);
  // Serial.println();
}


void enableDotmove() {
  // Serial.println("enableDotmove()");
  FastLED.clear();
  fadestripTimer.enable(dotSpeedTimer);
  fadestripTimer.enable(fadeSpeedTimer);
  // Serial.print("Enabled Timers: ");
  // Serial.print(dotSpeedTimer);
  // Serial.print(fadeSpeedTimer);
  // Serial.println();
}


void disableBlankDisplay() {
  // Serial.println("disableBlankDisplay()");
  FastLED.clear();
  blankTimer.disable(blankDrawTimer);
}


void enableBlankDisplay() {
  // Serial.println("enableBlankDisplay()");
  FastLED.clear();
  blankTimer.enable(blankDrawTimer);
}


void disableSirenDisplay() {
  // Serial.println("disableSirenDisplay()");
  sirenTimer.disable(sirenDrawTimer);
  FastLED.clear();
}


void enableSirenDisplay() {
  // Serial.println("enableSirenDisplay()");
  sirenTimer.enable(sirenDrawTimer);
  FastLED.clear();
}


void blankDisplay() {
  FastLED.clear();
}

void winkSelect() {
  // Serial.println("winkSelect()");
  // Decide if a lantern needs to be winked
  for (int i=0; i<NUM_LANTERNS; i++) {
    switch (winks[i][1]) {
      case 0:
        if (random8(NUM_LANTERNS*winkOutChance)==0) {
          winks[i][1] = ( ( winks[i][1]+1) % 4);
          Serial.print("Winking out started on ");
          Serial.println(i);
        }
        break;
      case 2:
        if (random8(NUM_LANTERNS*winkInChance)==0) {
          winks[i][1] = ( ( winks[i][1]+1) % 4);
          Serial.print("Winking back in started on ");
          Serial.println(i);
        }
        break;
      case 1:
      case 3:
        // Do nothing
        break;
    }
  }
}

void winkLanterns() {
  // Serial.println("winkLanterns()");
  // All lanterns on full, no movement in normal case
  // Randomly, one lantern is put into wink mode, where it slowly fades out
  // to black, and then back in to full light.
  // Multiple lanterns can be in wink mode.
  // winkSelect();

  // Adjust lanterns' wink values
  for (int i=0; i<NUM_LANTERNS; i++) {
    if (winks[i][1]==1) {
      // Winking out
      winks[i][0] = qadd8(winks[i][0], winkFadeAmount);
      if (winks[i][0]==0xFF) {
        winks[i][1]=2;
        Serial.print("Winking out ended on ");
        Serial.println(i);
      }
    } else if (winks[i][1]==3) {
      // Winking back in
      winks[i][0] = qsub8(winks[i][0], winkFadeAmount);
      if (winks[i][0]==0x00) {
        winks[i][1]=0;
        Serial.print("Winking back in ended on ");
        Serial.println(i);
      }
    }
  }

}

void inciteFlame() {
  // Serial.println("inciteFlame()");
  for (int i=0; i<NUM_LANTERNS; i++) {
    // winkColor[i] = CRGB::Green;
    winkColor[i] = ColorFromPalette(firePalette, random8(fireColorLow,fireColorHigh));
  }
}

void drawWinkingLanterns() {
  for (int i=0; i<NUM_LANTERNS; i++) {
    leds(lanternPos(i, 0), lanternPos(i,NUM_LEDS_PER_LANTERN-1)).fill_solid(CRGB::Black);

    // Direct/linear
    // leds(lanternPos(i, 0), lanternPos(i,NUM_LEDS_PER_LANTERN)).fadeToBlackBy(winks[i][0]);

    // Ease In/Out
    // leds(lanternPos(i, 0), lanternPos(i,NUM_LEDS_PER_LANTERN)).fadeToBlackBy(  ease8InOutCubic( winks[i][0] )  );

    // Use Skip value
    // for (int j=0; j<NUM_LEDS_PER_LANTERN; j+=WINK_SKIP) {
    for (int j=0; j<NUM_LEDS_PER_LANTERN; j+=ledSkipStep) {
      // leds[lanternPos(i,j)].fill_solid(lColor);
      // leds[lanternPos(i,j)] = lColor;
      leds[lanternPos(i,j)] = winkColor[i];
      leds[lanternPos(i,j)].fadeToBlackBy( ease8InOutCubic( winks[i][0] ) );
    }
  }
}

void dotmove() {
  // Serial.print("+");
  static uint16_t pos;
  static int8_t dir;

  dir = 1;

  pos += dir;
  pos %= NUM_COLUMNS;

  for (int i=0; i<NUM_LANTERNS; i++) {
    // leds(((NUM_LEDS_PER_LANTERN*i)+pos), ((NUM_LEDS_PER_LANTERN)*i)+1+pos).fill_solid(lColor);

    // leds(lanternPos(i,pos), lanternPos(i,pos));

    for (int j=0; j<NUM_ROWS; j++) {
      leds(lanternPos(i, j*(NUM_COLUMNS)+pos), lanternPos(i, j*(NUM_COLUMNS)+pos)).fill_solid(lColor);
    }

    // leds(((NUM_LEDS_PER_LANTERN*i)+pos), ((NUM_LEDS_PER_LANTERN*i)+pos)).fill_solid(lColor);
    // leds[i][pos] = CRGB::Red;
  }
  // leds.fill_solid(CRGB::Black);
  // leds[pos] = CRGB(255, 0, 0);
}

uint16_t lanternPos(uint8_t lantern_num, uint16_t pos_on_lantern) {
  return START_LEDS + ((NUM_LEDS_PER_LANTERN*lantern_num)+pos_on_lantern);
}

void sirenDraw() {
  const uint8_t circleInterval = 0xFF / NUM_COLUMNS; // How many steps/pct of whole is each column
  static uint8_t pos; // Offset for start of sine wave

  FastLED.clear(); // Start from black;

  for (int i=0; i<NUM_LANTERNS; i++) {
    for (int j=0; j<NUM_COLUMNS; j++) {
      int fadePct;
      fadePct = pos + (j*circleInterval);
      for (int f=0; f<sirenFold; f++) {
        // fadePct = sin8(fadePct);
        fadePct = cubicwave8(fadePct);
      }
      // fadePct = sin8(pos + (j*circleInterval));
      // fadePct = cubicwave8(pos + (j*circleInterval));
      // fadePct = qadd8(fadePct, 25);  // Dim the whole light segment, bottoming out at zero.
      CRGB colColor = lColor;
      colColor.fadeToBlackBy(fadePct);
      // colColor %= fadePct;
      // Serial.print("fadePct:");
      // Serial.print(fadePct);
      // Serial.print(" COL:");
      // Serial.print(j);
      // Serial.println();
      for (int k=0; k<NUM_ROWS; k++) {
        leds[ lanternPos(i,  j + k*NUM_COLUMNS  ) ] = colColor;
        // leds[ lanternPos(i, (j*NUM_ROWS) + (k*NUM_COLUMNS) ) ] = colColor;
        // leds(
        //   lanternPos(i, (j*NUM_ROWS) + k),
        //   lanternPos(i, (j*NUM_ROWS) + k)
        // ).fill_solid(colColor);
      }
    }
  }

  pos += sirenShiftAmount;
}

void fadeLEDs() {
  // Serial.println("fadeLEDs()");
  for (int i=0; i<NUM_LANTERNS; i++) {
    leds((START_LEDS+(NUM_LEDS_PER_LANTERN*i)),  (START_LEDS+( (NUM_LEDS_PER_LANTERN) * (i+1) )) - 1 ).fadeToBlackBy(fadeAmount);
  }
}

void reconnectWifi() {
  WiFi.begin();
}

void onDisconnected(const WiFiEventStationModeDisconnected& event) {
  disconnected = 1;
  reconnectWifi();
}

void loop() {
  ArduinoOTA.handle();

#ifdef HTTP_UPDATER
  httpServer.handleClient();
#endif

  if (disconnected == 1 && WiFi.status() == WL_CONNECTED) {
    Serial.print(String("[WIFI] IP: "));
    Serial.println(WiFi.localIP());
    disconnected = 0;
  }

  Blynk.run(); // Initiates Blynk
  // if (Blynk.connected()) {
  //   bl_connecting = 0;
  //   leds[1] = CRGB::Green;
  //   leds[1].fadeToBlackBy(200);
  //   // Blynk.run(); // Initiates Blynk
  // } else {
  //   leds[1] = CRGB::Red;
  //   leds[1].fadeToBlackBy(200);
  //   if (!bl_connecting) {
  //     // utilTimer.setTimeout(5000UL, bl_reconnect);
  //     bl_reconnect();
  //   } else {
  //     // Allow reconnect, do nothing.
  //   }
  // }
  if (Blynk.connected()) {
    leds[1] = CRGB::Green;
    leds[1].fadeToBlackBy(200);
  } else {
    leds[1] = CRGB::Red;
    leds[1].fadeToBlackBy(200);
  }
  // Blynk.run();

  // timer.run(); // Initiates SimpleTimer
  utilTimer.run();
  candleTimer.run();
  fadestripTimer.run();
  blankTimer.run();
  sirenTimer.run();

  int status = WiFi.status();
  switch (status) {
    case WL_CONNECTED:
    leds[0] = CRGB::Green;
    leds[0].fadeToBlackBy(200);
    break;
    case WL_IDLE_STATUS:
    leds[0] = CRGB::YellowGreen;
    leds[0].fadeToBlackBy(200);
    break;
    case WL_CONNECTION_LOST:
    leds[0] = CRGB::Red;
    leds[0].fadeToBlackBy(200);
    break;
    case WL_DISCONNECTED:
    leds[0] = CRGB::Blue;
    leds[0].fadeToBlackBy(200);
    break;
    default:
    leds[0] = CRGB::Purple;
    leds[0].fadeToBlackBy(200);
  }

  if (newDisplayMode!=displayMode) {
    modeSelect(newDisplayMode, displayMode);
    // displayMode = newDisplayMode;
  }
}

void modeSelect(Mode newMode, Mode &oldMode) {
  switch (oldMode) {
    case CANDLES:
      disableFlame();
      break;
    case FADESTRIPE:
      disableDotmove();
      break;
    case BLACK:
      disableBlankDisplay();
      break;
    case SIREN:
      disableSirenDisplay();
      break;
  }

  // disableFlame();
  // disableDotmove();
  // disableBlankDisplay();

  switch (newMode) {
    case CANDLES:
      enableFlame();
      break;
    case FADESTRIPE:
      enableDotmove();
      break;
    case BLACK:
      enableBlankDisplay();
      break;
    case SIREN:
      enableSirenDisplay();
      break;
  }
  oldMode = newMode;
}

void bl_reconnect() {
  bl_connecting = 1;
  leds[1] = CRGB::Blue;
  leds[1].fadeToBlackBy(200);
  FastLED.show();
  if (Blynk.connect(30)) {
    leds[1] = CRGB::Green;
    leds[1].fadeToBlackBy(200);
    FastLED.show();
  } else {
    utilTimer.setTimeout(5000UL, bl_reconnect);
    leds[1] = CRGB::Red;
    leds[1].fadeToBlackBy(200);
    FastLED.show();
  }
  // return Blynk.connected();
}

BLYNK_WRITE(V1) {
  // zeRGBa assigned to V1
  // get a RED channel value
  lColor.r = param[0].asInt();
  lColor.g = param[1].asInt();
  lColor.b = param[2].asInt();
  Serial.println("Color Updated");
}
BLYNK_READ(V1) {
  Blynk.virtualWrite(V1, lColor);
}

BLYNK_WRITE(V2) {
  dotSpeed = param.asInt();
  // slideTimer.deleteTimer(dotSpeedTimer);
  // dotSpeedTimer = slideTimer.setInterval(dotSpeed, dotmove);
  fadestripTimer.changeInterval(dotSpeedTimer, dotSpeed);
  Serial.println("Dot Speed Updated");
}
BLYNK_READ(V2) {
  Blynk.virtualWrite(V2, dotSpeed);
}

BLYNK_WRITE(V3) {
  fadeSpeed = param.asInt();
  // slideTimer.deleteTimer(fadeSpeedTimer);
  if (fadeSpeed>0) {
    // fadeSpeedTimer = slideTimer.setInterval(fadeSpeed, fadeLEDs);
    fadestripTimer.changeInterval(fadeSpeedTimer, fadeSpeed);
  }
  Serial.println("Fade Speed Updated");
}
BLYNK_READ(V3) {
  Blynk.virtualWrite(V3, fadeSpeed);
}

BLYNK_WRITE(V4) {
  fadeAmount = param.asInt();
  Serial.println("Fade Amount Updated");
}
BLYNK_READ(V4) {
  Blynk.virtualWrite(V4, fadeAmount);
}

BLYNK_WRITE(V5) {
  // Set Max Brightness
  brightness = param.asInt();
  FastLED.setBrightness(  brightness );
  Serial.println("Max Brightness Updated");
}
BLYNK_READ(V5) {
  Blynk.virtualWrite(V5, brightness);
}

BLYNK_WRITE(V6) {
  // Set LED Skip Step
  ledSkipStep = param.asInt();
  Serial.println("LED Skip Step Updated");
}
BLYNK_READ(V6) {
  Blynk.virtualWrite(V6, ledSkipStep);
}

BLYNK_WRITE(V7) {
  // Set Fire Color Low
  fireColorLow = min(param.asInt(), fireColorHigh);
  Serial.println("Fire Color Low Updated");
}
BLYNK_READ(V7) {
  Blynk.virtualWrite(V7, fireColorLow);
}

BLYNK_WRITE(V8) {
  // Set Fire Color High
  fireColorHigh = max(param.asInt(), fireColorLow);
  Serial.println("Fire Color High Updated");
}
BLYNK_READ(V8) {
  Blynk.virtualWrite(V8, fireColorHigh);
}

BLYNK_WRITE(V9) {
  // Set Wink Speed (amount to fade with each tick);
  winkFadeAmount = param.asInt();
  Serial.println("Wink Fade Amount Updated");
}
BLYNK_READ(V9) {
  Blynk.virtualWrite(V9, fireColorHigh);
}

BLYNK_WRITE(V10) {
  // Set Wink Out Chance
  winkOutChance = param.asInt();
  Serial.println("Wink Out Chance Updated");
}
BLYNK_READ(V10) {
  Blynk.virtualWrite(V10, winkOutChance);
}

BLYNK_WRITE(V11) {
  // Set Wink In Chance
  winkInChance = param.asInt();
  Serial.println("Wink In Chance Updated");
}
BLYNK_READ(V11) {
  Blynk.virtualWrite(V11, winkInChance);
}

BLYNK_WRITE(V12) {
  // Set Flame Flicker Delay
  inciteFlameDelay = param.asInt();
  // slideTimer.deleteTimer(winkFlameTimer);
  // winkFlameTimer = slideTimer.setInterval(inciteFlameDelay, inciteFlame);
  candleTimer.changeInterval(winkFlameTimer, inciteFlameDelay);
  Serial.println("Flame Flicker Delay Updated");
}
BLYNK_READ(V12) {
  Blynk.virtualWrite(V12, inciteFlameDelay);
}

BLYNK_WRITE(V13) {
  // Update Display Mode
  newDisplayMode = (Mode)param.asInt();
  Serial.println("Display Mode Updated");
}
BLYNK_READ(V13) {
  Blynk.virtualWrite(V13, (int)displayMode);
}

BLYNK_WRITE(V14) {
  // Set Siren Update Speed
  sirenDelay = param.asInt();
  sirenTimer.changeInterval(sirenDrawTimer, sirenDelay);
  Serial.println("Siren Update Speed");
}
BLYNK_READ(V14) {
  Blynk.virtualWrite(V14, sirenDelay);
}

BLYNK_WRITE(V15) {
  // Set siren shift amount
  sirenShiftAmount = param.asInt();
  Serial.println("Siren Shift Amount Updated");
}
BLYNK_READ(V15) {
  Blynk.virtualWrite(V15, sirenShiftAmount);
}

BLYNK_WRITE(V16) {
  // Set Siren Easeing folds
  sirenFold = param.asInt();
  Serial.println("Siren Easing Folds Updated");
}
BLYNK_READ(V16) {
  Blynk.virtualWrite(V16, sirenFold);
}

BLYNK_CONNECTED() {
  Blynk.syncAll();
  rtc.begin();
}

void updateLEDs() {
  FastLED.show();
}

void watchdogPrint() {
  // Serial.println("+");
  Serial.print("[ ");
  for (int i=0; i<NUM_LANTERNS; i++) {
    Serial.print(winks[i][1]);
    Serial.print(":");
    Serial.print(winks[i][0]);
    Serial.print(" ");
  }
  Serial.print("] RSSI: ");
  int rssi = WiFi.RSSI();  // eg. -63
  Serial.println(rssi);

}

void winkEntropy() {
  random16_add_entropy(now());
}
