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

BlynkTimer slideTimer;
WidgetRTC rtc;

const char hostOTA[] = "HalloweenLantern";
const char passOTA[] = "striper";

// const char blynkServer[] = "home.lightningflash.net";
const char blynkServer[] = "blynk.dyn-cms.org";
// const char blynkServer[] = "192.168.1.10";
// #define BLYNK_CUSTOM_FINGERPRINT "14:D6:6C:28:9B:A2:A1:6D:08:70:75:01:8A:02:D1:1A:C2:14:F3:CB" // ensure no invisible characters in the fingerprint
// #define BLYNK_CUSTOM_FINGERPRINT "BC:70:D1:AA:B7:5B:F3:8C:62:EB:A6:90:A0:4A:31:18:19:7C:6A:B8:44:F1:7C:9C:ED:C3:E2:DB:64:B2:49:F9" // SHA256
#define BLYNK_CUSTOM_FINGERPRINT "FA:0C:A4:C7:D0:EC:9B:CD:FF:15:42:DB:FD:28:67:15:50:95:7A:E5" // SHA1
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

uint8_t displayMode = 0;    // 0 = Candles/Flame
                            // 1 = Solid, Selectable Color
                            // 2 = Black (off)
uint8_t newDisplayMode = 0;

uint16_t dotSpeed = 4;
uint8_t fadeSpeed = 0;
uint8_t fadeAmount = 1;
uint8_t winkFadeAmount = 1;
uint8_t brightness = BRIGHTNESS;
uint8_t ledSkipStep = WINK_SKIP;
uint8_t fireColorLow = 130;
uint8_t fireColorHigh = 155;
uint8_t winkSpeed = 1;
uint8_t winkOutChance = 30;
uint8_t winkInChance = 5;
uint8_t inciteFlameDelay = 80;

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
void dotmove();
void updateLEDs();
void fadeLEDs();
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

void reconnectWifi();
void onDisconnected(const WiFiEventStationModeDisconnected& event);
uint8_t bl_connecting = 0;
void bl_reconnect();

// Flames
uint8_t winkTimer = slideTimer.setInterval(dotSpeed, winkLanterns);
uint8_t winkSelectTimer = slideTimer.setInterval(200UL, winkSelect);
uint8_t winkDrawTimer = slideTimer.setInterval(50UL, drawWinkingLanterns);
uint8_t winkFlameTimer = slideTimer.setInterval(inciteFlameDelay, inciteFlame);
// Fading Dots
uint8_t dotSpeedTimer = slideTimer.setInterval(dotSpeed, dotmove);
uint8_t fadeSpeedTimer = slideTimer.setInterval(fadeSpeed, fadeLEDs);

// Blank
uint8_t blankTimer = slideTimer.setInterval(1000UL, blankDisplay);

// Utility
uint8_t watchDogTimer = slideTimer.setInterval(500UL, watchdogPrint);
uint8_t entropyTimer = slideTimer.setInterval(5000UL, winkEntropy);

slideTimer.disable(winkTimer);
slideTimer.disable(winkSelectTimer);
slideTimer.disable(winkDrawTimer);
slideTimer.disable(winkFlameTimer);

slideTimer.disable(dotSpeedTimer);
slideTimer.disable(fadeSpeedTimer);

slideTimer.disable(blankTimer);

void tick() {
  //toggle state
  int state = digitalRead(BUILTIN_LED);  // get the current state of GPIO1 pin
  digitalWrite(BUILTIN_LED, !state);     // set pin to the opposite state
}

void saveConfigCallback () {  //callback notifying us of the need to save config
  Serial.println("Should save config");
  shouldSaveConfig = true;
  ticker.attach(0.2, tick);  // led toggle faster
}

void setup() {

  Serial.begin(115200);
  Serial.println();

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


  // Blynk.config(blynk_token, blynkServer, 8441, BLYNK_CUSTOM_FINGERPRINT);
  Blynk.config(blynk_token, blynkServer);
  Blynk.connect();

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, ((NUM_LEDS_PER_LANTERN*NUM_LANTERNS)+START_LEDS)).setCorrection( TypicalLEDStrip );
  FastLED.setBrightness(  BRIGHTNESS );

  // Initialize flame colors
  inciteFlame();

  // Start drawing LEDs
  slideTimer.setInterval(1000/FRAMES_PER_SECOND, updateLEDs);

  setSyncInterval(10*60); // Sync interval in seconds (10 minutes)

  switch (displayMode) {
    case 0:
      slideTimer.enable(winkDrawTimer);
      break;
    case 1:
      slideTimer.enable(dotSpeedTimer);
      break;
    case 2:
      slideTimer.enable(blankTimer);
      break;
  }

}

void disableFlame() {
  slideTimer.disable(winkTimer);
  slideTimer.disable(winkSelectTimer);
  slideTimer.disable(winkDrawTimer);
  slideTimer.disable(winkFlameTimer);
}


void enableFlame() {
  slideTimer.enable(winkTimer);
  slideTimer.enable(winkSelectTimer);
  slideTimer.enable(winkDrawTimer);
  slideTimer.enable(winkFlameTimer);
}


void disableDotmove() {
  slideTimer.disable(dotSpeedTimer);
  slideTimer.disable(fadeSpeedTimer);
}


void enableDotmove() {
  slideTimer.enable(dotSpeedTimer);
  slideTimer.enable(fadeSpeedTimer);
}


void disableBlankDisplay() {
  slideTimer.disable(blankTimer);
}


void enableBlankDisplay() {
  slideTimer.enable(blankTimer);
}


void blankDisplay() {
  FastLED.clear();
}

void winkSelect() {
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

void fadeLEDs() {
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

  if (Blynk.connected()) {
    bl_connecting = 0;
    leds[1] = CRGB(0, 128, 0); // Green
    // FastLED.show();
    Blynk.run(); // Initiates Blynk
  } else {
    if (!bl_connecting) {
      leds[1] = CRGB::Orange;
      slideTimer.setTimeout(5000UL, bl_reconnect);
    } else {
      leds[1] = CRGB::Red;
    }
  }
  // timer.run(); // Initiates SimpleTimer
  slideTimer.run();

  int status = WiFi.status();
  switch (status) {
    case WL_CONNECTED:
    leds[0] = CRGB::Green;
    break;
    case WL_IDLE_STATUS:
    leds[0] = CRGB::YellowGreen;
    break;
    case WL_CONNECTION_LOST:
    leds[0] = CRGB::Red;
    break;
    case WL_DISCONNECTED:
    leds[0] = CRGB::Blue;
    break;
    default:
    leds[0] = CRGB::Purple;
  }

  if (newDisplayMode!=displayMode) {
    switch (displayMode) {
      case 0:
        disableFlame();
        // slideTimer.deleteTimer(dotSpeedTimer);
        // dotSpeedTimer = slideTimer.setInterval(dotSpeed, winkLanterns);
        break;
      case 1:
        disableDotmove();
        // slideTimer.deleteTimer(dotSpeedTimer);
        // dotSpeedTimer = slideTimer.setInterval(dotSpeed, dotmove);
        break;
      case 2:
        disableBlankDisplay();
        // slideTimer.deleteTimer(dotSpeedTimer);
        // dotSpeedTimer = slideTimer.setInterval(dotSpeed, blankDisplay);
        break;
    }
    switch (newDisplayMode) {
      case 0:
        enableFlame();
        // slideTimer.deleteTimer(dotSpeedTimer);
        // dotSpeedTimer = slideTimer.setInterval(dotSpeed, winkLanterns);
        break;
      case 1:
        enableDotmove();
        // slideTimer.deleteTimer(dotSpeedTimer);
        // dotSpeedTimer = slideTimer.setInterval(dotSpeed, dotmove);
        break;
      case 2:
        enableBlankDisplay();
        // slideTimer.deleteTimer(dotSpeedTimer);
        // dotSpeedTimer = slideTimer.setInterval(dotSpeed, blankDisplay);
        break;
    }
    displayMode = newDisplayMode;
  }

}

void bl_reconnect() {
  bl_connecting = 1;
  leds[1] = CRGB::Purple;
  if (Blynk.connect(5)) {
    leds[1] = CRGB::Blue;
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
  dotSpeedTimer = slideTimer.changeInterval(dotSpeedTimer, dotSpeed);
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
  // Set Wink Speed
  winkFadeAmount = param.asInt();
  Serial.println("Wink Speed Updated");
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
  slideTimer.changeInterval(winkFlameTimer, inciteFlameDelay);
  Serial.println("Flame Flicker Delay Updated");
}
BLYNK_READ(V12) {
  Blynk.virtualWrite(V12, inciteFlameDelay);
}

BLYNK_WRITE(V13) {
  // Update Display Mode
  newDisplayMode = param.asInt();
  Serial.println("Display Mode Updated");
}
BLYNK_READ(V13) {
  Blynk.virtualWrite(V13, displayMode);
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
