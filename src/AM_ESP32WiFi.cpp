/*

   AMController libraries, example sketches (“The Software”) and the related documentation (“The Documentation”) are supplied to you
   by the Author in consideration of your agreement to the following terms, and your use or installation of The Software and the use of The Documentation
   constitutes acceptance of these terms.
   If you do not agree with these terms, please do not use or install The Software.
   The Author grants you a personal, non-exclusive license, under author's copyrights in this original software, to use The Software.
   Except as expressly stated in this notice, no other rights or licenses, express or implied, are granted by the Author, including but not limited to any
   patent rights that may be infringed by your derivative works or by other works in which The Software may be incorporated.
   The Software and the Documentation are provided by the Author on an "AS IS" basis.  THE AUTHOR MAKES NO WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT
   LIMITATION THE IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, REGARDING THE SOFTWARE OR ITS USE AND OPERATION
   ALONE OR IN COMBINATION WITH YOUR PRODUCTS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL OR CONSEQUENTIAL DAMAGES (INCLUDING,
   BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE,
   REPRODUCTION AND MODIFICATION OF THE SOFTWARE AND OR OF THE DOCUMENTATION, HOWEVER CAUSED AND WHETHER UNDER THEORY OF CONTRACT, TORT (INCLUDING NEGLIGENCE),
   STRICT LIABILITY OR OTHERWISE, EVEN IF THE AUTHOR HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   Author: Fabrizio Boco - fabboco@gmail.com

   All rights reserved

*/
#include <Arduino.h>
#include <AM_ESP32WiFi.h>

#if defined(ALARMS_SUPPORT)

AMController::AMController(WiFiServer *server,
                           void (*doWork)(void),
                           void (*doSync)(void),
                           void (*processIncomingMessages)(char *variable, char *value),
                           void (*processOutgoingMessages)(void),
#if defined(ALARMS_SUPPORT)
                           void (*processAlarms)(char *alarm),
#endif
                           void (*deviceConnected)(void),
                           void (*deviceDisconnected)(void)
                          ) : AMController(server, doWork, doSync, processIncomingMessages, processOutgoingMessages, deviceConnected, deviceDisconnected)
{
#ifdef ALARMS_SUPPORT

  _processAlarms = processAlarms;
  _startTime = 0;
  _lastAlarmCheck = 0;
#endif
}
#endif

AMController::AMController(WiFiServer *server,
                           void (*doWork)(void),
                           void (*doSync)(void),
                           void (*processIncomingMessages)(char *variable, char *value),
                           void (*processOutgoingMessages)(void),
                           void (*deviceConnected)(void),
                           void (*deviceDisconnected)(void)
                          ) {

  _var = true;
  _idx = 0;
  _server = server;
  _doWork = doWork;
  _doSync = doSync;
  _processIncomingMessages = processIncomingMessages;
  _processOutgoingMessages = processOutgoingMessages;
  _deviceConnected = deviceConnected;
  _deviceDisconnected = deviceDisconnected;
  _initialized = false;
  _pClient = NULL;

  _variable[0] = '\0';
  _value[0]    = '\0';

#ifdef ALARMS_SUPPORT
  _processAlarms = NULL;
#endif
}

void AMController::loop(void) {
  this->loop(150);
}

void AMController::loop(unsigned long loopDelay) {

  if (!_initialized) {
    _initialized = true;
    _server->begin();
    //Serial.println("Initialized");

#ifdef ALARMS_SUPPORT
    this->initializeAlarms();
#endif

    delay(loopDelay);
  }

#ifdef ALARMS_SUPPORT

  if ( (millis() / 1000 < 20 && _startTime == 0) ) {

    this->syncTime();
    _startTime = 100;
  }

  if (_processAlarms != NULL) {

    unsigned long now = _startTime + millis() / 1000;

    if ( (now - _lastAlarmCheck) > ALARM_CHECK_INTERVAL) {

      _lastAlarmCheck = now;
      this->checkAndFireAlarms();
    }
  }

#endif

  _doWork();

  WiFiClient localClient = _server->available();
  _pClient = &localClient;

  if (localClient) {

    // Client connected

    if (_deviceConnected != NULL) {

      delay(250);
      _deviceConnected();
      delay(250);
    }

    while (_pClient->connected()) {

      // Read incoming messages if any
      this->readVariable();

      if (strcmp(_value, "Start") > 0 && strcmp(_variable, "Sync") == 0) {
        // Process sync messages for the variable _value
        _doSync();
      }
      else {

#ifdef ALARMS_SUPPORT
        // Manages Alarm creation and update requests

        char id[8];
        unsigned long time;

        if (strlen(_value) > 0 && strcmp(_variable, "$AlarmId$") == 0) {
          strcpy(id, _value);

        } else if (strlen(_value) > 0 && strcmp(_variable, "$AlarmT$") == 0) {
          time = atol(_value);
        }
        else if (strlen(_value) > 0 && strcmp(_variable, "$AlarmR$") == 0) {

          if (time == 0)
            this->removeAlarm(id);
          else
            this->createUpdateAlarm(id, time, atoi(_value));
        }
        else
#endif

#ifdef SD_SUPPORT
            if (strlen(_variable) > 0 && strcmp(_variable, "SD") == 0) {
#ifdef DEBUG
              Serial.println("List of Files");
#endif
              File root = SD.open("/");

              if (!root) {
#ifdef DEBUG
                Serial.println("Failed to open /");
#endif
                return;
              }

              root.rewindDirectory();

              File entry =  root.openNextFile();

              if (!entry) {
#ifdef DEBUG
                Serial.println("Failed to open file");
#endif
                return;
              }

              while (entry) {

                Serial.println(entry.name());
                if (!entry.isDirectory()) {

                  this->writeTxtMessage("SD", entry.name());
#ifdef DEBUG
                  Serial.println(entry.name());
#endif
                }
                entry.close();

                entry =	 root.openNextFile();
              }

              root.close();

              uint8_t buffer[10];
              strcpy((char *)&buffer[0], "SD=$EFL$#");
              _pClient->write(buffer, 9 * sizeof(uint8_t));
#ifdef DEBUG
              Serial.println("File list sent");
#endif
            } else if (strlen(_variable) > 0 && strcmp(_variable, "$SDDL$") == 0) {

#ifdef DEBUG
              Serial.print("File: "); Serial.println(_value);
#endif
              File dataFile = SD.open(_value, FILE_READ);

              if (dataFile) {

                unsigned long n = 0;
                uint8_t buffer[64];

                strcpy((char *)&buffer[0], "SD=$C$#");
                _pClient->write(buffer, 7 * sizeof(uint8_t));

                delay(3000); // OK

                while (dataFile.available()) {

                  n = dataFile.read(buffer, sizeof(buffer));
                  _pClient->write(buffer, n * sizeof(uint8_t));
                }

                strcpy((char *)&buffer[0], "SD=$E$#");
                _pClient->write(buffer, 7 * sizeof(uint8_t));

                delay(150);

                dataFile.close();
#ifdef DEBUG
                Serial.print("Fine Sent");
#endif
              }

              _pClient->flush();
            }
#endif

#ifdef SDLOGGEDATAGRAPH_SUPPORT
        if (strlen(_variable) > 0 && strcmp(_variable, "$SDLogData$") == 0) {

#ifdef DEBUG
          Serial.print("Logged data request for: "); Serial.println(_value);
#endif
          sdSendLogData(_value);
        }
#endif
        if (strlen(_variable) > 0 && strlen(_value) > 0) {

          // Process incoming messages
          _processIncomingMessages(_variable, _value);
        }
      }

#ifdef ALARMS_SUPPORT
      // Check and Fire Alarms
      if (_processAlarms != NULL) {

        unsigned long now = _startTime + millis() / 1000;

        if ( (now - _lastAlarmCheck) > ALARM_CHECK_INTERVAL) {

          _lastAlarmCheck = now;
          this->checkAndFireAlarms();
        }
      }
#endif

      // Write outgoing messages
      _processOutgoingMessages();

#ifdef ALARMS_SUPPORT

      // Sync local time with NTP Server

#endif

      _doWork();

      delay(loopDelay);
    }

    // Client disconnected

    localClient.flush();
    localClient.stop();
    _pClient = NULL;

    if (_deviceDisconnected != NULL)
      _deviceDisconnected();
  }
}

void AMController::readVariable(void) {

  _variable[0] = '\0';
  _value[0] = '\0';
  _var = true;
  _idx = 0;

  while (_pClient->available()) {

    char c = _pClient->read();

    if (isprint (c)) {

      if (c == '=') {

        _variable[_idx] = '\0';
        _var = false;
        _idx = 0;
      }
      else {
        if (c == '#') {

          _value[_idx] = '\0';
          _var = true;
          _idx = 0;

          return;
        }
        else {

          if (_var) {

            if (_idx == VARIABLELEN)
              _variable[_idx] = '\0';
            else
              _variable[_idx++] = c;
          }
          else {

            if (_idx == VALUELEN)
              _value[_idx] = '\0';
            else
              _value[_idx++] = c;
          }
        }
      }
    }
  }
}

void AMController::writeMessage(const char *variable, int value) {
  char buffer[VARIABLELEN + VALUELEN + 3];

  if (_pClient == NULL)
    return;

  snprintf(buffer, VARIABLELEN + VALUELEN + 3, "%s=%d#", variable, value);

  _pClient->write((const uint8_t *)buffer, strlen(buffer)*sizeof(char));
}


void AMController::writeMessage(const char *variable, float value) {
  char buffer[VARIABLELEN + VALUELEN + 3];

  if (_pClient == NULL)
    return;

  snprintf(buffer, VARIABLELEN + VALUELEN + 3, "%s=%.3f#", variable, value);
  _pClient->write((const uint8_t *)buffer, strlen(buffer)*sizeof(char));
}

void AMController::writeTxtMessage(const char *variable, const char *value) {
  char buffer[128];
  snprintf(buffer, 128, "%s=%s#", variable, value);
  _pClient->write((const uint8_t *)buffer, strlen(buffer)*sizeof(char));
}

void AMController::writeTripleMessage(const char *variable, float vX, float vY, float vZ) {

  char buffer[VARIABLELEN + VALUELEN + 3];
  char vbufferAx[VALUELEN];
  char vbufferAy[VALUELEN];
  char vbufferAz[VALUELEN];

  dtostrf(vX, 0, 2, vbufferAx);
  dtostrf(vY, 0, 2, vbufferAy);
  dtostrf(vZ, 0, 2, vbufferAz);
  snprintf(buffer, VARIABLELEN + VALUELEN + 3, "%s=%s:%s:%s#", variable, vbufferAx, vbufferAy, vbufferAz);

  _pClient->write((const uint8_t *)buffer, strlen(buffer)*sizeof(char));
}

void AMController::temporaryDigitalWrite(uint8_t pin, uint8_t value, unsigned long ms) {
  boolean previousValue = digitalRead(pin);

  digitalWrite(pin, value);
  delay(ms);
  digitalWrite(pin, previousValue);
}

void AMController::log(const char *msg) {
  this->writeTxtMessage("$D$", msg);
}

void AMController::log(int msg) {
  char buffer[11];
  itoa(msg, buffer, 10);

  this->writeTxtMessage("$D$", buffer);
}

void AMController::logLn(const char *msg) {
  this->writeTxtMessage("$DLN$", msg);
}

void AMController::logLn(int msg) {
  char buffer[11];
  itoa(msg, buffer, 10);

  this->writeTxtMessage("$DLN$", buffer);
}

void AMController::logLn(long msg) {
  char buffer[11];
  ltoa(msg, buffer, 10);

  this->writeTxtMessage("$DLN$", buffer);
}

void AMController::logLn(unsigned long msg) {

  char buffer[11];
  ltoa(msg, buffer, 10);

  this->writeTxtMessage("$DLN$", buffer);
}


#ifdef ALARMS_SUPPORT

void AMController::syncTime() {

#ifdef DEBUG
  Serial.print("Synchronizing time ...");
#endif

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

  time_t rawtime;
  time (&rawtime);
  struct tm *gmtInfo = gmtime(&rawtime);

  time_t timeSinceEpoch = mktime(gmtInfo);

  Serial.println(timeSinceEpoch);
}

void AMController::initializeAlarms() {

#ifdef DEBUG
  Serial.println("Initializing alarms ....");
#endif

  if (!EEPROM.begin(1 + MAX_ALARMS * sizeof(Alarm))) {

    Serial.println("failed to initialise EEPROM");
    delay(2000);
    return;
  }

  if (EEPROM.read(0) != 0xEE) {

    Serial.println("Creating Alarms");

    EEPROM.write(0, 0xEE);

    Alarm alarms[MAX_ALARMS];
    Alarm a;

    a.id[0] = '\0';
    a.time = 0;
    a.repeat = false;

    for (int i = 0; i < MAX_ALARMS; i++) {

      alarms[i] = a;
    }

    uint8_t *p = (uint8_t *)&alarms;

    for (int i = 0; i < sizeof(alarms); i++) {

      EEPROM.write(i + 1, *(p + i));
    }

    EEPROM.commit();
  }

#ifdef DEBUG
  //dumpAlarms();
#endif
}

unsigned long AMController::now() {

  time_t now;
  time (&now);

  return now;
}

void AMController::dumpAlarms() {

  Alarm alarms[MAX_ALARMS];

  uint8_t *p = (uint8_t *)&alarms;

  for (int i = 0; i < sizeof(alarms); i++) {

    *(p + i) = EEPROM.read(i + 1);
  }

  Serial.println("--- Alarms ---");

  for (int i = 0; i < MAX_ALARMS; i++) {

    Alarm a = alarms[i];

    time_t rawtime = a.time;
    struct tm *timeinfo = gmtime(&rawtime);

    Serial.print("\tId: "); Serial.print(a.id);
    Serial.print(" Time: "); Serial.print(timeinfo, "%A, %B %d %Y %H:%M:%S GMT");
    Serial.print(" Repeat: "); Serial.println(a.repeat);
  }

  Serial.println("--------------");
}

void AMController::createUpdateAlarm(char *id, unsigned long time, bool repeat) {

  Alarm alarms[MAX_ALARMS];

  uint8_t *p = (uint8_t *)&alarms;

  for (int i = 0; i < sizeof(alarms); i++) {

    *(p + i) = EEPROM.read(i + 1);
  }

  boolean update = false;
  boolean create = false;

  for (int i = 0; i < MAX_ALARMS; i++) {

    if (strcmp(alarms[i].id, id) == 0) {
      // Update
      alarms[i].time = time;
      alarms[i].repeat = repeat;
      update = true;
    }
  }

  if (!update) {

    for (int i = 0; i < MAX_ALARMS; i++) {

      if (strcmp(alarms[i].id, "") == 0) {
        // Create
        strcpy(alarms[i].id, id);
        alarms[i].time = time;
        alarms[i].repeat = repeat;
        create = true;
        break;
      }
    }
  }

  if (create || update) {

    uint8_t *p = (uint8_t *)&alarms;

    for (int i = 0; i < sizeof(alarms); i++) {

      EEPROM.write(i + 1, *(p + i));
    }

    EEPROM.commit();
  }

#ifdef DEBUG
  dumpAlarms();
#endif
}

void AMController::removeAlarm(char *id) {

  Alarm alarms[MAX_ALARMS];

  uint8_t *p = (uint8_t *)&alarms;

  for (int i = 0; i < sizeof(alarms); i++) {

    *(p + i) = EEPROM.read(i + 1);
  }

  boolean update = false;

  for (int i = 0; i < MAX_ALARMS; i++) {

    if (strcmp(alarms[i].id, id) == 0) {
      // Update
      alarms[i].id[0] = '\0';
      alarms[i].time = 0;
      alarms[i].repeat = false;
      update = true;
    }
  }

  if (update) {

    uint8_t *p = (uint8_t *)&alarms;

    for (int i = 0; i < sizeof(alarms); i++) {

      EEPROM.write(i + 1, *(p + i));
    }

    EEPROM.commit();
  }

#ifdef DEBUG
  dumpAlarms();
#endif
}

void AMController::checkAndFireAlarms() {

  time_t now;
  time (&now);

#ifdef DEBUG
  struct tm *gmtInfo = gmtime(&now);
  Serial.print("Checking Alarms at ");
  Serial.println(gmtInfo, "%A, %B %d %Y %H:%M:%S GMT");
#endif

  Alarm alarms[MAX_ALARMS];

  uint8_t *p = (uint8_t *)&alarms;

  for (int i = 0; i < sizeof(alarms); i++) {

    *(p + i) = EEPROM.read(i + 1);
  }

  for (int i = 0; i < MAX_ALARMS; i++) {

    if (alarms[i].id[0] != '\0' && alarms[i].time <= now) {

#ifdef DEBUG
      Serial.print("Firing: "); Serial.println(alarms[i].id);
#endif

      _processAlarms(alarms[i].id);

      if (alarms[i].repeat) {

        alarms[i].time += 86400; // Scheduled again tomorrow

#ifdef DEBUG
        time_t rawtime = alarms[i].time;
        struct tm *timeinfo = gmtime(&rawtime);

        Serial.print("Alarm rescheduled at ");
        Serial.print(timeinfo, "%A, %B %d %Y %H:%M:%S GMT");
        Serial.println();
#endif
      }
      else {

        //Alarm removed

        alarms[i].id[0] = '\0';
        alarms[i].time = 0;
      }

    }
  }

  p = (uint8_t *)&alarms;

  for (int i = 0; i < sizeof(alarms); i++) {

    EEPROM.write(i + 1, *(p + i));
  }

  EEPROM.commit();

#ifdef DEBUG
  this->dumpAlarms();
#endif
}

#endif

#ifdef SDLOGGEDATAGRAPH_SUPPORT

void AMController::sdLogLabels(const char *variable, const char *label1) {

  this->sdLogLabels(variable, label1, NULL, NULL, NULL, NULL);
}

void AMController::sdLogLabels(const char *variable, const char *label1, const char *label2) {

  this->sdLogLabels(variable, label1, label2, NULL, NULL, NULL);
}

void AMController::sdLogLabels(const char *variable, const char *label1, const char *label2, const char *label3) {

  this->sdLogLabels(variable, label1, label2, label3, NULL, NULL);
}

void AMController::sdLogLabels(const char *variable, const char *label1, const char *label2, const char *label3, const char *label4) {

  this->sdLogLabels(variable, label1, label2, label3, label4, NULL);
}

void AMController::sdLogLabels(const char *variable, const char *label1, const char *label2, const char *label3, const char *label4, const char *label5) {

  char fileNameBuffer[VARIABLELEN + 1];

  strcpy(fileNameBuffer, "/");
  strcat(fileNameBuffer, variable);

  File dataFile = SD.open(fileNameBuffer, FILE_APPEND);

  if (dataFile) {

    if (dataFile.size() > 0) {

#ifdef DEBUG
      Serial.print("No Labels required for "); Serial.println(variable);
#endif
      dataFile.close();
      return;
    }

    dataFile.print("-");
    dataFile.print(";");
    dataFile.print(label1);
    dataFile.print(";");

    if (label2 != NULL)
      dataFile.print(label2);
    else
      dataFile.print("-");
    dataFile.print(";");

    if (label3 != NULL)
      dataFile.print(label3);
    else
      dataFile.print("-");
    dataFile.print(";");

    if (label4 != NULL)
      dataFile.print(label4);
    else
      dataFile.print("-");
    dataFile.print(";");

    if (label5 != NULL)
      dataFile.println(label5);
    else
      dataFile.println("-");

    dataFile.flush();
    dataFile.close();
  } else {
#ifdef DEBUG
    Serial.print("Error opening"); Serial.println(variable);
#endif
  }
}


void AMController::sdLog(const char *variable, unsigned long time, float v1) {

  char fileNameBuffer[VARIABLELEN + 1];

  strcpy(fileNameBuffer, "/");
  strcat(fileNameBuffer, variable);

  File dataFile = SD.open(fileNameBuffer, FILE_APPEND);

  if (dataFile)
  {
    dataFile.print(time);
    dataFile.print(";");
    dataFile.print(v1);

    dataFile.print(";-;-;-;-");
    dataFile.println();

    dataFile.flush();
    dataFile.close();
  }
  else {
#ifdef DEBUG
    Serial.print("Error opening"); Serial.println(variable);
#endif
  }
}

void AMController::sdLog(const char *variable, unsigned long time, float v1, float v2) {

  char fileNameBuffer[VARIABLELEN + 1];

  strcpy(fileNameBuffer, "/");
  strcat(fileNameBuffer, variable);

  File dataFile = SD.open(fileNameBuffer, FILE_APPEND);

  if (dataFile && time > 0)
  {
    dataFile.print(time);
    dataFile.print(";");
    dataFile.print(v1);
    dataFile.print(";");

    dataFile.print(v2);

    dataFile.print(";-;-;-");
    dataFile.println();

    dataFile.flush();
    dataFile.close();
  }
  else {
#ifdef DEBUG
    Serial.print("Error opening"); Serial.println(variable);
#endif
  }
}

void AMController::sdLog(const char *variable, unsigned long time, float v1, float v2, float v3) {

  char fileNameBuffer[VARIABLELEN + 1];

  strcpy(fileNameBuffer, "/");
  strcat(fileNameBuffer, variable);

  File dataFile = SD.open(fileNameBuffer, FILE_APPEND);

  if (dataFile && time > 0)
  {
    dataFile.print(time);
    dataFile.print(";");
    dataFile.print(v1);
    dataFile.print(";");

    dataFile.print(v2);
    dataFile.print(";");

    dataFile.print(v3);

    dataFile.print(";-;-");
    dataFile.println();

    dataFile.flush();
    dataFile.close();
  }
  else {
#ifdef DEBUG
    Serial.print("Error opening"); Serial.println(variable);
#endif
  }
}

void AMController::sdLog(const char *variable, unsigned long time, float v1, float v2, float v3, float v4) {

  char fileNameBuffer[VARIABLELEN + 1];

  strcpy(fileNameBuffer, "/");
  strcat(fileNameBuffer, variable);

  File dataFile = SD.open(fileNameBuffer, FILE_APPEND);

  if (dataFile && time > 0)
  {
    dataFile.print(time);
    dataFile.print(";");
    dataFile.print(v1);
    dataFile.print(";");

    dataFile.print(v2);
    dataFile.print(";");

    dataFile.print(v3);
    dataFile.print(";");

    dataFile.print(v4);

    dataFile.println(";-");
    dataFile.println();

    dataFile.flush();
    dataFile.close();
  }
  else {
#ifdef DEBUG
    Serial.print("Error opening"); Serial.println(variable);
#endif
  }
}

void AMController::sdLog(const char *variable, unsigned long time, float v1, float v2, float v3, float v4, float v5) {

  char fileNameBuffer[VARIABLELEN + 1];

  strcpy(fileNameBuffer, "/");
  strcat(fileNameBuffer, variable);

  File dataFile = SD.open(fileNameBuffer, FILE_APPEND);

  if (dataFile && time > 0)
  {
    dataFile.print(time);
    dataFile.print(";");
    dataFile.print(v1);
    dataFile.print(";");

    dataFile.print(v2);
    dataFile.print(";");

    dataFile.print(v3);
    dataFile.print(";");

    dataFile.print(v4);
    dataFile.print(";");

    dataFile.println(v5);

    dataFile.println();

    dataFile.flush();
    dataFile.close();
  }
  else {
#ifdef DEBUG
    Serial.print("Error opening"); Serial.println(variable);
#endif
  }
}

void AMController::sdSendLogData(const char *variable) {

  char fileNameBuffer[VARIABLELEN + 1];

  strcpy(fileNameBuffer, "/");
  strcat(fileNameBuffer, variable);

  File dataFile = SD.open(fileNameBuffer);

  if (dataFile) {

    char c;
    char buffer[128];
    int i = 0;

    dataFile.seek(0);

    while ( dataFile.available() ) {

      c = dataFile.read();

      if (c == '\n') {

        buffer[i++] = '\0';
#ifdef DEBUG
        Serial.println(buffer);
#endif
        this->writeTxtMessage(variable, buffer);

        i = 0;
      }
      else
        buffer[i++] = c;
    }

#ifdef DEBUG
    Serial.println("All data sent");
#endif

    dataFile.close();
  }
  else {
#ifdef DEBUG
    Serial.print("Error opening "); Serial.println(variable);
#endif
  }

  this->writeTxtMessage(variable, "");
}

// Size in bytes
uint16_t AMController::sdFileSize(const char *variable) {

  char fileNameBuffer[VARIABLELEN + 1];

  strcpy(fileNameBuffer, "/");
  strcat(fileNameBuffer, variable);

  File dataFile = SD.open(fileNameBuffer, FILE_READ);

  if (dataFile) {

#ifdef DEBUG
    Serial.print("Size of "); Serial.print(variable); Serial.print(" :"); Serial.println(dataFile.size());
#endif

    return dataFile.size();
  }

  return -1;
}

void AMController::sdPurgeLogData(const char *variable) {

  noInterrupts();

  char fileNameBuffer[VARIABLELEN + 1];

  strcpy(fileNameBuffer, "/");
  strcat(fileNameBuffer, variable);

  SD.remove(fileNameBuffer);

  interrupts();
}

#endif


#ifdef TWITTER_SUPPORT

void AMController::setTwitterKeys(char *consumerKey, char *consumerSecret, char *tokenKey, char *tokenSecret) {

  _pTwitterSender = new TwitterSender(consumerKey, consumerSecret, tokenKey, tokenSecret);
}

void AMController::checkTwitter() {

  FileManager	fileManager;

#ifdef DEBUG
  Serial.println("checkTwitter");
  dumpTwitter();
#endif

  if (_twitterClient.connect("api.twitter.com", 443)) {

#ifdef DEBUG
    Serial.println("connected to Twitter");
#endif

    time_t now;
    time (&now);

    for (int i = 0; i < MAX_TWITTER; i++) {

      TwitterInformation a;

      if (!fileManager.read(_twitterFile, i, (uint8_t *)&a, sizeof(a))) {

        _twitterClient.flush();
        _twitterClient.stop();
        return;
      }

#ifdef DEBUG
      Serial.print("Checking "); Serial.println(a.getId());
#endif

      if (_checkTwitter(a.getId(), a.getValue())) {

        Serial.print("Sending Message for "); Serial.println(a.getId());

        char *pMessage = _pTwitterSender->composeMessage(a.getMessage(), a.getUser(), now);

        Serial.println(pMessage);

        if (pMessage == NULL)
          return;

        _twitterClient.print("POST ");
        _twitterClient.print(pMessage);
        _twitterClient.println(" HTTP/1.1");
        _twitterClient.println("Host: api.twitter.com");
        _twitterClient.println("Content-Length: 0");
        _twitterClient.println("X-Target-URI: https://api.twitter.com");
        _twitterClient.println();
        free(pMessage);


#ifdef DEBUG
        Serial.println("Sent?");
#endif

        delay(500);
#ifdef DEBUG
        while (_twitterClient.available()) {
          char c = _twitterClient.read();
          Serial.write(c);
        }
#endif
        Serial.println();
      }
    }

    _twitterClient.flush();
    _twitterClient.stop();
#ifdef DEBUG
    Serial.println("Disconnected from Twitter");
#endif

  }
}

#ifdef DEBUG
void AMController::dumpTwitter() {

  FileManager	fileManager;

  Serial.println("\t----Dump Twitter Info -----");

  for (int i = 0; i < MAX_TWITTER; i++) {

    TwitterInformation a;

    if (!fileManager.read(_twitterFile, i, (uint8_t *)&a, sizeof(a)))
      return;

    a.dump(Serial);

    float x = Serial;

  }
}
#endif

void AMController::removeTwitter(char *id) {

  FileManager				fileManager;
  TwitterInformation 		a;
  int 					pos;
  pos = fileManager.find(_twitterFile, (uint8_t*)&a, sizeof(a), &checkT, id);

  if (pos > -1) {

    fileManager.remove(_twitterFile, pos, sizeof(a));
  }
}

void AMController::createUpdateTwitter(TwitterInformation &t) {

  FileManager			fileManager;
  TwitterInformation 	a;
  int 				pos;
  pos = fileManager.find(_twitterFile, (uint8_t*)&a, sizeof(a), &checkT, t.getId());

  if (pos > -1) {

    a.setValue(t.getValue());
    a.setUser(t.getUser());
    a.setMessage(t.getMessage());

    fileManager.update(_twitterFile, pos, (uint8_t *)&a, sizeof(a));

#ifdef DEBUG
    dumpTwitter();
#endif

    return;
  }

  a.setId(t.getId());
  a.setValue(t.getValue());
  a.setUser(t.getUser());
  a.setMessage(t.getMessage());

  fileManager.append(_twitterFile, (uint8_t *)&a, sizeof(a));
}

#endif