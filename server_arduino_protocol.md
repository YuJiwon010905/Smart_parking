## 1. 통신 규칙 정의
 - 문자열 대신 uint8_t 1byte 만 사용한다.

|       코드 | 방향             | 의미                |
| --------: | ---------------- | ----------------- |
|       `1` | Arduino → Server | 입차 차량 접근          |
|       `2` | Arduino → Server | 출차 차량 접근          |
|       `3` | Arduino → Server | 만차                |
|   `11~13` | Arduino → Server | P1~P3 일반 OCCUPIED |
|   `21~23` | Arduino → Server | P1~P3 EMPTY       |
|   `31~33` | Arduino → Server | P1~P3 정상 주차 완료    |
|   `41~43` | Arduino → Server | P1~P3 오주차         |
|   `51~53` | Arduino → Server | P1~P3 오주차 해제      |
|      `60` | Arduino → Server | 출차 완료             |
| `101~103` | Server → Arduino | P1~P3 배정          |
|     `110` | Server → Arduino | 출차 허가             |
|     `120` | Server → Arduino | Reset             |

