#include <SoftwareSerial.h>
#include <Servo.h>

//==============PIN===============

//Parking IR sensor
#define IR_1 2
#define IR_2 3
#define IR_3 4

// Servo
#define Servo_1 5
#define Servo_2 6

// 초음파
#define TRIG_1 7
#define ECHO_1 8

#define TRIG_2 9
#define ECHO_2 10

// 유도 LED
#define LED_1 11
#define LED_2 12
#define LED_3 13

#define DEBUG true

// ---------- ESP32 Serial ----------
// Arduino RX <- ESP32 TX
// Arduino TX -> ESP32 RX
//
// A0, A1을 Digital Pin처럼 사용
SoftwareSerial espSerial(A0, A1); // A0=SoftwareSerial RX=ESP32 TX / A1= SoftwareSerial TX=ESP32 RX;

//==============객체===============
Servo enterServo;
Servo exitServo;

//=================상수==================

//차단기 각도
const int GATE_OPEN=90;//90
const int GATE_CLOSE=180;//180

// 차량 접근 판단 거리
const float VEHICLE_DISTANCE = 10.0;

// 차단기 열린 상태 유지 시간
const unsigned long GATE_OPEN_TIME = 10000;

//==============Variables===============

//각 주차칸 상태
bool occupied[3]={false,false,false}; //현재
bool previousOccupied[3]={false,false,false}; //현재-1 시점

// 서버가 배정한 자리
int assignedSlot=-1; 
int wrongSlot=-1;
//0:P1, 1: P2, 2: P3 에 배정 & -1: 아직 배정하지 않음

// 차단기 상태
bool enterGateOpened = false;
bool exitGateOpened = false;

unsigned long enterGateOpenTime = 0;
unsigned long exitGateOpenTime = 0;

// 차량 접근 이벤트 중복 방지
bool enterVehicleDetected = false;
bool exitVehicleDetected = false;

// ENTRY_APPROACH를 서버로 보낸 후
// 서버 배정 응답을 기다리는 상태
bool entryRequestPending = false;
bool exitRequestPending = false;

// 출차 통과 상태변수
bool exitPassing=false;

// =====================================================
// NETWORK
// =====================================================
//WIFI 접속 관련
String ssid="3F_302";
String password="0424719222!!";

// 앞서 만든 C++ 서버의 IP 주소와 포트 번호
String serverIP = "192.168.0.36"; // C++ 서버(PC)의 IP 주소
String serverPort = "8080";

// =====================================================
// TCP Protocol
// =====================================================

// Arduino -> Server
const uint8_t MSG_ENTRY_APPROACH = 1;
const uint8_t MSG_EXIT_APPROACH  = 2;
const uint8_t MSG_PARKING_FULL   = 3;

const uint8_t MSG_SLOT_OCCUPIED_BASE = 11; // P1=11 P2=12 P3=13 (주차면 OCCUPIED)
const uint8_t MSG_SLOT_EMPTY_BASE    = 21; // P1=21 P2=22 P3=23 (주차면 EMPTY)

// 정상 주차 완료
// P1=31, P2=32, P3=33
const uint8_t MSG_PARK_COMPLETE_BASE = 31;

// 오주차
// P1=41, P2=42, P3=43
const uint8_t MSG_WRONG_PARK_BASE = 41;

// 오주차 해제
// P1=51, P2=52, P3=53
const uint8_t MSG_WRONG_CLEAR_BASE = 51;

// 출차 완료
const uint8_t MSG_EXIT_COMPLETE = 60;

// =====================================================
// TCP PROTOCOL
// Server -> Arduino
// =====================================================

// 주차면 배정
const uint8_t CMD_ASSIGN_P1 = 101;
const uint8_t CMD_ASSIGN_P2 = 102;
const uint8_t CMD_ASSIGN_P3 = 103;

// 출차 허가
const uint8_t CMD_EXIT_ALLOW = 110;

// reset
const uint8_t CMD_RESET = 120;

//==============Setup===============
void setup() {
  Serial.begin(9600);
  espSerial.begin(9600); //esp32
  Serial.setTimeout(100);
  espSerial.setTimeout(100);

  // ===================================================
  // ESP-01 / WIFI 초기화
  // ===================================================
  Serial.println("ESP-01 초기화 시작");

  // ESP-01 Reset
  sendATCommand("AT+RST\r\n",2000,DEBUG);

  // Station Mode
  sendATCommand("AT+CWMODE=1\r\n",1000, DEBUG);

  // Wi-Fi 접속
  String wifiCommand = "AT+CWJAP=\"" +ssid +"\",\"" +password +"\"\r\n";
  sendATCommand(wifiCommand,8000,DEBUG);

  // IP 확인
  sendATCommand("AT+CIFSR\r\n",1000,DEBUG);

  // TCP single connection
  sendATCommand("AT+CIPMUX=0\r\n",1000,DEBUG);

  // TCP Server 연결
  String tcpCommand ="AT+CIPSTART=\"TCP\",\"" +serverIP +"\"," +serverPort +"\r\n";
  sendATCommand(tcpCommand,4000,DEBUG);

  // ==================================================================================

  // IR 센서
  pinMode(IR_1,INPUT_PULLUP);
  pinMode(IR_2,INPUT_PULLUP);
  pinMode(IR_3,INPUT_PULLUP);

  // 서보
  enterServo.attach(Servo_1);
  exitServo.attach(Servo_2);

  enterServo.write(GATE_CLOSE);
  exitServo.write(GATE_CLOSE);

  //초음파
  pinMode(TRIG_1,OUTPUT);
  pinMode(ECHO_1,INPUT);
  pinMode(TRIG_2,OUTPUT);
  pinMode(ECHO_2,INPUT);

  //LED
  pinMode(LED_1,OUTPUT);
  pinMode(LED_2,OUTPUT);
  pinMode(LED_3,OUTPUT);

  digitalWrite(LED_1,LOW);
  digitalWrite(LED_2,LOW);
  digitalWrite(LED_3,LOW);

  delay(200);
  updateParkingStatus();
  for(int i=0;i<3;i++){previousOccupied[i]=occupied[i];}
  Serial.println("SMART_PARKING_READY");

  // 서버랑 현재 상태 동기화
  sendParkingStatus();
}

//==============Loop===============
void loop() {

  // 0. IR 센서를 통해 주차 여부 확인
  updateParkingStatus();

  // 1. 입구 차량 접근 확인
  checkEntering();

  // 2. 출구 차량 접근 확인
  checkExiting();

  // 3. 서버의 주차 자리 배정 -> LED 가이딩 + 차단기 개방
  checkServerCommand();

  // 4. 입구차단기 관리
  controlEnterGate();

  // 5. 출구차단기 관리
  controlExitGate();

  // 6. 주차 완료 상황 판정
  checkParkingResult();

  delay(50);
}


//---------------------------------------
// 0. IR 센서를 통해 주차 여부 확인
// IR센서 high 차량 없음, low 차량 있음
//---------------------------------------
void updateParkingStatus(){
  occupied[0]=(digitalRead(IR_1)==LOW); //occupied 시 true
  //occupied[0]=(digitalRead(IR_1)==HIGH); //occupied 시 true
  occupied[1]=(digitalRead(IR_2)==LOW);
  //occupied[1]=(digitalRead(IR_2)==HIGH);
  occupied[2]=(digitalRead(IR_3)==LOW);
  //occupied[2]=(digitalRead(IR_3)==HIGH);
}


//---------------------------------------
// 1. 차량 입차 감지 함수
//---------------------------------------
void checkEntering(){
  // 접근 감지하는 초음파 센서
  //float distance=getDistance(TRIG_1,ECHO_1);
  float distance=getStableDistance(TRIG_1,ECHO_1);

  //차량 접근 시
  if(distance<=VEHICLE_DISTANCE){ 
    if(!enterVehicleDetected &&!entryRequestPending){//새롭게 감지된 차량이면
      enterVehicleDetected=true;//차량 감지를 True 로 설정
      int available=getAvailableCount();  //주차 공간 빈자리 개수 세어줌
      if(available==0){
        //sendEvent("PARKING_FULL");
        Serial.println("PARKING_FULL");
        sendServerCode(MSG_PARKING_FULL);
        return;
      }
      
      // 입차 가능
      //sendEvent("ENTRY_APPROACH");
      Serial.println("ENTRY_APPROACH");
      // 서버의 자리 배정을 기다림
      entryRequestPending = true;
      // Server에게 숫자 1
      bool success =sendServerCode(MSG_ENTRY_APPROACH);
      if(!success){entryRequestPending=false;}
      /*
        카메라 촬영->
        OpenCV 번호판 인식->
        터치스크린에 빈자리 띄우기
      */
    }
  }

  else{   //차량 접근 안하면
    if(!enterGateOpened && assignedSlot==-1 &&!entryRequestPending){       // 문이 안열리고, 차량이 할당된 자리가 없을 때
      enterVehicleDetected=false;                   // 차량 할당 안됨
      // 현재 진행중인 입차가 전혀 없을 때만
      // 차량 감지 상태 초기화
    }
  }
}

//---------------------------------------
// 2. 차량 출차 감지 함수
//---------------------------------------
void checkExiting(){
  // 접근 감지하는 초음파 센서
  //float distance=getDistance(TRIG_2,ECHO_2);
  float distance=getStableDistance(TRIG_2,ECHO_2);

  if(distance<=VEHICLE_DISTANCE){ //차량 접근 시

    if(!exitVehicleDetected && !exitRequestPending){//새롭게 감지된 차량이면
      exitVehicleDetected=true;//차량 감지를 True 로 설정
      exitRequestPending = true;
      //sendEvent("EXIT_APPROACH");
      Serial.println("EXIT_APPROACH");
      bool success =sendServerCode(MSG_EXIT_APPROACH);
      if(!success) {
        exitRequestPending = false;
      }
      /*
        서버 동작:
        출구 Camera 촬영
            ↓
        번호판 인식
            ↓
        입차 기록 확인
            ↓
        주차시간 / 요금 계산 (추후)
            ↓
        EXIT_ALLOW
      */
    }
  }
  else{   //차량 접근 안하면
    if(!exitGateOpened&&!exitRequestPending){       
      exitVehicleDetected=false;
    }
  }
}


//---------------------------------------
// 거리 측정 함수
//---------------------------------------
float getDistance(int trig,int echo){
  digitalWrite(trig, LOW);
  delayMicroseconds(2);

  digitalWrite(trig, HIGH);
  delayMicroseconds(10);

  digitalWrite(trig,LOW);

  unsigned long duration = pulseIn(echo, HIGH,30000);

  if(duration==0){return 999;} // 수신받지 못하면 timeout

  float distance=duration*0.034/2;
  return distance;
}

float getStableDistance(int trig,int echo){
  float sum=0;
  int validCount=0;
  for(int i=0;i<3;i++){
    float d=getDistance(trig,echo);
    if(d!=999&&d>=2&&d<=200){
      sum+=d;
      validCount++;
    }
    delay(10);
  }
  if(validCount==0){return 999;}
  return sum/validCount;
}

//---------------------------------------
// 주차 공간 빈자리 갯수
//---------------------------------------
int getAvailableCount(){
  int count=0;
  for(int i=0;i<3;i++){
    if (!occupied[i]){count++;}
  }
  return count;
}


//-------------------------------------------------
// 3. 서버의 주차 자리 배정 -> LED 가이딩 + 차단기 개방
//-------------------------------------------------

/*void checkServerCommand(){
  if(Serial.available()){
    String command=Serial.readStringUntil('\n');
    command.trim();
    processCommand(command);
  }
  if(espSerial.available()){
    String command=espSerial.readStringUntil('\n');
    command.trim();
    processCommand(command);
  }
}*/

// =====================================================
// SERVER -> ARDUINO
// =====================================================
void checkServerCommand(){
  int code = readServerCode();
  // 수신 데이터 없음
  if(code < 0){return;}
  // Debug
  Serial.print("SERVER RX CODE: ");
  Serial.println(code);
  processServerCode((uint8_t)code);
}

// =====================================================
// SERVER CODE PROCESS
// =====================================================
void processServerCode(uint8_t code){

  // ===================================================
  // 101 ~ 103
  // Parking Slot Assignment
  // ===================================================
  if (code >= CMD_ASSIGN_P1 &&code <= CMD_ASSIGN_P3) {
    int slot =code - CMD_ASSIGN_P1;
    assignParkingSlot(slot);
    return;
  }

  // ===================================================
  // 110
  // Exit Allow
  // ===================================================
  if(code == CMD_EXIT_ALLOW){
    // 출구에 차량이 실제로 있는지 확인
    if(!exitRequestPending){
      Serial.println("ERROR:NO_EXIT_VEHICLE");
      return;
    }
    Serial.println("EXIT_AUTHORIZED");
    exitRequestPending=false;
    openExitGate();
    return;
  }

  // ===================================================
  // 120
  // Reset
  // ===================================================
  if(code == CMD_RESET){resetSession();return;}

  // ================================================
  // 알 수 없는 명령
  // ================================================
  else{
    Serial.print("UNKNOWN SERVER CODE: ");
    Serial.println(code);
  }
}

// =====================================================
// ASSIGN PARKING SLOT
// =====================================================

void assignParkingSlot(int slot){
  // ================================================
  // 1. 입구 차량 존재 확인
  // ================================================
  // 반드시 ENTRY_APPROACH 요청을 먼저
  // 서버에 보낸 상태여야 함
  if(!entryRequestPending){
    Serial.println("ERROR:NO_ENTRY_VEHICLE");
    return;
  }

  // ================================================
  // 2. Slot 범위 확인
  // ================================================

  if(slot < 0 || slot > 2){
    Serial.println("ERROR:INVALID_SLOT");
    return;
  }

  // ================================================
  // 3. 이미 차량이 있는 자리인지 확인
  // ================================================
  if(occupied[slot]){
    Serial.println("ERROR:SLOT_OCCUPIED");
    return;
  }

  // ================================================
  // 4. 이미 다른 자리가 배정되어 있는지 확인
  // ================================================

  if(assignedSlot != -1){
    Serial.print("ERROR:ALREADY_ASSIGNED:P");
    Serial.println(assignedSlot + 1);
    return;
  }

  // ================================================
  // 5. 정상적으로 주차면 배정
  // ================================================
  assignedSlot = slot;
  wrongSlot = -1;
  // 서버 응답을 정상적으로 받았으므로
  // Pending 해제
  entryRequestPending = false;
  Serial.print("SLOT_ASSIGNED:P");
  Serial.println(slot + 1);

  // LED 안내
  guideToSlot(slot);
  // 입구 차단기 개방
  openEnterGate();
}

/*void processCommand(String command){
  //===================================================
  // A. 주차 공간 배정
  //===================================================
  if(command.startsWith("ASSIGN:")){  // 할당 명령이 내려지면
  // 주차 자리 배정
    if(!enterVehicleDetected){sendEvent("ERROR:NO_ENTRY_VEHICLE");return;}

    int slot=command.substring(7).toInt();
    slot--;
    
    // 주차 가능 범위 확인
    if(slot<0||slot>2){
      sendEvent("ERROR:INVALID_SLOT");
      return;
    }

    // 이미 차가 있으면
    if(occupied[slot]){
      sendEvent("ERROR:SLOT_OCCUPIED");
      return;
    }

    //이미 해당 차에 다른 공간이 배정 완료
    if(assignedSlot!=-1){
      sendEvent("ERROR:ALREADY_ASSIGNED:P"+String(assignedSlot + 1));
      return;
    }

    // 아니면 주차공간이 정상적으로 배정된 것임

    assignedSlot=slot;
    wrongSlot=-1;
    sendEvent("SLOT_ASSIGNED:P "+String(slot+1));
    guideToSlot(slot);      //해당 구역으로 LED 가이딩
    openEnterGate();             // 차단기열기
  }

  //===================================================
  // B. 출차 허가
  //===================================================
  else if (command=="EXIT_ALLOW"){
    if(!exitVehicleDetected){sendEvent("ERROR:NO_EXIT_VEHICLE");return;}
    sendEvent("EXIT_AUTHORIZED");
    openExitGate();
  }

  // ===================================================
  // C. 압구 강제 Open
  // ===================================================
  else if (command == "OPEN_ENTRY_GATE") {
    openEnterGate();
  }
  // ===================================================
  // D. 출구 강제 Open
  // ===================================================
  else if (command == "OPEN_EXIT_GATE") {
    openExitGate();
  }

  // ===================================================
  // E. 현재 상태 요청
  // ===================================================
  else if (command == "GET_STATUS") {
    sendParkingStatus();
  }

  // ===================================================
  // F. Reset
  // ===================================================
  else if(command=="RESET"){resetSession();}
}*/

//processCommand==assignParkingSlot+processServerCode

//---------------------------------------
// 3-1 주차 자리 배정 LED 안내
//---------------------------------------
void guideToSlot(int slot){

  //초기화
  digitalWrite(LED_1,LOW);
  digitalWrite(LED_2,LOW);
  digitalWrite(LED_3,LOW);

  if (slot==0){   //1번 자리 배정 시
    digitalWrite(LED_1,HIGH);}
  else if (slot==1){   //2번 자리 배정 시
    digitalWrite(LED_2,HIGH);}
  else if (slot==2){   //3번 자리 배정 시
    digitalWrite(LED_3,HIGH);}

  Serial.print("GUIDE_ON:P");
  Serial.println(slot+1);
}


//---------------------------------------
// 3-2 차단기 개방 (입/출)
//---------------------------------------
void openEnterGate(){
  enterServo.write(GATE_OPEN); //현 그림 기준으로 90도에서 열림
  enterGateOpened=true;
  enterGateOpenTime=millis();
  Serial.println("ENTRY_GATE_OPEN");
}

void openExitGate(){
  exitServo.write(GATE_OPEN); //현 그림 기준으로 90도에서 열림
  exitGateOpened=true;
  exitGateOpenTime=millis();
  // 차량 통과 추적 시작
  exitPassing = exitVehicleDetected;
  Serial.println("EXIT_GATE_OPEN");
}

//---------------------------------------
// 3-3 차단기 폐쇄 (입/출)
//---------------------------------------
void closeEnterGate(){
  if(!enterGateOpened){return;}
  enterServo.write(GATE_CLOSE); //현 그림 기준으로 90도에서 열림
  enterGateOpened=false;
  Serial.println("ENTRY_GATE_CLOSED");
}

void closeExitGate(){
  if(!exitGateOpened){return;}
  exitServo.write(GATE_CLOSE); //현 그림 기준으로 90도에서 열림
  exitGateOpened=false;
  Serial.println("EXIT_GATE_CLOSED");
}


//---------------------------------------
// 4. 입구 차단기 컨트롤 
//---------------------------------------
void controlEnterGate(){
  if (!enterGateOpened) {return;}

  if(millis()-enterGateOpenTime>=GATE_OPEN_TIME){    //일정시간 경과시
    closeEnterGate();
  }
}

//---------------------------------------
// 5. 입구 차단기 컨트롤 
//---------------------------------------
void controlExitGate(){
  if (!exitGateOpened) {return;}
  float distance=getStableDistance(TRIG_2,ECHO_2);

  // 1. 차량이 아직 출구 센서 앞에 있음
  if(distance<=VEHICLE_DISTANCE){
    exitPassing=true;
  }

  // 2. 한번 차량을 확인했고 이후 센서 영역에서 사라짐
  if(exitPassing && distance>VEHICLE_DISTANCE){
    closeExitGate();
    Serial.println("EXIT_COMPLETE");
    sendServerCode(MSG_EXIT_COMPLETE);
    exitVehicleDetected=false;
    exitPassing=false;
    exitRequestPending=false;

    return;
  }

  // 3. 센서가 이상하거나 차량이 계속 머물러있으면 기본 10초 timeout 사용
  //if(exitGateOpened){
  if(millis()-exitGateOpenTime>=GATE_OPEN_TIME){    //일정시간 경과시
    closeExitGate();                 // 출입게이트를 닫음
    Serial.println("EXIT_TIMEOUT");
    exitVehicleDetected=false;
    exitPassing=false;
    exitRequestPending=false;
  }
  //}
}

//---------------------------------------
// 6. 주차 상황 판정
//---------------------------------------
void checkParkingResult(){
  // 할당된 슬롯이 있을 경우에
  for(int i=0;i<3;i++){
    //=======================================
    // CASE A
    // Empty -> Occupied
    // ======================================
    if(!previousOccupied[i] && occupied[i]){     //과거 시점엔 비었으나, 현재 시점엔 차 있으면
      //sendEvent("SLOT_OCCUIPIED:P"+String(i+1));
      Serial.print("SLOT_OCCUPIED:P");
      Serial.println(i + 1);
      
      // 현재 입차 차량에게 배정된 주차공간이 있을 때만 정상/오주차를 판단함
      if(assignedSlot!=-1){
        // A.1 정상 주차
        if(i==assignedSlot){        
          //sendEvent("PARKING_COMPLETE:P"+String(i+1));
          Serial.print("PARKING_COMPLETE:P");
          Serial.println(i + 1);
          // P1 = 31 P2 = 32 P3 = 33
          sendServerCode(MSG_PARK_COMPLETE_BASE + i);

          digitalWrite(LED_1, LOW);              // 모든 LED 를 끄기
          digitalWrite(LED_2, LOW);
          digitalWrite(LED_3, LOW);

          // 주차 완료 시 즉시 입구 차단기 닫기
          closeEnterGate();

          assignedSlot=-1;     // 할당슬롯 초기화
          wrongSlot=-1;

        }

        // A.2 오주차
        else{
          // 이미 다른 오주차 상태가 없으면 최초 1회만 기록
          if(wrongSlot==-1){
            wrongSlot=i;
            //sendEvent("WRONG_PARKING:P"+String(i+1));
            Serial.print("WRONG_PARKING:P");
            Serial.println(i + 1);
            // P1 = 41 P2 = 42 P3 = 43
            sendServerCode(MSG_WRONG_PARK_BASE + i);

            //sendEvent("EXPECTED:P"+String(assignedSlot+1));
            Serial.print("EXPECTED:P");
            Serial.println(assignedSlot+1);
            sendServerCode(MSG_WRONG_PARK_BASE + i);    // P1=41  P2=42 P3=43
          }
        }
      }
      // ===============================================
      // 배정과 무관한 일반 Occupied 변경
      // ===============================================
      else {
        // 11,12,13
        sendServerCode(MSG_SLOT_OCCUPIED_BASE + i);
      }
    }
    //=======================================
    // CASE B
    // Occupied -> Empty
    // ======================================
    if(previousOccupied[i]&&!occupied[i]){

      //sendEvent("SLOT_EMPTY:P"+String(i+1));
      Serial.print("SLOT_EMPTY:P");
      Serial.println(i + 1);
      // P1 = 21 P2 = 22 P3 = 23
      //B-1 오주차 차량이 이동
      if(i==wrongSlot){
        //sendEvent("WRONG_PARKING_CLEARED:P"+String(i+1));
        Serial.print("WRONG_PARKING_CLEARED:P");
        Serial.println(i + 1);
        // 51,52,53
        sendServerCode(MSG_WRONG_CLEAR_BASE + i);
        wrongSlot=-1; //오주차 초기화

        // 원래배정된위치 재안내
        if(assignedSlot!=-1){
          guideToSlot(assignedSlot);
          //sendEvent("PLEASE_PARK_AT:P"+String(assignedSlot+1));
          Serial.print("PLEASE_PARK_AT:P");
          Serial.println(assignedSlot + 1);
        }
      }
      
      //B-2 정상 주차 차량이 이동
      else {// 21,22,23
        sendServerCode(MSG_SLOT_EMPTY_BASE + i);
      }
    }
  }
  for(int i=0;i<3;i++){
    previousOccupied[i]=occupied[i];    //상태 갱신
  }
}

//---------------------------------------
// 4-1,주차 상황 공유(서버전송)
//---------------------------------------
void sendParkingStatus(){
  Serial.println("SEND FULL PARKING STATUS");
  //Serial.print("현재 주차 상황:");
  for(int i=0;i<3;i++){
    if(occupied[i]){
      uint8_t code=MSG_SLOT_OCCUPIED_BASE+i;
      Serial.print("P");
      Serial.print(i + 1);
      Serial.print(" OCCUPIED -> ");
      Serial.println(code);
      sendServerCode(code);
    }
    else{
      // P1=21, P2=22, P3=23
      uint8_t code = MSG_SLOT_EMPTY_BASE + i;
      Serial.print("P");
      Serial.print(i + 1);
      Serial.print(" EMPTY -> ");
      Serial.println(code);
      sendServerCode(code);
    }
    delay(100);
  }
}


// =====================================================
// EVENT_SEND
// =====================================================
/*void sendEvent(String message){
  Serial.println(message);
  espSerial.println(message);
}*/

// =====================================================
// RESET
// =====================================================
void resetSession() {
  assignedSlot = -1;
  wrongSlot = -1;
  enterVehicleDetected = false;
  exitVehicleDetected = false;
  entryRequestPending = false;
  exitRequestPending = false;
  enterGateOpened = false;
  exitGateOpened = false;

  exitPassing = false;
  // 실제 Servo도 닫기
  enterServo.write(GATE_CLOSE);
  exitServo.write(GATE_CLOSE);

  digitalWrite(LED_1,LOW);
  digitalWrite(LED_2,LOW);
  digitalWrite(LED_3,LOW);

  Serial.println("SESSION_RESET");
  sendParkingStatus();
}

// ================================================================
// AT 명령어를 ESP-01로 전송하고 응답을 시리얼 모니터에 출력하는 함수
// ================================================================
/*String sendData(String command, const int timeout, boolean debug) {
  String response = "";
  espSerial.print(command);
  long int time = millis();
  
  while ((time + timeout) > millis()) {
    while (espSerial.available()) {
      char c = espSerial.read();
      response += c;
    }
  }
  
  if (debug) {
    Serial.print(response);
  }
  return response;
}*/

// ================================================================
// 아두이노 -> 서버 숫자 송신 함수
// ================================================================
bool sendServerCode(uint8_t code) {
  // TCP로 1 byte 전송한다고 ESP-01에 알림
  espSerial.print("AT+CIPSEND=1\r\n");

  unsigned long start = millis();
  bool promptReceived = false;

  // ESP-01의 '>' 응답 대기
  while (millis() - start < 1000) {
    while (espSerial.available()) {
      char c = espSerial.read();
      if (c == '>') {
        promptReceived = true;
        break;
      }
    }
    if (promptReceived) {break;}
  }

  if (!promptReceived) {
    Serial.println("ERROR:CIPSEND_PROMPT");
    return false;
  }

  // ★ 문자열이 아니라 실제 binary 1 byte 전송
  espSerial.write(code);
  Serial.print("SERVER TX CODE: ");
  Serial.println(code);
  return true;
}

// ================================================================
// 아두이노 <- 서버 숫자 수신 함수
// ================================================================
int readServerCode(){
  static const char prefix[] = "+IPD,";
  static int prefixIndex = 0;
  static int state = 0;
  static int payloadLength = 0;
  static int payloadRead = 0;

  while(espSerial.available()){
    uint8_t c = espSerial.read();

    // =================================
    // STATE 0
    // "+IPD," 찾기
    // =================================

    if(state == 0){
      if(c == prefix[prefixIndex]){
        prefixIndex++;
        if(prefixIndex == 5){
          prefixIndex = 0;
          payloadLength = 0;
          state = 1;
        }
      }

      else{
        if(c == '+'){prefixIndex = 1;}
        else{prefixIndex = 0;}
      }
    }

    // =================================
    // STATE 1
    // payload length 읽기
    // =================================

    else if(state == 1){
      if(c >= '0' && c <= '9'){
        payloadLength = payloadLength * 10 + (c - '0');
      }

      else if(c == ':'){
        payloadRead = 0;
        state = 2;
      }
      else{state = 0;}
    }

    // =================================
    // STATE 2
    // 실제 binary data
    // =================================

    else if(state == 2){
      payloadRead++;
      uint8_t code = c;
      // 이번 프로젝트에서는
      // 항상 1-byte packet 사용
      if(payloadRead >= payloadLength){state = 0;}
      return code;
    }
  }
  return -1;
}

// =====================================================
// ESP-01 AT COMMAND
// setup용
// =====================================================
String sendATCommand(String command,const int timeout,boolean debug) {
  String response = "";
  espSerial.print(command);
  unsigned long start =millis();
  while (millis() - start< timeout) {
    while (espSerial.available()) {
      char c =espSerial.read();
      response += c;
    }
  }

  if (debug) {Serial.print(response);}
  return response;
}
