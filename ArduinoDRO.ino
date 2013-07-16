/////////////////////////////////////////////////////////////////////////
//  AdruinoDRO.ino
const char date[13]="7-14-2013";
const char version[6]="3.10";
//  Chris Kelley
//  Interfaces Arduino with a glass scale from DROPros.com on the timing belt
//  length measuring bench and outputs results to Adafruit 1.8" TFT live.
/////////////////////////////////////////////////////////////////////////
//  v2.0a - Start of code for use with glass scale from DROPros.com...
//  v2.1 - implemented dual D flip-flops on quadrature output to simplify the
//    ISRs for reading changes. Input still too fast for Arduino to keep up.
//  v3.0a - Start of  using LS7166 Quadrature counter IC to handle the accounting on the
//      encoder's position.
//  v3.10 - Steady initial release of timing belt measuring bench code.

//  DON'T FORGET TO PUT THE ARDUINO ITSELF INTO "UPLOAD MODE!!"
//
//  If you do forget, you'll need to lift the Arduino out of the rubber lid holder
//  and then remove the ship from the socket. NOW you'll be able to reload the
//  software.

// DRO / measuring bench
// Quadrature counter decoder LS7166 control lines
byte ic_nRD = 14;	//A0;   // 14
byte ic_CnD = 15;	//A1;   // 15
byte ic_nWR = 16;	//A2;   // 16
byte ic_nCS = 18;	//A4;   // 18
//  Note that data lines are D0 through D7, accessed as data port D

// Center-to-center minimum length MUST be remeasured if center block's bolts loosened or removed!
// When initialized or zero'd the readout would say 8.616
// added 3M rubber feet to the sliding block so that it wouldn't "slam" into the
// fixed block and 8.616 (218.851mm) minimum c-2-c changed to 8.710
// Now much change C2CMINMICRON from 218840 to 0.094" thicker, so add
// 2388 microns to it and use 221228
// C2CMINMICRON is actually 221238, but initial setting has error
//	error is about 8~10 microns for when there is no rubber pad and blocks can touch
//  W/rubber pad, this initial set error is somewhat epic and instead of 221228, it's 221193
#define C2CMINMICRON 221193
#define LS7166INISET 0
unsigned long EncoderCount = 0; // this is current encode position read from LS7166


/////////////////////////////////////////////////////////////////////////
// Adafruit 1.8TFT w/joystick 160x128 in landscape (128x160)
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SD.h>
#include <SPI.h>
// pinout for Mega: http://forums.adafruit.com/viewtopic.php?uid=115205&f=31&t=36738&start=0
const byte ADA_SCLK	=13;
const byte ADA_MOSI	=11;
const byte ADA_CS	=10;
const byte ADA_DC	=8;
const byte ADA_RST	=-1;
// you can also connect this to the Arduino reset
const byte ADA_JOYSTICK=3;  // analog pin 3 for button input
//  CKCK :: I didn't even bother solding this header onto the ADA 1.8TFT shield
//  #define SD_CS 4  // SD card chip select digital pin 4  

//  Joystick directions
// Works with our screen rotation, yaay
#define Neutral 0
#define Press 1
#define Up 2
#define Down 3
#define Right 4
#define Left 5
const char* Buttons[]={"Neutral", "Press", "Up", "Down", "Right", "Left" };
byte button=Neutral;
byte checkJoystick(void);
// TFT Color definitions
#define BLACK    0x0000
#define BLUE     0x001F
#define RED      0xF800
#define GREEN    0x07E0
#define CYAN     0x07FF
#define MAGENTA  0xF81F
#define ORANGE   0xFA60
#define YELLOW   0xFFE0 
#define WHITE    0xFFFF
#define GRAY     0xCCCC

// TFT Globals
// Hardware SPI
Adafruit_ST7735 tft = Adafruit_ST7735(ADA_CS, ADA_DC, ADA_RST);
// Software SPI
// Adafruit_ST7735 tft = Adafruit_ST7735(ADA_CS, ADA_DC, ADA_MOSI, ADA_SCLK, ADA_RST);

//A3 used by joystick!!    //17


/////////////////////////////////////////////////////////////////////////
//  Belt variables
float beltDiameter = 55.7000;  // diameter of belt cords on sprocket, in mm measured by hand
// ----------------------------------------------------------------------
const int beltTeeth[]={68,    70,   72,   88,     89,   93,     95};
const float beltMin[]={9.28,  9.67, 10.04,13.07,  13.1, 13.95,  14.350};
const float beltMax[]={68,    70,   72,   88,     89,   14.01,  14.350};
// ----------------------------------------------------------------------
//                        68            70              72        88        89       93       95
const char *beltNames[]={"TB800, TBF1", "TB900, TB796", "TB1100", "TB1198", "TB999", "TBST4", "TB996 TB748R"};
/////////////////////////////////////////////////////////////////////////
byte diaX = 0;
byte diaY = 0;

void setup() {
	pinMode(ic_nRD, OUTPUT);
	pinMode(ic_CnD, OUTPUT);
	pinMode(ic_nWR, OUTPUT);
	pinMode(ic_nCS, OUTPUT);

	digitalWrite(ic_nRD, HIGH);
	digitalWrite(ic_CnD, HIGH);
	digitalWrite(ic_nWR, HIGH);
	digitalWrite(ic_nCS, HIGH);

	//  Data port manipulation
	// from: http://www.arduino.cc/en/Reference/PortManipulation
	// and also http://hekilledmywire.wordpress.com/2011/02/23/direct-port-manipulation-using-the-digital-ports-tutorial-part-3/
	DDRD = 0; // sets Arduino Digital pins 0-7 all as inputs

	// If your TFT's plastic wrap has a Red Tab, use the following:
	tft.initR(INITR_REDTAB);   // initialize a ST7735R chip, red tab
	// The rotation parameter can be
	// 0, portrait mode, with the USB jack at the bottom left
	// 1, landscape (wide) mode, with the USB jack at the bottom right
	// 2, sets the display to a portrait (tall) mode, with the USB jack at the top right
	// 3, also landscape, but with the USB jack at the top left
	tft.setRotation(1);
	tft.fillScreen(BLACK);
	tft.setTextSize(2); //  1 = 5x8, 2 = 10x16; chars leave blank pixel on bottom
	// set up the foundation for our scale's reading
	tft.print("Is block all\n");
	tft.print("the way to\nthe RIGHT?\n");
	tft.setTextColor(GRAY);
	tft.print("Click black\nbutton\nwhen ready!\n");
	tft.setTextSize(1);
	tft.print( F("Dont slam the block right! CAREFULLY slide it...") );
	int button=Neutral;
	while( button == Neutral )
		button = checkJoystick();
	EncoderCount=0;
	tft.setTextSize(1);
	tft.setCursor(0, 110);
	char text[35];
	sprintf(text, "%s Button!", Buttons[button] );
	tft.print(text);
	tft.fillScreen(BLACK);
	//  NO SERIAL PORT!!! We're using D0:D7 for Data Port D input
	//  Serial.begin(9600);
	init_7166();
}
/////////////////////////////////////////////////////////////////////////
void cdcs_7166(boolean CnD, boolean nCS ) {
	digitalWrite(ic_CnD,CnD);
	digitalWrite(ic_nCS,nCS);
	delayMicroseconds(50);
}
/////////////////////////////////////////////////////////////////////////
//	ctrl_7166() formerly set cdcs(1,0), however let's move that to the caller
void ctrl_7166( byte control ){ 
	DDRD = 0xff; // sets Arduino Digital pins 0-7 all as outputs
	PORTD = control;
    latchWR_7166();
}
/////////////////////////////////////////////////////////////////////////
//  sends pulse after the command to tell the 7166 to latch in the data
void latchWR_7166(){
	delayMicroseconds(20);
	digitalWrite(ic_nWR,0);
	delayMicroseconds(20);
	digitalWrite(ic_nWR,1);
	delayMicroseconds(20);
}
/////////////////////////////////////////////////////////////////////////
//  this function ASSumes you already handled CnD, nCS before hand!!!
void write_7166(unsigned long Data ){
	DDRD = 0xff; // sets Arduino Digital pins 0-7 all as outputs
	PORTD =  (unsigned char)Data;
    latchWR_7166();
    
	Data >>= 8;
	PORTD =  (unsigned char)Data;
    latchWR_7166();

	Data >>= 8;
	PORTD =  (unsigned char)Data;
    latchWR_7166();
}
/////////////////////////////////////////////////////////////////////////
// Control registers are top two bits of the byte, MCR = 0
#define ICR	0x40
#define OCCR 0x80
#define QR 0xC0
void init_7166() {
	EncoderCount = 0;
    DDRD = 0xff; // sets Arduino Digital pins 0-7 all as outputs
	digitalWrite(ic_nRD,1);
	delayMicroseconds(20);
	cdcs_7166(1,0); // C/D /CS have to be 1,0 to write to MCR, ICR, OCCR, QR & to read OSR
	// from http://hades.mech.northwestern.edu/index.php/Using_the_LS7166_Quadrature_Counter
	ctrl_7166(0x20);    //Performs master reset
	ctrl_7166(0x04);   //Sub-reset
	ctrl_7166(ICR|0x18);    //Enables A/B, sets up pin 3-4
	ctrl_7166(OCCR|0x34); //Divide by n mode   0x04
	ctrl_7166(QR|0x03);   //x4 quadrature mode   0x04

	// Apparently, the PR is supposed to be the max expected value
	ctrl_7166( 1 );	// tell MCR to load PR
	cdcs_7166(0,0);	// somewhat rare usage of 0,0: read OL and write PR
	write_7166( LS7166INISET );
	cdcs_7166(1,0); // C/D /CS have to be 1,0 to write to MCR
	ctrl_7166( 8 );  // xfer PR to CNTR
}

//  latchRD_7166() -- unlike latching out, which is done after, reading
//  gets the data in a signal sandwich
byte latchRD_7166(){
	byte dataread=0;
	// nRD should already be high
	digitalWrite(ic_nRD,0); // enable reading
	delayMicroseconds(50);
	dataread=PIND;
	// read 1st byte, the LSB
	delayMicroseconds(50);
	digitalWrite(ic_nRD,1);
	delayMicroseconds(50);
	return dataread;
}

//  this function ASSumes you already handled CnD, nCS before hand!!!
unsigned long read_7166( ){
	unsigned long tmp=0, Data=0;
	// Tell MCR we want to read the OL
    DDRD = 0xff; // sets Arduino Digital pins 0-7 all as outputs
	cdcs_7166(1,0); // C/D /CS have to be 1,0 to write to MCR
	ctrl_7166(3);   // send 3 to MCR, transfers CNTR to OL

	// set up the OL for reading
	cdcs_7166(0,0);
	DDRD = 0; // sets Arduino Digital pins 0-7 all as inputs
	// read 1st byte, LSB
	tmp = latchRD_7166(); // byte 0, lsB
	Data |= tmp;
	// read 2nd byte, the middle byte
	tmp = latchRD_7166(); // byte 1
	tmp = tmp << 8; // shift over 8 bits
	Data |= tmp;
	// read 3rd byte, the MSB
	tmp = latchRD_7166(); // byte 1
	tmp = tmp << 16;     // shift over 16 bits this tmp is MSB
	Data |= tmp;
	return Data;
}
/////////////////////////////////////////////////////////////////////////
void loop() {
	char text[64];   // general output display buffer
	update();
	delay(10);
	button = checkJoystick();
	if( button ) {
		tft.fillScreen(BLACK);
		tft.setTextSize(1);
		tft.setCursor(0, 110);
		sprintf(text, "%s Button!", Buttons[button] );
		tft.print(text);
		calibrate();
	}
}
/////////////////////////////////////////////////////////////////////////
void calibrate(){
	char text[64];   // general output display buffer
	tft.fillScreen(BLACK);
	delay(300);
	tft.setTextSize(2);
	tft.setCursor(0, 10);
	tft.setTextColor(WHITE);
	tft.print("Calibrate:");
	tft.setCursor(0, 28);
	tft.setTextColor(GRAY);
	//  size 2:  1234567890123 <-- if last char is ON 13, \n not req'd; driver inserts it
	tft.print(F("Put block all" \
			  	"the way RIGHT"));
	tft.setTextSize(1);
	tft.setTextColor(WHITE);
	sprintf( text, "\n  then Press %c or %c or %c\n", 0x1ae, 0x1da, 0x1af ); // 0x10 is right solid arrow
	tft.print( text );
	tft.setTextColor(GRAY);
	//  size 1:     12345678901234567890123456 <-- if last char is ON 26, \n not req'd; driver inserts it
	tft.print( F("\nDont slam the block right! CAREFULLY slide it...") );
	sprintf( text, "\n\n    use %c to prepare for\n    new software upload", 0x12 ); // 0x10 is right solid arrow
	tft.print( text );
	button = Neutral;
	while( button == Neutral )
		button = checkJoystick();
	if( button==Up || button == Down ){
		// reset the outputs nCS = 1 disables chip for read/write
		cdcs_7166(0,1);
		delay(10);
		// D1 = Tx  ->  gets a 1 for output
		// D0 = Rx  ->  gets a 0 for input
		pinMode(0, INPUT);
		delay(10);
		pinMode(1, OUTPUT);
		delay(10);
		tft.setTextSize(2);
		tft.setCursor(0, 28);
		tft.fillScreen(BLACK);
		tft.print(F("DATA Port\nreset!\nOK to upload!\n"));
		tft.setTextSize(1);
		tft.print(F("Or reset to start over :)"));
		while( 1 )
			;
	}
	else {
		if( button == Right )
			EncoderCount=218851;
		if( button == Left )
			EncoderCount=0;
		tft.setTextSize(1);
		tft.setCursor(0, 110);
		sprintf(text, "%s Button!", Buttons[button] );
		tft.print(text);
		init_7166();
	}
	tft.fillScreen(BLACK);
	update();
}
/////////////////////////////////////////////////////////////////////////
char tLc2c[15]="new";
char tLc2cPrev[15]="old";
/////////////////////////////////////////////////////////////////////////
void update( ) {
	char text[64];   // general output display buffer
	char fBuffer[15]; // used in repeated dtostrf() calls
	float position, mmLength;

	// read from LS7166
	EncoderCount=read_7166();
	EncoderCount+=C2CMINMICRON;
	//  if limit switches get installed onto the base, here's where code
	//  would go to reset the encoder's position


	// do all calculations first...
	// convert counts to mm. Would be /1000 but we're only checking ONE
	//  quadrature phase, so we're only getting 1/4 of the data
	// mmLength= (float) (long)EncoderCount/250; //

	//  EncoderCount as a variable now starts AT the minimum length!
	//  as the slide moves, the encoder counts DOWN from its max possible value
	mmLength= (float)EncoderCount/1000;  // divide microns to get mm

	position = mmLength/25.4;
	dtostrf(position,-6,3,fBuffer); // output length to string, to 0.001
	// strcmp returns 0 if strings are equal
	if( !( strcmp((const char *)fBuffer, (const char *)tLc2c) )) {
		// in here, so strings ARE EQUAL
		return;
	}
	strcpy((char *)tLc2cPrev, (const char *)tLc2c);  // strcpy(destination,source)
	strcpy((char *)tLc2c, (const char *)fBuffer);  // strcpy(destination,source)
	char tLength[12];   // belt length in mm
	char tPitch[12];
	// calc'd belt length, mmLength is C2C distance, in mm
	// convert it to belt length, in mm
	mmLength=(mmLength*2)+(beltDiameter*3.14159);
	dtostrf(mmLength,-5,2,tLength);
	//  handle teeth, divide metric length by ideal pitch of 9.525mm (3/8")
	//  and have result round to an integer
	int8_t teeth=(int)(mmLength/9.525+.5);
	// position is current belt length, in mm
	// convert it to actual pitch with decimal point to display
	mmLength=mmLength/(float)teeth;
	dtostrf(mmLength,-4,3,tPitch);

	//  format and output data to the screen
	//  Use L1 as top of cursor for L2, and so on.
	//  Doing this with variable makes shifting around rows much easier
	byte L1 = 16+2+4;  // row 1: size 2=16 + 4 padding above + 2 padding below
	byte L2 = 16+2;   // row 2: size 1= 8 + 2 padding below
	byte L3 = 16+2;  // row 3: size 2=16 + 2 padding below
	byte L4 = 16+2;   // row 4: size 1= 8 + 2 padding below
	byte L5 = 8+2;   // row 5: size 1= 8 + 4 padding below
	byte L6 = 16+2;  // row 6: size 2=16 + 2 padding below

	byte L7 = 8+2;   // row 7: size 1= 8 + 2 padding below
	byte L8 = 8+2;   // row 7: size 1= 8 + 2 padding below
	byte L9 = 8+2;   // row 7: size 1= 8 + 2 padding below
	// line 1 (0,0) size 2 to (0,16)
	tft.setCursor(0,4);
	tft.setTextColor(ST7735_RED);
	bool found=false;
	// Show belt model -- or blank if not found
	for(int idx=0; idx<10; idx++ ) {
		if( teeth == beltTeeth[idx] ){
			blankPrint( beltNames[idx], 2, BLACK );
			found=true;
			tft.setTextColor(ORANGE); // ST7735_BLUE);
		}
	}
	if( ! found ) {
		blankPrint( "-----        ", 2, BLACK );
		tft.setTextColor(ST7735_BLUE);
	}
	// line 2 (0,16) size = 1 to (0,16+8)
	tft.setCursor(0,L1); // font size 2 = 2*5 x 2*8 = 10x16
	sprintf( text, "%dT p:%s", teeth, tPitch);
	blankPrint( text, 2, BLACK );
	// line 3 (0,16+10) size = 2 to (0,16+8+16)
	tft.setCursor(0,L1+L2); // font size 2 = 2*5 x 2*8 = 10x16
	tft.setTextColor(ST7735_WHITE);
	blankPrint( (const char *)tLc2c, 2, BLACK );
	tft.setTextColor(ST7735_BLUE);
	tft.setTextSize(1);
	tft.print(" inch c-2-c");
	// line 4 (0,16+8+16) size 1 to (0,16+8+16+8)
	tft.setCursor(0,L1+L2+L3); // font size 2 = 2*5 x 2*8 = 10x16
	tft.setTextColor(GRAY);
	blankPrint( tLength, 2, BLACK );
	tft.setTextColor(ST7735_BLUE);
	tft.setTextSize(1);
	tft.print( " mm length\n" );
	// line 5 (0,16+8+16+8) size 1 to (0,16+8+16+8+8)
	tft.setCursor(0,L1+L2+L3+L4); // font size 2 = 2*5 x 2*8 = 10x16
	diaX=tft.getCursor(1);
	diaY=tft.getCursor(0);
	tft.setTextColor(ST7735_BLUE);
	dtostrf(beltDiameter,-5,2,fBuffer);
	blankPrint( fBuffer, 1, BLACK );
	sprintf( text, " mm adjust %c w/%c%c", 0xec, 0x18, 0x19 );
	tft.print( text );
	// line 6
	// tft.setCursor(0,L1+L2+L3+L4+L5);
	//      blankPrint( "good/bad\n", 2, BLACK );
	// line 7
	// tft.setCursor(0,L1+L2+L3+L4+L5+L6);
	// line 8  -- doesn't quite fit...
	tft.setCursor(0,L1+L2+L3+L4+L5+L6+L7-3);
	tft.setTextSize(1);
	tft.print( "Encoder position: " );
	sprintf( text, "%lu    ", (unsigned long)EncoderCount );
	blankPrint( text, 1, BLACK );

	// last line (0,120) size 1
	tft.setCursor(0,120); // font size 2 = 2*5 x 2*8 = 10x16
	sprintf( text, "version %s  %s\n", version, date );
	tft.setTextSize(1);
	tft.print( text );
}

/////////////////////////////////////////////////////////////////////////
//  blankPrint() does just that. Blanks out this text and then prints in
//  new text. I got mad doing this manually all over the place just to update a number
void blankPrint( const char* text, int size, int bgColor ){
	int x=tft.getCursor(1), y=tft.getCursor(0), length=strlen(text)+2;
	int width=length*5*size+2, height=8*size; // height assumes single line
	tft.fillRect(x, y, width, height, bgColor);
	tft.setTextSize(size);
	tft.print(text);
}

/////////////////////////////////////////////////////////////////////////
//  Check the joystick position - from Adafruit example somewhere
byte checkJoystick() {
	int joystickState = analogRead(ADA_JOYSTICK);
	if (joystickState < 50) return  Right;	// for rotation(3) Left;
	if (joystickState < 150) return Up;		// for rotation(3) Down;
	if (joystickState < 250) return Press;  // always in the middle ;)
	if (joystickState < 500) return Left;	// for rotation(3) Right;
	if (joystickState < 650) return Down;	// for rotation(3) Up;
	return Neutral;
}
