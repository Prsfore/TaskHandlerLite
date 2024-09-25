
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <Credentials.h>  // Wi-Fi credentials

#if defined(ESP32)
  #include <Arduino.h>
  #include <WiFi.h>
  #include <HTTPClient.h>
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
  #include <freertos/timers.h>
  #include "FS.h"
  #include <LittleFS.h>
  #include <WiFi.h>
  #include <AsyncTCP.h>

  AsyncWebServer server(80);
#endif

void onOTAStart() {
  Serial.println("OTA update started!");
}

unsigned long ota_progress_millis = 0;
void onOTAProgress(size_t current, size_t final) {
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
    Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
  }
}

void onOTAEnd(bool success) {
  if (success) {
    Serial.println("OTA update finished successfully!");
  } else {
    Serial.println("There was an error during OTA update!");
  }
}

void initOTA(){
  ElegantOTA.begin(&server);  
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);                                          
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Hi! This is ElegantOTA .");
  });
  server.begin();
  Serial.println("HTTP server started");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const char* ssid = SSID;
const char* password = PASSWORD;

//Your Domain name with URL path or IP address with path
String serverName = "http://mes.yilbay.site/terminal/terminaldata";
String hostnameString;
String ipString;
String MAC;

HTTPClient http1; // For INIT and Production signal
HTTPClient http0; // For Alive signal


hw_timer_t *timer = NULL;

volatile bool startf = false;
volatile bool pausef = false;
volatile bool stopf = false;
volatile bool outpf = false;

volatile bool startOn = false;
volatile bool pauseOn = false;
volatile bool stopOn = false;

volatile bool uretimdenM30=false;

volatile bool isOnline; // adjusted true when login WiFi
volatile bool append=false; // To make sure appends or not in writeFile()
volatile bool fromOnline; // is it first Offline signal or not?
volatile bool afterKeepAlive=false;
volatile bool ReadPrev=false;

int startTimer=0 ,pauseTimer=0,stopTimer=0; // Create signal structure for timers

unsigned long button_timestart = 0,last_button_timestart=0;
unsigned long button_timepause = 0,last_button_timepause=0;
unsigned long button_timestop = 0,last_button_timestop=0;

TaskHandle_t xStopTaskHandle = NULL, xStartTaskHandle = NULL, xPauseTaskHandle = NULL,xOutpTaskHandle = NULL; 

const char * filePath= "/data.txt";
#define FORMAT_LITTLEFS_IF_FAILED true

void IRAM_ATTR StartISR() { // Start Button
  button_timestart=millis();
  if(button_timestart-last_button_timestart>5){
    startf=true;
  }
  last_button_timestart=button_timestart;
}

void IRAM_ATTR PauseISR() { // Pause Button
    button_timepause=millis();
    if(button_timepause-last_button_timepause>5){
      pausef=true;
    }
    last_button_timepause=button_timepause;
}

void IRAM_ATTR StopISR() { // M30 Button
  button_timestop=millis();
  if(button_timestop-last_button_timestop>5){
    if(!stopf){
      stopf=true;
    }
    else if(stopf){
      stopf=false;
    }
  }
  last_button_timestop=button_timestop;
}

void setup() {
    Serial.begin(115200);
    if(!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)){
    Serial.println("LittleFS Mount Failed");
    return;
    }
    LittleFS.format();
    delay(1000);
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    Serial.println("Connecting");
    while(WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("");
    Serial.print("Connected to WiFi network with IP Address: ");
    IPAddress ip = WiFi.localIP();
    ipString = ip.toString();
    Serial.println(ipString);
    Serial.print("MAC Address: ");
    MAC = WiFi.macAddress();
    Serial.println(MAC);
    const char* hostname = WiFi.getHostname();
    hostnameString = String(hostname);
    Serial.println(hostnameString);
    isOnline=true;
    initOTA();

    delay(1000);

    if(InitCNC()){
      timer = timerBegin(1000000);
      timerStop(timer);

      delay(500);

      timerStart(timer);
      pinMode(5,INPUT_PULLUP); // Alarm not consider
      pinMode(18, INPUT_PULLUP); // Start
      pinMode(19, INPUT_PULLUP); // Pause
      pinMode(21, INPUT_PULLUP); // Stop (M30)

      delay(500);

      xTaskCreatePinnedToCore(StartTask, "Start Task", 2048, NULL, 1, &xStartTaskHandle,0);
      xTaskCreatePinnedToCore(PauseTask, "Pause Task", 2048, NULL, 1, &xPauseTaskHandle,0);
      xTaskCreatePinnedToCore(StopTask, "Stop Task", 3072, NULL, 1, &xStopTaskHandle,0); // 100 data writen in file(each 900bytes) + 2 function to call= 94208 bytes
      xTaskCreatePinnedToCore(Outp, "Output Task", 94208, NULL, 1, &xOutpTaskHandle,0); 

      attachInterrupt(digitalPinToInterrupt(18), StartISR, FALLING);
      attachInterrupt(digitalPinToInterrupt(19), PauseISR, FALLING);
      attachInterrupt(digitalPinToInterrupt(21), StopISR, CHANGE);

      Serial.println("Machine is Initiliazed!2");
    }else{
      Serial.println("Machine Could Not Initiliazed!");
    }
    
}

void loop() {
  ElegantOTA.loop();

  int info=timerRead(timer)/1000000; // Keep alive
  if(info%300==0&&info!=0){
    Serial.println("In the loop!");
    afterKeepAlive=false;
    KeepAlive();
    afterKeepAlive=true;
    delay(1000);
  }
  if(ReadPrev==true){
    Serial.println("Just before Send");
    readFile(LittleFS,filePath);
    LittleFS.format(); // Format the file after readed.
    Serial.println("Previous Data Has Been Sended!");
    ReadPrev=false;
    delay(250);
  }
  delay(10);
}

void StartTask(void *p) { //Start Circuit
  while(1){
    while (startf) { // Start signal Low
      startOn=true;
      vTaskDelay(10/portTICK_PERIOD_MS);
      if(digitalRead(18)==1){
        startf=false;
        break;
      }
      stopOn=false;
      uretimdenM30=true;
      startTimer++;
    }
    startOn=false;
    vTaskDelay(20/portTICK_PERIOD_MS);
  }
}

void PauseTask(void *p) { // Pause Circuit
  while(1){
    while (pausef&&!startOn) { // Start signal Low
      pauseOn=true;
      vTaskDelay(10/portTICK_PERIOD_MS);
      if(digitalRead(19)==1){
        pausef=false;
        break;
      }
      pauseOn=true;
      pauseTimer++;
    }
    pauseOn=false;
    vTaskDelay(20/portTICK_PERIOD_MS);
  }
}

void StopTask(void *parameter) {  // M30 Circuit
  while(1){
    if(stopf&&!startOn&&!pauseOn){
      if(uretimdenM30){
        outpf=true;
        uretimdenM30=false;
      }
      if(afterKeepAlive){
        timerWrite(timer,0);
        afterKeepAlive=false;
      }
      stopTimer++;
      stopf=false;
      vTaskDelay(10/portTICK_PERIOD_MS);
    }
    vTaskDelay(10/portTICK_PERIOD_MS);
  }
}

void Outp(void *p) {
  while(1){
      if(outpf){
        int deneme=0;
        int start=startTimer/100;
        int pause=pauseTimer/100;
        int stop=stopTimer/2;

        startTimer=0;
        pauseTimer=0;
        stopTimer=0;
        startf = false;
        pausef = false;

        String serverPath = serverName + "?parametre1=" + MAC + "&" + "parametre2=" + String(start) + "&" + "parametre3=" + String(pause)+ "&" + "parametre4=" + String(stop) + "&" + "parametre5=" + "U" + "&" + "IPAddr=" + ipString + "&" + "parametrehost=" + hostnameString;

        Serial.printf("Start time: %d \n",start);
        Serial.printf("Pause time: %d \n",pause);
        Serial.printf("Idle time: %d \n",stop);

        if(isOnline){
          int httpResponseCodeU = SendData(serverPath,"Core0");
          while(httpResponseCodeU!=200 && deneme!=5){
            httpResponseCodeU = SendData(serverPath,"Core0");
            if(httpResponseCodeU==200){
              break;
            }
            deneme++;
            vTaskDelay(1000/portTICK_PERIOD_MS);
          }
          if(httpResponseCodeU==200){
            Serial.println("Production Process Saved!");
          }
          else if (httpResponseCodeU!=200){
            isOnline=false;
            fromOnline=true;
          }
          deneme=0;
        }

        if(!isOnline){
          int httpResponseCodeU=0;
          while(httpResponseCodeU!=200 && deneme!=2){
            httpResponseCodeU=SendData(serverPath,"Core0");
            if(httpResponseCodeU==200){
              break;
            }
            deneme++;
            vTaskDelay(1000/portTICK_PERIOD_MS);
          }
          if(httpResponseCodeU==200){
            isOnline=true;
            append=false;
            if(fromOnline==false){
              ReadPrev=true;
            }
          }
          if(httpResponseCodeU!=200){
            writeFile(LittleFS,filePath,serverPath.c_str(),append);
            if(!append){
              append=true;
              fromOnline=false;
            }
          }
          deneme=0;
        }
        outpf=false;
      }
      vTaskDelay(10/portTICK_PERIOD_MS);
  }
}

int InitCNC(){
  int deneme=0;

  String serverPath = serverName + "?parametre1=" + MAC + "&" + "parametre2=" + "0" + "&" + "parametre3=" + "0" + "&" + "parametre4=" + "0" + "&" + "parametre5=" + "boot" + "&" + "IPAddr=" + ipString + "&" + "parametrehost=" + hostnameString;
  int httpResponseCodeI=SendData(serverPath,"Core1");

  while(httpResponseCodeI!=200 &&deneme!=5){
    httpResponseCodeI = SendData(serverPath,"Core1");
    if(httpResponseCodeI==200){
      break;
    }
    deneme++;
    delay(1000);
  }
  deneme=0;
  if(httpResponseCodeI==200){
    return 1;
  }
  if(httpResponseCodeI!=200){
    return 0;
  }
}
void KeepAlive(){  // Needs to adjust for other core
  int deneme=0;

  String serverPath = serverName + "?parametre1=" + MAC + "&" + "parametre2=" + "0" + "&" + "parametre3=" + "0" + "&" + "parametre4=" + "0" + "&" + "parametre5=" + "info" + "&" + "IPAddr=" + ipString + "&" + "parametrehost=" + hostnameString;
  int httpResponseCodeA = SendData(serverPath,"Core1");

  while(httpResponseCodeA!=200&&deneme!=5){
    httpResponseCodeA=SendData(serverPath,"Core1");
    if(httpResponseCodeA==200){
      break;
    }
    deneme++;
    delay(1000);
  }
  if(httpResponseCodeA==200){
    Serial.println("Machine is Alive!");
  }
}
void writeFile(fs::FS &fs, const char *path, const char *message, bool appends) {  // Write file to .txt file in ESP32
    Serial.printf("Writing file: %s\r\n", path);

    File file;
    if (appends) {
        file = fs.open(path, FILE_APPEND);  // Open in append mode
        Serial.println("- opened in append mode");
    } else {
        file = fs.open(path, FILE_WRITE);   // Open in write mode (overwrites file)
        Serial.println("- opened in write mode");
    }

    if (!file) {
        Serial.println("- failed to open file for writing");
        return;
    }

    // Write message to the file followed by a newline
    if (file.println(message)) {
        Serial.println("- file written");
    } else {
        Serial.println("- write failed");
    }

    file.close();
    Serial.println("- file closed after writing");
}

void readFile(fs::FS &fs, const char *path) { // Read file from .txt file in ESP32

    Serial.printf("Reading file: %s\r\n", path);

    File file = fs.open(path);
    if (!file || file.isDirectory()) {
        Serial.println("- failed to open file for reading");
        return;
    }

    Serial.println("- read from file:");

    String line = "";
    while (file.available()) {
        char c = file.read();
        if (c == '\r') {
            continue;  // Skip carriage return characters
        }
        if (c == '\n') {  // Newline character detected
            int deneme=0;
            int httpResponseCodeU=SendData(line,"Core1");
            while (httpResponseCodeU!=200 && deneme!=3){
              httpResponseCodeU=SendData(line,"Core1");
              if(httpResponseCodeU==200){
                break;
              }
              deneme++;
              delay(1000);
            }
            Serial.println(line);  // Print the line
            line = "";  // Clear the buffer for the next line
            deneme=0;  // Clear the try
        } else {
            line += c;  // Append character to line
        }
    }

    // Handle the case where the last line does not end with a newline
    if (line.length() > 0) {
        Serial.println(line);  // Print any remaining line
    }

    file.close();
    Serial.println("- file closed after reading");
}

int SendData(String path,String Core){ // Send Get function from Specific Core
  if(Core.equals("Core0")){
    http0.end();
    http0.begin(path.c_str());
    int respond=http0.GET();
    if(respond==200){
      Serial.println("Data Sended Successfully!(0)");
      http0.end();
      return respond;
    }else{
      http0.end();
      Serial.println("Data Could Not Sended!(0)");
      return respond;
    }
  }
  if(Core.equals("Core1")){
    http1.end();
    http1.begin(path.c_str());
    int respond=http1.GET();
    if(respond==200){
      Serial.println("Data Sended Successfully!(1)");
      http1.end();
      return respond;
    }else{
      http1.end();
      Serial.println("Data Could Not Sended!(1)");
      return respond;
    } 
  }
}

