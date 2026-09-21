#include <OneWire.h>
#include "pitches.h"

#define iButtonPin A3      // Линия data ibutton
#define R_Led 2            // RGB Led
#define G_Led 3
#define B_Led 4
#define ACpinGnd 5         // Земля аналогового компаратора
#define ACpin 6            // Вход Ain0 аналогового компаратора 0.1В для EM-Marie 
#define BtnPin 8           // Кнопка переключения режима чтение/запись
#define BtnPinGnd 9        // Земля кнопки переключения режима 
#define speakerPin 10       // Спикер, он же buzzer, он же beeper
#define FreqGen 11         // генератор 125 кГц
#define speakerPinGnd 12   // земля Спикера
#define blueModePin A2      // Эмулятор ключа rfid
#define rfidBitRate 2       // Скорость обмена с rfid в kbps
#define rfidUsePWD 0        // ключ использует пароль для изменения
#define rfidPWD 123456      // пароль для ключа

OneWire ibutton (iButtonPin); 
byte addr[8];                             // временный буфер
byte keyID[8];                            // ID ключа для записи
byte rfidData[5];                         // значащие данные frid em-marine
byte halfT; // Переменная для таймингов АЦП (нужна для Cyfral и Metacom)
bool readflag = false;                    // флаг сигнализирует, что данные с ключа успечно прочианы в ардуино
bool writeflag = false;                   // режим запись/чтение
bool ignoreFilters = false;             // Флаг отключения проверки чётности
bool preBtnPinSt = HIGH;
enum emRWType {rwUnknown, TM01, RW1990_1, RW1990_2, TM2004, T5557, EM4305, RW99, RW2004};               // тип болванки
enum emkeyType {keyUnknown, keyDallas, keyTM2004, keyCyfral, keyMetacom, keyEM_Marie};    // тип оригинального ключа  
emkeyType keyType;

void setup() {
  pinMode(BtnPin, INPUT_PULLUP);                            // включаем чтение и подягиваем пин кнопки режима к +5В
  pinMode(BtnPinGnd, OUTPUT); digitalWrite(BtnPinGnd, LOW); // подключаем второй пин кнопки к земле
  pinMode(speakerPin, OUTPUT);
  pinMode(speakerPinGnd, OUTPUT); digitalWrite(speakerPinGnd, LOW); // подключаем второй пин спикера к земле
  pinMode(ACpin, INPUT);                                            // Вход аналогового компаратора 3В для Cyfral
  pinMode(ACpinGnd, OUTPUT); digitalWrite(ACpinGnd, LOW);           // подключаем второй пин аналогового компаратора Cyfral к земле 
  pinMode(R_Led, OUTPUT); pinMode(G_Led, OUTPUT); pinMode(B_Led, OUTPUT);  //RGB-led
  digitalWrite(blueModePin, LOW); pinMode(blueModePin, OUTPUT);
  clearLed();
  pinMode(FreqGen, OUTPUT);                               
  digitalWrite(B_Led, HIGH);                                //awaiting of origin key data
  Serial.begin(115200);
  Sd_StartOK();
}

void clearLed(){
  digitalWrite(R_Led, LOW);
  digitalWrite(G_Led, LOW);
  digitalWrite(B_Led, LOW);  
}

//*************** dallas **************
emRWType getRWtype(){    
  byte answer;
  // пробуем определить RW-1990.1
  ibutton.reset(); ibutton.write(0xD1); 
  ibutton.write_bit(1);                 
  delay(10); pinMode(iButtonPin, INPUT);
  ibutton.reset(); ibutton.write(0xB5); 
  answer = ibutton.read();
  if (answer == 0xFE){
    Serial.println(F("Тип заготовки: Dallas RW-1990.1"));
    return RW1990_1;            
  }
  // пробуем определить RW-1990.2
  ibutton.reset(); ibutton.write(0x1D);  
  ibutton.write_bit(1);                  
  delay(10); pinMode(iButtonPin, INPUT);
  ibutton.reset(); ibutton.write(0x1E);  
  answer = ibutton.read();
  if (answer == 0xFE){
    ibutton.reset(); ibutton.write(0x1D); 
    ibutton.write_bit(0);                 
    delay(10); pinMode(iButtonPin, INPUT);
    Serial.println(F("Тип заготовки: Dallas RW-1990.2"));
    return RW1990_2; 
  }
  // пробуем определить TM-2004
  ibutton.reset(); ibutton.write(0x33);                     
  for ( byte i=0; i<8; i++) ibutton.read();                 
  ibutton.write(0xAA);                                          
  ibutton.write(0x00); ibutton.write(0x00);                 
  answer = ibutton.read();                                  
  byte m1[3] = {0xAA, 0, 0};                                 
  if (OneWire::crc8(m1, 3) == answer) {
    answer = ibutton.read();                                  
    Serial.println(F("Тип заготовки: Dallas TM2004"));
    ibutton.reset();
    return TM2004; 
  }
  // пробуем определить RW99 / iK23 / VZ-TM
  ibutton.reset(); ibutton.write(0x1D);  
  ibutton.write_bit(1);                  
  delay(10); pinMode(iButtonPin, INPUT);
  ibutton.reset(); ibutton.write(0x1E);  
  answer = ibutton.read();
  if (answer == 0xFE) {
    ibutton.reset(); ibutton.write(0x1D); 
    ibutton.write_bit(0);                 
    delay(10); pinMode(iButtonPin, INPUT);
    Serial.println(F("Тип заготовки: Dallas RW99 (iK23)"));
    return RW99; 
  }
  // пробуем определить RW2004
  ibutton.reset(); ibutton.write(0x3C);                     
  ibutton.write(0x00); ibutton.write(0x01); 
  answer = ibutton.read();
  if (answer == 0xFE || answer == 0xAA) {   
    Serial.println(F("Тип заготовки: Dallas RW2004"));
    ibutton.reset();
    return RW2004;
  }
  ibutton.reset();
  Serial.println(F("Тип заготовки: неизвестный Dallas, попытка TM-01!"));
  return TM01;                              
}

bool write2iBtnTM2004(){                // функция записи на TM2004
  byte answer; bool result = true;
  ibutton.reset();
  ibutton.write(0x3C);                                      // команда записи ROM для TM-2004    
  ibutton.write(0x00); ibutton.write(0x00);                 // передаем адрес с которого начинается запись
  for (byte i = 0; i<8; i++){
    digitalWrite(R_Led, !digitalRead(R_Led));
    ibutton.write(keyID[i]);
    answer = ibutton.read();
    //if (OneWire::crc8(m1, 3) != answer){result = false; break;}     // crc не верный
    delayMicroseconds(600); ibutton.write_bit(1); delay(50);         // испульс записи
    pinMode(iButtonPin, INPUT);
    Serial.print('*');
    Sd_WriteStep();
    if (keyID[i] != ibutton.read()) { result = false; break;}    //читаем записанный байт и сравниваем, с тем что должно записаться
  } 
  if (!result){
    ibutton.reset();
    Serial.println(" Ошибка копирования! Попробуйте еще раз!");
    Sd_ErrorBeep();
    digitalWrite(R_Led, HIGH);
    return false;    
  }
  ibutton.reset();
  Serial.println(" Ключ скопирован.");
  Sd_ReadOK();
  delay(500);
  digitalWrite(R_Led, HIGH);
  return true;
}

bool write2iBtnRW2004() {
  byte answer; bool result = true;
  ibutton.reset();
  ibutton.write(0x3C);                                      // команда записи ROM для RW2004    
  ibutton.write(0x00); ibutton.write(0x00);                 // стартовый адрес записи (0x0000)
  
  for (byte i = 0; i < 8; i++) {
    digitalWrite(R_Led, !digitalRead(R_Led));
    ibutton.write(keyID[i]);
    answer = ibutton.read();
    
    // Специфический тайминг фиксации данных для чипа RW2004
    delayMicroseconds(400); 
    ibutton.write_bit(1); 
    delay(30);         
    
    pinMode(iButtonPin, INPUT);
    Serial.print('*');
    Sd_WriteStep();
    
    if (keyID[i] != ibutton.read()) { result = false; break; } // проверка записанного байта
  } 
  
  if (!result) {
    ibutton.reset();
    Serial.println("Ошибка копирования на RW2004! Попробуйте еще раз!");
    Sd_ErrorBeep();
    digitalWrite(R_Led, HIGH);
    return false;    
  }
  
  ibutton.reset();
  Serial.println("Ключ успешно записан на заготовку RW2004.");
  Sd_ReadOK();
  delay(500);
  digitalWrite(R_Led, HIGH);
  return true;
}

bool write2iBtnRW1990_1_2_TM01(emRWType rwType){              // функция записи на RW1990.1, RW1990.2, TM-01C(F)
  byte rwCmd, rwFlag = 1;
  switch (rwType){
    case TM01: rwCmd = 0xC1; break;                   //TM-01C(F)
    case RW1990_1: rwCmd = 0xD1; rwFlag = 0; break;  // RW1990.1  флаг записи инвертирован
    case RW1990_2: rwCmd = 0x1D; break;              // RW1990.2
  }
  ibutton.reset(); ibutton.write(rwCmd);       // send 0xD1 - флаг записи
  ibutton.write_bit(rwFlag);                   // записываем значение флага записи = 1 - разрешить запись
  delay(10); pinMode(iButtonPin, INPUT);
  ibutton.reset(); ibutton.write(0xD5);        // команда на запись
  for (byte i = 0; i<8; i++){
    digitalWrite(R_Led, !digitalRead(R_Led));
    if (rwType == RW1990_1) BurnByte(~keyID[i]);      // запись происходит инверсно для RW1990.1
      else BurnByte(keyID[i]);
    Serial.print('*');
    Sd_WriteStep();
  } 
  ibutton.write(rwCmd);                     // send 0xD1 - флаг записи
  ibutton.write_bit(!rwFlag);               // записываем значение флага записи = 1 - отключаем запись
  delay(10); pinMode(iButtonPin, INPUT);
  digitalWrite(R_Led, LOW);       
  if (!dataIsBurningOK()){          // проверяем корректность записи
    Serial.println(" Ошибка копирования! Попробуйте еще раз!");
    Sd_ErrorBeep();
    digitalWrite(R_Led, HIGH);
    return false;
  }
  Serial.println(F("Ключ успешно скопирован."));
  if ((keyType == keyMetacom)||(keyType == keyCyfral)){      
    ibutton.reset();
    if (keyType == keyCyfral) ibutton.write(0xCA);       
      else ibutton.write(0xCB);                       
    ibutton.write_bit(1);                             
    delay(10); pinMode(iButtonPin, INPUT);
    Serial.println(F("Финализация отправлена!"));
  }
  Sd_ReadOK(); delay(500); digitalWrite(R_Led, HIGH); return true;
}


bool write2RW99() {
  ibutton.reset();
  ibutton.write(0x1D);  // Секретная сервисная команда разлочки чипа RW99
  ibutton.write_bit(1); // Открываем флаг записи
  delay(10); 
  pinMode(iButtonPin, INPUT);
  
  ibutton.reset();
  ibutton.write(0xD5);  // Стандартная команда отправки данных
  for (byte i = 0; i < 8; i++) {
    BurnByte(keyID[i]); // Шьем байты без инверсии
    Serial.print('*');
    Sd_WriteStep();
  }
  
  // Закрываем сессию записи
  ibutton.reset();
  ibutton.write(0x1D);
  ibutton.write_bit(0); // Закрываем флаг записи
  delay(10);
  pinMode(iButtonPin, INPUT);
  
  // Проверяем корректность записи
  if (!dataIsBurningOK()) {
    Serial.println(" Ошибка копирования на RW99!");
    Sd_ErrorBeep();
    digitalWrite(R_Led, HIGH);
    return false;
  }
  Serial.println(" Ключ успешно записан!");
  Sd_ReadOK();
  delay(500);
  digitalWrite(R_Led, HIGH);
  return true;
}

void BurnByte(byte data){
  for(byte n_bit=0; n_bit<8; n_bit++){ 
    ibutton.write_bit(data & 1);  
    delay(5);                        // даем время на прошивку каждого бита до 10 мс
    data = data >> 1;                // переходим к следующему bit
  }
  pinMode(iButtonPin, INPUT);
}

bool dataIsBurningOK(){
  byte buff[8];
  if (!ibutton.reset()) return false;
  ibutton.write(0x33);
  ibutton.read_bytes(buff, 8);
  byte Check = 0;
  for (byte i = 0; i < 8; i++) 
    if (keyID[i] == buff[i]) Check++;      // сравниваем код для записи с тем, что уже записано в ключе.
  if (Check != 8) return false;             // если коды совпадают, ключ успешно скопирован
  return true;
}

bool write2iBtn(){
  int Check = 0;
  if (!ibutton.search(addr)) { 
    ibutton.reset_search(); 
    return false;
  }
  Serial.print("Новый код заготовки: ");
  for (byte i = 0; i < 8; i++) {
    Serial.print(addr[i], HEX); Serial.print(":");  
    if (keyID[i] == addr[i]) Check++;    // сравниваем код для записи с тем, что уже записано в ключе.
  }
  if (Check == 8) {                     // если коды совпадают, ничего писать не нужно
    digitalWrite(R_Led, LOW); 
    Serial.println(" Код ключа одинаковый; в записи не требуется.");
    Sd_ErrorBeep();
    digitalWrite(R_Led, HIGH);
    delay(500);
    return false;
  }
  
    emRWType rwType = getRWtype(); // определяем тип заготовки
  Serial.print("\nЗапись кода iButton: ");
  
  // ВЫБОР АЛГОРИТМА ЗАПИСИ
  if (rwType == TM2004) {
    return write2iBtnTM2004();  // Шьем TM2004
  }
  else if (rwType == RW99) {
    return write2RW99();        // Шьем RW99
  }
  else if (rwType == RW2004) {
    return write2iBtnRW2004();  // ДОБАВЛЕНО: Шьем RW2004
  }
  else {
    return write2iBtnRW1990_1_2_TM01(rwType); // Пробуем прошить стандартные RW1990 / TM-01
  }
}

bool searchIbutton(){
  if (!ibutton.reset()) {
    return false; 
  }
  if (!ibutton.search(addr)) { 
    ibutton.reset_search(); 
    return false;
  }  
  for (byte i = 0; i < 8; i++) {
    Serial.print(addr[i], HEX); Serial.print(":"); keyID[i] = addr[i];                               
  }
  if (addr[0] == 0x01) {                         
    keyType = keyDallas;
    if (getRWtype() == TM2004) keyType = keyTM2004;
    
    // ИСПРАВЛЕНО: Если CRC плохой, мы НЕ прерываем чтение, а просто предупреждаем!
    if (OneWire::crc8(addr, 7) != addr[7]) {
      Serial.println(F(" -> [ВНИМАНИЕ: CRC неверный! Ключ поврежден, но считан]")); 
      Sd_ErrorBeep(); 
      // return false; // Закомментировано! Ключ пропустят в систему для перезаписи
    } else {
      Serial.println(F(" -> [Dallas ОК]"));
    }
    return true;
  }
  if ((addr[0]>>4) == 0x0E) Serial.println(F(" Тип: Cyfral в режиме Dallas."));
    else Serial.println(F(" Тип: неизвестный Dallas."));
  keyType = keyUnknown; return true;
}

// Новая общая функция чтения импульсов через АЦП
unsigned long pulseACompA(bool pulse, byte Average = 80, unsigned long timeOut = 1500){  
  bool AcompState;
  unsigned long tEnd = micros() + timeOut;
  do {
    ADCSRA |= (1<<ADSC);
    while(ADCSRA & (1 << ADSC));
    if (ADCH > 200) return 0;
    if (ADCH > Average) AcompState = HIGH;  
      else AcompState = LOW;
    if (AcompState == pulse) {
      tEnd = micros() + timeOut;
      do {
          ADCSRA |= (1<<ADSC);
          while(ADCSRA & (1 << ADSC)); 
        if (ADCH > Average) AcompState = HIGH;  
          else AcompState = LOW;
        if (AcompState != pulse) return (unsigned long)(micros() + timeOut - tEnd);  
      } while (micros() < tEnd);
      return 0;                                                 
    }             
  } while (micros() < tEnd);
  return 0;
}

// Настройка АЦП на сверхбыстрое чтение
void ADCsetOn(){
  ADMUX = (ADMUX&0b11110000) | 0b0011 | (1<<ADLAR);
  ADCSRB = (ADCSRB & 0b11111000) | (1<<ACME);       
  ADCSRA = (ADCSRA & 0b11111000) | 0b011 | (1<<ADEN) | (1<<ADSC); 
}

// Функция автоподстройки под уровень сигнала ключа
byte calcAverage(){
  unsigned int sum = 127; byte preADCH = 0, j = 0; 
  for (byte i = 0; i<255; i++) {
    ADCSRA |= (1<<ADSC);
    delayMicroseconds(10);
    while(ADCSRA & (1 << ADSC)); 
    sum += ADCH;
  }
  sum = sum >> 8;
  unsigned long tSt = micros();
  for (byte i = 0; i<255; i++) {
    delayMicroseconds(4);
    ADCSRA |= (1<<ADSC);
    while(ADCSRA & (1 << ADSC)); 
    if (((ADCH > sum)&&(preADCH < sum)) | ((ADCH < sum)&&(preADCH > sum))) {
      j++;
      preADCH = ADCH;
    }   
  }
  halfT = (byte)((micros() - tSt) / j);
  return (byte)sum;
}

// Цифрал
bool read_cyfral(byte* buf, byte CyfralPin){
  unsigned long ti; byte i=0, j = 0, k = 0;
  analogRead(iButtonPin); 
  ADCsetOn(); 
  byte aver = calcAverage();
  unsigned long tEnd = millis() + 30;
  do{
    ti = pulseACompA(HIGH, aver);
    if ((ti == 0) || (ti > 260) || (ti < 10)) {i = 0; j=0; k = 0; continue;}
    if ((i < 3) && (ti > halfT)) {i = 0; j = 0; k = 0; continue;} 
    if ((i == 3) && (ti < halfT)) continue;      
    
    if (ti > halfT) bitSet(buf[i >> 3], 7-j);
      else if (i > 3) k++; 
      
    if ((i > 3) && ((i-3)%4 == 0) ){ 
      if (!ignoreFilters && (k != 1)) {
        for (byte n = 0; n < (i >> 3)+2; n++) buf[n] = 0; 
        i = 0; j = 0; k = 0; 
        continue;
      } 
      k = 0; 
    }
    j++; if (j>7) j=0;
    i++;
  } while ((millis() < tEnd) && (i < 36));
  if (i < 36) return false;
  return true;
}

bool searchCyfral(){
  for (byte i = 0; i < 8; i++) addr[i] = 0;
  if (!read_cyfral(addr, iButtonPin)) return false; 
  
  keyType = keyCyfral;
  for (byte i = 0; i < 8; i++) {
    Serial.print(addr[i], HEX); Serial.print(":");
    keyID[i] = addr[i]; 
  }
  Serial.println(F(" Тип: Cyfral DC2000 "));
  return true;  
}

// Метаком
bool read_metacom(byte* buf, byte MetacomPin){
  unsigned long ti; byte i = 0, j = 0, k = 0;
  analogRead(iButtonPin);
  ADCsetOn();
  byte aver = calcAverage();
  unsigned long tEnd = millis() + 30;
  do{
    ti = pulseACompA(LOW, aver);
    if ((ti == 0) || (ti > 500)) {i = 0; j=0; k = 0; continue;}
    if ((i == 0) && (ti+30 < (halfT<<1))) continue;      
    if ((i == 2) && (ti > halfT)) {i = 0; j = 0;  continue;}      
    if (((i == 1) || (i == 3)) && (ti < halfT)) {i = 0; j = 0; continue;}      
    if (ti < halfT) {   
      bitSet(buf[i >> 3], 7-j);
      if (i > 3) k++;                             
    }
    
    if ((i > 3) && ((i-3)%8 == 0) ){        
      if (!ignoreFilters && (k & 1)) { 
        for (byte n = 0; n < (i >> 3)+1; n++) buf[n] = 0; 
        i = 0; j = 0;  k = 0; 
        continue;
      }              
      k = 0;
    }   
    j++; if (j>7) j=0;
    i++;
  }  while ((millis() < tEnd) && (i < 36));
  if (i < 36) return false;
  return true;
}

bool searchMetacom(){
  for (byte i = 0; i < 8; i++) addr[i] = 0;
  if (!read_metacom(addr, iButtonPin)) return false;
  
  keyType = keyMetacom;
  for (byte i = 0; i < 8; i++) {
    Serial.print(addr[i], HEX); Serial.print(":");
    keyID[i] = addr[i];                               
  }
  Serial.println(F(" Тип: METAKOM "));
  return true;  
}

//**********EM-Marine***************************
bool vertEvenCheck(byte* buf){        // проверка четности столбцов с данными
  byte k;
  k = 1&buf[1]>>6 + 1&buf[1]>>1 + 1&buf[2]>>4 + 1&buf[3]>>7 + 1&buf[3]>>2 + 1&buf[4]>>5 + 1&buf[4] + 1&buf[5]>>3 + 1&buf[6]>>6 + 1&buf[6]>>1 + 1&buf[7]>>4;
  if (k&1) return false;
  k = 1&buf[1]>>5 + 1&buf[1] + 1&buf[2]>>3 + 1&buf[3]>>6 + 1&buf[3]>>1 + 1&buf[4]>>4 + 1&buf[5]>>7 + 1&buf[5]>>2 + 1&buf[6]>>5 + 1&buf[6] + 1&buf[7]>>3;
  if (k&1) return false;
  k = 1&buf[1]>>4 + 1&buf[2]>>7 + 1&buf[2]>>2 + 1&buf[3]>>5 + 1&buf[3] + 1&buf[4]>>3 + 1&buf[5]>>6 + 1&buf[5]>>1 + 1&buf[6]>>4 + 1&buf[7]>>7 + 1&buf[7]>>2;
  if (k&1) return false;
  k = 1&buf[1]>>3 + 1&buf[2]>>6 + 1&buf[2]>>1 + 1&buf[3]>>4 + 1&buf[4]>>7 + 1&buf[4]>>2 + 1&buf[5]>>5 + 1&buf[5] + 1&buf[6]>>3 + 1&buf[7]>>6 + 1&buf[7]>>1;
  if (k&1) return false;
  if (1&buf[7]) return false;
  //номер ключа, который написан на корпусе
  rfidData[0] = (0b01111000&buf[1])<<1 | (0b11&buf[1])<<2 | buf[2]>>6;
  rfidData[1] = (0b00011110&buf[2])<<3 | buf[3]>>4;
  rfidData[2] = buf[3]<<5 | (0b10000000&buf[4])>>3 | (0b00111100&buf[4])>>2;
  rfidData[3] = buf[4]<<7 | (0b11100000&buf[5])>>1 | 0b1111&buf[5];
  rfidData[4] = (0b01111000&buf[6])<<1 | (0b11&buf[6])<<2 | buf[7]>>6;
  return true;
}

byte ttAComp(unsigned long timeOut = 10000){  // pulse 0 or 1 or -1 if timeout
  byte AcompState, AcompInitState;
  unsigned long tStart = micros();
  AcompInitState = (ACSR >> ACO)&1;               // читаем флаг компаратора
  do {
    AcompState = (ACSR >> ACO)&1;                 // читаем флаг компаратора
    if (AcompState != AcompInitState) {
      delayMicroseconds(1000/(rfidBitRate*4));    // 1/4 Period on 2 kBps = 125 mks 
      AcompState = (ACSR >> ACO)&1;               // читаем флаг компаратора      
      delayMicroseconds(1000/(rfidBitRate*2));    // 1/2 Period on 2 kBps = 250 mks 
      return AcompState;  
    }
  } while ((long)(micros() - tStart) < timeOut);
  return 2;                                             //таймаут, компаратор не сменил состояние
}

bool readEM_Marie(byte* buf){
  unsigned long tStart = millis();
  byte ti; byte j = 0, k=0;
  for (int i = 0; i<64; i++){    // читаем 64 bit
    ti = ttAComp();
    if (ti == 2)  break;         //timeout
    //Serial.print("b ");
    if ( ( ti == 0 ) && ( i < 9)) {  // если не находим 9 стартовых единиц - начинаем сначала
      if ((long)(millis()-tStart) > 50) { ti=2; break;}  //timeout
      i = -1; j=0; continue;
    }
    if ((i > 8) && (i < 59)){     //начиная с 9-го бита проверяем контроль четности каждой строки
      if (ti) k++;                // считаем кол-во единиц
      if ( (i-9)%5 == 4 ){        // конец строки с данными из 5-и бит, 
        if (k & 1) {              //если нечетно - начинаем сначала
          i = -1; j = 0; k = 0; continue; 
        }
        k = 0;
      }
    }
    if (ti) bitSet(buf[i >> 3], 7-j);
      else bitClear(buf[i >> 3], 7-j);
    j++; if (j>7) j=0; 
  }
  if (ti == 2) return false;         //timeout
  return vertEvenCheck(buf);
}

void rfidACsetOn(){
  //включаем генератор 125кГц
  TCCR2A = _BV(COM2A0) | _BV(COM2B1) | _BV(WGM21) | _BV(WGM20);  //Вкючаем режим Toggle on Compare Match на COM2A (pin 11) и счет таймера2 до OCR2A
  TCCR2B = _BV(WGM22) | _BV(CS20);                                // Задаем делитель для таймера2 = 1 (16 мГц)
  OCR2A = 63;                                                    // 63 тактов на период. Частота на COM2A (pin 11) 16000/64/2 = 125 кГц, Скважнось COM2A в этом режиме всегда 50% 
  OCR2B = 31;                                                     // Скважность COM2B 32/64 = 50%  Частота на COM2A (pin 3) 16000/64 = 250 кГц
  // включаем компаратор
  ADCSRB &= ~(1<<ACME);           // отключаем мультиплексор AC
  ACSR &= ~(1<<ACBG);             // отключаем от входа Ain0 1.1V
}

bool searchEM_Marine( bool copyKey = true){
  byte gr = digitalRead(G_Led); bool rez = false; rfidACsetOn(); delay(13);                
  
  // ИСПРАВЛЕНО: Включаем ignoreFilters принудительно перед чтением, 
  // чтобы заготовка с битым паритетом смогла отдать свои байты для перезаписи
  bool backupFilter = ignoreFilters; 
  ignoreFilters = true; // Временно отключаем проверку фильтров строк
  
  bool readStatus = readEM_Marie(addr);
  ignoreFilters = backupFilter; // Возвращаем настройку обратно
  
  if (!readStatus) {
    if (!copyKey) TCCR2A &=0b00111111; 
    digitalWrite(G_Led, gr); 
    return rez;
  }
  
  rez = true; 
  keyType = keyEM_Marie; 
  for (byte i = 0; i<8; i++){ if (copyKey) keyID[i] = addr[i]; Serial.print(addr[i], HEX); Serial.print(":"); }
  
  Serial.print(F(" (код ")); Serial.print(rfidData[0]); Serial.print(F(" номер "));
  unsigned long keyNum = (unsigned long)rfidData[1]<<24 | (unsigned long)rfidData[2]<<16 | (unsigned long)rfidData[3]<<8 | (unsigned long)rfidData[4];
  Serial.print(keyNum); Serial.println(F(") Считан EM-Marine"));
  if (!copyKey) TCCR2A &=0b00111111; digitalWrite(G_Led, gr); return rez;
}

void TxBitRfid(byte data){
  if (data & 1) delayMicroseconds(54*8); 
    else delayMicroseconds(24*8);
  rfidGap(19*8);                       //write gap
}

void TxByteRfid(byte data){
  for(byte n_bit=0; n_bit<8; n_bit++){
    TxBitRfid(data & 1);
    data = data >> 1;                   // переходим к следующему bit
  }
}

void rfidGap(unsigned int tm){
  TCCR2A &=0b00111111;                //Оключить ШИМ COM2A 
  delayMicroseconds(tm);
  TCCR2A |= _BV(COM2A0);              // Включить ШИМ COM2A (pin 11)      
}

bool T5557_blockRead(byte* buf){
  byte ti; byte j = 0; 
  for (int i = 0; i<33; i++){    // читаем стартовый 0 и 32 значащих bit
    ti = ttAComp(2000);
    if (ti == 2)  break;         // Таймаут
    if ( ( ti == 1 ) && ( i == 0)) {  
      ti=2; 
      Serial.println(F(" Ошибка: сбой синхронизации RFID!")); 
      break;
    }
    if (i > 0){     
      if (ti) bitSet(buf[(i-1) >> 3], 7-j);
        else bitClear(buf[(i-1) >> 3], 7-j);
      j++; if (j>7) j=0;
    }
  }
  if (ti == 2) return false; return true;
}

bool sendOpT5557(byte opCode, unsigned long password = 0, byte lockBit = 0, unsigned long data = 0, byte blokAddr = 1){
  TxBitRfid(opCode >> 1); TxBitRfid(opCode & 1); // передаем код операции 10
  if (opCode == 0b00) return true;
  // password
  TxBitRfid(lockBit & 1);               // lockbit 0
  if (data != 0){
    for (byte i = 0; i<32; i++) {
      TxBitRfid((data>>(31-i)) & 1);
    }
  }
  TxBitRfid(blokAddr>>2); TxBitRfid(blokAddr>>1); TxBitRfid(blokAddr & 1);      // адрес блока для записи
  delay(4);                       // ждем пока пишутся данные
  return true;
}

bool write2rfidT5557(byte* buf){
  bool result; unsigned long data32;
  delay(6);
  for (byte k = 0; k<2; k++){                                       // send key data
    byte offset = k << 2;
    // ОФИЦИАЛЬНОЕ РЕШЕНИЕ БАГА КОМПИЛЯТОРА:
    // Сначала жестко объявляем каждый байт как 32-битное число, а уже потом двигаем!
    unsigned long b0 = buf[offset + 0];
    unsigned long b1 = buf[offset + 1];
    unsigned long b2 = buf[offset + 2];
    unsigned long b3 = buf[offset + 3];

    data32 = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;

    rfidGap(30 * 8);                                                 //start gap
    sendOpT5557(0b10, 0, 0, data32, k+1);                            //передаем 32 бита ключа в blok k
    Serial.print('*'); delay(6);
  }
  delay(6);
  rfidGap(30 * 8);          //start gap
  sendOpT5557(0b00);
  
  delay(50); // Пауза, чтобы EEPROM чипа успел физически зафиксировать заряд
  
  result = readEM_Marie(addr);
  TCCR2A &=0b00111111;              //Оключить ШИМ COM2A (pin 11)
  
  // Возвращаем строгое родное сравнение автора
  for (byte i = 0; i < 8; i++) {
    if (addr[i] != keyID[i]) { result = false; break; }
  }

  if (!result){
    Serial.println(" Ошибка копирования! Попробуйте ещё раз!");
    Sd_ErrorBeep();
  } else {
    Serial.println(" [УСПЕХ!] Конфигурация жестко записана в память чипа.");
    Sd_ReadOK();
    
    // Автовыход дубликатора в обычный режим чтения
    readflag = false;
    writeflag = false;
    clearLed();
    digitalWrite(B_Led, HIGH); 
  }
  digitalWrite(R_Led, HIGH);
  return result;  
}

emRWType getRfidRWtype(){
  unsigned long data32, data33; byte buf[4] = {0, 0, 0, 0}; 
  rfidACsetOn();            // включаем генератор 125кГц и компаратор
  delay(13);                // 13 мс длятся переходные процессы детектора
  rfidGap(30 * 8);          // start gap
  sendOpT5557(0b11, 0, 0, 0, 1); // переходим в режим чтения Vendor ID 
  if (!T5557_blockRead(buf)) return rwUnknown; 
  data32 = (unsigned long)buf[0]<<24 | (unsigned long)buf[1]<<16 | (unsigned long)buf[2]<<8 | (unsigned long)buf[3];
  delay(4);
  rfidGap(20 * 8);          // gap  
  data33 = 0b00000000000101001000000001000000 | (rfidUsePWD << 4);   // конфиг регистр 0b00000000000101001000000001000000
  sendOpT5557(0b10, 0, 0, data33, 0);   // передаем конфиг регистр
  delay(4);
  rfidGap(30 * 8);          // start gap
  sendOpT5557(0b11, 0, 0, 0, 1); // переходим в режим чтения Vendor ID 
  if (!T5557_blockRead(buf)) return rwUnknown; 
  data33 = (unsigned long)buf[0]<<24 | (unsigned long)buf[1]<<16 | (unsigned long)buf[2]<<8 | (unsigned long)buf[3];
  sendOpT5557(0b00, 0, 0, 0, 0);  // send Reset
  delay(6);
  if (data32 != data33) return rwUnknown;    
  Serial.print(F(" Тип ключа: T5557. Код: ")); // Обернули в F() для экономии памяти
  Serial.println(data32, HEX);
  return T5557;
}

bool write2rfid(){
  bool Check = true;
  if (searchEM_Marine(false)) {
    for (byte i = 0; i < 8; i++)
      if (addr[i] != keyID[i]) { Check = false; break; }  
    if (Check) {                                          
      digitalWrite(R_Led, LOW); 
      Serial.println(F("Код RFID совпадает. Запись не требуется."));
      Sd_ErrorBeep();
      digitalWrite(R_Led, HIGH);
      delay(500);
      return false;
    }
  }
  
  emRWType rwType = getRfidRWtype(); // определяем тип T5557 (T5577)
  if (rwType != rwUnknown) Serial.print(F("\nЗапись кода RFID: "));
  
  switch (rwType){
    case T5557: return write2rfidT5557(keyID); break; // пишем T5577
    case rwUnknown: break;
  }
  return false;
}

void loop() {
    // === 1. ПРОВЕРКА КОМАНД ИЗ СЕРИАЛ-ПОРТА ===
  if (Serial.available() > 0) {
    String bufString = Serial.readString(); bufString.trim(); 
    
    if (bufString.equalsIgnoreCase("ignore")) {
      ignoreFilters = true; Serial.println(F("Всеядный режим"));
      clearLed(); digitalWrite(G_Led, HIGH); delay(300); digitalWrite(G_Led, LOW); digitalWrite(B_Led, HIGH);
      return;
    }
    
    // ФЛАГ РЕАНИМАЦИИ ДАЛЛАС
    if (bufString.equalsIgnoreCase("reset")) {
      readflag = false; writeflag = false;
      // Используем свободные флаги или выводим статус
      Serial.println(F("[РЕЖИМ РЕАНИМАЦИИ RW1990.1] ПРИЛОЖИТЕ КЛЮЧ К ЛУЗЕ..."));
      clearLed();
      // Заставим мигать красный светодиод в цикле ожидания
      while(!ibutton.reset()) {
        digitalWrite(R_Led, !digitalRead(R_Led));
        delay(100);
        if (Serial.available() > 0) { Serial.readString(); Serial.println(F("Отменено")); digitalWrite(B_Led, HIGH); return; } // Выход если что-то ввели
      }
      
      // КЛЮЧ ПОЯВИЛСЯ! ШЬЕМ!
      Serial.println(F("Чип обнаружен! Запись «вслепую»..."));
      byte recoveryID[8] = { 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x2F }; 
      
      ibutton.write(0xD1); ibutton.write_bit(0); delay(10); pinMode(iButtonPin, INPUT);
      ibutton.reset(); ibutton.write(0xD5); 
      for (byte i = 0; i < 8; i++) {
        byte data = ~recoveryID[i];
        for(byte n_bit=0; n_bit<8; n_bit++){ ibutton.write_bit(data & 1); delay(5); data = data >> 1; }
      }
      delay(20); pinMode(iButtonPin, INPUT);
      ibutton.reset(); ibutton.write(0xD1); ibutton.write_bit(1); delay(10); pinMode(iButtonPin, INPUT);
      
      clearLed(); digitalWrite(B_Led, HIGH);
      Serial.println(F("Успех! Записан вездеход DALLAS (CRC 2F)."));
      Sd_ReadOK();
      return;
    }
    
    // (Ниже идет ваш стандартный парсинг обычных HEX кодов для записи)
    int spaceIndex = bufString.indexOf(' '); String hexString = bufString; String typeString = ""; 
    if (spaceIndex != -1) {
      hexString = bufString.substring(0, spaceIndex); typeString = bufString.substring(spaceIndex + 1);
      typeString.trim(); typeString.toLowerCase();
    }
    hexString.replace(":", "");
    
    if (hexString.length() == 16) {
      for (byte i = 0; i < 8; i++) {
        String byteString = hexString.substring(i * 2, (i * 2) + 2);
        keyID[i] = (byte) strtol(byteString.c_str(), NULL, 16); addr[i] = keyID[i];       
      }
      if (typeString.length() > 0) {
        if (typeString == "cyfral") { keyType = keyCyfral; Serial.println(F("Ручной: CYFRAL")); } 
        else if (typeString == "metakom") { keyType = keyMetacom; Serial.println(F("Ручной: METAKOM")); } 
        else if (typeString == "rfid") { keyType = keyEM_Marie; Serial.println(F("Ручной: RFID (Запись через антенну)")); } 
        else { keyType = keyDallas; Serial.println(F("Ручной: DALLAS")); }
      } else {
        if (keyID[0] == 0x01) { keyType = keyDallas; Serial.println(F("Авто: Dallas")); } 
        else { keyType = keyCyfral; Serial.println(F("Авто: CYFRAL (Будет финализация!)")); }
      }
      readflag = true; writeflag = true; clearLed(); digitalWrite(R_Led, HIGH); 
      Serial.print(F("Код: "));
      for (byte i = 0; i < 8; i++) { Serial.print(keyID[i], HEX); if (i < 7) Serial.print(":"); }
      Serial.println(F(" | Запись...")); Sd_ReadOK(); 
    } else { Serial.println(F("Ошибка ввода")); }
  }

  // === 2. ОБРАБОТКА НАЖАТИЯ ФИЗИЧЕСКОЙ КНОПКИ ===
  bool BtnPinSt  = digitalRead(BtnPin); bool BtnClick;
  if ((BtnPinSt == LOW) && (preBtnPinSt != LOW)) BtnClick = true; else BtnClick = false; preBtnPinSt = BtnPinSt;
  
  if (BtnClick) {  
    if (readflag == true) {
      writeflag = !writeflag; clearLed(); 
      if (writeflag) digitalWrite(R_Led, HIGH);
        else { digitalWrite(G_Led, HIGH); ignoreFilters = false; Serial.println(F("Фильтры включены")); }
      Serial.print(F("Запись = ")); Serial.print(writeflag); Serial.print(F(" | Код = "));
      for (byte i = 0; i < 8; i++) { Serial.print(keyID[i], HEX); Serial.print(":"); }; Serial.println();
    } else { clearLed(); Sd_ErrorBeep(); digitalWrite(B_Led, HIGH); }
  }
  
  // === 3. РЕЖИМ ОБЫЧНОГО ДУБЛИРОВАНИЯ ===
  if (!writeflag) {
    // Сначала даём шанс бесконтактной антенне RFID
    if (searchEM_Marine(true)) { 
      digitalWrite(G_Led, LOW); Sd_ReadOK(); readflag = true; clearLed(); digitalWrite(G_Led, HIGH);
      ignoreFilters = false; 
    } 
    // Если в эфире пусто, опрашиваем контактную лузу iButton (Dallas / Cyfral / Metacom)
    else if (searchIbutton() || searchCyfral() || searchMetacom()) { 
      digitalWrite(G_Led, LOW); Sd_ReadOK(); readflag = true; clearLed(); digitalWrite(G_Led, HIGH);
      ignoreFilters = false; 
    } 
    else { 
      delay(100); 
      return; 
    } 
  }
  // === 4. РЕЖИМ ЗАПИСИ КЛЮЧЕЙ ===
  if (writeflag && readflag) {
    if (keyType == keyEM_Marie) {
      write2rfid(); 
    } else {
      write2iBtn();
    }
  }
  delay(200);
}


//***************** звуки****************
void Sd_ReadOK() {  // звук ОК
  for (int i=400; i<6000; i=i*1.5) { tone(speakerPin, i); delay(20); }
  noTone(speakerPin);
}

void Sd_WriteStep(){  // звук "очередной шаг"
  for (int i=2500; i<6000; i=i*1.5) { tone(speakerPin, i); delay(10); }
  noTone(speakerPin);
}

void Sd_ErrorBeep() {  // звук "ERROR"
  for (int j=0; j <3; j++){
    for (int i=1000; i<2000; i=i*1.1) { tone(speakerPin, i); delay(10); }
    delay(50);
    for (int i=1000; i>500; i=i*1.9) { tone(speakerPin, i); delay(10); }
    delay(50);
  }
  noTone(speakerPin);
}

void Sd_StartOK(){   // звук "Успешное включение"
  tone(speakerPin, NOTE_A7); delay(100);
  tone(speakerPin, NOTE_G7); delay(100);
  tone(speakerPin, NOTE_E7); delay(100); 
  tone(speakerPin, NOTE_C7); delay(100);  
  tone(speakerPin, NOTE_D7); delay(100); 
  tone(speakerPin, NOTE_B7); delay(100); 
  tone(speakerPin, NOTE_F7); delay(100); 
  tone(speakerPin, NOTE_C7); delay(100);
  noTone(speakerPin); 
}
