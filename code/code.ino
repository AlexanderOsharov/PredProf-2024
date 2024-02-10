#include <Wire.h> // Стандартная бибилиотека для взаимодействия с устройствами I2C
#include <String.h> // Стандартная бибилиотека
#include <iarduino_OLED_txt.h> // Библиотека для работы с дисплеем

iarduino_OLED_txt myOLED(0x3C); // Дисплей на 0x3C
extern uint8_t SmallFontRus[]; // Используем Кириллицу

#define BT 2 // Порт кнопки
#define RL 4 // Порт красного светодиода
#define GL 3 // Порт зелёного светодиода

class Potentiometer // Класс, описывающий работу с потенциометром
{
  public:
    void setResistance(int r){ // Установить сопротивление
      Wire.beginTransmission(0x2f); // Открываем связь с 0x2f
      Wire.write(byte(0x00)); // Посылаем нулевой байт
      Wire.write(r); // Посылаем значния сопротивления (255 - 100 кОМ)
      Wire.endTransmission(); // Закрываем связь с 0x2f
    }
};
Potentiometer p; // Создаём объект класса Потенциометр

class Multimeter{
  private:
    uint8_t i2c_adress = 0x40; // Адресс можно переназначить
    float rShunt = 0.0;
    float iMax = 0.0;
    float currentLSB = 0.0;
    float powerLSB = 0.0;
    uint16_t calibreValue = 0;
    
  public:
    Multimeter(uint8_t i2c_adress_in, const float r_shunt = 10.0f, const float i_max = 1.0f)
    : rShunt(r_shunt), iMax(i_max){
      i2c_adress = i2c_adress_in;
    }

    // Проверка работоспособности мультиметра
    bool begin(){
        Wire.begin();
        if (!testConnection()) return false; // Если проверка соединения не удалась, то устройство не отвечает
        calibrate();
        return true;
    }
    // Проверка соединения на указанном порту
    bool testConnection(){
        Wire.beginTransmission(i2c_adress);
        return (bool)!Wire.endTransmission();
    }
    // Установить значение калиброки
    void setCalibration(uint16_t cal){
        writeRegister(0x05, cal);
        calibreValue = cal;
    }
    // Калибровка мультиметра
    void calibrate(){
        writeRegister(0x00, 0x8000);

        currentLSB = iMax / 32768.0f;
        powerLSB = currentLSB * 20.0f;
        calibreValue = trunc(0.04096f / (currentLSB * rShunt));

        setCalibration(calibreValue);

        uint16_t cfg_register = readRegister(0x00) & ~(0b11 << 11);
        uint16_t vshunt_max_mv = (rShunt * iMax) * 1000;

        if (vshunt_max_mv <= 40) cfg_register |= 0b00 << 11;
        else if (vshunt_max_mv <= 80) cfg_register |= 0b01 << 11;
        else if (vshunt_max_mv <= 160) cfg_register |= 0b10 << 11;
        else cfg_register |= 0b11 << 11;
        writeRegister(0x00, cfg_register);
    }

    // Записать данные калибровки
    void writeRegister(uint8_t address, uint16_t data){
        Wire.beginTransmission(i2c_adress);
        Wire.write(address);
        Wire.write(highByte(data));
        Wire.write(lowByte(data));
        Wire.endTransmission();
    }
    // Прочесть данные с адреса
    uint16_t readRegister(uint8_t address){
        Wire.beginTransmission(i2c_adress);
        Wire.write(address);
        Wire.endTransmission();
        Wire.requestFrom(i2c_adress, (uint8_t)2);
        return Wire.read() << 8 | Wire.read();
    }

    // Получаем выходное напряжение
    float getVoltage(){
        uint16_t value = readRegister(0x02); // Читаем данные
        return (value >> 3) * 0.004f; // Обрабатываем и возвращаем значение
    }
};
Multimeter ina(0x40); // Создаём объект класса Мультиметр

void setup() {
  Serial.begin(9600);
  
  pinMode(BT, INPUT);
  pinMode(RL, OUTPUT);
  pinMode(GL, OUTPUT);
  
  myOLED.begin();
  myOLED.setFont(SmallFontRus);
  myOLED.setCoding(TXT_UTF8);

  // Устройство готово к работе
  if(ina.begin()){
    myOLED.print("iИлья", OLED_C, 3);
    myOLED.print("готов к работе", OLED_C, 4);
  }
  else{
    myOLED.print("iИлья", OLED_C, 3);
    myOLED.print("Проблема с мультиметром", OLED_C, 4);

    delay(3000);
    
    while(true){
      Serial.println("Проблема с мультиметром");
      myOLED.clrScr();
      myOLED.print("Проблема с мультиметром");
      delay(3000);
    }
  }

  delay(2000); // Ждём 2 секунды

  myOLED.clrScr(); // Отчищаем дисплей
}

void loop() {
  bool f = false; // Нажата ли кнока
  if(digitalRead(BT)){ // Если кнопку нажали
    delay(200); // Ждём 200 мс
    f = true; // Кнопка нажата
  }
  if(f){ // Если кнопку точно нажали
    myOLED.invScr(1); // Идёт проверка - дисплей светлый
    digitalWrite(RL, LOW); // Идёт проверка - выключаем светодиоды
    digitalWrite(GL, LOW);

    bool test_live = true; // Пройдена ли проверка (значение измениться при обработке (*)
    bool proverka = false; // Вторая перменная для "Пройдена ли проверка" (значение может измениться при обработке (*)
    
    float voltages[11] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    // Изменяем сопротивление потенциометри в течение 10 секунд каждую секунду
    Serial.println("iИлья начинает тест"); // Сообщение в монитор порта о начале теста
    for(int i = 0; i <= 250; i += 25){
      p.setResistance(i); // Тут изменение значения
      delay(900); // Ждём обновления

      float vsp_v = ina.getVoltage(); // Читаем выходное напряжение на тестируемом элементе

      // Выводим информацию на дисплей
      myOLED.clrScr();
      myOLED.print("iИлья начинает тест", OLED_C, 3);
      myOLED.print("Вых. напр.: " + String(vsp_v) + "В", OLED_C, 4);

      // Сообщение в монитор порта о выходном напряжении
      Serial.println("Вых. напр.: " + String(vsp_v) + "В"); 

      // Сохраняем выходное напряжение в перменную для дальнейшей обработки
      voltages[i / 25] = vsp_v;
      
      delay(100);
    }

    // Сравниваем значения - (*) обработка значений
    for(int i = 1; i < 11; i++){
      if(proverka){ // Если 2 соседних значения НЕ равны с учётом погрешности +- 0.1В
        if(float(voltages[i - 1]) < float(voltages[i]) - 0.1f or float(voltages[i - 1]) > float(voltages[i]) + 0.1f){
          test_live = false; // То тест провален
          break;
        }
      }

      // Если предыдущее значение больше
      if(voltages[i - 1] > voltages[i]){
        test_live = false; // То тест провален
        break;
      } // Если 2 соседних значения равны с учётом погрешности +- 0.1В
      else if(float(voltages[i - 1]) > float(voltages[i]) - 0.1f and float(voltages[i - 1]) < float(voltages[i]) + 0.1f){
        proverka = true; // То начинаем смотреть, чтобы все следующие также были равны между собой
      }
    }

    if(test_live){
      if(voltages[10] > voltages[9]){
        test_live = false;
      }
    }

    if(test_live){ // Тест пройден
      digitalWrite(RL, LOW);
      digitalWrite(GL, HIGH);
      myOLED.clrScr();
      myOLED.print("iИлья завершил тест", OLED_C, 3);
      myOLED.print("ТЕСТ ПРОЙДЕН", OLED_C, 4);

      Serial.println("iИлья завершил тест"); 
      Serial.println("ТЕСТ ПРОЙДЕН");
      Serial.println(" ");
      Serial.println(" ");
      Serial.println(" ");
    }
    else{ // Тест провален
      digitalWrite(RL, HIGH);
      digitalWrite(GL, LOW);
      myOLED.clrScr();
      myOLED.print("iИлья завершил тест", OLED_C, 3);
      myOLED.print("ТЕСТ ПРОВАЛЕН", OLED_C, 4);

      Serial.println("iИлья завершил тест"); 
      Serial.println("ТЕСТ ПРОВАЛЕН");
      Serial.println(" ");
      Serial.println(" ");
      Serial.println(" ");
    }

    for(int i = 0; i < 5; i++){ // Дисплей мигает после завершения проверки и возвращает дисплей в тёмный режим
      myOLED.invScr(i % 2);
      delay(500);
    }
  }
}
