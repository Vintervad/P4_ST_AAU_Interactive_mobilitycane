// Example to demonstrate write latency for preallocated exFAT files.
// I suggest you write a PC program to convert very large bin files.
//
// The maximum data rate will depend on the quality of your SD,
// the size of the FIFO, and using dedicated SPI.
#include "ExFatLogger.h"
#include "FreeStack.h"
#include "SdFat.h"
#include <SPI.h>
#include "fcntl.h"
#include <FunctionalInterrupt.h>

// ---------------------- Indstilling af parametre START ----------------------

// Her vælges LED pins og de sættes til output
#define ERROR_LED_PIN 17 // Vælg pin til grøn LED
#define SUCCESS_LED_PIN 22 // Vælg pin til grøn LED
#define DATATRANSFER_LED_PIN 21 // Vælg pin til blå LED

// Her initialiserer vi trykknap til at starte script
#define BUTTON1 0 // Vælg hvilken pin vi bruger som trykknap (PIN 0 er trykknap til venstre på ESP32)

// Indstilling af filnavn og optagelsestid
int record_time_ms = 20000; // Antallet millisekunder scriptet skal optage i
const uint32_t LOG_INTERVAL_USEC = 5000; // Interval mellem samples i mikrosekunder --> f.eks. 5000µs = 200Hz
char binName[] = "SamletTest09maj_FP2_00.bin"; // Filnavn --> der tælles én op hver gang der optages, så filerne hedder "xyz00", "xyz01", "xyz02" osv.

// Her vælges størrelsen af stack'en --> sørger for at vi ikke for Guru meditation errors
SET_LOOP_TASK_STACK_SIZE(16 * 1024); // 16KB


// Her fortæller vi at det er software SPI --> 2 = software SPI
#if SPI_DRIVER_SELECT == 2  // Must be set in SdFat/SdFatConfig.h

// SD_FAT_TYPE = 0 for SdFat/File as defined in SdFatConfig.h,
// 1 for FAT16/FAT32, 2 for exFAT, 3 for FAT16/FAT32 and exFAT.
#define SD_FAT_TYPE 1

// Indstilling af pins til software SPI
const uint8_t SD_CS_PIN = 33;
const uint8_t SOFT_MISO_PIN = 27;
const uint8_t SOFT_MOSI_PIN = 26;
const uint8_t SOFT_SCK_PIN = 25;

// SdFat software SPI template
SoftSpiDriver<SOFT_MISO_PIN, SOFT_MOSI_PIN, SOFT_SCK_PIN> softSpi;

// Speed argument is ignored for software SPI.
#if ENABLE_DEDICATED_SPI
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(0), &softSpi)
#else  // ENABLE_DEDICATED_SPI
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(0), &softSpi)
#endif  // ENABLE_DEDICATED_SPI


// ---------------------- Indstilling af parametre SLUT ----------------------

int counter = 0;

class Button
{
public:
	Button(uint8_t reqPin) : PIN(reqPin){
		pinMode(PIN, INPUT_PULLUP);
	};

        void begin(){
            attachInterrupt(PIN, std::bind(&Button::isr,this), FALLING);
        }

	~Button() {
		detachInterrupt(PIN);
	}

	void ARDUINO_ISR_ATTR isr() {
		numberKeyPresses += 1;
		pressed = true;
	}

  	void checkPressed() {
		if (pressed) {
			// Serial.printf("Button on pin %u has been pressed %u times\n", PIN, numberKeyPresses);
			pressed = false;
      counter = numberKeyPresses;
		}
	}

private:
    const uint8_t PIN;
    volatile uint32_t numberKeyPresses;
    volatile bool pressed;
};

Button button1(BUTTON1);

//------------------------------------------------------------------------------

// Set USE_RTC nonzero for file timestamps.
// RAM use will be marginal on Uno with RTClib.
// 0 - RTC not used
// 1 - DS1307
// 2 - DS3231
// 3 - PCF8523
#define USE_RTC 0
#if USE_RTC
#include "RTClib.h"
#endif  // USE_RTC

// FIFO SIZE - 512 byte sectors.  Modify for your board.
#ifdef __AVR_ATmega328P__
// Use 512 bytes for 328 boards.
#define FIFO_SIZE_SECTORS 1
#elif defined(__AVR__)
// Use 2 KiB for other AVR boards.
#define FIFO_SIZE_SECTORS 4
#else  // __AVR_ATmega328P__
// Use 8 KiB for non-AVR boards.
#define FIFO_SIZE_SECTORS 16
#endif  // __AVR_ATmega328P__

// Preallocate 1GiB file.
const uint32_t PREALLOCATE_SIZE_MiB = 1024UL;

// Save SRAM if 328.
#ifdef __AVR_ATmega328P__
#include "MinimumSerial.h"
MinimumSerial MinSerial;
#define Serial MinSerial
#endif  // __AVR_ATmega328P__
//==============================================================================
// Replace logRecord(), printRecord(), and ExFatLogger.h for your sensors.
void logRecord(data_t* data, uint16_t overrun) {
  if (overrun) {
    // Add one since this record has no adc data. Could add overrun field.
    overrun++;
    data->adc[0] = 0X8000 | overrun;
  } else {
    for (size_t i = 0; i < ADC_COUNT; i++) {
      data->adc[i] = analogRead(i);
    }
  }
}
//------------------------------------------------------------------------------
void printRecord(Print* pr, data_t* data) {
  static uint32_t nr = 0;
  if (!data) {
    pr->print(F("LOG_INTERVAL_USEC,"));
    pr->println(LOG_INTERVAL_USEC);
    pr->print(F("rec#"));
    for (size_t i = 0; i < ADC_COUNT; i++) {
      pr->print(F(",adc"));
      pr->print(i);
    }
    pr->println();
    nr = 0;
    return;
  }
  if (data->adc[0] & 0X8000) {
    uint16_t n = data->adc[0] & 0X7FFF;
    nr += n;
    pr->print(F("-1,"));
    pr->print(n);
    pr->println(F(",overuns"));
  } else {
    pr->print(nr++);
    for (size_t i = 0; i < ADC_COUNT; i++) {
      pr->write(',');
      pr->print(data->adc[i]);
    }
    pr->println();
  }
}
//==============================================================================
const uint64_t PREALLOCATE_SIZE = (uint64_t)PREALLOCATE_SIZE_MiB << 20;
// Max length of file name including zero byte.
#define FILE_NAME_DIM 40
// Max number of records to buffer while SD is busy.
const size_t FIFO_DIM = 512 * FIFO_SIZE_SECTORS / sizeof(data_t);

#if SD_FAT_TYPE == 0
typedef SdFat sd_t;
typedef File file_t;
#elif SD_FAT_TYPE == 1
typedef SdFat32 sd_t;
typedef File32 file_t;
#elif SD_FAT_TYPE == 2
typedef SdExFat sd_t;
typedef ExFile file_t;
#elif SD_FAT_TYPE == 3
typedef SdFs sd_t;
typedef FsFile file_t;
#else  // SD_FAT_TYPE
#error Invalid SD_FAT_TYPE
#endif  // SD_FAT_TYPE

sd_t sd;

file_t binFile;
file_t csvFile;

//------------------------------------------------------------------------------
#if USE_RTC
#if USE_RTC == 1
RTC_DS1307 rtc;
#elif USE_RTC == 2
RTC_DS3231 rtc;
#elif USE_RTC == 3
RTC_PCF8523 rtc;
#else  // USE_RTC == type
#error USE_RTC type not implemented.
#endif  // USE_RTC == type
// Call back for file timestamps.  Only called for file create and sync().

#endif  // USE_RTC
//------------------------------------------------------------------------------
#define error(s) sd.errorHalt(&Serial, F(s))
#define dbgAssert(e) ((e) ? (void)0 : error("assert " #e))
//-----------------------------------------------------------------------------
// Convert binary file to csv file.
void binaryToCsv() {
  uint8_t lastPct = 0;
  uint32_t t0 = millis();
  data_t binData[FIFO_DIM];

  if (!binFile.seekSet(512)) {
    error("binFile.seek failed");
  }
  uint32_t tPct = millis();
  printRecord(&csvFile, nullptr);
  while (!Serial.available() && binFile.available()) {
    int nb = binFile.read(binData, sizeof(binData));
    if (nb <= 0) {
      error("read binFile failed");
    }
    size_t nr = nb / sizeof(data_t);
    for (size_t i = 0; i < nr; i++) {
      printRecord(&csvFile, &binData[i]);
    }

    if ((millis() - tPct) > 1000) {
      uint8_t pct = binFile.curPosition() / (binFile.fileSize() / 100);
      if (pct != lastPct) {
        tPct = millis();
        lastPct = pct;
        Serial.print(pct, DEC);
        Serial.println('%');
        csvFile.sync();
      }
    }
    if (Serial.available()) {
      break;
    }
  }
  csvFile.close();
  Serial.print(F("Done: "));
  
  Serial.print(0.001 * (millis() - t0));
  Serial.println(F(" Seconds"));
  digitalWrite(SUCCESS_LED_PIN,HIGH);
  delay(2000);
  digitalWrite(SUCCESS_LED_PIN,LOW);
}
//------------------------------------------------------------------------------
void clearSerialInput() {
  uint32_t m = micros();
  do {
    if (Serial.read() >= 0) {
      m = micros();
    }
  } while (micros() - m < 10000);
}
//-------------------------------------------------------------------------------
void createBinFile() {
  digitalWrite(DATATRANSFER_LED_PIN,HIGH);
  binFile.close();
  while (sd.exists(binName)) {
    char* p = strchr(binName, '.');
    if (!p) {
      error("no dot in filename");
    }
    while (true) {
      p--;
      if (p < binName || *p < '0' || *p > '9') {
        error("Can't create file name");
      }
      if (p[0] != '9') {
        p[0]++;
        break;
      }
      p[0] = '0';
    }
  }
  if (!binFile.open(binName, O_RDWR | O_CREAT)) {
    error("open binName failed");
  }
  Serial.println(binName);
  if (!binFile.preAllocate(PREALLOCATE_SIZE)) {
    error("preAllocate failed");
  }

  Serial.print(F("preAllocated: "));
  Serial.print(PREALLOCATE_SIZE_MiB);
  Serial.println(F(" MiB"));
  digitalWrite(DATATRANSFER_LED_PIN,LOW);
  delay(3000);
}
//-------------------------------------------------------------------------------
bool createCsvFile() {
  char csvName[FILE_NAME_DIM];
  if (!binFile.isOpen()) {
    Serial.println(F("No current binary file"));
    return false;
  }

  // Create a new csvFile.
  binFile.getName(csvName, sizeof(csvName));
  char* dot = strchr(csvName, '.');
  if (!dot) {
    error("no dot in filename");
  }
  strcpy(dot + 1, "csv");
  if (!csvFile.open(csvName, O_WRONLY | O_CREAT | O_TRUNC)) {
    error("open csvFile failed");
  }
  clearSerialInput();
  Serial.print(F("Writing: "));
  Serial.print(csvName);
  // Serial.println(F(" - type any character to stop"));
  return true;
}
//-------------------------------------------------------------------------------
void logData() {
  int32_t delta;  // Jitter in log time.
  int32_t maxDelta = 0;
  uint32_t maxLogMicros = 0;
  uint32_t maxWriteMicros = 0;
  size_t maxFifoUse = 0;
  size_t fifoCount = 0;
  size_t fifoHead = 0;
  size_t fifoTail = 0;
  uint16_t overrun = 0;
  uint16_t maxOverrun = 0;
  uint32_t totalOverrun = 0;
  uint32_t fifoBuf[128 * FIFO_SIZE_SECTORS];
  data_t* fifoData = (data_t*)fifoBuf;

  // Write dummy sector to start multi-block write.
  dbgAssert(sizeof(fifoBuf) >= 512);
  memset(fifoBuf, 0, sizeof(fifoBuf));
  if (binFile.write(fifoBuf, 512) != 512) {
    error("write first sector failed");
  }
  clearSerialInput();
  // Serial.println(F("Type any character to stop"));

  // Wait until SD is not busy.
  while (sd.card()->isBusy()) {
  }

  // Start time for log file.
  uint32_t m = millis();

  // Time to log next record.
  uint32_t logTime = micros();
  do {
    digitalWrite(DATATRANSFER_LED_PIN,HIGH);
    // Time for next data record.
    logTime += LOG_INTERVAL_USEC;

    // Wait until time to log data.
    delta = micros() - logTime;
    if (delta > 0) {
      Serial.print(F("delta: "));
      Serial.println(delta);
      error("Rate too fast");
    }
    while (delta < 0) {
      delta = micros() - logTime;
    }

    if (fifoCount < FIFO_DIM) {
      uint32_t m = micros();
      logRecord(fifoData + fifoHead, overrun);
      m = micros() - m;
      if (m > maxLogMicros) {
        maxLogMicros = m;
      }
      fifoHead = fifoHead < (FIFO_DIM - 1) ? fifoHead + 1 : 0;
      fifoCount++;
      if (overrun) {
        if (overrun > maxOverrun) {
          maxOverrun = overrun;
        }
        overrun = 0;
      }
    } else {
      totalOverrun++;
      overrun++;
      if (overrun > 0XFFF) {
        error("too many overruns");
      }
      if (ERROR_LED_PIN >= 0) {
        digitalWrite(ERROR_LED_PIN, HIGH);
      }
    }
    // Save max jitter.
    if (delta > maxDelta) {
      maxDelta = delta;
    }
    // Write data if SD is not busy.
    if (!sd.card()->isBusy()) {
      size_t nw = fifoHead > fifoTail ? fifoCount : FIFO_DIM - fifoTail;
      // Limit write time by not writing more than 512 bytes.
      const size_t MAX_WRITE = 512 / sizeof(data_t);
      if (nw > MAX_WRITE) nw = MAX_WRITE;
      size_t nb = nw * sizeof(data_t);
      uint32_t usec = micros();
      if (nb != binFile.write(fifoData + fifoTail, nb)) {
        error("write binFile failed");
      }
      usec = micros() - usec;
      if (usec > maxWriteMicros) {
        maxWriteMicros = usec;
      }
      fifoTail = (fifoTail + nw) < FIFO_DIM ? fifoTail + nw : 0;
      if (fifoCount > maxFifoUse) {
        maxFifoUse = fifoCount;
      }
      fifoCount -= nw;

      
      if (Serial.available()) {
        Serial.println(millis());
        Serial.println(m);
        break;

      }
    }
  } while ((millis() - m) < record_time_ms);

  Serial.print(F("\nLog time: "));
  Serial.print(0.001 * (millis() - m));
  Serial.println(F(" Seconds"));
  binFile.truncate();
  binFile.sync();
  Serial.print(("File size: "));
  // Warning cast used for print since fileSize is uint64_t.
  Serial.print((uint32_t)binFile.fileSize());
  Serial.println(F(" bytes"));
  Serial.print(F("totalOverrun: "));
  Serial.println(totalOverrun);
  Serial.print(F("FIFO_DIM: "));
  Serial.println(FIFO_DIM);
  Serial.print(F("maxFifoUse: "));
  Serial.println(maxFifoUse);
  Serial.print(F("maxLogMicros: "));
  Serial.println(maxLogMicros);
  Serial.print(F("maxWriteMicros: "));
  Serial.println(maxWriteMicros);
  Serial.print(F("Log interval: "));
  Serial.print(LOG_INTERVAL_USEC);
  Serial.print(F(" micros\nmaxDelta: "));
  Serial.print(maxDelta);
  Serial.println(F(" micros"));
  digitalWrite(DATATRANSFER_LED_PIN,LOW);
}
//------------------------------------------------------------------------------

void printUnusedStack() {
#if HAS_UNUSED_STACK
  Serial.print(F("\nUnused stack: "));
  Serial.println(UnusedStack());
#endif  // HAS_UNUSED_STACK
}
//------------------------------------------------------------------------------

void setup() {
  pinMode(DATATRANSFER_LED_PIN,OUTPUT);
  // pinMode(pin_roed,OUTPUT);
  pinMode(SUCCESS_LED_PIN,OUTPUT);

  if (ERROR_LED_PIN >= 0) {
    pinMode(ERROR_LED_PIN, OUTPUT);
    digitalWrite(ERROR_LED_PIN, HIGH);
  }
  Serial.begin(115200);
   button1.begin();

  // Wait for USB Serial
  while (!Serial) {
    yield();
  }
  delay(1000);
  // Serial.println(F("Type any character to begin"));

  
  // ÆNDRET HER 24/04
  // while (!Serial.available()) {
  //   yield();
  // }

  FillStack();
#if !ENABLE_DEDICATED_SPI
  Serial.println(
      F("\nFor best performance edit SdFatConfig.h\n"
        "and set ENABLE_DEDICATED_SPI nonzero"));
#endif  // !ENABLE_DEDICATED_SPI

  Serial.print(FIFO_DIM);
  Serial.println(F(" FIFO entries will be used."));

  // Initialize SD.
  if (!sd.begin(SD_CONFIG)) {
    digitalWrite(ERROR_LED_PIN,HIGH);
    sd.initErrorHalt(&Serial);
  }
#if USE_RTC
  if (!rtc.begin()) {
    error("rtc.begin failed");
  }
  if (!rtc.isrunning()) {
    // Set RTC to sketch compile date & time.
    // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    error("RTC is NOT running!");
  }
  // Set callback
  FsDateTime::setCallback(dateTime);
#endif  // USE_RTC
}
//------------------------------------------------------------------------------
void loop() {
  
  button1.checkPressed();
  printUnusedStack();
  // Read any Serial data.
  clearSerialInput();

  if (ERROR_LED_PIN >= 0) {
    digitalWrite(ERROR_LED_PIN, LOW);
  }

  if (counter == 0) {
    Serial.println(F("Press button on GPIO 0 to start"));
    // delay(2000);

  } else  if (counter == 1) {
    createBinFile();
    logData();
    delay(100);
    // NOGET MED EN BLÅ LED --> f.eks. blå mens den overfører data
  } 

  if (counter == 1) {
   if (createCsvFile()) {
      binaryToCsv();
      counter = counter + 1;
      // NOGET MED EN GRØN LED
    }
  } 
  delay(1000);
}

#else  // SPI_DRIVER_SELECT
#error SPI_DRIVER_SELECT must be two in SdFat/SdFatConfig.h
#endif  // SPI_DRIVER_SELECT