#pragma mark - Depend ESP8266Audio and ESP8266_Spiram libraries
#define LILYGO_WATCH_HAS_SDCARD
#define LILYGO_WATCH_HAS_MOTOR

#include "config.h"
#include "AudioFileSourcePROGMEM.h"
#include "AudioFileSourceID3.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include <WiFi.h>
#include <HTTPClient.h>         //Remove Audio Lib error
#include "pika.h"

TTGOClass *ttgo;
AudioGeneratorMP3 *mp3;
AudioFileSourcePROGMEM *filemp3;
AudioOutputI2S *out;
AudioFileSourceID3 *id3;
BMA *sensor;
AXP20X_Class *power;

bool irq = false;

int16_t x, y; // ตำแหน่งที่กดจอ
uint8_t hh, mm, ss, mmonth, dday; // H, M, S variables
uint16_t yyear; // Year is 16 bit int

int centerx, centery, centerz;
int custombutton = 36;
int powerbutton = 0;
int batteryPower = 0; // ข้อมูล Battery ที่เหลือ
int diffcultMode = 1; // เช็คว่าเข้าโหมดไหน
int startstate = 0; // เช็คว่าเริ่มทำงานยัง
int silentmode = 1; // เปิดโหมดเงียบ
int stateDisplay = 0; // 0 = เปิด 1 = ปิด
int countSDcard = 5;
int firstloop = 0;
int countsilentmode = 0;

bool rtcIrq = false;
float rangePerDigit = .000061f;

unsigned long period = 1000; //ระยะเวลาที่ต้องการรอ
unsigned long last_time = 0; //ประกาศตัวแปรเป็น global เพื่อเก็บค่าไว้ไม่ให้ reset จากการวนloop

String dataforsave; //
String datetime;
String anglesetting = "45";
String outangle = "";
String currentday = "";

void setup()
{
  Serial.begin(9600);
  WiFi.mode(WIFI_OFF);
  delay(500);
  //********************************** Setup MainBoard **************
  ttgo = TTGOClass::getWatch();
  ttgo->begin();
  ttgo->openBL();
  ttgo->tft->setTextColor(TFT_WHITE, TFT_BLACK);
  ttgo->tft->setRotation(1);
  pinMode(36, INPUT);
  //********************************** SetUp Accel stage **************
  sensor = ttgo->bma;
  Acfg cfg;
  cfg.odr = BMA4_OUTPUT_DATA_RATE_100HZ;
  cfg.range = BMA4_ACCEL_RANGE_4G;
  cfg.bandwidth = BMA4_ACCEL_NORMAL_AVG4;
  cfg.perf_mode = BMA4_CONTINUOUS_MODE;
  sensor->accelConfig(cfg);
  sensor->enableAccel();
  //********************************* Setup RTC **********************
  pinMode(RTC_INT_PIN, INPUT_PULLUP);
  //ttgo->rtc->setDateTime(2021, 6, 1, 11, 5, 00); //ตั้งเวลาRTC
  //********************************* Setup PowerButton *************
  power = ttgo->power;
  pinMode(AXP202_INT, INPUT_PULLUP);
  power->enableIRQ(AXP202_PEK_SHORTPRESS_IRQ, true);
  power->clearIRQ();
  //********************************* Setup Micro SD Card ********
  while (countSDcard > 0) {
    ttgo->tft->setCursor(0, 0);// กำหนดตำแหน่ง x,y ที่จะแสดงผล
    ttgo->tft->setTextSize(2);
    if (ttgo->sdcard_begin()) {
      ttgo->tft->println("Micro SD Card Found");
      ttgo->tft->println("Ready For Save Data");
      delay(1000);
      break;
    }
    ttgo->tft->println("Micro SD not found , Count Down " + String(countSDcard) + "sec");
    countSDcard--;
    delay(1000);
  }
  //********************************* Setup Audio ****************
  ttgo->enableLDO3();
  filemp3 = new AudioFileSourcePROGMEM(pika, sizeof(pika));
  id3 = new AudioFileSourceID3(filemp3);
  out = new AudioOutputI2S(0, 1);
  mp3 = new AudioGeneratorMP3();
  //********************************* Setup motor ****************
  ttgo->motor_begin();
  //********************************* Setup Battery percent ******
  ttgo->power->adc1Enable(AXP202_VBUS_VOL_ADC1 | AXP202_VBUS_CUR_ADC1 | AXP202_BATT_CUR_ADC1 | AXP202_BATT_VOL_ADC1, true);
  //**************************************************************
  ttgo->tft->fillScreen(TFT_BLACK);

}

void loop()
{
  //****************************************** Setup Button******************
  if (digitalRead(AXP202_INT) == LOW) {
    if (powerbutton == 1) {
      powerbutton = 0;
    }
    else {
      powerbutton = 1;
    }
    delay(200);
    if (powerbutton = 1) {
      if (stateDisplay == 0) {
        ttgo->closeBL();
        ttgo->displaySleep();
        setCpuFrequencyMhz(20);
        gpio_wakeup_enable ((gpio_num_t)AXP202_INT, GPIO_INTR_LOW_LEVEL);
        esp_sleep_enable_gpio_wakeup();
        esp_light_sleep_start();
        stateDisplay = 1;
        powerbutton = 0;
      }
      else {
        setCpuFrequencyMhz(160);
        ttgo->displayWakeup();
        ttgo->openBL();
        stateDisplay = 0;
        powerbutton = 0;
      }
    }
  }
  power->clearIRQ();
  int per = ttgo->power->getBattPercentage();
  int modebutton = digitalRead(36);
  Accel acc;
  bool res = sensor->getAccel(acc);
  if (modebutton == LOW) {
    startstate++;
    delay(500);
  }
  //******************************************** Start Phase ****************
  if (startstate == 0) {
    ttgo->tft->setCursor(0, 0);// กำหนดตำแหน่ง x,y ที่จะแสดงผล
    ttgo->tft->setTextSize(2);
    ttgo->tft->print("Battery: "); ttgo->tft->print(per); ttgo->tft->println(" %");
    ttgo->tft->setTextSize(4);
    ttgo->tft->println("Welcome");
    ttgo->tft->setTextSize(2);
    ttgo->tft->println("Ready to use");
    ttgo->tft->println("Press Start");
  }
  if (startstate == 1) {
    firstloop = 0;
    startstate = 2;
    ttgo->tft->fillScreen(TFT_BLACK); // ลบภาพในหน้าจอทั้งหมด
  }
  //******************************************** Select Mode Phase ****************
  if (startstate == 2) {
    ttgo->tft->setCursor(0, 0); // กำหนดตำแหน่ง x,y ที่จะแสดงผล
    ttgo->tft->setTextSize(2);
    ttgo->tft->print("Battery: "); ttgo->tft->print(per); ttgo->tft->println(" %");
    ttgo->tft->setTextSize(4);
    ttgo->tft->println("SelectMode");
    ttgo->tft->setTextSize(3);
    // สั่งให้จอแสดงผล
    if (ttgo->touched()) {
      if (diffcultMode < 3) {
        diffcultMode++;
        delay(500);
      }
      else {
        diffcultMode = 1;
        delay(500);
      }
    }
    if (diffcultMode == 1) {
      ttgo->tft->println("45*");
      anglesetting = "45";
    }
    else if (diffcultMode == 2) {
      ttgo->tft->println("30*");
      anglesetting = "30";
    }
    else if (diffcultMode == 3) {
      ttgo->tft->println("15*");
      anglesetting = "15";
    }
  }
  if (startstate == 3) {
    float angleX = acc.x * rangePerDigit * 9.80665f;
    float angleY = acc.y * rangePerDigit * 9.80665f;
    float angleZ = acc.z * rangePerDigit * 9.80665f;
    centerx = -(atan2(angleX, sqrt(angleY * angleY + angleZ * angleZ)) * 180.0) / M_PI;
    centery = (atan2(angleY, angleZ) * 180.0) / M_PI;
    ttgo->tft->fillScreen(TFT_BLACK); // ลบภาพในหน้าจอทั้งหมด
    startstate = 4;
  }
  //******************************************** In-use Phase ****************
  if (startstate == 4) {
    if (ttgo->touched()) {
      if (countsilentmode < 2) {
        countsilentmode++;
        delay(100);
      }
      else {
        countsilentmode = 2;
        delay(100);
      }
    }
    float angleX = acc.x * rangePerDigit * 9.80665f;
    float angleY = acc.y * rangePerDigit * 9.80665f;
    float angleZ = acc.z * rangePerDigit * 9.80665f;
    int pitch = -(atan2(angleX, sqrt(angleY * angleY + angleZ * angleZ)) * 180.0) / M_PI;
    int roll = (atan2(angleY, angleZ) * 180.0) / M_PI;

    ttgo->tft->setCursor(0, 0); // กำหนดตำแหน่ง x,y ที่จะแสดงผล
    ttgo->tft->setTextSize(2);
    ttgo->tft->print("Battery: "); ttgo->tft->print(per); ttgo->tft->println(" %");
    ttgo->tft->setTextSize(2);
    if (silentmode == 1) {
      ttgo->tft->println("SilentMode: ON");
    }
    else {
      ttgo->tft->println("SilentMode: OFF");
    }
    ttgo->tft->setTextSize(3);
    switch (diffcultMode) {
      case 1: if (pitch > centerx + 45 || roll > centery + 45 || pitch < centerx - 45 || roll < centery - 45) {
          ttgo->tft->println("Warning1");
          outangle = "Warning";
          if (silentmode != 1) {
            if (mp3->isRunning() != 1) {
              mp3->begin(id3, out);
            }
          }
          else {
            ttgo->motor->onec();
          }
        }
        else {
          if (silentmode != 1) {
            if (mp3->isRunning()) {
              if (!mp3->loop()) mp3->stop();
            }
          }
          outangle = "";
        }
        break;
      case 2: if (pitch > centerx + 30 || roll > centery + 30 || pitch < centerx - 30 || roll < centery - 30) {
          ttgo->tft->println("Warning2");
          outangle = "Warning";
          if (silentmode != 1) {
            if (mp3->isRunning() != 1) {
              mp3->begin(id3, out);
            }
          }
          else {
            ttgo->motor->onec();
          }
        }
        else {
          if (silentmode != 1) {
            if (mp3->isRunning()) {
              if (!mp3->loop()) mp3->stop();
            }
          }
          outangle = "";
        }
        break;
      case 3: if (pitch > centerx + 15 || roll > centery + 15 || pitch < centerx - 15 || roll < centery - 15) {
          ttgo->tft->println("Warning3");
          outangle = "Warning";
          if (silentmode != 1) {
            if (mp3->isRunning() != 1) {
              mp3->begin(id3, out);
            }
          }
          else {
            ttgo->motor->onec();
          }
        }
        else {
          if (silentmode != 1) {
            if (mp3->isRunning()) {
              if (!mp3->loop()) mp3->stop();
            }
          }
          outangle = "";
        }
        break;
      default:
        if (mp3->isRunning()) {
          if (!mp3->loop()) mp3->stop();
        }
        outangle = "";
        break;
    }
    ttgo->tft->println("");
    ttgo->tft->print("AngelX: ");
    ttgo->tft->println(pitch);
    ttgo->tft->print("AngelY: ");
    ttgo->tft->println(roll);

    if ( millis() - last_time > period) {
      ttgo->tft->fillScreen(TFT_BLACK);
      RTC_Date tnow = ttgo->rtc->getDateTime();
      hh = tnow.hour;
      mm = tnow.minute;
      ss = tnow.second;
      dday = tnow.day;
      mmonth = tnow.month;
      yyear = tnow.year + 543;
      datetime = String(dday) + "/" + String(mmonth) + "/" + String(yyear) + " || " + String(hh) + ":" + String(mm) + ":" + String(ss); // เขียนข้อมูลลง datetime
      dataforsave = datetime + "|| AngleX: " + pitch + ", AngleY: " + roll + " " + outangle; // เขียนข้อมูลลง dataforsave
      if (currentday != String(dday) + "/" + String(mmonth) + "/" + String(yyear)) {
        firstloop = 0;
      }
      currentday = String(dday) + "/" + String(mmonth) + "/" + String(yyear);
      if (firstloop == 0) {
        newwrite(SD, String(dday), String(mmonth), String(yyear));
        String centerSetting = "CenterX: " + String(centerx) + "|| CenterY: " + String(centery) + "|| Mode: " + anglesetting;
        appendFile(SD, "/" + String(dday) + "-" + String(mmonth) + "-" + String(yyear) + ".txt", centerSetting);
        appendFile(SD, "/" + String(dday) + "-" + String(mmonth) + "-" + String(yyear) + ".txt", dataforsave);
        firstloop = 1;
      }
      else {
        appendFile(SD, "/" + String(dday) + "-" + String(mmonth) + "-" + String(yyear) + ".txt", dataforsave);
      }
      last_time = millis(); //เซฟเวลาปัจจุบันไว้เพื่อรอจนกว่า millis() จะมากกว่าตัวมันเท่า period
      if (countsilentmode >= 2) {
        if (silentmode == 1) {
          silentmode = 0;
        }
        else if (silentmode == 0) {
          silentmode = 1;
        }
      }
      countsilentmode = 0;
    }
    //******************************************** Finished Phase ****************
  }
  if (startstate == 5) {
    if (mp3->isRunning()) {
      if (!mp3->loop()) mp3->stop();
    }
    ttgo->tft->fillScreen(TFT_BLACK); // ลบภาพในหน้าจอทั้งหมด
    startstate = 0;
  }

}

//****************************** Function *********************
void writeFile(fs::FS &fs, String path, String message) {
  Serial.printf("Writing file: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.println(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}
//***********************************************************
void appendFile(fs::FS &fs, String path, String message) {
  Serial.printf("Appending to file: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if (file.println(message)) {
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  file.close();
}
//***********************************************************
void newwrite(fs::FS &fs, String dday, String mmonth, String yyear)
{
  int filenumber = 1;
  int havesame = 0;
  File root = fs.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.println(file.name());
    if (String(file.name()) == "/" + dday + "-" + mmonth + "-" + yyear + ".txt") {
      havesame = 1;
      break;
    }
    file = root.openNextFile();
  }
  if (havesame == 0) {
    writeFile(SD, "/" + dday + "-" + mmonth + "-" + yyear + ".txt", "//**************Start***************");
  }
  else {
    appendFile(SD, "/" + dday + "-" + mmonth + "-" + yyear + ".txt", "//**************Start***************");
  }
}
