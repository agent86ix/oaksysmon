#include <Wire.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>

#include "sketch_oak_ui.h"

#define PIN_PM_RESET 5

#define I2C_ADDRESS_PM 8
#define I2C_ADDRESS_OAK 9
#define I2C_REQUEST_MAX 32

#define REPORT_BUFFER_SIZE 1500
#define MAX_HTTP_SEND_SIZE 2920

/*  For later:
 *  https://github.com/esp8266/Arduino/issues/1740
 *  SSL support for HTTP server is coming soon(?)
 *  Then you could send username/password or use 
 *  HTTP Basic Auth:
 *  https://github.com/esp8266/Arduino/blob/master/libraries/ESP8266WebServer/examples/HttpBasicAuth/HttpBasicAuth.ino
 */

ESP8266WebServer server(80);
byte reportBuffer[REPORT_BUFFER_SIZE];
long reportTime = 0;
long resetTime = 0;
byte pmConnected = 0;

int htmlLen = -1;

const char* responseSuccess = "{\"r\":\"200\"}";
const char* responseFail = "{\"r\":\"500\"}";

void resetPM() {
  long now = millis();
  if(resetTime+10000 < now) {
    resetTime = now;
    Serial.println("Resetting Pro Micro...");
    digitalWrite(PIN_PM_RESET, LOW);
    delay(10);
    digitalWrite(PIN_PM_RESET, HIGH);
  }
}

void i2cWrite(byte data) {
  int retry = 0;
  int lastError = 0;
  while(retry < 10) {
    // Attempt the write
    Wire.beginTransmission(I2C_ADDRESS_PM);
    Wire.write(data);
    int error = Wire.endTransmission();
    
    // Successful write
    if(error == 0) {
      pmConnected = 1;
      break;
    }

    // Failure, retry
    retry++;
    delay(10);
    if (error == 3) {
      // Error 3 is a NACK, or "slow down a bit"
      // No need to print in this case.
    }
    else {
      // Something bad happened
      if(error != lastError) {
        Serial.print("Error writing: ");
        Serial.println(error);
      }
    }
    lastError = error;
  }
  if(retry == 10) {
    pmConnected = 0;
    Serial.println("Failed to write, ProMicro is disconnected!");
  }
}

void sendLargeBuffer(WiFiClient client, const char* buf, int bufLen) {
  // The ESP8266 web client can't handle writing buffers that are too large,
  // perhaps around 1400 bytes or so?  This function just breaks it up into
  // MAX_HTTP_SEND_SIZE chunks.
  int bytesWritten = 0;
  while(bytesWritten < bufLen) {
    int curLen = MAX_HTTP_SEND_SIZE;
    if(curLen > (bufLen - bytesWritten)) curLen = bufLen - bytesWritten;
    client.write(&buf[bytesWritten], curLen);
    bytesWritten += curLen;
  }
}

void handleRoot() {
  // Show the HTML page
  
  WiFiClient client = server.client();
  server.send(200, "text/html", "");
  sendLargeBuffer(client, htmlFile, htmlLen);
}

void kbdStr() {
  // This function sends a string of characters to the Pro Micro
  WiFiClient client = server.client();
  int responseCode = 500;

  String str = "";
  if(server.hasArg("str")) {
    str = server.arg("str");
  }
  
  int mod = 0;
  if(server.hasArg("mod")) {
    String modStr = server.arg("mod");
    mod = modStr.toInt();
  }
  
  if(str.length() != 0) {
    byte cmd = (1<<4)|(mod&0xf);
    for(int i=0; i<str.length(); i++) {
      i2cWrite(cmd);
      i2cWrite(str.charAt(i));
    }
    responseCode = 200;
  }

  if(responseCode == 200) {
    server.send(responseCode, "text/json", responseSuccess);
  } else {
    server.send(responseCode, "text/json", responseFail);
  }  
}

void kbdSpecial() {
  // This function sends a non-ascii keystroke to the Pro Micro
  WiFiClient client = server.client();
  int responseCode = 500;

  int chr = 0;
  if(server.hasArg("chr")) {
    String chrStr = server.arg("chr");
    chr = chrStr.toInt();
  }
  
  int mod = 0;
  if(server.hasArg("mod")) {
    String modStr = server.arg("mod");
    mod = modStr.toInt();
  }
  
  if(chr != 0) {
    byte cmd = (1<<4)|(mod&0xf);
    i2cWrite(cmd);
    i2cWrite((byte)chr);
    responseCode = 200;
  }
  
  if(responseCode == 200) {
    server.send(responseCode, "text/json", responseSuccess);
  } else {
    server.send(responseCode, "text/json", responseFail);
  }  
}


void usbRst() {
  // This pulls the reset pin on the Pro Micro
  // Sometimes, it gets hung if the computer goes to sleep
  WiFiClient client = server.client();
  resetPM();
  server.send(200, "text/json", responseSuccess);
}


void usbState() {
  // Get the state of the Pro Micro, based on
  // whether or not it is responding to i2c requests
  String response = "{\"r\":\"200\",\"state\":\"";
  response = response+pmConnected+"\"}";
  server.send(200, "text/json", response);
}

void sendReport() {
  // Send the report we cached from the PC
  WiFiClient client = server.client();
  server.send(200, "text/json", "");
  sendLargeBuffer(client, (const char*)reportBuffer, strlen((const char*)reportBuffer));
}

void setup() {
  // Kick off the serial.  R
  Serial.begin(115200);
  delay(100);
  Serial.println("");
  Serial.println("oak startup");

  // Calculate the length of the HTML file, which shouldn't change
  htmlLen = strlen(htmlFile);

  // Configure the Pro Micro reset pin
  pinMode(PIN_PM_RESET, OUTPUT);
  digitalWrite(PIN_PM_RESET, HIGH);

  // Start the i2c communication
  Wire.begin(I2C_ADDRESS_OAK);
  i2cWrite(0);
  reportTime = millis();

  // Register handlers for the web server
  server.on("/", handleRoot);
  server.on("/report", sendReport);
  server.on("/usbrst", usbRst);
  server.on("/usbstate", usbState);
  server.on("/kbdstr", kbdStr);
  server.on("/kbdspec", kbdSpecial);
  server.begin();

}

void getReport() {
  // This function requests the report from the PC,
  // which is cached in the Pro Micro
  int i = 0;
  i2cWrite(0);

  reportBuffer[0] = 0;
  delay(10);

  while(Wire.available()) {
    Serial.println(Wire.read());
    i++;
    if(i>50) break;
  }

  // We can't request bytes unless we know how many.
  // The first step is to get the report size from the Pro Micro.
  // Ask for 2 bytes of size
  Wire.requestFrom(I2C_ADDRESS_PM,2);
  if(!Wire.available()) {
    Serial.println("Error getting report length...");
    return;
  }

  byte hibyte = Wire.read();
  byte lobyte = Wire.read();

  // This is the report length
  int reportLen = (hibyte&0xf)<<8|lobyte;

  if(reportLen > REPORT_BUFFER_SIZE) {
    Serial.println("Report is too large!");
    return;
  }

  // There are limitations in the Arduino Wire library
  // that mean we can only ask for 32 bytes or less per transfer.
  // This loop breaks the report into 32 byte chunks and requests it
  i = 0;
  int requestLeft = 0;
  while(1) {
    if(requestLeft == 0) {
      if(reportLen < I2C_REQUEST_MAX) requestLeft = reportLen;  
      else requestLeft = I2C_REQUEST_MAX;
      if(requestLeft == 0) break;
      reportLen -= requestLeft;
      Wire.requestFrom(I2C_ADDRESS_PM, requestLeft);
      if(!Wire.available()) {
        Serial.println("Error requesting report chunk...");
        reportBuffer[0] = 0;
        return;
      }
      while(Wire.available() && requestLeft != 0) {
        reportBuffer[i] = Wire.read();
        i++;
        requestLeft--;
      }
      requestLeft = 0;
    }
    // Make sure we 0-terminate the buffer, so we can calcluate the size
    // using strlen()
    reportBuffer[i] = 0;
  }
}

long lastLoopTime = 0;

long getReportTime = 0;
long outsideTime = 0;
long handleClientTime = 0;

void printProfile() {
  Serial.print("Profile: getReportTime = ");
  Serial.print(getReportTime);
  Serial.print(" handleClientTime = ");
  Serial.print(handleClientTime);
  Serial.print(" outsideTime = ");
  Serial.println(outsideTime);

  getReportTime = 0;
  outsideTime = 0;
  handleClientTime = 0;  
}

void loop() {

  if(lastLoopTime != 0) {
    outsideTime += millis() - lastLoopTime;
  }
  
  
  // This is a debug interface for manually sending keyboard commands.
  // It's not strictly required...
  if(Serial.available()) {
    byte byteRead = Serial.read();
    Serial.print("new byte: 0x");
    Serial.println(byteRead, HEX);
    i2cWrite(0x12);
    i2cWrite(byteRead);
  }

  // Only get the report every so often.
  // You don't want to call delay() in your loop, because that blocks
  // the CPU.  If we don't have work to do, we should release control
  // as fast as possible.
  // (Actually, in the ESP8266 Arduino code, they yield during delay(),
  // but I still consider it a bad practice...)
  long now = millis();
  if(now > reportTime+5000) {
    //Serial.println("report time!");
    reportTime = now;
    getReport();
    getReportTime += millis() - reportTime;
    printProfile();
    Serial.println((const char*)reportBuffer);
  }

  // This must be called for the web server to operate.
  now = millis();
  server.handleClient();
  handleClientTime += millis() - now;

  lastLoopTime = millis();
}
