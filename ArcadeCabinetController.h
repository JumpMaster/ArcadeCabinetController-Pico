#ifndef ARCADE_CABINET_CONTROLLER_H
#define ARCADE_CABINET_CONTROLLER_H

#include "secrets.h"
#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "Logging.h"
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

WiFiClient rpiClient;
PubSubClient mqttClient(rpiClient);
uint32_t nextMetricsUpdate = 0;
unsigned long wifiReconnectPreviousMillis = 0;
unsigned long wifiReconnectInterval = 30000;
uint8_t wifiReconnectCount = 0;

const char* deviceConfig = "{\"identifiers\":\"be6beb17-0012-4a70-bc76-a484d34de5cb\",\"name\":\"ArcadeCabinet\",\"sw_version\":\"0.2\",\"model\":\"ArcadeCabinet\",\"manufacturer\":\"JumpMaster\"}";

HAMqttDevice mqttPowerButton("ArcadeCabinet Power Button", HAMqttDevice::BUTTON, "homeassistant");
HAMqttDevice mqttPowerState("ArcadeCabinet Power State", HAMqttDevice::BINARY_SENSOR, "homeassistant");
HAMqttDevice mqttParentalMode("ArcadeCabinet Parental Mode", HAMqttDevice::SWITCH, "homeassistant");
uint32_t nextMqttConnectAttempt = 0;
const uint32_t mqttReconnectInterval = 10000;

volatile bool parentalMode = false;
volatile bool cabinetPowerState = LOW;
volatile bool cabinetTargetPowerState = LOW;
bool reportedPowerState = LOW;

#endif