/*
   Test Arduino Manager for iPad / iPhone / Mac

   A simple test program to show the Arduino Manager
   features.

   Author: Fabrizio Boco - fabboco@gmail.com

   Version: 2.0

   05/16/2021

   All rights reserved

*/

/*

   AMController libraries, example sketches (The Software) and the related documentation (The Documentation) are supplied to you
   by the Author in consideration of your agreement to the following terms, and your use or installation of The Software and the use of The Documentation
   constitutes acceptance of these terms.
   If you do not agree with these terms, please do not use or install The Software.
   The Author grants you a personal, non-exclusive license, under authors copyrights in this original software, to use The Software.
   Except as expressly stated in this notice, no other rights or licenses, express or implied, are granted by the Author, including but not limited to any
   patent rights that may be infringed by your derivative works or by other works in which The Software may be incorporated.
   The Software and the Documentation are provided by the Author on an AS IS basis.  THE AUTHOR MAKES NO WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT
   LIMITATION THE IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, REGARDING THE SOFTWARE OR ITS USE AND OPERATION
   ALONE OR IN COMBINATION WITH YOUR PRODUCTS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL OR CONSEQUENTIAL DAMAGES (INCLUDING,
   BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE,
   REPRODUCTION AND MODIFICATION OF THE SOFTWARE AND OR OF THE DOCUMENTATION, HOWEVER CAUSED AND WHETHER UNDER THEORY OF CONTRACT, TORT (INCLUDING NEGLIGENCE),
   STRICT LIABILITY OR OTHERWISE, EVEN IF THE AUTHOR HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/
#include <ESP32Servo.h>
#include <WiFi.h>
#include "time.h"
#include <AM_ESP32WiFi.h>
#include "arduino_secrets.h"

#define SD_SELECT 17

/*

   AMController Library configuration

*/
IPAddress ip(192, 168, 1, 233);
IPAddress dns(8, 8, 8, 8);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

char ssid[] = SECRET_SSID;  // your network SSID (name) i.g. "MYNETWORK"
char pass[] = SECRET_PASS;  // your network password i.g. "MYPASSWORD"

int status = WL_IDLE_STATUS;

WiFiServer server(80);

/**

   Other initializations

*/
#define YELLOWLEDPIN GPIO_NUM_5
int yellowLed = HIGH;

#define CONNECTIONPIN GPIO_NUM_16
int connectionLed = LOW;

#define POTENTIOMETERPIN GPIO_NUM_35
int pot;

#define TEMPERATUREPIN GPIO_NUM_32
float temperature;

#define SERVOPIN 4
Servo servo;
int servoPos;

unsigned long lastSample = 0;


/*

   Prototypes of amControllerESP32 callbacks


*/
void doWork();
void doSync();
void processIncomingMessages(char *variable, char *value);
void processOutgoingMessages();
void processAlarms(char *variable);
void deviceConnected();
void deviceDisconnected();
float getVoltage(int pin);
/*

   amController Library initialization

*/
#ifdef ALARMS_SUPPORT
AMController amController(&server, &doWork, &doSync, &processIncomingMessages, &processOutgoingMessages, &processAlarms, &deviceConnected, &deviceDisconnected);
#else
AMController amController(&server, &doWork, &doSync, &processIncomingMessages, &processOutgoingMessages, &deviceConnected, &deviceDisconnected);
#endif

void setup() {

  Serial.begin(9600);

  Serial.println("Start");

#if (defined(SD_SUPPORT))
  Serial.println("Initializing SD card...");
  delay(1000);
  // see if the card is present and can be initialized
  if (!SD.begin(SD_SELECT)) {
    Serial.println("Card failed, or not present");
  }
  else {
    Serial.println("Card initialized");
  }
#endif

  // attempt to connect to Wifi network
  WiFi.config(ip, gateway, subnet, dns);
  Serial.print("Attempting to connect to SSID: "); Serial.println(ssid);
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print("Attempting to connect to SSID: "); Serial.println(ssid);
  }

  // print your WiFi IP address

  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

#if defined(ALARMS_SUPPORT)

  // Configure time
  configTime(0, 0, "pool.ntp.org");

#endif

  /**

     Other initializations

  */

  // Yellow LED on
  pinMode(YELLOWLEDPIN, OUTPUT);
  digitalWrite(YELLOWLEDPIN, yellowLed);


  // Servo position at 90 degrees
  servo.attach(SERVOPIN);
  servoPos = 90;
  servo.write(servoPos);

  // Red LED OFF
  pinMode(CONNECTIONPIN, OUTPUT);
  digitalWrite(CONNECTIONPIN, connectionLed);


  pinMode(TEMPERATUREPIN, INPUT);

#ifdef SDLOGGEDATAGRAPH_SUPPORT

  if (amController.sdFileSize("TodayT") > 2000) {

    amController.sdPurgeLogData("TodayT");
    Serial.println("TodayT purged");
  }

  amController.sdLogLabels("TodayT", "T");

#endif
}

/**

   Standard loop function

*/
void loop() {
  amController.loop(200);
}

/**


  This function is called periodically and its equivalent to the standard loop() function

*/
void doWork() {

  temperature = getVoltage(TEMPERATUREPIN);  //getting the voltage reading from the temperature sensor
  temperature = (temperature) * 100;         // converting from 10 mv per degree

  digitalWrite(YELLOWLEDPIN, yellowLed);

  servo.write(servoPos);

  //  light = analogRead(LIGHTPIN);
  pot = analogRead(POTENTIOMETERPIN);

#if defined(SDLOGGEDATAGRAPH_SUPPORT)
  if ( (millis() - lastSample) > 5000) {

    lastSample = millis();

    Serial.println("Temperature stored");

    amController.sdLog("TodayT", amController.now(), temperature);
  }
#endif
}


/**


  This function is called when the ios device connects and needs to initialize the position of switches and knobs

*/
void doSync () {

  amController.writeMessage("Knob1", (float)map(servo.read(), 0, 180, 0, 1023));
  amController.writeMessage("S1", yellowLed);
  amController.writeTxtMessage("Msg", "Hello, I'm your ESP32 board");
}

/**


  This function is called when a new message is received from the iOS device

*/
void processIncomingMessages(char *variable, char *value) {

  //Serial.print(variable); Serial.print( : ); Serial.println(value);

  if (strcmp(variable, "S1") == 0) {

    yellowLed = atoi(value);
  }

  if (strcmp(variable, "Knob1") == 0) {

    servoPos = atoi(value);
    servoPos = map(servoPos, 0, 1023, 0, 180);
  }

  if (strcmp(variable, "Push1") == 0) {

    amController.temporaryDigitalWrite(CONNECTIONPIN, LOW, 500);
  }

  if (strcmp(variable, "Cmd_01") == 0) {

    amController.log("Command: "); amController.logLn(value);

    if (strcmp(value, "blink") == 0) {
      amController.temporaryDigitalWrite(CONNECTIONPIN, LOW, 500);
    }

    Serial.print("Command: ");
    Serial.println(value);
  }
}

/**


  This function is called periodically and messages can be sent to the iOS device

*/
void processOutgoingMessages() {

  amController.writeMessage("T", temperature);
  amController.writeMessage("Led13", yellowLed);
  amController.writeMessage("Pot", pot);
}


#if defined(ALARMS_SUPPORT)
/**

  This function is called when a Alarm is fired

*/
void processAlarms(char *alarm) {

  Serial.print(alarm); Serial.println(" fired");

  amController.temporaryDigitalWrite(CONNECTIONPIN, LOW, 500);
  servoPos = 0;
}
#endif


/**


  This function is called when the iOS device connects

*/
void deviceConnected () {

  digitalWrite(CONNECTIONPIN, HIGH);
  Serial.println("Device connected");
}

/**


  This function is called when the iOS device disconnects

*/
void deviceDisconnected () {

  Serial.println("Device disconnected");
  digitalWrite(CONNECTIONPIN, LOW);
}

/**

  Auxiliary functions

*/

/*
   getVoltage()  returns the voltage on the analog input defined by pin

*/
float getVoltage(int pin) {

  return (analogRead(pin) * 3.3 / 2048.0 * 1);   // converting from a 0 to 4096 digital range into voltage
}