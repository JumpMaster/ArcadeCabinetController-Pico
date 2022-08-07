#include <EasyButton.h>
#include <Sequence.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>

typedef enum
{
    LIGHT_MODE_OFF = 0,
    LIGHT_MODE_SOLID = 1,
    LIGHT_MODE_RAINBOW = 2,
    LIGHT_MODE_RAINBOW_SOLID = 3,
    LIGHT_MODE_MANUAL = 4,
} LightMode;

typedef enum
{
    i2c_LIGHT_BRIGHTNESS = 0x01,
    i2c_LIGHT_COLOR = 0x02,
    i2c_LIGHT_MODE = 0x10,
    i2c_LIGHT_MANUAL = 0x20,
    i2c_LIGHT_OFF = 0xFF,
};

const uint8_t NUMPIXELS = 34;

const uint8_t ONBOARD_LED_PIN = 25;

const uint8_t LED_STRIP_PIN = 14;
const uint8_t PI_POWEROFF_PIN = 15;
const uint8_t PLAYER1_BUTTON_INPUT_PIN = 16;
const uint8_t PLAYER1_BUTTON_OUTPUT_PIN = 17;

bool player1ButtonState;
//EasyButton player1Button(PLAYER1_BUTTON_INPUT_PIN, debounce, pullup, invert);
EasyButton player1Button(PLAYER1_BUTTON_INPUT_PIN, 35, true, true);

const uint8_t relay_PIN1 = 18; // 240V - Pi and Screen
const uint8_t relay_PIN2 = 19; // 12V - LEDs and Amplifier
const uint8_t relay_PIN3 = 20; // 5V - Unused due to noisy PSU affecting Pi audio
const uint8_t relay_PIN4 = 21;

uint32_t lastPowerChange = 0;
bool startupComplete = false;

uint32_t nextBrightnessChange = 0;
const uint8_t brightnessDelay = 10; // Milliseconds between brightness changes

Adafruit_NeoPixel pixels(NUMPIXELS, LED_STRIP_PIN, NEO_RGB + NEO_KHZ800);

LightMode lightMode = LIGHT_MODE_OFF;
uint8_t lightBrightness = 0;
uint8_t lightTargetBrightness = 255;


uint8_t lightColor[3] = {255, 255, 255};
uint8_t manualLEDColor[3] = {255, 255, 255};
uint8_t manualLEDPosition = 0;

bool cabinetPowerState = LOW;
bool cabinetTargetPowerState = LOW;

void setCabinetPower(bool newState)
{
    if (cabinetPowerState == LOW && newState == HIGH)
    {
        digitalWrite(relay_PIN2, HIGH); // 12V
        delay(250);
        digitalWrite(relay_PIN3, HIGH); // 5V
        delay(250);
        digitalWrite(relay_PIN1, HIGH); // 240V
        cabinetPowerState = HIGH;
    }
    else if (cabinetPowerState == HIGH && newState == LOW)
    {
        digitalWrite(relay_PIN1, LOW);
        digitalWrite(relay_PIN2, LOW);
        digitalWrite(relay_PIN3, LOW);
        cabinetPowerState = LOW;
    }
    lastPowerChange = millis();
}

uint8_t registerParser(uint8_t i2cRegister, uint8_t position, uint8_t data)
{
    switch (i2cRegister)
    {
        case i2c_LIGHT_BRIGHTNESS:
            lightTargetBrightness = data;
            return 0;

        case i2c_LIGHT_COLOR:
            if (position >= 1 && position <= 3)
                lightColor[position-1] = data;

            if (position < 3)
                return position+1;
            else
                return 0;

        case i2c_LIGHT_MODE:
            switch (data)
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
            return 0;

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

        case i2c_LIGHT_OFF:
            lightMode = LIGHT_MODE_OFF;
            return 0;

        default:
            return 0;
    }
    return 0;
}

void setup()
{
    pinMode(ONBOARD_LED_PIN, OUTPUT);
    pinMode(PI_POWEROFF_PIN, INPUT_PULLDOWN);
    pinMode(PLAYER1_BUTTON_OUTPUT_PIN, OUTPUT);

    player1Button.begin();

    pinMode(relay_PIN1, OUTPUT);
    pinMode(relay_PIN2, OUTPUT);
    pinMode(relay_PIN3, OUTPUT);
    pinMode(relay_PIN4, OUTPUT);

    digitalWrite(ONBOARD_LED_PIN, HIGH);
    digitalWrite(PLAYER1_BUTTON_OUTPUT_PIN, LOW);
    digitalWrite(relay_PIN1, LOW);
    digitalWrite(relay_PIN2, LOW);
    digitalWrite(relay_PIN3, LOW);
    digitalWrite(relay_PIN4, LOW);

	Wire.begin(0x0a);

    pixels.begin();
    pixels.clear();
    pixels.setBrightness(lightBrightness);
}

void loop()
{
    static uint8_t i2cRegister = 0;
    static uint8_t i2cReadPosition = 0;
    static uint32_t i2cLastAvailable = 0;
    static uint16_t lightIndex = 0;

    // TURN OFF ONBOARD LED ONCE UPTIME IS GREATER THEN 5 SECONDS
    if (!startupComplete && millis() > 5000)
    {
        digitalWrite(ONBOARD_LED_PIN, LOW);
        startupComplete = true;
    }

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
            if (lightMode != LIGHT_MODE_OFF)
                lightMode = LIGHT_MODE_OFF;
            if (lightBrightness == 0)
                setCabinetPower(LOW);
        }
    }

    // MANAGE PLAYER 1 START BUTTON
    player1Button.read();

    if (cabinetPowerState == HIGH)
    {
        if (player1Button.isPressed() != player1ButtonState)
        {
            player1ButtonState = player1Button.isPressed();
            digitalWrite(PLAYER1_BUTTON_OUTPUT_PIN, player1ButtonState ? HIGH : LOW);
        }

        if (player1Button.pressedFor(10000))
            cabinetTargetPowerState = LOW;
    }
    else if (cabinetPowerState == LOW)
    {
        if (player1Button.pressedFor(1000) && !player1Button.pressedFor(10000))
            cabinetTargetPowerState = HIGH;
    }

    // DETECT PI POWEROFF - dtoverlay=gpio-poweroff
    if (cabinetPowerState == HIGH && digitalRead(PI_POWEROFF_PIN) == HIGH)
        cabinetTargetPowerState = LOW;

    // HELP IF I2C DATA IS INCOMPLETE
    if (i2cLastAvailable != 0 && millis() > (i2cLastAvailable+100))
    {
        i2cLastAvailable = 0;
        i2cReadPosition = 0;
    }

    // MANAGE I2C MESSAGES
    while (Wire.available())
    {
        i2cLastAvailable = millis();
        byte data = Wire.read();

        if (i2cReadPosition == 0)
        {
            i2cRegister = data;
            i2cReadPosition++;
        }
        else
        {
            i2cReadPosition = registerParser(i2cRegister, i2cReadPosition, data);
        }
    }
	
    // MANAGE LEDS
    if (cabinetPowerState == HIGH)
    {
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
            if (lightBrightness > 0)
            {
                lightBrightness--;
                pixels.setBrightness(lightBrightness);
            }
            else
            {
                pixels.clear();
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
        else if (lightMode == LIGHT_MODE_MANUAL)
        {
            // DO NOTHING
        }
        pixels.show();
    }
}