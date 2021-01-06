
// *******************************************************************
//  ESP32 example code
//
//  Copyright (C) 2020 Francois DESLANDES <koxx33@gmail.com>
//
// *******************************************************************

// *******************************************************************

#include <Arduino.h>

// ########################## DEFINES ##########################

#define DEBUG 0
#define DEBUG_SERIAL 0
#define TEST_DYNAMIC_FLUX 0
#define PATCHED_ESP32_FWK 1

// serial
#define SERIAL_BAUD 921600        // [-] Baud rate for built-in Serial (used for the Serial Monitor)
#define BAUD_RATE_SMARTESC 115200 //115200

// pinout
#define PIN_SERIAL_ESP_TO_CNTRL 27 //TX
#define PIN_SERIAL_CNTRL_TO_ESP 14 //RX
#define PIN_IN_ABRAKE 34           //Brake
#define PIN_IN_ATHROTTLE 39        //Throttle

// delays
#define TIME_SEND 50 // [ms] Sending time interval
#define DELAY_CMD 10

// send frame headers
#define SERIAL_START_FRAME_DISPLAY_TO_ESC_REG_SET 0x01
#define SERIAL_START_FRAME_DISPLAY_TO_ESC_REG_GET 0x02
#define SERIAL_START_FRAME_DISPLAY_TO_ESC_CMD 0x03 // [-] Start frame definition for serial commands
#define SERIAL_START_FRAME_ESC_TO_DISPLAY_OK 0xF0
#define SERIAL_START_FRAME_ESC_TO_DISPLAY_ERR 0xFF

// commandes
#define SERIAL_FRAME_CMD_START 0x01
#define SERIAL_FRAME_CMD_STOP 0x02
#define SERIAL_FRAME_CMD_FAULT_ACK 0x07

// registers
#define FRAME_REG_TARGET_MOTOR 0x00
#define FRAME_REG_STATUS 0x02
#define FRAME_REG_CONTROL_MODE 0x03
#define FRAME_REG_SPEED 0x04
#define FRAME_REG_TORQUE 0x08
#define FRAME_REG_TORQUE_KP 0x09
#define FRAME_REG_TORQUE_KI 0x0A
#define FRAME_REG_FLUX_REF 0x0C
#define FRAME_REG_FLUX_KI 0x0D
#define FRAME_REG_FLUX_KP 0x0E
#define FRAME_REG_SPEED_MEESURED 0x1E

// motor orders
#define THROTTLE_TO_TORQUE_FACTOR 8 // 128 for max
#define BRAKE_TO_TORQUE_FACTOR 2
#define THROTTLE_MINIMAL_TORQUE 1000

#define TORQUE_KP 200 // divided by 1024
#define TORQUE_KI 20  // divided by 16384
#define FLUX_KP 200   // divided by 1024
#define FLUX_KI 200   // divided by 16384
#define STARUP_FLUX_REFERENCE 300

#define SECURITY_OFFSET 100

// Global variables
uint8_t idx = 0;       // Index for new data pointer
uint8_t bufStartFrame; // Buffer Start Frame
byte *p;               // Pointer declaration for the new received data
byte incomingByte;
byte incomingBytePrev;
uint8_t receiveBuffer[1000]; // Buffer Start Frame

// Trottle
int32_t analogValueThrottle = 0;
uint16_t analogValueThrottleRaw = 0;
uint16_t analogValueThrottleMinCalibRaw = 0;

// Brake
int32_t analogValueBrake = 0;
uint16_t analogValueBrakeRaw = 0;
uint16_t analogValueBrakeMinCalibRaw = 0;

int32_t speed = 0;
int16_t torque = 0;

unsigned long iTimeSend = 0;
uint8_t state = 0;
uint32_t iLoop = 0;

char print_buffer[500];

#pragma pack(push, 1)
typedef struct
{
  uint8_t Frame_start;
  uint8_t Lenght;
  uint8_t Command;
  uint8_t CRC8;
} __attribute__((packed)) SerialCommand;
#pragma pack(pop)
SerialCommand command;

#pragma pack(push, 1)
typedef struct
{
  uint8_t Frame_start;
  uint8_t Lenght;
  uint8_t Reg;
  int32_t Value;
  uint8_t CRC8;
} __attribute__((packed)) SerialRegSet32;
#pragma pack(pop)
SerialRegSet32 regSet32;

#pragma pack(push, 1)
typedef struct
{
  uint8_t Frame_start;
  uint8_t Lenght;
  uint8_t Reg;
  int16_t Value;
  uint8_t CRC8;
} __attribute__((packed)) SerialRegSet16;
#pragma pack(pop)
SerialRegSet16 regSetS16;

#pragma pack(push, 1)
typedef struct
{
  uint8_t Frame_start;
  uint8_t Lenght;
  uint8_t Reg;
  uint16_t Value;
  uint8_t CRC8;
} __attribute__((packed)) SerialRegSetU16;
#pragma pack(pop)
SerialRegSetU16 regSetU16;

#pragma pack(push, 1)
typedef struct
{
  uint8_t Frame_start;
  uint8_t Lenght;
  uint8_t Reg;
  int8_t Value;
  uint8_t CRC8;
} __attribute__((packed)) SerialRegSet8;
#pragma pack(pop)
SerialRegSet8 regSet8;

typedef enum
{
  ICLWAIT = 12,               /*!< Persistent state, the system is waiting for ICL
                           deactivation. Is not possible to run the motor if
                           ICL is active. Until the ICL is active the state is
                           forced to ICLWAIT, when ICL become inactive the state
                           is moved to IDLE */
  IDLE = 0,                   /*!< Persistent state, following state can be IDLE_START
                           if a start motor command has been given or
                           IDLE_ALIGNMENT if a start alignment command has been
                           given */
  IDLE_ALIGNMENT = 1,         /*!< "Pass-through" state containg the code to be executed
                           only once after encoder alignment command.
                           Next states can be ALIGN_CHARGE_BOOT_CAP or
                           ALIGN_OFFSET_CALIB according the configuration. It
                           can also be ANY_STOP if a stop motor command has been
                           given. */
  ALIGN_CHARGE_BOOT_CAP = 13, /*!< Persistent state where the gate driver boot
                           capacitors will be charged. Next states will be
                           ALIGN_OFFSET_CALIB. It can also be ANY_STOP if a stop
                           motor command has been given. */
  ALIGN_OFFSET_CALIB = 14,    /*!< Persistent state where the offset of motor currents
                           measurements will be calibrated. Next state will be
                           ALIGN_CLEAR. It can also be ANY_STOP if a stop motor
                           command has been given. */
  ALIGN_CLEAR = 15,           /*!< "Pass-through" state in which object is cleared and
                           set for the startup.
                           Next state will be ALIGNMENT. It can also be ANY_STOP
                           if a stop motor command has been given. */
  ALIGNMENT = 2,              /*!< Persistent state in which the encoder are properly
                           aligned to set mechanical angle, following state can
                           only be ANY_STOP */
  IDLE_START = 3,             /*!< "Pass-through" state containg the code to be executed
                           only once after start motor command.
                           Next states can be CHARGE_BOOT_CAP or OFFSET_CALIB
                           according the configuration. It can also be ANY_STOP
                           if a stop motor command has been given. */
  CHARGE_BOOT_CAP = 16,       /*!< Persistent state where the gate driver boot
                           capacitors will be charged. Next states will be
                           OFFSET_CALIB. It can also be ANY_STOP if a stop motor
                           command has been given. */
  OFFSET_CALIB = 17,          /*!< Persistent state where the offset of motor currents
                           measurements will be calibrated. Next state will be
                           CLEAR. It can also be ANY_STOP if a stop motor
                           command has been given. */
  CLEAR = 18,                 /*!< "Pass-through" state in which object is cleared and
                           set for the startup.
                           Next state will be START. It can also be ANY_STOP if
                           a stop motor command has been given. */
  START = 4,                  /*!< Persistent state where the motor start-up is intended
                           to be executed. The following state is normally
                           SWITCH_OVER or RUN as soon as first validated speed is
                           detected. Another possible following state is
                           ANY_STOP if a stop motor command has been executed */
  SWITCH_OVER = 19,           /**< TBD */
  START_RUN = 5,              /*!< "Pass-through" state, the code to be executed only
                           once between START and RUN states itâ€™s intended to be
                           here executed. Following state is normally  RUN but
                           it can also be ANY_STOP  if a stop motor command has
                           been given */
  RUN = 6,                    /*!< Persistent state with running motor. The following
                           state is normally ANY_STOP when a stop motor command
                           has been executed */
  ANY_STOP = 7,               /*!< "Pass-through" state, the code to be executed only
                           once between any state and STOP itâ€™s intended to be
                           here executed. Following state is normally STOP */
  STOP = 8,                   /*!< Persistent state. Following state is normally
                           STOP_IDLE as soon as conditions for moving state
                           machine are detected */
  STOP_IDLE = 9,              /*!< "Pass-through" state, the code to be executed only
                           once between STOP and IDLE itâ€™s intended to be here
                           executed. Following state is normally IDLE */
  FAULT_NOW = 10,             /*!< Persistent state, the state machine can be moved from
                           any condition directly to this state by
                           STM_FaultProcessing method. This method also manage
                           the passage to the only allowed following state that
                           is FAULT_OVER */
  FAULT_OVER = 11,            /*!< Persistent state where the application is intended to
                          stay when the fault conditions disappeared. Following
                          state is normally STOP_IDLE, state machine is moved as
                          soon as the user has acknowledged the fault condition.
                      */
  WAIT_STOP_MOTOR = 20

} State_t;

HardwareSerial hwSerCntrl(1);

void displayBuffer(uint8_t *buffer, uint8_t size)
{
  for (int i = 0; i < size; i++)
  {
    Serial.printf("%02x ", buffer[i]);
  }
  Serial.printf("\n");
}
uint8_t getCrc(uint8_t *buffer, uint8_t size)
{
  uint16_t crc = 0;
  for (int i = 0; i < size - 1; i++)
  {
    crc = crc + buffer[i];
    //Serial.printf("crc = %02x\n", crc);
  }

  uint8_t finalCrc = (uint8_t)(crc & 0xff) + ((crc >> 8) & 0xff);
  //Serial.printf("finalCrc = %02x\n", finalCrc);
  //Serial.printf("\n");
  return finalCrc;
}

// ########################## SEND ##########################

void SendCmd(uint8_t cmd)
{
  // Create command
  command.Frame_start = SERIAL_START_FRAME_DISPLAY_TO_ESC_CMD;
  command.Lenght = 1;
  command.Command = cmd;
  command.CRC8 = getCrc((uint8_t *)&command, sizeof(command));

  displayBuffer((uint8_t *)&command, sizeof(command));

  // Write to Serial
  hwSerCntrl.write((uint8_t *)&command, sizeof(command));
}

void SendMode(uint8_t mode)
{
  // Create command
  regSet8.Frame_start = SERIAL_START_FRAME_DISPLAY_TO_ESC_REG_SET;
  regSet8.Lenght = 5;
  regSet8.Reg = FRAME_REG_CONTROL_MODE;
  regSet8.Value = mode;
  regSet8.CRC8 = getCrc((uint8_t *)&regSet8, sizeof(regSet8));

  displayBuffer((uint8_t *)&regSet8, sizeof(regSet8));

  // Write to Serial
  hwSerCntrl.write((uint8_t *)&regSet8, sizeof(regSet8));
}

void GetReg(uint8_t reg)
{
  // Create command
  command.Frame_start = SERIAL_START_FRAME_DISPLAY_TO_ESC_REG_GET;
  command.Lenght = 1;
  command.Command = reg;
  command.CRC8 = getCrc((uint8_t *)&command, sizeof(command));

  displayBuffer((uint8_t *)&command, sizeof(command));

  // Write to Serial
  hwSerCntrl.write((uint8_t *)&command, sizeof(command));
}

void SetRegU16(uint8_t reg, uint16_t val)
{
  // Create command
  regSetU16.Frame_start = SERIAL_START_FRAME_DISPLAY_TO_ESC_REG_SET;
  regSetU16.Lenght = 3;
  regSetU16.Reg = reg;
  regSetU16.Value = val;
  regSetU16.CRC8 = getCrc((uint8_t *)&regSetU16, sizeof(regSetU16));

  displayBuffer((uint8_t *)&regSetU16, sizeof(regSetU16));

  // Write to Serial
  hwSerCntrl.write((uint8_t *)&regSetU16, sizeof(regSetU16));
}

void SetRegS16(uint8_t reg, int16_t val)
{
  // Create command
  regSetS16.Frame_start = SERIAL_START_FRAME_DISPLAY_TO_ESC_REG_SET;
  regSetS16.Lenght = 3;
  regSetS16.Reg = reg;
  regSetS16.Value = val;
  regSetS16.CRC8 = getCrc((uint8_t *)&regSetS16, sizeof(regSetS16));

  displayBuffer((uint8_t *)&regSetS16, sizeof(regSetS16));

  // Write to Serial
  hwSerCntrl.write((uint8_t *)&regSetS16, sizeof(regSetS16));
}

// ########################## RECEIVE ##########################
void Receive()
{
  uint16_t nbBytes = 0;

  // Check for new data availability in the Serial buffer
  while (hwSerCntrl.available())
  {
    incomingByte = hwSerCntrl.read();      // Read the incoming byte
    receiveBuffer[nbBytes] = incomingByte; // Construct the start frame

    nbBytes++;
  }
  if (nbBytes > 0)
  {
#if DEBUG_SERIAL
    // display frame
    Serial.print("   received : ");
    for (int i = 0; i < nbBytes; i++)
    {
      Serial.printf("%02x ", receiveBuffer[i]);
    }
    Serial.println();
#endif

    int iFrame = 0;
    bool continueReading = true;
    uint16_t msgSize = 0;
    while (continueReading)
    {
      msgSize = receiveBuffer[iFrame + 1];
#if DEBUG_SERIAL
      Serial.printf("   size = %02x\n", msgSize);
#endif
      if (receiveBuffer[iFrame] == SERIAL_START_FRAME_ESC_TO_DISPLAY_OK)
      {
        Serial.printf("   ==> ok");
      }
      else if (receiveBuffer[iFrame] == SERIAL_START_FRAME_ESC_TO_DISPLAY_ERR)
      {
        Serial.printf("   ==> KO !!!!!!!!!");
      }

      if (msgSize == 0)
      {
        Serial.println("   ===> CMD or REG_SET");
      }
      else if (msgSize == 1)
      {
        Serial.printf("   ===> value = %02x\n", receiveBuffer[iFrame + 2]);

        if ((receiveBuffer[iFrame] == SERIAL_START_FRAME_ESC_TO_DISPLAY_ERR) && ((receiveBuffer[iFrame] == FAULT_NOW) || (receiveBuffer[iFrame] == FAULT_OVER)) && (state >= 8))
        {
          state = 0;
          delay(5000);
        }
      }
      else if (msgSize == 2)
      {
        Serial.printf("   ===> unknonw");
      }
      else if (msgSize == 4)
      {
        memcpy(&speed, &(receiveBuffer[iFrame + 2]), 4);
        Serial.printf("   ===> speed : %d\n", speed);
      }
      else
      {
        Serial.printf("   ===> ko (unknonw data) ----------------------------------------\n");
      }

#if DEBUG_SERIAL
      Serial.printf("   nbBytes = %d / iFrame = %d / nextIFrame = %d\n", nbBytes, iFrame, msgSize + 3);
#endif
      iFrame += msgSize + 3;

#if DEBUG_SERIAL
      Serial.printf("   next msg : iFrame = %d / msgSize = %02x\n", iFrame, msgSize);
#endif

      if (iFrame < nbBytes)
      {
        continueReading = true;
#if DEBUG_SERIAL
        Serial.println("      continue");
#endif
      }
      else
      {
        continueReading = false;
#if DEBUG_SERIAL
        Serial.println("      stop");
#endif
      }
    }

    Serial.println();
  }
}

// ########################## LOOP ##########################

void loop(void)
{
  unsigned long timeNow = millis();

  // Check for new received data
  Receive();

  // Avoid delay
  if (iTimeSend > timeNow)
    return;
  iTimeSend = timeNow + TIME_SEND;

  if (state == 0)
  {

    analogValueThrottleRaw = 0;
    analogValueBrakeRaw = 0;

    Serial.printf("%d / send : GET REG STATUS : ", state);
    GetReg(FRAME_REG_STATUS);
    state++;
  }
  else if (state == 1)
  {
    Serial.printf("%d / send : CMD STOP : ", state);
    SendCmd(SERIAL_FRAME_CMD_STOP);
    state++;

    delay(DELAY_CMD);
  }

  else if (state == 2)
  {
    Serial.printf("%d / send : GET REG STATUS : ", state);
    GetReg(FRAME_REG_STATUS);
    state++;
  }
  else if (state == 3)
  {

    Serial.printf("%d / send : CMD FAULT_ACK : ", state);
    SendCmd(SERIAL_FRAME_CMD_FAULT_ACK);

    delay(5);

    Serial.printf("%d / send : SET REG CONTROL_MODE : ", state);
    SetRegU16(FRAME_REG_CONTROL_MODE, 0x00);

    delay(10);

    Serial.printf("%d / send : REG_TORQUE_KI : ", state);
    SetRegU16(FRAME_REG_TORQUE_KI, TORQUE_KI);

    delay(10);

    Serial.printf("%d / send : REG_TORQUE_KP : ", state);
    SetRegU16(FRAME_REG_TORQUE_KP, TORQUE_KP);

    delay(10);

#if TEST_DYNAMIC_FLUX
    Serial.printf("%d / send : REG_FLUX_KI : ", state);
    SetRegU16(FRAME_REG_FLUX_KI, FLUX_KI);

    delay(10);

    Serial.printf("%d / send : REG_FLUX_KP : ", state);
    SetRegU16(FRAME_REG_FLUX_KP, FLUX_KP);

    delay(10);

    Serial.printf("%d / send : REG_FLUX_REF : ", state);
    SetRegU16(FRAME_REG_FLUX_REF, STARUP_FLUX_REFERENCE);
#endif

    state++;

    delay(DELAY_CMD);
  }

  else if (state == 4)
  {
    Serial.printf("%d / send : GET REG STATUS : ", state);
    GetReg(FRAME_REG_STATUS);
    state++;

    delay(DELAY_CMD);
  }
  else if (state == 5)
  {

    delay(500);

    Serial.printf("%d / send : CMD START : ", state);
    SendCmd(SERIAL_FRAME_CMD_START);

    delay(DELAY_CMD);

    state++;

    delay(DELAY_CMD);
  }
  else if (state == 6)
  {
    Serial.printf("%d / send : GET REG STATUS : ", state);
    GetReg(FRAME_REG_STATUS);
    state++;
  }
  else if (state == 7)
  {

    state++;
  }
  else if (state == 8)
  {

    // Compute throttle
    analogValueThrottleRaw = analogRead(PIN_IN_ATHROTTLE);
    analogValueThrottle = analogValueThrottleRaw - analogValueThrottleMinCalibRaw - SECURITY_OFFSET;
    analogValueThrottle = analogValueThrottle / 4;
    if (analogValueThrottle > 255)
      analogValueThrottle = 255;
    if (analogValueThrottle < 0)
      analogValueThrottle = 0;

    // Compute brake
    analogValueBrakeRaw = analogRead(PIN_IN_ABRAKE);
    analogValueBrake = analogValueBrakeRaw - analogValueBrakeMinCalibRaw - SECURITY_OFFSET;
    analogValueBrake = analogValueBrake / 3;
    if (analogValueBrake > 255)
      analogValueBrake = 255;
    if (analogValueBrake < 0)
      analogValueBrake = 0;

    Serial.println("throttleRaw = " + (String)analogValueThrottleRaw + " / throttleMinCalibRaw = " +                            //
                   (String)analogValueThrottleMinCalibRaw + " / throttle = " + (String)analogValueThrottle + " / brakeRaw = " + //
                   (String)analogValueBrakeRaw + " / brakeMinCalibRaw = " + (String)analogValueBrakeMinCalibRaw +               //
                   " / brake = " + (String)analogValueBrake + " / torque = " + (String)torque + " / speed = " + (String)speed);

    // Send torque commands
    if ((analogValueBrake > 0) && (speed > 0))
      torque = -analogValueBrake * BRAKE_TO_TORQUE_FACTOR;
    else if (analogValueThrottle > 0)
      torque = THROTTLE_MINIMAL_TORQUE + (analogValueThrottle * THROTTLE_TO_TORQUE_FACTOR);
    else
      torque = 0;

    Serial.printf("%d / send : GET REG STATUS : ", state);
    SetRegS16(FRAME_REG_TORQUE, torque);

#if RAMP_ENABLED
#define RAMP 400
    if (iLoop % RAMP < RAMP / 2)
      torque = iLoop % RAMP;
    else
      torque = (RAMP / 2) - ((iLoop % RAMP) - (RAMP / 2));
#endif

    state++;
  }
  else if (state == 9)
  {

    Serial.printf("%d / send : GET REG STATUS : ", state);
    GetReg(FRAME_REG_STATUS);

    delay(10);

#if TEST_DYNAMIC_FLUX
    if (speed > 100)
    {
      Serial.printf("%d / send : SET REG FRAME_REG_FLUX_REF : ", state);
      SetRegU16(FRAME_REG_FLUX_REF, 0);
    }
#endif

    state++;
  }
  else if (state == 10)
  {

    Serial.printf("%d / send : GET REG SPEED : ", state);
    GetReg(FRAME_REG_SPEED_MEESURED);
    state = state - 2;
  }

  delay(5); // 20 Hz orders

  iLoop++;
}

// ########################## SETUP ##########################
void setup()
{
  Serial.begin(SERIAL_BAUD);
  Serial.println("SmartESC Serial v2.0");

  pinMode(PIN_IN_ATHROTTLE, INPUT);
  pinMode(PIN_IN_ABRAKE, INPUT);

  // do it twice to improve values
  analogValueThrottleMinCalibRaw = analogRead(PIN_IN_ATHROTTLE);
  analogValueThrottleMinCalibRaw = analogRead(PIN_IN_ATHROTTLE);
  analogValueBrakeMinCalibRaw = analogRead(PIN_IN_ABRAKE);
  analogValueBrakeMinCalibRaw = analogRead(PIN_IN_ABRAKE);

  hwSerCntrl.begin(BAUD_RATE_SMARTESC, SERIAL_8N1, PIN_SERIAL_CNTRL_TO_ESP, PIN_SERIAL_ESP_TO_CNTRL);
#if PATCHED_ESP32_FWK
  hwSerCntrl.setUartIrqIdleTrigger(1);
#endif
}

// ########################## END ##########################
