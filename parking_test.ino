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

// ---------- ESP32 Serial ----------
// Arduino RX <- ESP32 TX
// Arduino TX -> ESP32 RX
//
// A0, A1을 Digital Pin처럼 사용
SoftwareSerial espSerial(A0, A1);
// A0=SoftwareSerial RX=ESP32 TX

// A1= SoftwareSerial TX=ESP32 RX;

//==============객체===============
Servo enterServo;
Servo exitServo;

//=================상수==================
//차단기 각도
const int GATE_OPEN=90;
const int GATE_CLOSE=180;

// 차량 접근 판단 거리
const float VEHICLE_DISTANCE = 6.0;

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

// 차량 접근 이벤트 중복 방지
bool vehicleDetected=false;

// 차단기 상태
bool enterGateOpened = false;
bool exitGateOpened = false;

unsigned long enterGateOpenTime = 0;
unsigned long exitGateOpenTime = 0;

bool enterVehicleDetected = false;
bool exitVehicleDetected = false;

//==============Setup===============
void setup() {
  Serial.begin(9600);
  espSerial.begin(9600); //esp32
  Serial.setTimeout(100);
  espSerial.setTimeout(100);

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
  sendEvent("SMART_PARKING_READY");
  sendParkingStatus();
}

//==============Loop===============
void loop() {
  // put your main code here, to run repeatedly:

  // 0. IR 센서를 통해 주차 여부 확인
  /*Serial.print("P1=");
  Serial.print(digitalRead(IR_1));

  Serial.print(" P2=");
  Serial.print(digitalRead(IR_2));

  Serial.print(" P3=");
  Serial.println(digitalRead(IR_3));*/
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
  float distance=getDistance(TRIG_1,ECHO_1);

  if(distance<=VEHICLE_DISTANCE){ //차량 접근 시
    if(!enterVehicleDetected){//새롭게 감지된 차량이면
      enterVehicleDetected=true;//차량 감지를 True 로 설정
      int available=getAvailableCount();  //주차 공간 빈자리 개수 세어줌
      if(available==0){sendEvent("PARKING_FULL"); return;}
      
      // 입차 가능
      sendEvent("ENTRY_APPROACH");
      sendParkingStatus(); // 주차 상태 서버 전송
      /*
        카메라 촬영->
        OpenCV 번호판 인식->
        터치스크린에 빈자리 띄우기
      */
      
    }
  }
  else{   //차량 접근 안하면
    if(!enterGateOpened && assignedSlot==-1){       // 문이 안열리고, 차량이 할당된 자리가 없을 때
      enterVehicleDetected=false;                   // 차량 할당 안됨
    }
  }
}

//---------------------------------------
// 2. 차량 출차 감지 함수
//---------------------------------------
void checkExiting(){
  // 접근 감지하는 초음파 센서
  float distance=getDistance(TRIG_2,ECHO_2);

  if(distance<=VEHICLE_DISTANCE){ //차량 접근 시
    if(!exitVehicleDetected){//새롭게 감지된 차량이면
      exitVehicleDetected=true;//차량 감지를 True 로 설정
      sendEvent("EXIT_APPROACH");
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

      sendParkingStatus(); // 주차 상태 서버 전송
      /*
        카메라 촬영->
        OpenCV 번호판 인식->
        터치스크린에 빈자리 띄우기
      */
      
    }
  }
  else{   //차량 접근 안하면
    if(!exitGateOpened){       
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
void checkServerCommand(){
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
}

void processCommand(String command){
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
}


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
  sendEvent("GUIDE_ON:P"+String(slot+1));
}


//---------------------------------------
// 3-2 차단기 개방 (입/출)
//---------------------------------------
void openEnterGate(){
  enterServo.write(GATE_OPEN); //현 그림 기준으로 90도에서 열림
  enterGateOpened=true;
  enterGateOpenTime=millis();
  sendEvent("ENTRY_GATE_OPEN");
}

void openExitGate(){
  exitServo.write(GATE_OPEN); //현 그림 기준으로 90도에서 열림
  exitGateOpened=true;
  exitGateOpenTime=millis();
  sendEvent("EXIT_GATE_OPEN");
}


//---------------------------------------
// 4. 입구 차단기 컨트롤 
//---------------------------------------
void controlEnterGate(){
  if (!enterGateOpened) {return;}
  if(enterGateOpened){ //차단기가 열린 상태이면
    if(millis()-enterGateOpenTime>=GATE_OPEN_TIME){    //일정시간 경과시
      enterServo.write(GATE_CLOSE);                 // 출입게이트를 닫음
      enterGateOpened=false;                           // 게이트가 닫혔다고 ~
      sendEvent("ENTRY_GATE_CLOSED");
    }
  }
}

//---------------------------------------
// 5. 입구 차단기 컨트롤 
//---------------------------------------
void controlExitGate(){
  if (!exitGateOpened) {return;}
  if(exitGateOpened){ //차단기가 열린 상태이면
    if(millis()-exitGateOpenTime>=GATE_OPEN_TIME){    //일정시간 경과시
      exitServo.write(GATE_CLOSE);                 // 출입게이트를 닫음
      exitGateOpened=false;                           // 게이트가 닫혔다고 ~
      sendEvent("EXIT_GATE_CLOSED");
      sendEvent("EXIT_COMPLETE");
    }
  }
}

//---------------------------------------
// 6. 주차 상황 판정
//---------------------------------------
void checkParkingResult(){
  if(assignedSlot==-1){                   // Case 1: 할당된 슬롯이 없을 경우에
    for(int i=0;i<3;i++){
      previousOccupied[i]=occupied[i];    //그대로 둔다
    }
    return;
  }
  // Case 2 : 할당된 슬롯이 있을 경우에
  for(int i=0;i<3;i++){
    //=======================================
    // CASE A
    // Empty -> Occupied
    // ======================================
    if(!previousOccupied[i] && occupied[i]){     //과거 시점엔 비었으나, 현재 시점엔 차 있으면
      sendEvent("SLOT_OCCUIPIED:P"+String(i+1));
      // 현재 입차 차량에게 배정된 주차공간이 있을 때만 정상/오주차를 판단함
      if(assignedSlot!=-1){
        // A.1 정상 주차
        if(i==assignedSlot){        
          sendEvent("PARKING_COMPLETE:P"+String(i+1));
          digitalWrite(LED_1, LOW);              // 모든 LED 를 끄기
          digitalWrite(LED_2, LOW);
          digitalWrite(LED_3, LOW);
          assignedSlot=-1;     // 할당슬롯 초기화
          wrongSlot=-1;
          sendParkingStatus(); // 주차 상황을 공유}
        }

        // A.2 오주차
        else{
          // 이미 다른 오주차 상태가 없으면 최초 1회만 기록
          if(wrongSlot==-1){
            wrongSlot=i;
            sendEvent("WRONG_PARKING:P"+String(i+1));
            sendEvent("EXPECTED:P"+String(assignedSlot+1));
          }
        }
      }
    }
    //=======================================
    // CASE B
    // Occupied -> Empty
    // ======================================
    if(previousOccupied[i]&&!occupied[i]){

      sendEvent("SLOT_EMPTY:P"+String(i+1));
      //B-1 오주차 차량이 이동
      if(i==wrongSlot){
        sendEvent("WRONG_PARKING_CLEARED:P"+String(i+1));
        wrongSlot=-1; //오주차 초기화

        if(assignedSlot!=-1){
          guideToSlot(assignedSlot);
          sendEvent("PLEASE_PARK_AT:P"+String(assignedSlot+1));
        }
      }
      
      //B-2 정상 주차 차량이 이동

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
  String status="STATUS:";
  //Serial.print("현재 주차 상황:");
  for(int i=0;i<3;i++){
    status+="P";
    status+=String(i+1);
    status+="=";

    if(occupied[i]){status+=("OCCUPIED");}
    else{status+=("EMPTY");}

    if(i<2){status+=", ";}
  }
  status+=", AVAILABLE=";
  status+=String(getAvailableCount());
  sendEvent(status);
}

// =====================================================
// EVENT_SEND
// =====================================================
void sendEvent(String message){
  Serial.println(message);
  espSerial.println(message);
}

// =====================================================
// RESET
// =====================================================
void resetSession() {
  assignedSlot = -1;
  wrongSlot = -1;
  enterVehicleDetected = false;
  exitVehicleDetected = false;
  enterGateOpened = false;
  exitGateOpened = false;
  // 실제 Servo도 닫기
  enterServo.write(GATE_CLOSE);
  exitServo.write(GATE_CLOSE);

  digitalWrite(LED_1,LOW);
  digitalWrite(LED_2,LOW);
  digitalWrite(LED_3,LOW);

  sendEvent("SESSION_RESET");
  sendParkingStatus();
}