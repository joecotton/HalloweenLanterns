#include <FS.h>                   //this needs to be first, or it all crashes and burns...
//#define BLYNK_DEBUG           // Comment this out to disable debug and save space
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
#include <FastLED.h>

// How many leds in your strip?
#define BRIGHTNESS  64              // Default brightness.
#define LED_TYPE    WS2812
#define COLOR_ORDER GRB
#define NUM_LANTERNS 4              // Number of Individual Lanterns
#define NUM_LEDS_PER_LANTERN 32     // Number of LEDs per Lantern
                                    // 28 gives 4 row on TP tube, at 8 LEDs per row.
#define NUM_COLUMNS 8
#define NUM_ROWS (NUM_LEDS_PER_LANTERN/NUM_COLUMNS)
#define FRAMES_PER_SECOND 60        // LED update frequency

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

#include <BlynkSimpleEsp8266.h>
#include <SimpleTimer.h>
SimpleTimer timer;

const char hostOTA[] = "HalloweenLantern";
const char passOTA[] = "striper";

const char blynkServer[] = "blynk.dyn-cms.org";

#ifdef HTTP_UPDATER
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;
#endif

// Define the array of leds
// CRGB leds[NUM_LEDS];
// CRGBArray<NUM_LEDS> leds;
CRGBArray<NUM_LEDS_PER_LANTERN*NUM_LANTERNS> leds;
// CRGBSet* leds[NUM_LANTERNS];
CRGB lColor = CRGB::Red;
uint16_t dotSpeed = 35;
uint8_t fadeSpeed = 5;
uint8_t fadeAmount = 10;
uint8_t brightness = BRIGHTNESS;

uint8_t hue = 0;

SimpleTimer slideTimer;

void slide1();
void slide2();
void dotmove();
void updateLEDs();
void fadeLEDs();
uint16_t lanternPos(uint8_t lantern_num, uint16_t pos_on_lantern);

uint8_t dotSpeedTimer = slideTimer.setInterval(dotSpeed, dotmove);
uint8_t fadeSpeedTimer = slideTimer.setInterval(fadeSpeed, fadeLEDs);

void tick()
{
  //toggle state
  int state = digitalRead(BUILTIN_LED);  // get the current state of GPIO1 pin
  digitalWrite(BUILTIN_LED, !state);     // set pin to the opposite state
}

void saveConfigCallback () {  //callback notifying us of the need to save config
  Serial.println("Should save config");
  shouldSaveConfig = true;
  ticker.attach(0.2, tick);  // led toggle faster
}

void setup()
{

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
  if (!wifiManager.autoConnect("HalloweenLanternController", "4111 Meadowwood")) {
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


  Blynk.config(blynk_token, blynkServer);
  Blynk.connect();

  // LEDS.addLeds<WS2812,DATA_PIN,RGB>(leds,NUM_LEDS);
  // LEDS.setBrightness(84);

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS_PER_LANTERN*NUM_LANTERNS).setCorrection( TypicalLEDStrip );
  FastLED.setBrightness(  BRIGHTNESS );

  // slideTimer.setInterval(dotSpeed, dotmove);
  slideTimer.setInterval(FRAMES_PER_SECOND/1000, updateLEDs);


}

// void fadeall() { for(int i = 0; i < NUM_LEDS; i++) { leds[i].nscale8(250); } }

void dotmove() {
  static uint16_t pos;
  static int8_t dir;

  // if (pos > (NUM_LEDS_PER_LANTERN)-2) {
  // if (pos > (NUM_COLUMNS)-2) {
  //   dir = -1; // Down
  // } else if (pos == 0) {
  //   dir = 1; // Up
  // }
  dir = 1;

  pos += dir;
  pos %= NUM_COLUMNS;

  for (int i=0; i<NUM_LANTERNS; i++) {
    // Serial.print("LANTERN:");
    // Serial.println(i);
    // Serial.print("POS:");
    // Serial.println(pos);
    //
    // Serial.print((NUM_LEDS_PER_LANTERN*i));
    // Serial.print(":");
    // Serial.print( ( (NUM_LEDS_PER_LANTERN) * (i+1) ) - 1 );
    // Serial.println(":B");
    //
    // Serial.print(((NUM_LEDS_PER_LANTERN*i)+pos));
    // Serial.print(":");
    // Serial.print(((NUM_LEDS_PER_LANTERN)*i)+1+pos);
    // Serial.println(":R");

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

  // FastLED.show();
}

uint16_t lanternPos(uint8_t lantern_num, uint16_t pos_on_lantern) {
  return ((NUM_LEDS_PER_LANTERN*lantern_num)+pos_on_lantern);
}

void fadeLEDs() {
  for (int i=0; i<NUM_LANTERNS; i++) {
    leds((NUM_LEDS_PER_LANTERN*i),  ( (NUM_LEDS_PER_LANTERN) * (i+1) ) - 1 ).fadeToBlackBy(fadeAmount);
  }
}

void loop()
{
  ArduinoOTA.handle();
  #ifdef HTTP_UPDATER
  httpServer.handleClient();
  #endif
  Blynk.run(); // Initiates Blynk
  timer.run(); // Initiates SimpleTimer
  slideTimer.run();

  // EVERY_N_MILLISECONDS(40) {
  //   FastLED.show();
  // }
}

BLYNK_WRITE(V1) // zeRGBa assigned to V1
{
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
  slideTimer.deleteTimer(dotSpeedTimer);
  dotSpeedTimer = slideTimer.setInterval(dotSpeed, dotmove);
  Serial.println("Dot Speed Updated");
}
BLYNK_READ(V2) {
  Blynk.virtualWrite(V2, dotSpeed);
}

BLYNK_WRITE(V3) {
  fadeSpeed = param.asInt();
  slideTimer.deleteTimer(fadeSpeedTimer);
  fadeSpeedTimer = slideTimer.setInterval(fadeSpeed, fadeLEDs);
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

BLYNK_CONNECTED() {
    Blynk.syncAll();
}

void updateLEDs() {
  FastLED.show();
}
