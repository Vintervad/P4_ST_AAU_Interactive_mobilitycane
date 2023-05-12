#include <Pangodream_18650_CL.h>
#include <Arduino.h>
#include "a2dp_source.h"
#include "SD.h"
#include "FS.h"
#include <FunctionalInterrupt.h>

#define cm                  1
#define inch                0
#define SD_CS               33 // Valg af pins til SD kort
#define SPI_MOSI            26
#define SPI_MISO            27
#define SPI_SCK             25
#define PW_PIN              13 // Valg af pin til MB1260
#define ALARM_PIN           17
#define FULL_CHARGE_PIN     22
#define DETECTION_PIN       21

int alarmeringsafstand      = 198;

char BT_SINK_NAME[]         = "MAJOR IV"; // sink devicename
char BT_SINK_PIN[]          = "1234";             // sink pincode
char BT_DEVICE_NAME[]       = "ESP_A2DP_SRC_Nicolaj";     // source devicename

File            audiofile;    // @suppress("Abstract class cannot be instantiated")
uint32_t        sampleRate;
uint32_t        bitRate;
uint8_t         channels;
uint8_t         bitsPerSample=16;
uint32_t        posDataSection;

int BT_counter              = 0;
int BT_loop_counter         = 0;
int alarm_delay_ms          = 1000;
int k                       = 0;
int x                       = 0;


float cm_dis                =   0.00;
float Inch_dis              =   0.00;
int detected_objects[2048]  =   {};
int detection_delay         =   100;
int detection_counter       =   0;
int battery[100]            =   {};


Pangodream_18650_CL BL;

// -------------------- Class: ultralyd --------------------

class SonarEZ0pw_MB1260
{
  public :
  SonarEZ0pw_MB1260(int SonarPin);
   float Distance(int Mode);
   
   private:
   int Sonar_Pin;
   float Inch ,Cm ;
   long signal;
};


SonarEZ0pw_MB1260::SonarEZ0pw_MB1260(int SonarPin)
{
  pinMode(SonarPin,INPUT);
  Sonar_Pin=SonarPin;
}
 float SonarEZ0pw_MB1260::Distance(int Mode)
 {
    signal = pulseIn(Sonar_Pin, HIGH);
     Cm = (signal/58) + 4; 
     Inch = Cm/2.54;
     if(Mode)
     return Cm ;
     else
     return Inch;
 }

SonarEZ0pw_MB1260 Sonar(PW_PIN); //Valg af pin på ESP32

int counter = 0;
float signal_distance[5];
int alarm_distance;
int cnt = 0;
float signal_dist[3];
unsigned long timer;
int alarmering = 0;


//---------------------------------------------PARSE WAV----------------------------------------------------------------
bool parseWAV(fs::FS &fs, String path){
    char chbuf[256];
    audiofile=fs.open(path.c_str());
    String afn = (String)audiofile.name();  //audioFileName

    if(afn.endsWith(".wav")) {
        audiofile.readBytes(chbuf, 4); // read RIFF tag
        if ((chbuf[0] != 'R') || (chbuf[1] != 'I') || (chbuf[2] != 'F') || (chbuf[3] != 'F')){
            Serial.println("file has no RIFF tag");
            audiofile.seek(0);
            return false;
        }

        audiofile.readBytes(chbuf, 4); // read chunkSize (datalen)
        uint32_t cs = (uint32_t)(chbuf[0] + (chbuf[1] <<8) + (chbuf[2] <<16) + (chbuf[3] <<24) - 8);

        audiofile.readBytes(chbuf, 4); /* read wav-format */ chbuf[5] = 0;
        if ((chbuf[0] != 'W') || (chbuf[1] != 'A') || (chbuf[2] != 'V') || (chbuf[3] != 'E')){
            Serial.println("format tag is not WAVE");
            audiofile.seek(0);
            return false;
        }

        while(true){ // skip wave chunks, seek for fmt element
            audiofile.readBytes(chbuf, 4); /* read wav-format */
            if ((chbuf[0] == 'f') && (chbuf[1] == 'm') && (chbuf[2] == 't')){
                //if(audio_info) audio_info("format tag found");
                break;
            }
        }

        audiofile.readBytes(chbuf, 4); // fmt chunksize
        cs = (uint32_t) (chbuf[0] + (chbuf[1] <<8));
        if(cs>40) return false; //something is wrong
        uint8_t bts=cs-16; // bytes to skip if fmt chunk is >16
        audiofile.readBytes(chbuf, 16);
        uint16_t fc  = (uint16_t)(chbuf[0]  + (chbuf[1] <<8));  // Format code
        uint16_t nic = (uint16_t)(chbuf[2]  + (chbuf[3] <<8));  // Number of interleaved channels
        uint32_t sr  = (uint32_t)(chbuf[4]  + (chbuf[5] <<8) + (chbuf[6]  <<16) + (chbuf[7]  <<24)); // Smpling rate
        uint32_t dr  = (uint32_t)(chbuf[8]  + (chbuf[9] <<8) + (chbuf[10] <<16) + (chbuf[11] <<24)); // Data rate
        uint16_t dbs = (uint16_t)(chbuf[12] + (chbuf[13] <<8));  // Data block size
        uint16_t bps = (uint16_t)(chbuf[14] + (chbuf[15] <<8));  // Bits per sample
        Serial.printf("FormatCode=%u\n", fc);
        Serial.printf("Channel=%u\n", nic);
        Serial.printf("SampleRate=%u\n", sr);
        Serial.printf("DataRate=%u\n", dr);
        Serial.printf("DataBlockSize=%u\n", dbs);
        Serial.printf("BitsPerSample=%u\n", bps);


        if(fc != 1){
            Serial.println("format code is not 1 (PCM)");
            return false;
        }

        if(nic != 1 && nic != 2){
            Serial.print("number of channels must be 1 or 2");
            return false;
        }

        if(bps != 8 && bps !=16){
            Serial.println("bits per sample must be 8 or 16");
            return false;
        }
        bitsPerSample=bps;
        channels = nic;
        sampleRate = sr;
        bitRate = nic * sr * bps;
        Serial.printf("BitRate=%u\n", bitRate);

        audiofile.readBytes(chbuf, bts); // skip to data
        uint32_t s = audiofile.position();
        //here can be extra info, seek for data;
        while(true){
            audiofile.seek(s);
            audiofile.readBytes(chbuf, 4); /* read header signature */
            if ((chbuf[0] == 'd') && (chbuf[1] == 'a') && (chbuf[2] == 't') && (chbuf[3] == 'a')) break;
            s++;
        }

        audiofile.readBytes(chbuf, 4); // read chunkSize (datalen)
        cs = chbuf[0] + (chbuf[1] <<8) + (chbuf[2] <<16) + (chbuf[3] <<24) - 44;
        sprintf(chbuf, "DataLength=%u\n", cs);
        Serial.print(chbuf);
        posDataSection = audiofile.position();
        return true;
    }
    return false;
}

void setup() {
  Serial.begin(115200);
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  SD.begin(SD_CS);
  pinMode(ALARM_PIN, OUTPUT);
  pinMode(FULL_CHARGE_PIN, OUTPUT);
  pinMode(DETECTION_PIN, OUTPUT);
  parseWAV(SD, "/beep-05.wav"); //Her fortæller vi hvilken fil vi vil afspille. OBS! Filen skal være gemt på SD kortet!
  a2dp_source_init(BT_SINK_NAME, BT_SINK_PIN);
  Serial.printf("Total space: %lluMB\n", SD.totalBytes() / (1024 * 1024));
  Serial.printf("Used space: %lluMB\n", SD.usedBytes() / (1024 * 1024));
  delay(15000);
  BT_counter = 1;
  bt_loop();

}

void loop() {
  timer = millis();
  //Printer distancen
  cm_dis = Sonar.Distance(cm); // To calculate the distance in cm
  Serial.println("Distance " );
  Serial.print(cm_dis);
  Serial.println(" cm ");
  int m = get_APP_AV_STATE(); // Hvis m = 5, er BT device connected
  int n = get_APP_AV_MEDIA_STATE(); // Hvis n = 1 eller 2 er transmission startet



  bt_loop();


  //// Algoritme til gang ////
 
  if (BL.getBatteryChargeLevel()>=5){ // 5% fordi det svarer til ca. 3.4V, hvorefter vores system fungerer i yderligere 60 min
    // Serial.println("Charge level: >25\%");
    digitalWrite(FULL_CHARGE_PIN, HIGH);
    digitalWrite(ALARM_PIN, LOW);


  } else if (BL.getBatteryChargeLevel()<5) {
    // Serial.println("Charge level: <25\%");
    digitalWrite(ALARM_PIN, HIGH);
    digitalWrite(FULL_CHARGE_PIN, LOW);
  }


  if (m == 5 && n >= 1) {

  if (counter == 0 && cm_dis > 30.00 && cm_dis < 400.00) {
    signal_distance[counter] = {cm_dis};                                                 //Gemmer den nuværende distance i et array på d. 0. plads
    counter = counter + 1;                                                                
    cm_dis = Sonar.Distance(cm);                                                          // Er åbenbart nødvendigt

    if (counter == 1 && signal_distance[counter-1]>cm_dis){                               // Tjekker om counter er 1 og om den nuværende distance er mindre end den forrige
      signal_distance[counter] = {cm_dis};
      Serial.println("sample_1 distance " );
      Serial.print(signal_distance[counter]);
      Serial.println(" cm ");
      counter = counter + 1;
      cm_dis = Sonar.Distance(cm);

      if (counter == 2 && signal_distance[counter-1]>cm_dis){
      signal_distance[counter] = {cm_dis};
      Serial.println("sample_2 distance " );
      Serial.print(signal_distance[counter]);
      Serial.println(" cm ");
      counter = counter + 1;
      cm_dis = Sonar.Distance(cm);

     
        if (counter == 3 && (signal_distance[counter-3]-signal_distance[counter-1])>= 10.00 && (signal_distance[counter-3]-signal_distance[counter-1]) <= 40.00 && signal_distance[counter-1] <= alarmeringsafstand){       //Er blevet ændret til at forskellen skal være større end noget
    
        detected_objects[detection_counter] = timer;
        Serial.print("Detection counter = ");
        Serial.println(detected_objects[detection_counter]);



        if ((detected_objects[detection_counter] - detected_objects[detection_counter-1]) > alarm_delay_ms) { 
          Serial.print("t_A start = ");
          Serial.println(timer,DEC);
          BT_counter = BT_counter + 1;
          detection_counter =  detection_counter + 1;
          bt_loop();
          // Serial.println(new_timer,DEC);
          Serial.print("Detection counter = ");
          Serial.println(detection_counter);
        }


          signal_distance[counter] = {cm_dis};
          alarm_distance = signal_distance[counter];
          Serial.println("ALARM"); 
          Serial.println(alarm_distance);       //Printer distancen der hvor man får alarm 
          // delay(1700);                      // Ændr på den her hvis alarmerne kommer for ofte
          counter = 0;
          Serial.print("Fil slut??? = ");
          Serial.println(timer,DEC);
        }


          else {
            counter = 0;
          }
      }

      else {
        counter = 0;
      }
    }

      else {
        counter = 0;
      }
  } 

  else {
    counter = 0;
  }

  //// Algoritme til stående ////

  // if (cm_dis > 30.00 && cm_dis < 300.00) {
  //   signal_dist[cnt] = {cm_dis};
  //   cnt = cnt + 1;

  //    if (cnt == 3 && (signal_dist[cnt-3]-signal_dist[cnt-2]) <= 2 && (signal_dist[cnt-2]-signal_dist[cnt-1]) <= 2 ) {         // tilføj brug af de tre samples
  //     BT_counter = BT_counter + 1;
  //     Serial.print("BT_counter = ");
  //     Serial.println(BT_counter);
  
  //     if (BT_counter % 2 == 1) { 
  //         bt_loop();
  //     }

  //     Serial.println("ALARM_stående");   
  //     cnt = 0;
  //     alarmering = alarmering + 1;
  //    }

  //    if (cnt>3){
  //     cnt = 0;
  //     }
  // }

  // else {
  //   cnt = 0;
  // }
  
  // if (timer >= 150000){
  //   Serial.println(alarmering);
  //   Serial.println(timer);
  //   delay(100000);
  // }

  }
}



//---------------------------------------------EVENTS-------------------------------------------------------------------
int32_t
 bt_data(uint8_t *data, int32_t len, uint32_t* sr){
 
    *sr = sampleRate;
    if (len < 0 || data == NULL) {
        return 0;
    }
// Serial.println("før afspilning");
//     Serial.println(timer,DEC);
    len = audiofile.read(data, len);
    // Serial.println("efter afspilning");
    // Serial.println(timer,DEC);


    // if(len == 0 && BT_counter % 2  == 1) {
    //   Serial.print("t_A stop = ");
    //   Serial.println(timer,DEC);

    // }

    if(len == 0 && BT_counter % 2  == 0) {
      Serial.print("Counter in bt_loop start = ");
      Serial.println(BT_counter);
      Serial.print("Audiofile.seek start = ");
      Serial.println(timer,DEC);
      audiofile.seek(posDataSection);
      // delay(200);
      BT_counter = BT_counter + 1;
      BT_loop_counter = BT_loop_counter + 1;
    }

    return len;
}
void bt_info(const char* info){
    Serial.printf("bt_info: %s\n", info);
}
