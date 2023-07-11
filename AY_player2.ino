/////////////////////////////////////////////////////////////
//0.96" 128x32 i2c OLED.  SDA=A4, SCL=A5                   //
/////////////////////////////////////////////////////////////
//Based on https://gist.github.com/anteo/0e5d8867df7568a6523d54e19983d8e0
//july 23 - edit to match pcb

#include <SPI.h>
#include "SdFat.h"
#include "SSD1306Ascii.h"
#include "SSD1306AsciiAvrI2c.h"
#define I2C_ADDRESS 0x3C
//#define RST_PIN -1
SSD1306AsciiAvrI2c oled;

#define pinClock    3  // AY38910 clock PD3
#define pinReset    2  // AY38910 reset PD2
#define pinBC1      8  // AY38910 BC1   PB0
#define pinBDIR     9  // AY38910 BDIR  PB1
#define pinSHCP     4  // 74HC595 clock PD4
#define pinSTCP     5  // 74HC595 latch PD5
#define pinDS       6  // 74HC595 data  PD6
#define pinSkip     A3 // skip to next random song PC3
#define pinPause    A0 // pause A0 = 14
#define pinCS       10 // SD card select  PB2

const char *supportedFileExt = ".psg";
const byte fileExtLen = strlen(supportedFileExt);
const int bufSize = 300;
enum AYMode {INACTIVE, WRITE, LATCH};
SdFat sd;
SdFile fp;
SdFile dir;
byte volumeA;
byte volumeB;
byte volumeC;
byte playBuf[bufSize];
bool playbackFinished = true;
bool playbackSkip;
int filesCount;
int fileNum;
int loadPos;
int playPos;
int skipCnt;
int totalPos;
float globalVolume = 1;
bool pause = false;
bool keyPressed = false;
byte scrl = 0;
bool scrlDir = 0;

void setup() {
  Serial.begin(115200);
  pinMode(pinBC1, OUTPUT);
  pinMode(pinBDIR, OUTPUT);
  pinMode(pinReset, OUTPUT);
  pinMode(pinClock, OUTPUT);
  pinMode(pinSHCP, OUTPUT);
  pinMode(pinSTCP, OUTPUT);
  pinMode(pinDS, OUTPUT);
  pinMode(pinSkip, INPUT_PULLUP);
  pinMode(pinPause, INPUT_PULLUP);

  unsigned long seed = seedOut(31);
  randomSeed(seed);

  Serial.println(F("AY-player2, OLED begin."));
  oled.begin(&Adafruit128x32, I2C_ADDRESS);
  oled.setFont(Adafruit5x7);
  delay(100);
  oled.clear();
  oled.println(F("AY-player2"));
  oled.print(F("SD init... "));
  initSD();

  cli();                           //Timer setup
  TCCR1A = 0;
  TCCR1B = _BV(WGM12) | _BV(CS12);
  TIMSK1 = _BV(OCIE1A);
  TCNT1 = 0;
  OCR1A = 1250;
  sei();

  const int PERIOD = 9;            // 9 CPU cycles ~ 1.778 MHz
  TCCR2B = 0;                      // stop timer
  TCNT2 = 0;                       // reset timer
  TCCR2A = _BV(COM2B1)             // non-inverting PWM on OC2B
           | _BV(WGM20)            // fast PWM mode, TOP = OCR2A
           | _BV(WGM21);           // ...ditto
  TCCR2B = _BV(WGM22) | _BV(CS20); // ...ditto
  OCR2A = PERIOD - 1;
  OCR2B = PERIOD / 2 - 1;

  resetAY();
  loadRandomFile();
}

unsigned int bitOut(void) {
  static unsigned long firstTime = 1, prev = 0;
  unsigned long bit1 = 0, bit0 = 0, x = 0, port = 6, limit = 99;
  if (firstTime)
  {
    firstTime = 0;
    prev = analogRead(port);       //ADC6 - pin19
  }
  while (limit--)
  {
    x = analogRead(port);
    bit1 = (prev != x ? 1 : 0);
    prev = x;
    x = analogRead(port);
    bit0 = (prev != x ? 1 : 0);
    prev = x;
    if (bit1 != bit0)
      break;
  }
  return bit1;
}

unsigned long seedOut(unsigned int noOfBits) {
  // return value with 'noOfBits' random bits set
  unsigned long seed = 0;
  for (int i = 0; i < noOfBits; ++i)
    seed = (seed << 1) | bitOut();
  return seed;
}

void loop() {
  if (pause == false) loadNextByte();
  else {
    if (scrlDir == 0) scrl++;
    if (scrlDir == 1) scrl--;
    if (scrl == 100) scrlDir = 1;
    if (scrl == 0) scrlDir = 0;
    oled.setCursor(scrl, 24);
    oled.print(F("Pause"));
    delay(3000);                  //respectively to the high timer speed!
    oled.setCursor(scrl, 24);
    oled.print(F("     "));
  }

  if (playbackFinished) {
    resetAY();
    loadRandomFile();
  }

  if (digitalRead(pinPause) == LOW && pause == false && keyPressed == false) {
    setVolume(0);
    keyPressed = true;
    cli();
    oled.setCursor(0, 24);
    oled.print(F("                      "));
    pause = true;
  }
  if (digitalRead(pinPause) == LOW && pause == true && keyPressed == false) {
    keyPressed = true;
    pause = false;
    sei();
    setVolume(1);
  }

  if (digitalRead(pinPause) == HIGH && keyPressed == true) {
    delay(250);
    keyPressed = false;
  }
}


void initSD() {
  if (!sd.begin(pinCS, SD_SCK_MHZ(50))) {
    oled.println(F("error!"));
    oled.println(" ");
    oled.println(F("See serial for errors"));
    sd.initErrorHalt();
  }
  oled.print("OK!");
  delay(666);
  loadDirectory();
}

void loadRandomFile() {
  SdFile file;
  //Serial.println("Srf.");
  fileNum = random(0, filesCount); //-1
  int n = fileNum;
  dir.close();
  if (!dir.open("/", O_READ)) {
    sd.errorHalt("Orf");
  }
  while (file.openNext(&dir, O_READ)) {
    if (checkFile(file)) {
      if (n <= 0) {
        playFile(file);
        return;
      }
      n--;
    }
    file.close();
  }
  sd.errorHalt("Nmf.");
}

void playFile(SdFile entry) {
  fp.close();
  fp = entry;
  // Serial.print("P");
  // fp.printName(&Serial);
  // Serial.println("...");
  loadPos = playPos = totalPos = 0;

  oled.clear();
  oled.print(F("File: "));
  char buf[4];
  sprintf (buf, "%03d", fileNum + 1);
  oled.print(buf);
  oled.print(" of ");
  sprintf (buf, "%03d", filesCount);
  oled.println(buf);
  oled.print(F("Name: "));
  char name[15];
  fp.getName(name, 15);
  oled.println(name);
  oled.print(F("Size: "));
  oled.print(fp.fileSize());
  oled.println(F(" bytes"));

  while (fp.available()) {
    byte b = fp.read();
    if (b == 0xFF) break;
  }
  playbackFinished = false;
}

bool checkFile(SdFile entry) {
  char name[256];
  entry.getName(name, 255);
  return !entry.isHidden() && !entry.isDir() && (strlen(name) > fileExtLen &&
         !strcasecmp(name + strlen(name) - fileExtLen, supportedFileExt));
}

void loadDirectory() {
  SdFile file;
  if (!dir.open("/", O_READ)) {
    sd.errorHalt("Open root failed");
  }
  while (file.openNext(&dir, O_READ)) {
    if (checkFile(file)) {
      filesCount++;
    }
    file.close();
  }
}

void resetAY() {
  setAYMode(INACTIVE);
  volumeA = volumeB = volumeC = 0;
  globalVolume = 1;
  digitalWrite(pinReset, LOW);
  delay(50);
  digitalWrite(pinReset, HIGH);
  delay(50);
}

void setAYMode(AYMode mode) {
  switch (mode) {
    case INACTIVE:
      PORTB &= B11111100;
      break;
    case WRITE:
      PORTB |= B00000010;
      break;
    case LATCH:
      PORTB |= B00000011;
      break;
  }
}

void setVolume(float volume) {
  globalVolume = volume;
  writeAY(8, volumeA);
  writeAY(9, volumeB);
  writeAY(10, volumeC);
}

void writeAY(byte port, byte data) {
  if (port == 8 || port == 9 || port == 10) {
    if (port == 8) volumeA = data;
    if (port == 9) volumeB = data;
    if (port == 10) volumeC = data;
    data = (byte)(data * globalVolume);
  }
  setAYMode(INACTIVE);
  digitalWrite(pinSTCP, LOW);
  shiftOut(pinDS, pinSHCP, MSBFIRST, port);
  digitalWrite(pinSTCP, HIGH);
  setAYMode(LATCH);
  setAYMode(INACTIVE);
  digitalWrite(pinSTCP, LOW);
  shiftOut(pinDS, pinSHCP, MSBFIRST, data);
  digitalWrite(pinSTCP, HIGH);
  setAYMode(WRITE);
  setAYMode(INACTIVE);
}

bool loadNextByte() {
  if (loadPos == playPos - 1 || loadPos == bufSize - 1 && playPos == 0)
    return false;
  byte b = fp.available() ? fp.read() : 0xFD;
  playBuf[loadPos++] = b;
  if (loadPos == bufSize) loadPos = 0;
  return true;
}

bool isNextByteAvailable() {
  return playPos != loadPos;
}

byte getNextByte() {
  if (!isNextByteAvailable()) return 0;
  byte b = playBuf[playPos++];
  if (playPos == bufSize) playPos = 0;
  totalPos++;
  return b;
}

void displayOLED() {
  if (playbackFinished) return;
  oled.setCursor(0, 24);
  oled.print(F("                      "));
  oled.setCursor(volumeC / 1.25, 24);
  oled.print(">");
  oled.setCursor((122 - volumeA / 1.25), 24);
  oled.print("<");
  oled.setCursor((62 + volumeB / 1.5), 24);
  oled.print("]");
  oled.setCursor((57 - volumeB / 1.5), 24);
  oled.print("[");
}


void playNotes() {
  if (digitalRead(pinSkip) == LOW)
    playbackSkip = true;
  else if (playbackSkip) {
    playbackSkip = false;
    playbackFinished = true;
  }
  if (playbackFinished || --skipCnt > 0)
    return;
  int oldPlayPos = playPos;
  int oldTotalPos = totalPos;
  while (isNextByteAvailable()) {
    byte b = getNextByte();
    if (b == 0xFF) {
      break;
    } else if (b == 0xFD) {
      playbackFinished = true;
      break;
    } else if (b == 0xFE) {
      if (isNextByteAvailable()) {
        skipCnt = getNextByte();
        skipCnt *= 4;
        break;
      }
    } else if (b <= 0xFC) {
      if (isNextByteAvailable()) {
        byte v = getNextByte();
        if (b < 16) writeAY(b, v);
      }
    }
  }
  if (!isNextByteAvailable()) {
    playPos = oldPlayPos;
    totalPos = oldTotalPos;
  }
}

ISR(TIMER1_COMPA_vect) {
  displayOLED();
  playNotes();
}
