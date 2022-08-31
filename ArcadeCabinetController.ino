#include "secrets.h"
#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "PapertrailLogger.h"
#include <Adafruit_NeoPixel.h>
#include <PubSubClient.h>
#include "HAMqttDevice.h"
#include <EasyButton.h>

typedef enum
{
    LIGHT_MODE_OFF = 0,
    LIGHT_MODE_SOLID = 1,
    LIGHT_MODE_RAINBOW = 2,
    LIGHT_MODE_RAINBOW_SOLID = 3,
    LIGHT_MODE_MANUAL = 4,
} LightMode;

enum
{
    i2c_LIGHT_BRIGHTNESS = 0x01,
    i2c_LIGHT_COLOR = 0x02,
    i2c_LIGHT_MODE = 0x10,
    i2c_LIGHT_MANUAL = 0x20,
    i2c_LIGHT_OFF = 0xFF,
};

const uint8_t NUMPIXELS = 34;
const uint8_t ONBOARD_LED_PIN = LED_BUILTIN;

const uint8_t LED_STRIP_PIN = 13;
const uint8_t PI_SHUTDOWN_PIN = 14;
const uint8_t PI_POWEROFF_PIN = 15;
const uint8_t PLAYER1_BUTTON_INPUT_PIN = 16;
const uint8_t PLAYER1_BUTTON_OUTPUT_PIN = 17;

bool player1ButtonState;
//EasyButton player1Button(PLAYER1_BUTTON_INPUT_PIN, debounce, pullup, invert);
EasyButton player1Button(PLAYER1_BUTTON_INPUT_PIN, 35, true, true);

const uint8_t relay_PIN1 = 18; // 240V - Pi and Screen
const uint8_t relay_PIN2 = 19; // 12V  - Amplifier
const uint8_t relay_PIN3 = 20; // 12V  - LEDs
const uint8_t relay_PIN4 = 21; // 5V   - Unused due to noisy PSU affecting Pi audio

bool ledPowerState = LOW;

bool startupComplete = false;

Adafruit_NeoPixel pixels(NUMPIXELS, LED_STRIP_PIN, NEO_RGB + NEO_KHZ800);

LightMode lightMode = LIGHT_MODE_OFF;
uint8_t lightBrightness = 0;
uint8_t lightTargetBrightness = 255;
uint32_t nextBrightnessChange = 0;
const uint8_t brightnessDelay = 10; // Milliseconds between brightness changes
uint8_t lightColor[3] = {255, 255, 255};
uint8_t manualLEDColor[3] = {255, 255, 255};
uint8_t manualLEDPosition = 0;

PapertrailLogger *infoLog;
WiFiClient rpiClient;
PubSubClient mqttClient(rpiClient);
unsigned long wifiReconnectPreviousMillis = 0;
unsigned long wifiReconnectInterval = 30000;

const char* deviceConfig = "{\"identifiers\":\"be6beb17-0012-4a70-bc76-a484d34de5cb\",\"name\":\"ArcadeCabinet\",\"sw_version\":\"0.2\",\"model\":\"ArcadeCabinet\",\"manufacturer\":\"JumpMaster\"}";

HAMqttDevice mqttPowerButton("ArcadeCabinet Power Button", HAMqttDevice::BUTTON, "homeassistant");
HAMqttDevice mqttPowerState("ArcadeCabinet Power State", HAMqttDevice::BINARY_SENSOR, "homeassistant");
HAMqttDevice mqttParentalMode("ArcadeCabinet Parental Mode", HAMqttDevice::SWITCH, "homeassistant");

bool parentalMode = false;
bool cabinetPowerState = LOW;
bool cabinetTargetPowerState = LOW;

uint32_t currentMillis = 0;

void setCabinetPower(bool newState)
{
    if (cabinetPowerState == LOW && newState == HIGH)
    {
        digitalWrite(relay_PIN2, HIGH); // 12V
        digitalWrite(relay_PIN4, HIGH); // 5V
        digitalWrite(relay_PIN1, HIGH); // 240V
        cabinetPowerState = HIGH;
    }
    else if (cabinetPowerState == HIGH && newState == LOW)
    {
        digitalWrite(relay_PIN1, LOW); // 240V
        digitalWrite(relay_PIN2, LOW); // 12V
        digitalWrite(relay_PIN4, LOW); // 5V
        cabinetPowerState = LOW;
    }
}

void mqttCallback(char* topic, byte* payload, unsigned int length)
{
    char data[length + 1];
    memcpy(data, payload, length);
    data[length] = '\0';

    if (strcmp(topic, mqttPowerButton.getCommandTopic().c_str()) == 0)
    {
        if (strcmp(data, "PRESS") == 0)
        {
            infoLog->println("Virtual power button pressed");
            mqttClient.publish("log/mqttCallback", "Virtual power button pressed");
            if (cabinetPowerState == LOW)
            {   // Power on
                cabinetTargetPowerState = HIGH;
                infoLog->println("Cabinet power on initiated");
                mqttClient.publish("log/mqttCallback", "Cabinet power on initiated");
            }
            else
            {   // Shutdown
                digitalWrite(PI_SHUTDOWN_PIN, HIGH);
                delay(200);
                digitalWrite(PI_SHUTDOWN_PIN, LOW);
                infoLog->println("Cabinet shutdown initiated");
                mqttClient.publish("log/mqttCallback", "Cabinet shutdown initiated");
            }
        }
    }

    if (strcmp(mqttParentalMode.getCommandTopic().c_str(), topic) == 0)
    {
        if (strcmp(data, "ON") == 0)
        {
            //digitalWrite(ONBOARD_LED_PIN, HIGH);
            parentalMode = true;
        }
        else
        {
           //digitalWrite(ONBOARD_LED_PIN, LOW);
           parentalMode = false;
        }
        mqttClient.publish(mqttParentalMode.getStateTopic().c_str(), data, true);
        infoLog->printf("Parental mode turned %s\n", parentalMode ? "on" : "off");
    }
}

void mqttConnect()
{
    // Loop until we're reconnected
    while (!mqttClient.connected())
    {
        infoLog->println("Connecting to MQTT");
        // Attempt to connect
        if (mqttClient.connect(deviceName, mqtt_username, mqtt_password))
        {
            infoLog->println("Connected to MQTT");

            mqttClient.publish(mqttPowerButton.getConfigTopic().c_str(), mqttPowerButton.getConfigPayload().c_str(), true);
            mqttClient.publish(mqttPowerState.getConfigTopic().c_str(), mqttPowerState.getConfigPayload().c_str(), true);
            mqttClient.publish(mqttParentalMode.getConfigTopic().c_str(), mqttParentalMode.getConfigPayload().c_str(), true);

            mqttClient.publish(mqttPowerState.getStateTopic().c_str(), cabinetPowerState ? "ON" : "OFF", true);
            mqttClient.publish(mqttParentalMode.getStateTopic().c_str(), parentalMode ? "ON" : "OFF", true);

            mqttClient.subscribe(mqttPowerButton.getCommandTopic().c_str());
            mqttClient.subscribe(mqttParentalMode.getCommandTopic().c_str());

        }
        else
        {
            infoLog->println("Failed to connect to MQTT");
            return;
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}

void connectToNetwork()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID, wifiPassword);
 
    while (WiFi.waitForConnectResult() != WL_CONNECTED)
    {
        delay(1000);
    }

    if (WiFi.status() == WL_CONNECTED)
        infoLog->println("Connected to WiFi");
}

void setupMQTT()
{
    mqttPowerButton.addConfigVar("device", deviceConfig);
    mqttPowerState.addConfigVar("device", deviceConfig);
    mqttParentalMode.addConfigVar("device", deviceConfig);

    mqttClient.setBufferSize(4096);
    mqttClient.setServer(mqtt_server, 1883);
    mqttClient.setCallback(mqttCallback);
}

void setupPixels()
{
    pixels.begin();
    pixels.clear();
    pixels.setBrightness(lightBrightness);
}

void setupOTA()
{
    ArduinoOTA.setHostname(deviceName);
  
    ArduinoOTA.onStart([]()
    {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "sketch";
        } else {  // U_FS
            type = "filesystem";
        }

        // NOTE: if updating FS this would be the place to unmount FS using FS.end()
        Serial.println("Start updating " + type);
    });

    ArduinoOTA.onEnd([]()
    {
        Serial.println("\nEnd");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
    {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error)
    {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
        {
            Serial.println("Auth Failed");
        }
        else if (error == OTA_BEGIN_ERROR)
        {
            Serial.println("Begin Failed");
        }
        else if (error == OTA_CONNECT_ERROR)
        {
            Serial.println("Connect Failed");
        }
        else if (error == OTA_RECEIVE_ERROR)
        {
            Serial.println("Receive Failed");
        }
        else if (error == OTA_END_ERROR)
        {
            Serial.println("End Failed");
        }
    });
    
    ArduinoOTA.begin();
}


void i2cReceiveData(int bytecount)
{
    mqttClient.publish("log/i2cReceiveData", "Start");

    if (bytecount <= 1 || bytecount > 10)
    {
        mqttClient.publish("log/i2cReceiveData", "Wrong number of bytes");
        return;
    }

    byte data[10];
    char buffer[10];
    for (int i = 0; i < bytecount; i++)
    {
        data[i] = Wire.read();
        char buffer[50];
        snprintf(buffer, 10, "data=%d", data[i]);
        mqttClient.publish("log/i2cReceiveData", buffer);
    }

    switch (data[0])
    {
        case i2c_LIGHT_BRIGHTNESS:
            mqttClient.publish("log/i2cReceiveData", "i2c_LIGHT_BRIGHTNESS");
            lightTargetBrightness = data[1];
            return;

        case i2c_LIGHT_COLOR:
            mqttClient.publish("log/i2cReceiveData", "i2c_LIGHT_COLOR");
            if (bytecount <= 3)
            {
               mqttClient.publish("log/i2cReceiveData/i2c_light_color", "Wrong number of bytes");
            }
            else
            {
                lightColor[0] = data[1];
                lightColor[1] = data[2];
                lightColor[2] = data[3];
            }
            return;

        case i2c_LIGHT_MODE:
            mqttClient.publish("log/i2cReceiveData", "i2c_LIGHT_MODE");
            switch (data[1])
            {
                case 0:
                    lightMode = LIGHT_MODE_OFF;
                    break;
                case 1:
                    lightMode = LIGHT_MODE_SOLID;
                    break;
                case 2:
                    lightMode = LIGHT_MODE_RAINBOW;
                    break;
                case 3:
                    lightMode = LIGHT_MODE_RAINBOW_SOLID;
                    break;
                case 4:
                    lightMode = LIGHT_MODE_MANUAL;
                    break;
            }
            return;
/*
        case i2c_LIGHT_MANUAL:           
            if (position == 1 && data >= 0 && data < NUMPIXELS)
                manualLEDPosition = data;
            
            if (position >= 2 && position <= 4)
                manualLEDColor[position-2] = data;
            
            if (position == 4)
            {
                pixels.setPixelColor(manualLEDPosition, manualLEDColor[0], manualLEDColor[1], manualLEDColor[2]);
                pixels.show();
                return 0;
            }
            
            return position+1;
*/
        case i2c_LIGHT_OFF:
            mqttClient.publish("log/i2cReceiveData", "i2c_LIGHT_OFF");
            lightMode = LIGHT_MODE_OFF;
            return;

        default:
            return;
    }
}

void i2cSendData()
{
    mqttClient.publish("log/sendData", "Sending 0x0a"); // This should never happen
    Wire.write(0x0a);
}


void setup()
{
    Serial.begin(115200);
    Serial.println("Setup");
    pinMode(ONBOARD_LED_PIN, OUTPUT);
    pinMode(PI_POWEROFF_PIN, INPUT_PULLDOWN);
    pinMode(PLAYER1_BUTTON_OUTPUT_PIN, OUTPUT);
    pinMode(PI_SHUTDOWN_PIN, OUTPUT);

    player1Button.begin();

    pinMode(relay_PIN1, OUTPUT);
    pinMode(relay_PIN2, OUTPUT);
    pinMode(relay_PIN3, OUTPUT);
    pinMode(relay_PIN4, OUTPUT);

    digitalWrite(ONBOARD_LED_PIN, HIGH);
    digitalWrite(PLAYER1_BUTTON_OUTPUT_PIN, LOW);
    digitalWrite(PI_SHUTDOWN_PIN, LOW);
    digitalWrite(relay_PIN1, LOW);
    digitalWrite(relay_PIN2, LOW);
    digitalWrite(relay_PIN3, LOW);
    digitalWrite(relay_PIN4, LOW);

    setupMQTT();

    //Wire.setClock(19000); // Works 9/10 times with the host set to 10000
    Wire.begin(0x0a);
    Wire.onReceive(i2cReceiveData);
    Wire.onRequest(i2cSendData);

    infoLog = new PapertrailLogger(papertrailAddress, papertrailPort, LogLevel::Info, WiFi.macAddress(), deviceName);

    setupPixels();

    connectToNetwork();

    setupOTA();

    mqttConnect();
}

void manageOnboardLED()
{
    if (startupComplete)
        return;

    // TURN OFF ONBOARD LED ONCE UPTIME IS GREATER THEN 5 SECONDS
    if (millis() > 5000)
    {
        digitalWrite(ONBOARD_LED_PIN, LOW);
        startupComplete = true;
    }
}

void manageWiFi()
{
    // if WiFi is down, try reconnecting
    if ((WiFi.status() != WL_CONNECTED) && (currentMillis - wifiReconnectPreviousMillis >= wifiReconnectInterval))
    {
        //Serial.print(currentMillis);
        //Serial.println("Reconnecting to WiFi...");
        WiFi.disconnect();

        //WiFi.reconnect();
        connectToNetwork();

        if (WiFi.status() == WL_CONNECTED)
            infoLog->println("Reconnected to WiFi");

        wifiReconnectPreviousMillis = currentMillis;
    }
}

void manageMQTT()
{
    if (!mqttClient.connected())
    {
        mqttConnect();
    }
    else
    {
        mqttClient.loop();
    }
}

void managePowerStates()
{
    // DETECT PI POWEROFF - dtoverlay=gpio-poweroff
    if (cabinetPowerState == HIGH && digitalRead(PI_POWEROFF_PIN) == HIGH)
        cabinetTargetPowerState = LOW;
  
    // MANAGE POWER STATES
    if (cabinetTargetPowerState != cabinetPowerState)
    {
        if (cabinetTargetPowerState == HIGH)
        {
            setCabinetPower(HIGH);
            lightMode = LIGHT_MODE_RAINBOW;
        }
        else if (cabinetTargetPowerState == LOW)
        {
            setCabinetPower(LOW);
            lightMode = LIGHT_MODE_OFF;
        }

        if (mqttClient.connected())
            mqttClient.publish(mqttPowerState.getStateTopic().c_str(), cabinetPowerState ? "ON" : "OFF", true);
    
        infoLog->printf("Cabinet powering %s\n", cabinetTargetPowerState ? "on" : "off");
    }
}

void manageStartButton()
{
    // MANAGE PLAYER 1 START BUTTON
    player1Button.read();

    if (cabinetPowerState == HIGH)
    {
        if (player1Button.isPressed() != player1ButtonState)
        {
            player1ButtonState = player1Button.isPressed();
            digitalWrite(PLAYER1_BUTTON_OUTPUT_PIN, player1ButtonState ? HIGH : LOW);

            if (player1Button.pressedFor(10000))
                cabinetTargetPowerState = LOW;
        }
    }
    else if (!parentalMode && cabinetPowerState == LOW)
    {
        if (player1Button.pressedFor(1000) && !player1Button.pressedFor(10000))
            cabinetTargetPowerState = HIGH;
    }
}

void manageLEDs()
{
    static uint16_t lightIndex = 0;

    if (ledPowerState == LOW && lightMode != LIGHT_MODE_OFF)
    {
        digitalWrite(relay_PIN3, HIGH); // 12V LED
        pixels.clear();
        lightTargetBrightness = 255;
        ledPowerState = HIGH;
        delay(50);
    }
    else if (ledPowerState == HIGH && lightMode == LIGHT_MODE_OFF && lightBrightness == 0)
    {
        digitalWrite(relay_PIN3, LOW); // 12V LED
        ledPowerState = LOW;
    }

    // MANAGE LEDS
    if (ledPowerState == HIGH)
    {
        //
        // Set brightness
        //
        if (lightBrightness != lightTargetBrightness && millis() > nextBrightnessChange)
        {
            if (lightTargetBrightness > lightBrightness)
                lightBrightness++;
            else
                lightBrightness--;
            pixels.setBrightness(lightBrightness);
            nextBrightnessChange = millis() + brightnessDelay;
        }

        if (lightMode == LIGHT_MODE_OFF)
        {
            if (lightTargetBrightness != 0)
            {
                lightTargetBrightness = 0;
            }
        }
        else if (lightMode == LIGHT_MODE_SOLID)
        {
            pixels.clear();

            for (uint8_t i = 0; i < NUMPIXELS; i++)
            {
                if (i < 4 || i > (NUMPIXELS-4))
                    pixels.setPixelColor(i, 255, 255, 255);
                else
                    pixels.setPixelColor(i, lightColor[0], lightColor[1], lightColor[2]);
            }
        }
        else if (lightMode == LIGHT_MODE_RAINBOW)
        {
            lightIndex += 10;
            pixels.rainbow(lightIndex, -1);

            for (uint8_t i = 0; i < 4; i++)
            {
                pixels.setPixelColor(i, 255, 255, 255);
                pixels.setPixelColor(NUMPIXELS - 1 - i, 255, 255, 255);
            }
        }
        else if (lightMode == LIGHT_MODE_RAINBOW_SOLID)
        {
            lightIndex += 10;
            uint32_t color = pixels.ColorHSV(lightIndex, 255, 255);

            for (uint8_t i = 0; i < NUMPIXELS; i++)
            {
                if (i < 4 || i > (NUMPIXELS-4))
                    pixels.setPixelColor(i, 255, 255, 255);
                else
                    pixels.setPixelColor(i, color);
            }
        }
        /*
        else if (lightMode == LIGHT_MODE_MANUAL)
        {
            // DO NOTHING
        }
        */
        pixels.show();
    }
}

void loop()
{
    currentMillis = millis();

    ArduinoOTA.handle();

    manageWiFi();

    manageMQTT();

    manageOnboardLED();

    managePowerStates();

    manageStartButton();

    manageLEDs();
}
