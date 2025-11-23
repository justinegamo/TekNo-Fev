  #include <SoftwareSerial.h>
  #include <DFRobotDFPlayerMini.h>
  #include <Servo.h>

  // ========== PIN DEFINITIONS ==========
  #define flameSensorPin 7
  #define mq2SensorPin 8
  #define mq2AnalogPin A0
  #define buzzerPin 6
  #define servoPin 9

  #define ledReady 4        
  #define ledFire 5         
  #define ledSmoke 12      

  // ========== PHONE NUMBERS ==========
  const String smsRecipients[] = {
    "+639525008752",   
    "+639928820854",   
    "+639987654321"     
  };
  const int smsRecipientCount = 3;

  // NUMBER FOR CALL !
  const String emergencyCallNumber = "+639525008752";

  // ========== CONFIGURATION ==========
  const int dfplayerVolume = 30;
  const int fireMessageTrack = 1;
  const int gasMessageTrack = 2;
  const int emergencyMessageTrack = 3;

  const int servoScanDelay = 30;
  const int smokeThreshold = 400;
  const unsigned long callDelay = 3000;

  // Auto-reset timing 
  const unsigned long autoResetDelay = 50000; 
  unsigned long alertCompletionTime = 0;
  bool alertSequenceComplete = false;

  // Startup beep patterns
  const int beepStartup = 100;
  const int beepReady = 200;
  const int beepAlert = 500;
  const int beepEmergency = 1000;

  // ========== HARDWARE OBJECTS PIN==========
  SoftwareSerial dfPlayerSerial(10, 11);
  DFRobotDFPlayerMini dfPlayer;
  SoftwareSerial sim800lSerial(3, 2);
  Servo scanServo;

  // ========== STATE VARIABLES TO BE USE IN CONDITION ==========
  bool fireDetected = false;
  bool smokeDetected = false;
  bool emergencyMode = false;
  bool fireAlertSent = false;
  bool smokeAlertSent = false;
  bool fireCallMade = false;
  bool systemReady = false;

  int currentServoAngle = 0;
  int flameDetectionAngle = -1;
  bool scanningEnabled = true;

  bool inCall = false;
  bool messagePlayed = false;
  unsigned long callStartTime = 0;
  unsigned long callEndTime = 0;

  // LED blink timing for emergency mode
  unsigned long lastLedBlink = 0;
  bool ledState = false;

  enum SystemState {
    IDLE,
    SCANNING,
    FIRE_DETECTED,
    SMOKE_DETECTED,
    EMERGENCY,
    ALERT_COMPLETE
  };
  SystemState currentState = SCANNING;

  // ========== STARTUP BEEP PATTERNS ==========
  void beep(int duration) {
    digitalWrite(buzzerPin, HIGH);
    delay(duration);
    digitalWrite(buzzerPin, LOW);
    delay(100);
  }

  void startupBeeps() {
    for(int i = 0; i < 3; i++) {
      beep(beepStartup);
      delay(200);
    }
  }

  void readyBeeps() {
    beep(beepReady);
    delay(300);
    beep(beepReady);
  }

  void alertBeeps() {
    for(int i = 0; i < 5; i++) {
      beep(100);
      delay(100);
    }
  }

  // ========== LED CONTROL ==========
  void updateLedStatus() {
    if (emergencyMode && currentState != ALERT_COMPLETE) {
      // Blink red LED rapidly in emergency mode
      if (millis() - lastLedBlink >= 250) {
        ledState = !ledState;
        digitalWrite(ledFire, ledState);
        digitalWrite(ledSmoke, ledState);
        lastLedBlink = millis();
      }
      digitalWrite(ledReady, LOW);
    } else if (fireDetected && currentState != ALERT_COMPLETE) {
      digitalWrite(ledFire, HIGH);
      digitalWrite(ledSmoke, LOW);
      digitalWrite(ledReady, LOW);
    } else if (smokeDetected && currentState != ALERT_COMPLETE) {
      digitalWrite(ledFire, LOW);
      digitalWrite(ledSmoke, HIGH);
      digitalWrite(ledReady, LOW);
    } else if (systemReady) {
      digitalWrite(ledFire, LOW);
      digitalWrite(ledSmoke, LOW);
      digitalWrite(ledReady, HIGH);
    }
  }

  // ========== SYSTEM RESET FUNCTION ==========
  void resetSystem() {
    Serial.println(F("\n========================================"));
    Serial.println(F("  AUTO-RESET: Resuming Monitoring"));
    Serial.println(F("========================================"));
    
    // Reset all state variables
    fireDetected = false;
    smokeDetected = false;
    emergencyMode = false;
    fireAlertSent = false;
    smokeAlertSent = false;
    fireCallMade = false;
    flameDetectionAngle = -1;
    scanningEnabled = true;
    alertSequenceComplete = false;
    alertCompletionTime = 0;
    
    // Turn off buzzer
    activateBuzzer(false);
    
    // Reset servo to start position
    scanServo.write(0);
    currentServoAngle = 0;
    
    // Return to scanning state
    currentState = SCANNING;
    
    // Two beeps to indicate reset
    beep(beepReady);
    delay(200);
    beep(beepReady);
    
    Serial.println(F("System reset complete - Scanning resumed"));
  }

  void setup() {
    Serial.begin(9600);
    
    // Initialize LED pins
    pinMode(ledReady, OUTPUT);
    pinMode(ledFire, OUTPUT);
    pinMode(ledSmoke, OUTPUT);
    digitalWrite(ledReady, LOW);
    digitalWrite(ledFire, LOW);
    digitalWrite(ledSmoke, LOW);
    
    // Initialize other pins
    pinMode(flameSensorPin, INPUT);
    pinMode(mq2SensorPin, INPUT_PULLUP);
    pinMode(buzzerPin, OUTPUT);
    digitalWrite(buzzerPin, LOW);
    
    startupBeeps();
    
    // Blink all LEDs during startup
    for(int i = 0; i < 3; i++) {
      digitalWrite(ledReady, HIGH);
      digitalWrite(ledFire, HIGH);
      digitalWrite(ledSmoke, HIGH);
      delay(200);
      digitalWrite(ledReady, LOW);
      digitalWrite(ledFire, LOW);
      digitalWrite(ledSmoke, LOW);
      delay(200);
    }
    
    Serial.println(F("========================================"));
    Serial.println(F("  FIRE DETECTION SYSTEM - INITIALIZING"));
    Serial.println(F("========================================"));
    
    // Initialize servo
    scanServo.attach(servoPin);
    scanServo.write(0);
    currentServoAngle = 0;
    Serial.println(F("Servo initialized"));
    beep(beepStartup);
    
    // Initialize DFPlayer
    dfPlayerSerial.begin(9600);
    Serial.println(F("Initializing DFPlayer..."));
    delay(1000);
    
    if (!dfPlayer.begin(dfPlayerSerial)) {
      Serial.println(F("DFPlayer initialization failed!"));
      digitalWrite(ledFire, HIGH);
      beep(beepEmergency);
      digitalWrite(ledFire, LOW);
    } else {
      Serial.println(F("DFPlayer initialized"));
      dfPlayer.volume(dfplayerVolume);
      beep(beepStartup);
      delay(100);
    }
    
    // Initialize SIM800L
    sim800lSerial.begin(9600);
    Serial.println(F("Initializing SIM800L..."));
    digitalWrite(ledSmoke, HIGH);
    delay(3000);
    
    sendAtCommand("AT", 1000);
    sendAtCommand("AT+CMGF=1", 1000);
    sendAtCommand("AT+CLIP=1", 1000);
    Serial.println(F("SIM800L initialized"));
    beep(beepStartup);
    digitalWrite(ledSmoke, LOW);
    
    // Warm up MQ2 sensor
    Serial.println(F("MQ2 sensor warming up (30 seconds)..."));
    for(int i = 30; i > 0; i--) {
      digitalWrite(ledSmoke, i % 2);
      Serial.print(i);
      Serial.print("... ");
      delay(1000);
    }
    digitalWrite(ledSmoke, LOW);
    Serial.println(F("\nMQ2 sensor ready"));
    beep(beepStartup);
    
    // System ready
    systemReady = true;
    Serial.println(F("\n========================================"));
    Serial.println(F("  SYSTEM READY - CONTINUOUS MONITORING"));
    Serial.println(F("========================================"));
    Serial.print(F("SMS recipients: "));
    Serial.println(smsRecipientCount);
    Serial.print(F("Emergency call: "));
    Serial.println(emergencyCallNumber);
    Serial.println(F("Auto-reset after alerts: 50 seconds"));
    
    readyBeeps();
    digitalWrite(ledReady, HIGH);
    delay(2000);
  }

  void loop() {
    // Update LED status
    updateLedStatus();
    
    // ========== SENSOR MONITORING ==========
    checkSensors();
    
    // ========== SERVO SCANNING (if enabled) ==========
    if (scanningEnabled && currentState == SCANNING) {
      performServoScan();
    }
    
    // ========== STATE MACHINE ==========
    handleSystemState();
    
    // ========== AUTO-RESET AFTER ALERT COMPLETION ==========
    if (alertSequenceComplete && (millis() - alertCompletionTime >= autoResetDelay)) {
      resetSystem();
    }
    
    // ========== HANDLE INCOMING CALLS ==========
    if (sim800lSerial.available()) {
      String response = sim800lSerial.readString();
      Serial.print("SIM800L: ");
      Serial.println(response);
      
      if (response.indexOf("CALL READY") != -1 || 
          response.indexOf("VOICE CALL: BEGIN") != -1 ||
          (response.indexOf("OK") != -1 && inCall)) {
        if (!inCall) {
          inCall = true;
          callStartTime = millis();
          messagePlayed = false;
          Serial.println(F("Call connected"));
          beep(beepStartup);
        }
      }
      
      if (response.indexOf("NO CARRIER") != -1 || 
          response.indexOf("BUSY") != -1 ||
          response.indexOf("NO ANSWER") != -1) {
        inCall = false;
        messagePlayed = false;
        callEndTime = millis();
        Serial.println(F("Call ended"));
        beep(beepStartup);
        
        // Mark alert sequence as complete when call ends
        if (!alertSequenceComplete && fireCallMade) {
          alertSequenceComplete = true;
          alertCompletionTime = millis();
          currentState = ALERT_COMPLETE;
          Serial.println(F("\nAlert sequence complete."));
          Serial.println(F("System will auto-reset in 30 seconds..."));
        }
      }
    }
    
    // ========== PLAY MESSAGE DURING CALL ==========
    if (inCall && !messagePlayed && (millis() - callStartTime > callDelay)) {
      Serial.println(F("Playing emergency message..."));
      
      if (emergencyMode) {
        dfPlayer.play(emergencyMessageTrack);
      } else {
        dfPlayer.play(fireMessageTrack);
      }
      
      messagePlayed = true;
      delay(100);
    }
    
    delay(100);
  }

  // ========== SENSOR CHECKING ==========
  void checkSensors() {
    // Only check sensors if we're in scanning mode or not in alert complete state
    if (currentState == ALERT_COMPLETE) {
      return; // Don't detect new events during cooldown period
    }
    
    int flameValue = digitalRead(flameSensorPin);
    bool currentFlameDetected = (flameValue == LOW);
    
    int mq2Value = digitalRead(mq2SensorPin);
    int mq2Analog = analogRead(mq2AnalogPin);
    bool currentSmokeDetected = (mq2Value == LOW && mq2Analog > smokeThreshold);
    
    // Update fire detection state
    if (currentFlameDetected && !fireDetected) {
      fireDetected = true;
      flameDetectionAngle = currentServoAngle;
      scanningEnabled = false;
      
      Serial.println(F("\nFIRE DETECTED!"));
      Serial.print(F("Location: "));
      Serial.print(flameDetectionAngle);
      Serial.println(F(" degrees"));
      
      currentState = FIRE_DETECTED;
      alertBeeps();
      activateBuzzer(true);
    }
    
    // Update smoke detection state
    if (currentSmokeDetected && !smokeDetected) {
      smokeDetected = true;
      
      Serial.println(F("\nSMOKE/GAS DETECTED!"));
      Serial.print(F("MQ2 Analog Value: "));
      Serial.println(mq2Analog);
      
      if (fireDetected) {
        currentState = EMERGENCY;
        emergencyMode = true;
        Serial.println(F("\nEMERGENCY MODE: FIRE + GAS!"));
        for(int i = 0; i < 3; i++) {
          beep(beepEmergency);
          delay(200);
        }
      } else {
        currentState = SMOKE_DETECTED;
        alertBeeps();
        activateBuzzer(true);
      }
    }
  }

  // ========== SERVO SCANNING ==========
  void performServoScan() {
    static bool scanDirection = true;
    static unsigned long lastScanTime = 0;
    
    if (millis() - lastScanTime >= servoScanDelay) {
      if (scanDirection) {
        currentServoAngle += 5;
        if (currentServoAngle >= 180) {
          currentServoAngle = 180;
          scanDirection = false;
        }
      } else {
        currentServoAngle -= 5;
        if (currentServoAngle <= 0) {
          currentServoAngle = 0;
          scanDirection = true;
        }
      }
      
      scanServo.write(currentServoAngle);
      lastScanTime = millis();
    }
  }

  // ========== STATE MACHINE HANDLER ==========
  void handleSystemState() {
    switch (currentState) {
      case SCANNING:
        // Normal operation - continuous monitoring
        break;
        
      case FIRE_DETECTED:
        if (!fireAlertSent) {
          beep(beepStartup);
          sendSmsToAll("FIRE ALERT! Fire detected at " + String(flameDetectionAngle) + " degrees. Emergency response initiated.");
          fireAlertSent = true;
          beep(beepReady);
          delay(2000);
        }
        
        if (!fireCallMade) {
          beep(beepStartup);
          makeEmergencyCall(emergencyCallNumber);
          fireCallMade = true;
        }
        break;
        
      case SMOKE_DETECTED:
        if (!smokeAlertSent) {
          beep(beepStartup);
          sendSmsToAll("SMOKE/GAS ALERT! Hazardous gas or smoke detected. Please investigate immediately.");
          smokeAlertSent = true;
          beep(beepReady);
          
          // For smoke only, mark as complete immediately (no call)
          alertSequenceComplete = true;
          alertCompletionTime = millis();
          currentState = ALERT_COMPLETE;
          Serial.println(F("\nSmoke alert sent."));
          Serial.println(F("System will auto-reset in 30 seconds..."));
        }
        break;
        
      case EMERGENCY:
        if (!fireAlertSent) {
          beep(beepEmergency);
          sendSmsToAll("EMERGENCY! Fire AND gas detected at " + String(flameDetectionAngle) + " degrees. EVACUATE IMMEDIATELY!");
          fireAlertSent = true;
          delay(2000);
          beep(beepEmergency);
        }
        
        if (!fireCallMade) {
          makeEmergencyCall(emergencyCallNumber);
          fireCallMade = true;
        }
        
        // Continuous alarm
        if (!alertSequenceComplete) {
          activateBuzzer(true);
          delay(500);
          activateBuzzer(false);
          delay(500);
        }
        break;
        
      case ALERT_COMPLETE:
        // Waiting for auto-reset - turn off buzzer
        activateBuzzer(false);
        break;
    }
  }

  // ========== BUZZER CONTROL ==========
  void activateBuzzer(bool state) {
    digitalWrite(buzzerPin, state ? HIGH : LOW);
  }

  // ========== SMS SENDING TO MULTIPLE RECIPIENTS ==========
  void sendSmsToAll(String message) {
    Serial.println(F("\nSending SMS to multiple recipients..."));
    Serial.print(F("Message: "));
    Serial.println(message);
    
    for(int i = 0; i < smsRecipientCount; i++) {
      Serial.print(F("Sending to recipient "));
      Serial.print(i + 1);
      Serial.print(F(" ("));
      Serial.print(smsRecipients[i]);
      Serial.println(F(")"));
      
      sim800lSerial.println("AT+CMGF=1");
      delay(1000);
      
      sim800lSerial.print("AT+CMGS=\"");
      sim800lSerial.print(smsRecipients[i]);
      sim800lSerial.println("\"");
      delay(1000);
      
      sim800lSerial.print(message);
      delay(500);
      
      sim800lSerial.write(26);
      delay(5000);
      
      Serial.print(F("SMS sent to recipient "));
      Serial.println(i + 1);
      beep(beepStartup);
    }
    
    Serial.println(F("All SMS messages sent!"));
  }

  // ========== EMERGENCY CALL ==========
  void makeEmergencyCall(String number) {
    Serial.println(F("\nMaking emergency call..."));
    Serial.print(F("Calling: "));
    Serial.println(number);
    
    String cmd = "ATD" + number + ";";
    sim800lSerial.println(cmd);
    inCall = true;
    callStartTime = millis();
    messagePlayed = false;
    delay(1000);
  }

  // ========== AT COMMAND SENDER ==========
  void sendAtCommand(String command, int timeout) {
    sim800lSerial.println(command);
    long int time = millis();
    String response = "";
    
    while ((time + timeout) > millis()) {
      while (sim800lSerial.available()) {
        char c = sim800lSerial.read();
        response += c;
      }
    }
  }