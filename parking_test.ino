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

//==============객체===============
Servo myservo1
Servo myservo2;

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
bool gateOpened=false; //차단기 상태

// 서버가 배정한 자리
int assignedSlot=-1; 
//0:P1, 1: P2, 2: P3 에 배정 & -1: 아직 배정하지 않음

// 차량 접근 이벤트 중복 방지
bool vehicleDetected=false;

// 차단기 상태
unsigned long gateOpenTime=0;
bool gateOpened=false;

//==============Setup===============
void setup() {
  Serial.begin(9600);
  pinMode(IR_1,INPUT);
  pinMode(IR_2,INPUT);
  pinMode(IR_3,INPUT);

  myservo1.attach(Servo_1);
  myservo2.attach(Servo_2);

  myservo1.write(GATE_CLOSE);
  myservo2.write(GATE_CLOSE);

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

  Serial.println("스마트 주차장 시스템 가동\n");
}

//==============Loop===============
void loop() {
  // put your main code here, to run repeatedly:

  // 0. IR 센서를 통해 주차 여부 확인
  updateParkingStatus();

  // 1. 입구 차량 접근 확인
  checkEntering();

  // 2. 서버의 주차 자리 배정 -> LED 가이딩 + 차단기 개방
  checkServerCommand();

  // 3. 차단기 닫기
  controlGate();

  // 4. 주차 완료 상황 판정
  checkParkingResult();

  delay(50);
}



//---------------------------------------
// 0. IR 센서를 통해 주차 여부 확인
// IR센서 high 차량 없음, low 차량 있음
//---------------------------------------
void updateParkingStatus(){
  occupied[0]=(digitalRead(IR_1)==LOW); //occupied 시 true
  occupied[1]=(digitalRead(IR_2)==LOW);
  occupied[2]=(digitalRead(IR_3)==LOW);
}


//---------------------------------------
// 1. 차량 접근 감지 함수
//---------------------------------------
void checkEntering(){
  // 접근 감지하는 초음파 센서
  float distance=getDistance(TRIG_1,ECHO_1);

  if(distance<=VEHICLE_DISTANCE){ //차량 접근 시
    if(!vehicleDetected){//새롭게 감지된 차량이면
      vehicleDetected=true;//차량 감지를 True 로 설정
      int available=getAvailableCount();  //주차 공간 빈자리 개수 세어줌
      if(available==0){Serial.println("만차입니다");}
      else{
        /*
          카메라 촬영
          OpenCV 번호판 인식
          터치스크린에 빈자리 띄우기
        */
        Serial.println("차량 접근을 허가합니다");
        Serial.print("주차 가능 수:");
        Serial.println(available);
        sendParkingStatus(); // 주차 상태 서버 전송
      }
    }
  }
  else{   //차량 접근 안하면
    if(!gateOpened && assignedSlot==-1){       // 문이 안열리고, 차량이 할당된 자리가 없을 때
      vehicleDetected=false;                   // 차량 할당 안됨
    }
  }
}

//---------------------------------------
// 1-1 거리 측정 함수
//---------------------------------------
float getDistance(int trig,int echo){
  digitalWrite(trig, LOW);
  delayMicroseconds(2);

  digitalWrite(trig, HIGH);
  delayMicroseconds(10);

  digitalWrite(trig,LOW);

  unsigned long duration = pulseIn(echo, HIGH);

  if(duration==0){return 999} // 수신받지 못하면 timeout

  float distance=duration*0.034/2;
  return distance;
}

//---------------------------------------
// 1.2 주차 공간 빈자리 갯수
//---------------------------------------
int getAvailableCount(){
  int count=0;
  for(int i=0;i<3;i++){
    if (!occupied[i]){count++;}
  }
  return count;
}


//-------------------------------------------------
// 2. 서버의 주차 자리 배정 -> LED 가이딩 + 차단기 개방
//-------------------------------------------------
void checkServerCommand(){
  if(!Serial.available()){return;}
  String command=Serial.readStringUntil('\n');
  command.trim();
  if(command.startsWith("ASSIGN:")){  // 할당 명령이 내려지면
  // 주차 자리 배정
    int slot=command.substring(7).toint();
    slot--;
    
    // 주차가능한 범위를 벗어나면(0,1,2 가 아님)
    if(slot<0||slot>2){
      Serial.println("주차 불가능한 구역입니다.");
      return;
    }

    // 이미 차가 있으면
    if(occupied[slot]){
      Serial.println("주차 자리가 이미 사용중입니다");
      return;
    }

    // 아니면
    assignedSlot=slot;
    Serial.print("주차자리가 배정되었습니다: ");
    Serial.println(slot+1); // 어느 구역으로 배정되었는지

    guideToSlot(slot);      //해당 구역으로 LED 가이딩
    openGate();             // 차단기열기
  }
  else if(command=="OPEN_GATE"){
    openGate();
  }
}


//---------------------------------------
// 2-1 주차 자리 배정 LED 안내
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
}


//---------------------------------------
// 2-2 차단기 개방
//---------------------------------------
void openGate(){
  servo1.write(GATE_OPEN); //현 그림 기준으로 90도에서 열림
  gateOpened=true;
  gateOpenTime=millis();
  Serial.println("차단기가 열렸습니다");
}

//---------------------------------------
// 3. 차단기 닫기 
//---------------------------------------
void controlGate(){
  if(gateOpened){ //차단기가 열린 상태이면
    if(millis()-gateOpenTime>=GATE_OPEN_TIME){    //일정시간 경과시
      myservo1.write(GATE_CLOSE);                 // 출입게이트를 닫음
      gateOpened=false;                           // 게이트가 닫혔다고 ~
      Serial.println("차단기가 닫혔습니다");
    }
  }
}


//---------------------------------------
// 4. 주차 상황 판정
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
    if(!previousOccupied[i] && occupied[i]){     //현재-1 시점엔 비었으나, 현재 시점엔 차 있으면
      if(i==assignedSlot){                       // Case 2-1(정상 주차): 그리고 그 슬롯이 할당되었을 경우
        Serial.print("주차가 완료되었습니다\n");    // 할당이 완료된 것
        digitalWrite(LED_1, LOW);              // 모든 LED 를 끄기
        digitalWrite(LED_2, LOW);
        digitalWrite(LED_3, LOW);

        sendParkingStatus(); // 주차 상황을 공유
        assignedSlot=-1;     // 할당슬롯 초기화
        vehicleDetected=false;
      }
      else{ // Case 2-2(주차 이상하게 함)
        Serial.print("잘못된 구역에 주차하셨습니다:P");
        Serial.println(i+1);

        Serial.print("주차 가능한 구역은:P");
        Serial.print(assignedSlot);
        Serial.println("입니다.");
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
  Serial.print("현재 주차 상황:");
  for(int i=0;i<3;i++){
    Serial.print("P");
    Serial.print(i+1); // P1, P2, P3 인지
    Serial.print("=");

    if(occupied[i]){Serial.print("사용중");}
    else(Serial.print("사용가능");)

    if(i<2){Serial.print(", ");}
  }
  Serial.print("\n 주차 가능한 자리수:");
  Serial.println(getAvailableCount());
}