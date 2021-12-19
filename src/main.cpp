#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <DNSServer.h>        //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h> //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>      //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

#include <RTClib.h>
#include <time.h>
#include <NTPClient.h>
#include <CronAlarms.h>

#include <FastLED.h>

// How many leds in your strip?
#define NUM_LEDS 6

// For led chips like WS2812, which have a data line, ground, and power, you just
// need to define DATA_PIN.  For led chipsets that are SPI based (four wires - data, clock,
// ground, and power), like the LPD8806 define both DATA_PIN and CLOCK_PIN
// Clock pin only needed for SPI based chipsets when not using hardware SPI
#define DATA_PIN 6

// Define the array of leds
CRGB leds[NUM_LEDS];

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET LED_BUILTIN // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

WiFiManager wifiManager;
RTC_DS3231 rtc;
const long utcOffsetInSeconds = 2 * 60 * 60;
WiFiUDP udp;
NTPClient timeClient(udp, "pool.ntp.org", utcOffsetInSeconds);
std::unique_ptr<ESP8266WebServer> webServer;

const int OneHertzPin = 14;
const int ModePin = 0;
volatile bool displayWeather = false;
volatile bool forceRefresh = false;

#define NUM_TIME_BITS 6

byte hoursBits[NUM_TIME_BITS];
byte minutesBits[NUM_TIME_BITS];
byte secondsBits[NUM_TIME_BITS];

void UpdateDisplay();
void UpdateLEDs();
void UpdateTime();
void UpdateNtp();

void InitRTC();
void InitLEDs();
void InitDisplay();

void FiveMinuteTimer();

void configModeCallback(WiFiManager *wifiManager)
{
  String connectMsg = "Connect to " + wifiManager->getConfigPortalSSID();
  display.println(connectMsg);
  display.display();
}

void ICACHE_RAM_ATTR OneHertzCallback()
{
  forceRefresh = true;
  UpdateTime();
  UpdateLEDs();
}

void ICACHE_RAM_ATTR ModeCallback()
{
  Serial.println("AAAAAAAAAA");
  displayWeather = !displayWeather;
  forceRefresh = true;
}

void getWeather()
{
  HTTPClient httpClient;

  Serial.println("Trying to use httpclient");
  httpClient.begin("https://community-open-weather-map.p.rapidapi.com/weather?q=Modderfontein%2CSouth%20Africa&units=metric", "0DBFDE38B3FC6E55B11B8E3D47FDAEF1FCF60828");
  httpClient.addHeader("x-rapidapi-key", "75e9db27admsh02967254cef447bp1d236cjsnfc576fdf6de5");
  httpClient.addHeader("x-rapidapi-host", "community-open-weather-map.p.rapidapi.com");
  httpClient.addHeader("useQueryString", "true");

  Serial.println("making GET request");
  int response = httpClient.GET();
  Serial.println("Got HTTP " + response);
  if (response > 0)
  {
    String body = httpClient.getString();
    Serial.println(body);
  }
}

DateTime getNtpTime(NTPClient *timeClient)
{
  unsigned long epochTime = timeClient->getEpochTime();

  //Get a time structure
  struct tm *ptm = gmtime((time_t *)&epochTime);

  return DateTime(ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
}

String BitsToString(byte source[], uint length)
{
  char chars[length + 1];
  for(uint i = 0; i < length; i++)
  {
    chars[i] = (char)source[i] + '0';
  }
  chars[length] = '\0';
  
  return String(chars);
}

void UpdateDisplay()
{
  display.clearDisplay();
  display.setCursor(0, 0);
  if (!displayWeather)
  {
    display.println("H" + BitsToString(hoursBits, NUM_TIME_BITS));
    display.println("M" + BitsToString(minutesBits, NUM_TIME_BITS));
    display.println("S" + BitsToString(secondsBits, NUM_TIME_BITS));
  }
  else
  {
    display.println(F("Weather mode!!"));
  }

  display.display();
}

void UpdateTime()
{
  DateTime now = rtc.now();
  int hours = now.hour();
  int minutes = now.minute();
  int seconds = now.second();
  
  for (uint i = 0; i < NUM_TIME_BITS; ++i)
  {
    hoursBits[NUM_TIME_BITS - i - 1] = hours >> i & 1;
    minutesBits[NUM_TIME_BITS - i - 1] = minutes >> i & 1;
    secondsBits[NUM_TIME_BITS - i - 1] = seconds >> i & 1;
  }
}

void UpdateLEDs()
{
  for(int i = 0; i < NUM_LEDS; i++)
  {
    leds[NUM_LEDS - 1 - i] = secondsBits[i] == 1 ? CRGB::Blue : CRGB::Red;
  }
  FastLED.show();
}

void UpdateNtp()
{
  timeClient.update();
  rtc.adjust(getNtpTime(&timeClient));
}

void FiveMinuteTimer()
{
  UpdateNtp();
}

void InitLEDs()
{
  FastLED.addLeds<WS2812, DATA_PIN, RGB>(leds, NUM_LEDS); 
  FastLED.clear(true);
  for(int i = 0; i < NUM_LEDS; i++)
  {
    leds[i] = CRGB::White;
  }
  FastLED.setBrightness(10);
  FastLED.show();
}

void InitDisplay()
{
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ; // Don't proceed, loop forever
  }
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.display();
}

void InitRTC()
{
  if(!rtc.begin())
  {
    Serial.println(F("RTC not found!"));
    for (;;)
      ; // Don't proceed, loop forever
  }
  rtc.writeSqwPinMode(DS3231_SquareWave1Hz);
}

void setup()
{
  Serial.begin(74880);
  pinMode(OneHertzPin, INPUT_PULLUP);
  pinMode(ModePin, INPUT);

  InitRTC();
  InitDisplay();
  InitLEDs();
  
  attachInterrupt(digitalPinToInterrupt(OneHertzPin), OneHertzCallback, FALLING);
  attachInterrupt(digitalPinToInterrupt(ModePin), ModeCallback, RISING);
  Cron.create("5 * * * *", FiveMinuteTimer, false);
  
  display.println(F("Setting up wifi..."));
  display.display();

  WiFi.hostname("CLOCKYBOI");
  wifiManager.setDebugOutput(false);
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.autoConnect("CLOCKYBOI", "YESTHISREALLYISACLOCK");
  display.println(F("Connected to wifi!!!!"));
  display.println(F("Now what?"));
  display.display();

  timeClient.begin();
  UpdateNtp();
  webServer.reset(new ESP8266WebServer());
  webServer->on("/",[](){
    webServer->send(200, "text/plain", "its ya boi clockyboi");
  });
  webServer->onNotFound([]() {
    webServer->send(404, "text/plain", "naw fam that aint there");
  });
  webServer->begin();
}

void loop()
{
  if(forceRefresh)
  {
    forceRefresh = false;
    UpdateDisplay();
  }
  Cron.delay();
}
