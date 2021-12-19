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

#define TFT_CS D0  //for D1 mini or TFT I2C Connector Shield (V1.1.0 or later)
#define TFT_DC D8  //for D1 mini or TFT I2C Connector Shield (V1.1.0 or later)
#define TFT_RST -1 //for D1 mini or TFT I2C Connector Shield (V1.1.0 or later)
#define TS_CS D3   //for D1 mini or TFT I2C Connector Shield (V1.1.0 or later)

// Touch calibration values
#define xCalM 0.07
#define xCalC -20.0
#define yCalM -0.09
#define yCalC 340.0

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TS_CS);

int touchX;
int touchY;

//Wifi and MQTT
const char *ssid = "IoTWiFi";
const char *password = "themagicpassword";
const char *mqtt_server = "192.168.1.50";

WiFiClient espClient;
PubSubClient client(espClient);
// unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (400)
char msg[MSG_BUFFER_SIZE];
// int value = 0;

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


// Buttons
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
    render();
  }

  void render()
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

Button WGarBtn;
Button GarManDoorBtn;
Button EGarBtn;
Button BarnLights;
Button DrivewaySensor;
Button PlayRoomLights;
Button PlayRoomLabel;
int spacing = 10;
int btnWidth = (tft.width() - (2 * spacing)) / 3;
int smBtnWidth = (tft.width() / 6);
unsigned long lastTouch = millis();

//TEMP DISPLAY
void setup_TempsGrid()
{
  //Temp grid
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(40, tft.height() - 50);
  tft.print("Barn");
  tft.setCursor(tft.width() / 2 + 7, tft.height() - 50);
  tft.print("Garden Shed");
  tft.drawLine(0, tft.height() - 45, tft.width(), tft.height() - 45, ILI9341_DARKGREEN);
  tft.drawFastHLine(0, tft.height() - 44, tft.width() - 1, ILI9341_BLUE);
  tft.drawFastVLine(tft.width() / 2 - 1, tft.height() - 65, 65, ILI9341_BLUE);
  tft.drawLine(tft.width() / 2, tft.height() - 1, tft.width() / 2, tft.height() - 65, ILI9341_DARKGREEN);
  //dummy data
  tft.setFont(&FreeSans24pt7b);
  tft.setCursor(5, tft.height() - 5);
  tft.print("--.-");
  tft.setCursor(tft.width() / 2 + 5, tft.height() - 5);
  tft.print("--.-");
  tft.setFont(&FreeSans9pt7b);
}

float convCtoF_SingleDecimal(float tempC)
{
  long intTemp;
  //convert to F
  float tempF = ((tempC * 9 / 5) + 32);
  //convert to single decimal
  intTemp = tempF * 10;
  tempF = float(intTemp) / 10;
  return tempF;
}

void showBarnTemp(float barnTemp, int batLevel)
{
  tft.fillRect(1, tft.height() - 42, tft.width()/2 - 4, tft.height() - 1, ILI9341_BLACK);
  tft.setFont(&FreeSans24pt7b);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(5, tft.height() - 5);
  tft.print(convCtoF_SingleDecimal(barnTemp),1);
  tft.setFont(&FreeSans9pt7b);
  tft.fillRect(0, tft.height() - 45, tft.width() / 2 * (batLevel * .01), 2, ILI9341_GREEN);
}

void showShedTemp(float shedTemp, int batLevel)
{
  tft.fillRect(tft.width() / 2 + 1, tft.height() - 42, tft.width()/2 - 4, tft.height() - 1, ILI9341_BLACK);
  tft.setFont(&FreeSans24pt7b);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(tft.width() / 2 + 5, tft.height() - 5);
  tft.print(convCtoF_SingleDecimal(shedTemp),1);
  tft.setFont(&FreeSans9pt7b);
  tft.fillRect(tft.width() / 2, tft.height() - 45, tft.width() / 2 * (batLevel * .01), 2, ILI9341_GREEN);
}

//MQTT Message Actions
void garageMsgAction(byte *payload)
{
  Serial.println("in gar status handling");
  
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
  GarManDoorBtn.render();
  String(GarW) == "ON" ? WGarBtn.butColor = ILI9341_RED : WGarBtn.butColor = ILI9341_GREEN;
  WGarBtn.render();
  String(GarE) == "ON" ? EGarBtn.butColor = ILI9341_RED : EGarBtn.butColor = ILI9341_GREEN;
  EGarBtn.render();
}

void driveSensMsgAction(byte *payload)
{
  Serial.println("in drive sensor handling");

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
  DrivewaySensor.render();

}

void playRmLightMsgAction(byte *payload)
{
  Serial.println("in playroom light handling");

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
    POWER = doc["POWER"];         // "OFF"

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
  PlayRoomLights.render();
}

void tempMsgAction(byte *payload)
{
  StaticJsonDocument<200> doc;

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
  const char *alias = doc["alias"];                                   // "BarnTemp"
  // const char *Time = doc["Time"];                                     // "2021-12-17T22:24:11"
  // int RSSI_via_BarnBLEBridge = doc["RSSI_via_BarnBLEBridge"];         // -34
  // const char *via_device = doc["via_device"];                         // "BarnBLEBridge"
  // int Humidity = doc["Humidity"];                                     // 36

  if (String(alias) == "BarnTemp")
  {
    Serial.print("Temperature is :");
    Serial.println(float(Temperature));
    showBarnTemp(float(Temperature), Battery);
  }
  else if (String(alias) == "ShedTemp")
  {
    showShedTemp(float(Temperature), Battery);
  }
}

void callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (uint16_t i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println(String(topic));

  if (String(topic) == "tele/garage/SENSOR") //garage door sensors
  {
    garageMsgAction(payload);
  }
  else if (String(topic) == "tele/DriveBell/STATE" || String(topic) == "stat/DriveBell/POWER") //driveway sensor chime
  {
    driveSensMsgAction(payload);
  }
  // else if (String(topic) == "tele/tasmota_53AA9D/STATE" || String(topic) == "stat/tasmota_53AA9D/POWER") //playroom light
  // {
  //   playRmLightMsgAction(payload);
  // }
  else if (String(topic) == "tele/tasmota_blerry/BarnTemp" || String(topic) == "tele/tasmota_blerry/ShedTemp")
  {
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
      client.subscribe("tele/DriveBell/STATE"); //driveway alert chime plug
      client.subscribe("stat/DriveBell/POWER"); //driveway alert chime plug
      // client.subscribe("tele/tasmota_53AA9D/STATE"); //Playroom lights
      // client.subscribe("stat/tasmota_53AA9D/POWER"); //Playroom lights
      client.subscribe("tele/tasmota_blerry/BarnTemp");
      client.subscribe("tele/tasmota_blerry/ShedTemp");
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
  client.setBufferSize(450);
  Serial.print("MQTT message buffer size: ");
  Serial.println(client.getBufferSize());

  //Screen and Touch
  tft.begin();
  ts.begin();
  tft.setRotation(0);
  ts.setRotation(0);
  tft.fillScreen(ILI9341_BLACK);

  //Buttons
  WGarBtn.initButton(0, 0, btnWidth, btnWidth, (new String("W Gar"))->c_str(), ILI9341_BLACK, ILI9341_GREEN);
  GarManDoorBtn.initButton(btnWidth + spacing, 0, btnWidth, btnWidth, (new String("Mud Rm"))->c_str(), ILI9341_BLACK, ILI9341_GREEN);
  EGarBtn.initButton(btnWidth * 2 + spacing * 2, 0, btnWidth, btnWidth, (new String("E Gar"))->c_str(), ILI9341_BLACK, ILI9341_GREEN);
  BarnLights.initButton(1, btnWidth + spacing, tft.width() - 1, smBtnWidth, (new String("Barn Lights"))->c_str(), ILI9341_BLACK, ILI9341_GREEN);
  DrivewaySensor.initButton(1, btnWidth + smBtnWidth + spacing * 2, tft.width() - 1, smBtnWidth, (new String("Driveway Sensor"))->c_str(), ILI9341_BLACK, ILI9341_GREEN);
  //PlayRoomLights.initButton(tft.width() - 1 - smBtnWidth, btnWidth + smBtnWidth *2 + spacing *3 , smBtnWidth, smBtnWidth, (new String(""))->c_str(), ILI9341_BLACK, ILI9341_GREEN);
  //PlayRoomLabel.initButton(0, PlayRoomLights.y, tft.width() - spacing - PlayRoomLights.width, PlayRoomLights.height, (new String("Play Room Lights"))->c_str(), ILI9341_WHITE, ILI9341_BLACK);

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
    // if (PlayRoomLabel.isClicked(sp) || PlayRoomLights.isClicked(sp))
    // {
    //   client.publish("cmnd/tasmota_53AA9D/POWER", "TOGGLE");
    // }
  }


}

