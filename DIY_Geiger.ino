#include "ESP8266WiFi.h"
#include <SPI.h>
#include <Wire.h>
#include <PolledTimeout.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define FREQUENCY   	80	  					// valid 80, 160

#define INPUT_PIN		14						// GPIO14 = D5

#define HAS_DISPLAY		1
#define USE_LED			0

#if HAS_DISPLAY
// Declaration for SSD1306 display connected using software SPI (default case):
#define SCREEN_WIDTH 	128 					// OLED display width, in pixels
#define SCREEN_HEIGHT 	32 						// OLED display height, in pixels

#define SDA_PIN 4
#define SCL_PIN 5
const int16_t I2C_SLAVE = 0x3C;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

// radioactivity sign as indicator for received pulse
static const unsigned char sign[] PROGMEM =
{
	0x01, 0x00, 0x00, 0x80, 0x03, 0x80, 0x01, 0xC0, 0x07, 0x80, 0x01, 0xE0, 0x0F, 0xC0, 0x03, 0xF0,
	0x1F, 0xC0, 0x03, 0xF8, 0x1F, 0xE0, 0x07, 0xF8, 0x3F, 0xF0, 0x0F, 0xFC, 0x3F, 0xF0, 0x0F, 0xFC,
	0x7F, 0xF8, 0x1F, 0xFE, 0x7F, 0xF8, 0x1F, 0xFE, 0x7F, 0xF8, 0x1F, 0xFE, 0xFF, 0xF1, 0x8F, 0xFF,
	0xFF, 0xF3, 0xCF, 0xFF, 0xFF, 0xE7, 0xE7, 0xFF, 0xFF, 0xE7, 0xE7, 0xFF, 0x00, 0x07, 0xE0, 0x00,
	0x00, 0x07, 0xE0, 0x00, 0x00, 0x03, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x20, 0x00,
	0x00, 0x07, 0xE0, 0x00, 0x00, 0x0F, 0xF0, 0x00, 0x00, 0x0F, 0xF0, 0x00, 0x00, 0x1F, 0xF8, 0x00,
	0x00, 0x1F, 0xF8, 0x00, 0x00, 0x3F, 0xFC, 0x00, 0x00, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xFE, 0x00,
	0x00, 0x7F, 0xFE, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x3F, 0xFC, 0x00, 0x00, 0x0F, 0xF0, 0x00
};

#endif

#define LOG_PERIOD 		15000  					// Logging period in milliseconds, recommended value 15000-60000.
#define MAX_PERIOD 		60000					// Maximum logging period without modifying this sketch
#define LOG_LIMIT		  100					// process at more than 100 counts

unsigned long counts;							//variable for GM Tube events
unsigned long cpm;								//variable for CPM
unsigned int multiplier;  						//variable for calculation CPM in this sketch
unsigned long previousMillis;					//variable for time measurement
double factor = 0.00812037;  					// J305ß Geiger
double usievert;			 					// Analog meas value
long value[] =
{ 10, 10, 10, 10, 10, 10, 10, 10 };  	 		// array for averaging (last 8 measurements)
int vindex = 0;									// index for array
long cpmsum;									// cpm calc.
long pulse_millis = 0;
bool was_impulse = false, first_run = true;
bool log_limit_reached = false;
unsigned long logMillis, startMillis;

void ICACHE_RAM_ATTR tube_impulse()
{					   							// subprocedure for capturing events from Geiger
	counts++;
	was_impulse = true;
}

void setup()
{							   					// setup subprocedure
	WiFi.forceSleepBegin();						// turn off ESP8266 RF
	delay(1);									// give RF section time to shutdown
	system_update_cpu_freq(FREQUENCY);

	counts = 0;
	cpm = 0;
	multiplier = MAX_PERIOD / LOG_PERIOD;	  	// calculating multiplier, depend on your log period
	Serial.begin(115200);
#if USE_LED
	pinMode(LED_BUILTIN, OUTPUT);   			// Initialize the LED_BUILTIN pin as an output
	digitalWrite(LED_BUILTIN, HIGH);
#endif
	pinMode(INPUT_PIN, INPUT);
	attachInterrupt(digitalPinToInterrupt(INPUT_PIN), tube_impulse, FALLING); // define external interrupts
#if	HAS_DISPLAY
	Wire.begin(SDA_PIN, SCL_PIN); // join i2c bus (address optional for master)
	// SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
	if (!display.begin(SSD1306_SWITCHCAPVCC, I2C_SLAVE))
	{
		Serial.println(F("SSD1306 allocation failed"));
		for (;;)
			; // Don't proceed, loop forever
	}

	// Clear the buffer
	display.clearDisplay();
	display.setTextSize(2);      				// Normal 1:2 pixel scale
	display.setTextColor(SSD1306_WHITE); 		// Draw white text
	display.cp437(true);         				// Use full 256 char 'Code Page 437' font
	display.setCursor(34, 0);     				// Start at top-left corner
	display.print(F("Geiger-"));
	display.setCursor(34, 16);     				// Start at top-left corner
	display.print(F("counter"));
	display.drawRect(12, 0, 8, 32, SSD1306_WHITE);
	display.display();
#endif
	previousMillis = millis();
	logMillis = previousMillis;
	startMillis = previousMillis;
}

void loop()										// main cycle
{
	unsigned long currentMillis = millis();
	int j;
	char tstr[8];

	if (was_impulse)
	{
		if(counts > LOG_LIMIT)
		{
			counts *= LOG_PERIOD / (currentMillis - logMillis);
			log_limit_reached = true;
		}
	}
	if (log_limit_reached || ((currentMillis - previousMillis) > LOG_PERIOD))
	{
		first_run = false;
		previousMillis = currentMillis;
		logMillis = currentMillis;
		log_limit_reached = false;
		cpm = counts * multiplier;			   	// multiplier correction
		counts = 0;
		value[vindex] = cpm;			   		// ring array load value
		vindex = ++vindex % 8;
		cpmsum = 0;			   					// summary value reset
		for (j = 0; j < 8; j++)			   		// averaging from last 8 meas.
		{
			cpmsum += value[j];
		}
		cpmsum >>= 3;

		usievert = cpmsum * factor;				// correction with pipe spectific factor, see:
												// https://www.ob121.com/doku.php?id=de:arduino:radioactivity#cpm_-_sievert_conversion
		Serial.print(F("CPM: "));
		Serial.print(cpm);
		Serial.print(F(", CPM long: "));
		Serial.print(cpmsum);
		Serial.print(F(", dosis: "));
		Serial.print(usievert);
		Serial.println(F(" µSv / h"));
#if	HAS_DISPLAY
		display.writeFillRect(34, 0, 128-34, 32, SSD1306_BLACK);
		display.setTextSize(2);      				// Normal 1:1 pixel scale
		display.setCursor(34, 0);     				// Start at top-left corner
		sprintf(tstr, "%5d", cpm);
		display.print(tstr);
		display.setTextSize(1);      				// Normal 1:1 pixel scale
		display.setCursor(92, 8);     				// Start at top-left corner
		display.print(F(" cpm"));
		display.setTextSize(2);      				// Normal 1:1 pixel scale
		display.setCursor(34, 18);     				// Start at top-left corner
		j = 3;
		while(j && ((usievert / pow(10, 3 -j)) > 10.0))
		{
			j--;
		}
		display.print(usievert, j);
		display.setTextSize(1);      				// Normal 1:1 pixel scale
		display.setCursor(92, 24);     				// Start at top-left corner
		display.print(F(" uSv/h"));
#endif
	}
	if(! first_run)
	{
		if (was_impulse)
		{
#if USE_LED
			digitalWrite(LED_BUILTIN, LOW);
#endif
#if	HAS_DISPLAY
			display.drawBitmap(0, 0, sign, 32, 32, SSD1306_WHITE);
			display.display();
#endif
			pulse_millis = currentMillis;
			was_impulse = false;
		}
		if (pulse_millis && ((currentMillis - pulse_millis) > 100))
		{
#if USE_LED
			digitalWrite(LED_BUILTIN, HIGH);
#endif
#if	HAS_DISPLAY
			display.writeFillRect(0, 0, 32, 32, SSD1306_BLACK);
			display.display();
#endif
			pulse_millis = 0;
		}
	}
#if	HAS_DISPLAY
	else
	{
		display.writeFillRect(12, 31 - (30UL * (currentMillis - startMillis)) / LOG_PERIOD, 8, ((30UL * (currentMillis - startMillis)) / LOG_PERIOD), SSD1306_WHITE);
		display.display();
		delay(10);
	}
#endif
}
