#include <Adafruit_NeoPixel.h>
#include <Wire.h>

#define PIN        2
#define NUMPIXELS 67

typedef enum
{
    LIGHT_MODE_OFF = 0,
    LIGHT_MODE_SOLID = 1,
    LIGHT_MODE_RAINBOW = 2,
    LIGHT_MODE_RAINBOW_SOLID = 3,
} LightMode;

typedef enum
{
    i2c_LIGHT_BRIGHTNESS = 0x01,
    i2c_LIGHT_MODE_SOLID = 0x10,
    i2c_LIGHT_MODE_RAINBOW = 0x20,
    i2c_LIGHT_MODE_RAINBOW_SOLID = 0x30,
    i2c_LIGHT_OFF = 0xff,
};

Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

LightMode lightMode = LIGHT_MODE_OFF;
uint8_t lightBrightness = 10;
uint8_t lightColor[3] = {255, 255, 255};

uint8_t registerParser(uint8_t i2cRegister, uint8_t position, uint8_t data)
{
    switch (i2cRegister)
    {
        case i2c_LIGHT_BRIGHTNESS:
            lightBrightness = data;
            pixels.setBrightness(lightBrightness);
            return 0;

        case i2c_LIGHT_MODE_SOLID:
            lightMode = LIGHT_MODE_SOLID;
            if (position >= 1 && position <= 3)
                lightColor[position-1] = data;
            
            if (position < 3)
                return position+1;
            else
                return 0;
        
        case i2c_LIGHT_MODE_RAINBOW:
            lightMode = LIGHT_MODE_RAINBOW;
            return 0;
        
        case i2c_LIGHT_MODE_RAINBOW_SOLID:
            lightMode = LIGHT_MODE_RAINBOW_SOLID;
            return 0;

        case i2c_LIGHT_OFF:
            lightMode = LIGHT_MODE_OFF;

        default:
            return 0;
    }
}

void setup()
{
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

    if (i2cLastAvailable != 0 && millis() > (i2cLastAvailable+100))
    {
        i2cLastAvailable = 0;
        i2cReadPosition = 0;
    }

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
	
    if (lightMode == LIGHT_MODE_OFF)
    {
        pixels.clear();
    }
    else if (lightMode == LIGHT_MODE_SOLID)
    {
        pixels.clear();
        for (uint8_t i = 0; i < NUMPIXELS; i++)
            pixels.setPixelColor(i, lightColor[0], lightColor[1], lightColor[2]);
        }
    else if (lightMode == LIGHT_MODE_RAINBOW)
    {
        lightIndex += 10;
        pixels.rainbow(lightIndex, 1);
    }
    else if (lightMode == LIGHT_MODE_RAINBOW_SOLID)
    {
        lightIndex += 10;
        uint32_t color = pixels.ColorHSV(lightIndex, 255, 255);
        for (uint8_t i = 0; i < NUMPIXELS; i++)
            pixels.setPixelColor(i, color);
    }
    pixels.show();
}