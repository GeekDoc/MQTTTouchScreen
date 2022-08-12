#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

//LCD and Touch I2C pins
#define TFT_CS D0  //for D1 mini or TFT I2C Connector Shield (V1.1.0 or later)
#define TFT_DC D8  //for D1 mini or TFT I2C Connector Shield (V1.1.0 or later)
#define TFT_RST -1 //for D1 mini or TFT I2C Connector Shield (V1.1.0 or later)
#define TS_CS D3   //for D1 mini or TFT I2C Connector Shield (V1.1.0 or later)

// Touch calibration values (calculated for individual screen/touch panel)
#define xCalM 0.07
#define xCalC -20.0
#define yCalM -0.09
#define yCalC 340.0

//Create screen and touch objects
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TS_CS);

//variables for touch input
int touchX;
int touchY;

//Wifi and MQTT info
const char *ssid = "MagicIoT";
const char *password = "abracadabra";
const char *mqtt_server = "192.168.1.50";

//Create WiFi and MQTT client objects
WiFiClient espClient;
PubSubClient client(espClient);

//variables for MQTT message
#define MSG_BUFFER_SIZE (400)
char msg[MSG_BUFFER_SIZE];

void setup_wifi()
{

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}


// ScreenPoint class
// Adjusts for touch calibration
class ScreenPoint
{
public:
  int16_t x;
  int16_t y;

  ScreenPoint()
  {
    // default contructor
  }

  ScreenPoint(int16_t xIn, int16_t yIn)
  {
    x = xIn;
    y = yIn;
  }
};

ScreenPoint getScreenCoords(int16_t x, int16_t y)
{
  int16_t xCoord = round((x * xCalM) + xCalC);
  int16_t yCoord = round((y * yCalM) + yCalC);
  if (xCoord < 0)
    xCoord = 0;
  if (xCoord >= tft.width())
    xCoord = tft.width() - 1;
  if (yCoord < 0)
    yCoord = 0;
  if (yCoord >= tft.height())
    yCoord = tft.height() - 1;
  return (ScreenPoint(xCoord, yCoord));
}

//Button class
//Allows for placement, size, text, colors, touch validation
class Button
{
public:
  int x;
  int y;
  int width;
  int height;
  String text;
  uint16_t textColor;
  uint16_t butColor;

  Button()
  {
  }

  void initButton(int xPos, int yPos, int butWidth, int butHeight,  String butText, uint16_t butTextColor, uint16_t buttonColor)
  {
    x = xPos;
    y = yPos;
    width = butWidth;
    height = butHeight;
    text = butText;
    textColor = butTextColor;
    butColor = buttonColor;
    renderButton();
  }

  void renderButton()
  {
    int16_t trashX = 0;
    int16_t trashY = 0;
    uint16_t textHeight = 0;
    uint16_t textWidth = 0;
    tft.fillRoundRect(x, y, width, height, 5, butColor); // draw rectangle
    tft.setFont(&FreeSans9pt7b);
    tft.getTextBounds( text, 0, 0, &trashX, &trashY, &textWidth, &textHeight);
    tft.setCursor(x + (width/2) - (textWidth/2), y + (height/2) + (textHeight/2));
    tft.setTextSize(1);
    tft.setTextColor(textColor);
    tft.print(text);
  }

  bool isClicked(ScreenPoint sp)
  {
    if ((sp.x >= x) && (sp.x <= (x + width)) && (sp.y >= y) && (sp.y <= (y + height)))
    {
      return true;
    }
    else
    {
      return false;
    }
  }
};

//Button objects
Button WGarBtn;
Button GarManDoorBtn;
Button EGarBtn;
Button BarnLights;
Button DrivewaySensor;
Button PlayRoomLights;
Button PlayRoomLabel;

//Button layout variables
int spacing = 10;
int btnWidth = (tft.width() - (2 * spacing)) / 3;
int smBtnWidth = (tft.width() / 6);

//Button debounce variable (even touch screens "bounce"!)
unsigned long lastTouch = millis();

//TempModule class
//A multi-data display with temp, humidity, and battery level
//Size, position, and text label configurable
//TODO: Make colors configurable
class TempModule
{
public:
  int x;
  int y;
  int width;
  int height;
  String text;
  int batLevel;
  float tempReading;
  int humidityReading;
  uint16_t labelHeight = 0;
  uint16_t labelWidth = 0;
  uint16_t tempHeight = 0;
  uint16_t tempWidth = 0;

  TempModule()
  {
  }

  void initTempModule(int xPos, int yPos, int tempWidth, int tempHeight, String labelText, int tempBattery)
  {
    x = xPos;
    y = yPos;
    width = tempWidth;
    height = tempHeight;
    text = labelText;
    batLevel = tempBattery;
    tempReading = 0;
    humidityReading = 50;

    renderTemp();
  }

  void renderTemp()
  {
    //throw-away variables for text width calculation
    int16_t trashX = 0;
    int16_t trashY = 0;

    tft.drawRect(x, y, width, height + 1, ILI9341_BLUE);

    //title
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(ILI9341_WHITE);
    tft.getTextBounds(text, 0, 0, &trashX, &trashY, &labelWidth, &labelHeight);
    tft.setCursor(x + (width / 2) - (labelWidth / 2), y + 15);
    tft.print(text);

    update(tempReading, batLevel, humidityReading);
  }

  void update(float newTempC, int newBatLevel, int newHumidity)
  {
    //battery bar
    tft.fillRect(x + 1, y + 18, (width - 2), 3, ILI9341_DARKCYAN);
    tft.fillRect(x + 1, y + 18, (width - 2) * (newBatLevel * .01), 3, ILI9341_GREEN);

    //temp
    tft.fillRect(x + 1, y + 21, width - 2, height - 22, ILI9341_BLACK);
    tft.setFont(&FreeSans24pt7b);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(1);
    tft.setCursor(x + 5, y + height - 7);
    float newTempF = ((newTempC * 1.8) + 32.0); //convert to F
    tft.print(newTempF, 1);
    tft.setFont(&FreeSans9pt7b);

    //humidity
    tft.fillRect(x + width - 4, y + 21, 3, 47, ILI9341_DARKCYAN);
    tft.fillRect(x + width - 4, y + 68, 3, -47 * newHumidity * 0.01, ILI9341_BLUE);
  }
};

//TempModule objects
TempModule BarnTemp;
TempModule ShedTemp;
TempModule FreezerTemp;
TempModule OutsideTemp;

void setup_TempsGrid()
{
  //Temp grid 2x2
  int gridTop = btnWidth + smBtnWidth * 2 + spacing * 3; //calculate top from buttons above temps
  int tempWidth = 120; //TODO: this should probably be calculated
  int tempHeight = 68; //TODO: this should probably be calculated
  BarnTemp.initTempModule(0, gridTop, tempWidth, tempHeight, "Barn", 0);
  ShedTemp.initTempModule(tempWidth, gridTop, tempWidth, tempHeight, "Garden Shed", 0);
  FreezerTemp.initTempModule(0, gridTop + tempHeight, tempWidth, tempHeight, "Chest Freezer", 0);
  OutsideTemp.initTempModule(tempWidth, gridTop + tempHeight, tempWidth, tempHeight, "Outdoors", 0);
}

//MQTT Message Actions
void garageMsgAction(byte *payload)
{
  StaticJsonDocument<100> doc;

  DeserializationError error = deserializeJson(doc, payload);

  if (error)
  {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }


  //  const char *Time = doc["Time"];       // "2021-12-08T21:05:44"
  const char *MudRm = doc["Switch3"];   // "OFF" == unlocked
  const char *GarW = doc["Switch4"];    // "OFF" == closed
  const char *GarE = doc["Switch5"];    // "OFF" == closed
//  const char *Switch6 = doc["Switch6"]; // "OFF" (not used)

  Serial.print("MudRm = ");
  Serial.println(MudRm);

  String(MudRm) == "OFF" ? GarManDoorBtn.butColor = ILI9341_RED : GarManDoorBtn.butColor = ILI9341_GREEN;
  GarManDoorBtn.renderButton();
  String(GarW) == "ON" ? WGarBtn.butColor = ILI9341_RED : WGarBtn.butColor = ILI9341_GREEN;
  WGarBtn.renderButton();
  String(GarE) == "ON" ? EGarBtn.butColor = ILI9341_RED : EGarBtn.butColor = ILI9341_GREEN;
  EGarBtn.renderButton();
}

void driveSensMsgAction(byte *payload)
{
  String payloadString = (String((char *)payload));
  const char *POWER = "";
 
  if (payloadString.length() < 30) //status message
  {
    if (payloadString.startsWith("ON"))
    {
      POWER = "ON";
    }
    else if (payloadString.startsWith("OFF"))
    {
      POWER = "OFF";
    }
  }
  else // telemetry message
  {
    StaticJsonDocument<400> doc;
    
    DeserializationError error = deserializeJson(doc, payload);

    if (error)
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }

    // const char *Time = doc["Time"];           // "2021-12-09T15:07:28"
    // const char *Uptime = doc["Uptime"];       // "4T00:55:23"
    // long UptimeSec = doc["UptimeSec"];        // 348923
    // int Heap = doc["Heap"];                   // 27
    // const char *SleepMode = doc["SleepMode"]; // "Dynamic"
    // int Sleep = doc["Sleep"];                 // 50
    // int LoadAvg = doc["LoadAvg"];             // 19
    // int MqttCount = doc["MqttCount"];         // 5
    POWER = doc["POWER"];         // "ON"

    // JsonObject Wifi = doc["Wifi"];
    // int Wifi_AP = Wifi["AP"];                     // 1
    // const char *Wifi_SSId = Wifi["SSId"];         // "MagicIoT"
    // const char *Wifi_BSSId = Wifi["BSSId"];       // "B4:FB:E4:44:3E:C4"
    // int Wifi_Channel = Wifi["Channel"];           // 11
    // int Wifi_RSSI = Wifi["RSSI"];                 // 46
    // int Wifi_LinkCount = Wifi["LinkCount"];       // 1
    // const char *Wifi_Downtime = Wifi["Downtime"]; // "0T00:00:07"
  }
  String(POWER) == "OFF" ? DrivewaySensor.butColor = ILI9341_RED : DrivewaySensor.butColor = ILI9341_GREEN;
  DrivewaySensor.renderButton();

}

void playRmLightMsgAction(byte *payload)
{
  String payloadString = (String((char *)payload));
  const char *POWER = "";

  if (payloadString.length() < 30) //status message
  {
    if (payloadString.startsWith("ON"))
    {
      POWER = "ON";
    }
    else if (payloadString.startsWith("OFF"))
    {
      POWER = "OFF";
    }
  }
  else // telemetry message
  {
    StaticJsonDocument<384> doc;

    DeserializationError error = deserializeJson(doc, payload);

    if (error)
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }

    // const char *Time = doc["Time"];           // "2021-12-09T15:18:14"
    // const char *Uptime = doc["Uptime"];       // "4T01:11:07"
    // long UptimeSec = doc["UptimeSec"];        // 349867
    // int Heap = doc["Heap"];                   // 25
    // const char *SleepMode = doc["SleepMode"]; // "Dynamic"
    // int Sleep = doc["Sleep"];                 // 50
    // int LoadAvg = doc["LoadAvg"];             // 23
    // int MqttCount = doc["MqttCount"];         // 0
    POWER = doc["POWER"]; // "OFF"

    // JsonObject Wifi = doc["Wifi"];
    // int Wifi_AP = Wifi["AP"];                     // 1
    // const char *Wifi_SSId = Wifi["SSId"];         // "MagicIoT"
    // const char *Wifi_BSSId = Wifi["BSSId"];       // "B4:FB:E4:44:3E:C4"
    // int Wifi_Channel = Wifi["Channel"];           // 11
    // int Wifi_RSSI = Wifi["RSSI"];                 // 70
    // int Wifi_Signal = Wifi["Signal"];             // -65
    // int Wifi_LinkCount = Wifi["LinkCount"];       // 1
    // const char *Wifi_Downtime = Wifi["Downtime"]; // "0T00:00:11"
  }
  String(POWER) == "ON" ? PlayRoomLights.butColor = ILI9341_RED : PlayRoomLights.butColor = ILI9341_GREEN;
  PlayRoomLights.renderButton();
}

void barnLightsMsgAction(byte *payload)
{
  String payloadString = (String((char *)payload));
  const char *POWER = "";

  if (payloadString.length() < 30) //status message
  {
    if (payloadString.startsWith("ON"))
    {
      POWER = "ON";
    }
    else if (payloadString.startsWith("OFF"))
    {
      POWER = "OFF";
    }
  }
  else // telemetry message
  {
    StaticJsonDocument<384> doc;

    DeserializationError error = deserializeJson(doc, payload);

    if (error)
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }

    POWER = doc["POWER"]; // "OFF"
  }
  String(POWER) == "ON" ? BarnLights.butColor = ILI9341_RED : BarnLights.butColor = ILI9341_GREEN;
  BarnLights.renderButton();
}

void tempMsgAction(byte *payload)
{
  StaticJsonDocument<500> doc;

  DeserializationError error = deserializeJson(doc, payload);

  if (error)
  {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  float Temperature = doc["Temperature"];                             // 21.94
  int Battery = doc["Battery"];                                       // 100
  // const char *Time_via_BarnBLEBridge = doc["Time_via_BarnBLEBridge"]; // "2021-12-17T22:24:11"
  // float DewPoint = doc["DewPoint"];                                   // 6.16
  // const char *mac = doc["mac"];                                       // "A4C1388075C7"
  // int RSSI = doc["RSSI"];                                             // -34
  const char *alias = doc["Alias"];                                   // "BarnTemp"
  // const char *Time = doc["Time"];                                     // "2021-12-17T22:24:11"
  // int RSSI_via_BarnBLEBridge = doc["RSSI_via_BarnBLEBridge"];         // -34
  // const char *via_device = doc["via_device"];                         // "BarnBLEBridge"
  int Humidity = doc["Humidity"];                                     // 36

  if (String(alias) == "BarnTemp")
  {
    Serial.print("Temperature is :");
    Serial.println(float(Temperature));
    BarnTemp.update(float(Temperature), Battery, Humidity);
  }
  else if (String(alias) == "ShedTemp")
  {
    ShedTemp.update(float(Temperature), Battery, Humidity);
  }
  else if (String(alias) == "BasementFreezer")
  {
    FreezerTemp.update(float(Temperature), Battery, Humidity);
  }
  else if (String(alias) == "OutsideTemp")
  {
    OutsideTemp.update(float(Temperature), Battery, Humidity);
  }
  else
  {
    Serial.print("No Temp routine found for Alias: ");
    Serial.println(alias);
  }
}

void callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.println(topic);
  Serial.println("] ");
  // for (uint16_t i = 0; i < length; i++)
  // {
  //   Serial.print((char)payload[i]);
  // }
  // Serial.println(String(topic));

  if (String(topic) == "tele/garage/SENSOR") //garage door sensors
  {
    Serial.println("garageMsgAction");
    garageMsgAction(payload);
  }
  else if (String(topic) == "tele/DriveBell/STATE" || String(topic) == "stat/DriveBell/POWER") //driveway sensor chime
  {
    Serial.println("driveSensMsgAction");
    driveSensMsgAction(payload);
  }
  else if (String(topic) == "tele/barnlightsN/STATE" || String(topic) == "stat/barnlightsN/POWER") //Barn N Lights
  {
    Serial.println("barnLightsMsgAction");
    barnLightsMsgAction(payload);
  }
  else if (String(topic).startsWith("tele/tasmota_blerry/"))
  {
    Serial.println("tempMsgAction");
    tempMsgAction(payload);
  }
}

void reconnect()
{
  // Loop until we're reconnected
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "Display1Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str()))
    {
      Serial.println("connected");
      // ... and resubscribe
      client.subscribe("tele/garage/SENSOR"); //garage sensors
      client.subscribe("tele/DrivewaySensor/#");   //driveway alert chime plug
      client.subscribe("tele/BarnLightsNorth/#"); //Barn N lights
      client.subscribe("tele/BarnLightsSouth/#"); //Barn S lights
      client.subscribe("tele/tasmota_blerry/#"); //Bluetooth temp sensors
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}



void setup()
{
  Serial.begin(115200);

  //WiFi and MQTT
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  client.setBufferSize(512); //big buffer for long status messages
  Serial.print("MQTT message buffer size: ");
  Serial.println(client.getBufferSize());

  //Screen and Touch
  tft.begin();
  ts.begin();
  tft.setRotation(0);
  ts.setRotation(0);
  tft.fillScreen(ILI9341_BLACK);

  //Buttons
  //TODO: Break this section out to a function, like the temps grid
  WGarBtn.initButton(0, 0, btnWidth, btnWidth, (new String("W Gar"))->c_str(), ILI9341_BLACK, ILI9341_GREENYELLOW);
  GarManDoorBtn.initButton(btnWidth + spacing, 0, btnWidth, btnWidth, (new String("Mud Rm"))->c_str(), ILI9341_BLACK, ILI9341_GREENYELLOW);
  EGarBtn.initButton(btnWidth * 2 + spacing * 2, 0, btnWidth, btnWidth, (new String("E Gar"))->c_str(), ILI9341_BLACK, ILI9341_GREENYELLOW);
  BarnLights.initButton(1, btnWidth + spacing, tft.width() - 1, smBtnWidth, (new String("Barn Lights"))->c_str(), ILI9341_BLACK, ILI9341_GREENYELLOW);
  DrivewaySensor.initButton(1, btnWidth + smBtnWidth + spacing * 2, tft.width() - 1, smBtnWidth, (new String("Driveway Sensor"))->c_str(), ILI9341_BLACK, ILI9341_GREENYELLOW);

  //Temps
  setup_TempsGrid();
}

void loop(void)
{
  if (!client.connected())
  {
    reconnect();
  }
  client.loop();

  if (ts.touched() && lastTouch < (millis() - 1500))
  {
    lastTouch = millis();
    TS_Point p = ts.getPoint();
    ScreenPoint sp = getScreenCoords(p.x, p.y);

    if (EGarBtn.isClicked(sp))
    {
      client.publish("cmnd/garage/POWER1", "ON");
    }
    if (WGarBtn.isClicked(sp))
    {
      client.publish("cmnd/garage/POWER2", "ON");
    }
    if (DrivewaySensor.isClicked(sp))
    {
      client.publish("cmnd/DriveBell/POWER", "TOGGLE");
    }
    if (BarnLights.isClicked(sp))
    {
      Serial.println("BarnLights touched");
      if(BarnLights.butColor==ILI9341_RED) //this is the only way I currently have of knowing status
      {
        Serial.println("BarnLights RED");
        client.publish("cmnd/BarnLightsSouth/POWER", "OFF");
        client.publish("cmnd/BarnLightsNorth/POWER", "OFF");
      }
      else
      {
        Serial.println("BarnLights not RED");
        client.publish("cmnd/BarnLightsSouth/POWER", "ON");
        client.publish("cmnd/BarnLightsNorth/POWER", "ON");
      }
      Serial.println("BarnLights exit");
    }
  }

}

