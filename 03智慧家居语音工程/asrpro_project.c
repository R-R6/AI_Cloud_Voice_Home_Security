/*该工程是天问ASRPRO语音识别模块的工程（是字符编程模式的代码）*/
#define ASR_ID_LED_ON  1
#define ASR_ID_LED_OFF  2
#define ASR_ID_TEMP_GET  3
#define ASR_ID_HUMI_GET  4
#define ASR_ID_TIME_GET  5
#define ASR_ID_DATE_GET  6
#define ASR_ID_FIRE_GET 7
#define ASR_ID_GAS_GET 8

#include "asr.h"
extern "C"{ void * __dso_handle = 0 ;}
#include "setup.h"
#include "HardwareSerial.h"
#include "myLib/asr_event.h"

uint32_t snid;
String serial_rx = "";
uint32_t value_u32 = 0;
uint32_t asr_id = 0;
uint8_t hour = 0;
uint8_t minute = 0;
uint8_t second = 0;
uint16_t year = 0;
uint8_t month = 0;
uint8_t date = 0;
uint8_t weekday = 0;
void task_serial();
void ASR_CODE();

//{speak:橙子-甜美客服,vol:2,speed:10,platform:haohaodada}
//{playid:10001,voice:欢迎使用语音助手，用智慧管家唤醒我。}
//{playid:10002,voice:我退下了，记得用智慧管家唤醒我哟}

//宏定义
void sys_sleep_hook()
{
  Serial.print("asr exit#");
  Serial2.print("asr exit#");
  //PA3引脚输出高电平，告知当前语音识别模块为退出
  digitalWrite(3,1);

}

void task_serial(){
  while (1) {
    if(Serial2.available() > 0){
      serial_rx = Serial2.readStringUntil('#');
      value_u32 = String(serial_rx).toInt();
      Serial.print("val_int：");
      Serial.println(value_u32,HEX);
      Serial.print("asr_id：");
      Serial.println(asr_id,HEX);
      Serial.flush();
      Serial2.flush();
      /*
       * MCU 主动报警（无语音识别前置）：
       * 仅发 3#/4#/5#/6#，此处强制进入火警/烟雾分支并播报，不依赖 asr_id 残留。
       * 3=火警安全  4=火警危险  5=烟雾安全  6=烟雾危险
       * 与 FIRE_GET/GAS_GET 中 value_u32 含义一致。
       */
      if((value_u32 & 0x80000000) == 0){
        if(value_u32 == 3 || value_u32 == 4){
          asr_id = ASR_ID_FIRE_GET;
        }else if(value_u32 == 5 || value_u32 == 6){
          asr_id = ASR_ID_GAS_GET;
        }
      }
      if((value_u32&0x80000000)){
        if((value_u32&0x40000000)){
          asr_id = ASR_ID_LED_ON;
        }
        if((value_u32&0x20000000)){
          asr_id = ASR_ID_LED_OFF;
        }
        if((value_u32&0x10000000)){
          asr_id = ASR_ID_TEMP_GET;
        }
        if((value_u32&0x08000000)){
          asr_id = ASR_ID_HUMI_GET;
        }
        if((value_u32&0x04000000)){
          asr_id = ASR_ID_TIME_GET;
        }
        if((value_u32&0x02000000)){
          asr_id = ASR_ID_DATE_GET;
        }
        if((value_u32&0x01000000)){
          asr_id = ASR_ID_FIRE_GET;
        }
        if((value_u32&0x00800000)){
          asr_id = ASR_ID_GAS_GET;
        }
        value_u32 = (value_u32&0x00FFFFFF);
      }
      digitalWrite(3,0);
      delay(200);
      enter_wakeup(15000);
      delay(200);
      delay(500);
      switch (asr_id) {
       case ASR_ID_LED_ON:
        if(value_u32){
          //{playid:10500,voice:灯打开成功}
          play_audio(10500);
        }
        break;
       case ASR_ID_LED_OFF:
        if(value_u32){
          //{playid:10501,voice:灯关闭成功}
          play_audio(10501);
        }
        break;
       case ASR_ID_TEMP_GET:
        //{playid:10502,voice:当前温度}
        play_audio(10502);
        play_num((int64_t)(value_u32 * 100), 1);
        //{playid:10503,voice:摄氏度}
        play_audio(10503);
        break;
       case ASR_ID_HUMI_GET:
        //{playid:10504,voice:当前湿度}
        play_audio(10504);
        //{playid:10505,voice:百分之}
        play_audio(10505);
        play_num((int64_t)(value_u32 * 100), 1);
        break;
       case ASR_ID_TIME_GET:
        //{playid:10506,voice:当前时间}
        play_audio(10506);
        //自定义代码
        hour = (value_u32 >> 16) & 0xFF;
        minute = (value_u32 >> 8) & 0xFF;
        second = value_u32 & 0xFF;
        if((hour >= 0) && (hour < 6)){
          //{playid:10507,voice:凌晨}
          play_audio(10507);
        }
        if((hour >= 6) && (hour < 12)){
          //{playid:10508,voice:上午}
          play_audio(10508);
        }
        if((hour >= 12) && (hour < 13)){
          //{playid:10509,voice:中午}
          play_audio(10509);
        }
        if((hour >= 13) && (hour < 18)){
          //{playid:10510,voice:下午}
          play_audio(10510);
        }
        if((hour >= 18) && (hour < 24)){
          //{playid:10511,voice:晚上}
          play_audio(10511);
        }
        play_num((int64_t)(hour * 100), 1);
        //{playid:10512,voice:点}
        play_audio(10512);
        play_num((int64_t)(minute * 100), 1);
        //{playid:10513,voice:分}
        play_audio(10513);
        play_num((int64_t)(second * 100), 1);
        //{playid:10514,voice:秒}
        play_audio(10514);
        break;
       case ASR_ID_DATE_GET:
        //{playid:10515,voice:当前日期}
        play_audio(10515);
        //自定义代码
        year = 2000+((value_u32 >> 13) & 0x7F);
        month= (value_u32 >> 9) & 0x0F;
        date = (value_u32 >> 4) & 0x1F;
        weekday = value_u32 & 0x0F;
        play_num((int64_t)(year * 100), 0);
        //{playid:10516,voice:年}
        play_audio(10516);
        play_num((int64_t)(month * 100), 1);
        //{playid:10517,voice:月}
        play_audio(10517);
        play_num((int64_t)(date * 100), 1);
        //{playid:10518,voice:号}
        play_audio(10518);
        //{playid:10519,voice:星期}
        play_audio(10519);
        if(weekday == 7){
          //{playid:10520,voice:天}
          play_audio(10520);
        }
        else{
          play_num((int64_t)(weekday * 100), 1);
        }
        break;
       case ASR_ID_FIRE_GET:
        //{playid:10521,voice:当前火警状态}
        play_audio(10521);
        if(value_u32 == 3){
          //{playid:10522,voice:安全}
          play_audio(10522);
        }
        if(value_u32 == 4){
          //{playid:10523,voice:不安全}
          play_audio(10523);
        }
        break;
       case ASR_ID_GAS_GET:
        //{playid:10524,voice:当前烟雾报警状态}
        play_audio(10524);
        if(value_u32 == 5){
          //{playid:10525,voice:安全}
          play_audio(10525);
        }
        if(value_u32 == 6){
          //{playid:10526,voice:不安全}
          play_audio(10526);
        }
        break;
      }
    }
    delay(500);
  }
  vTaskDelete(NULL);
}

/*描述该功能...
*/
void ASR_CODE(){
  //PA3引脚输出低电平，告知当前语音识别模块在工作
  digitalWrite(3,0);
  //本函数是语音识别成功钩子程序
  //运行时间越短越好，复杂控制启动新线程运行
  //唤醒时间设置必须在ASR_CODE中才有效
  set_state_enter_wakeup(15000);
  //用switch分支选择，根据不同的识别成功的ID执行相应动作，点击switch左上齿轮
  //可以增加分支项
  asr_id = snid;
  switch (snid) {
   case 1:
    Serial.write("LED ON#");
    Serial2.write("LED ON#");
    break;
   case 2:
    Serial.write("LED OFF#");
    Serial2.write("LED OFF#");
    break;
   case 3:
    Serial.write("TEMP#");
    Serial2.write("TEMP#");
    break;
   case 4:
    Serial.write("HUMI#");
    Serial2.write("HUMI#");
    break;
   case 5:
    Serial.write("TIME#");
    Serial2.write("TIME#");
    break;
   case 6:
    Serial.write("DATE#");
    Serial2.write("DATE#");
    break;
   case 7:
    Serial.write("FIRE#");
    Serial2.write("FIRE#");
    break;
   case 8:
    Serial.write("GAS#");
    Serial2.write("GAS#");
    break;
   case 9999:
    delay(500);
    exit_wakeup_deal(0);
    break;
  }

}

void hardware_init(){
  //需要操作系统启动后初始化的内容
  //音量范围1-7
  vol_set(5);
  xTaskCreate(task_serial,"task_serial",512,NULL,4,NULL);
  vTaskDelete(NULL);
}

void setup()
{
  //需要操作系统启动前初始化的内容
  //播报音下拉菜单可以选择，合成音量是指TTS生成文件的音量
  //欢迎词指开机提示音，可以为空
  //退出语音是指休眠时提示音，可以为空
  //休眠后用唤醒词唤醒后才能执行命令，唤醒词最多5个。回复语可以空。ID范围为0-9999
  //{ID:0,keyword:"唤醒词",ASR:"智慧管家",ASRTO:"主人，我在"}
  //{ID:1,keyword:"命令词",ASR:"打开灯光",ASRTO:"好的"}
  //{ID:2,keyword:"命令词",ASR:"关闭灯光",ASRTO:"好的"}
  //{ID:3,keyword:"命令词",ASR:"当前温度",ASRTO:"好的"}
  //{ID:4,keyword:"命令词",ASR:"当前湿度",ASRTO:"好的"}
  //{ID:5,keyword:"命令词",ASR:"当前时间",ASRTO:"好的"}
  //{ID:6,keyword:"命令词",ASR:"当前日期",ASRTO:"好的"}
  //{ID:7,keyword:"命令词",ASR:"当前火警状态",ASRTO:"好的"}
  //{ID:8,keyword:"命令词",ASR:"当前烟雾报警状态",ASRTO:"好的"}
  //{ID:9999,keyword:"命令词",ASR:"退出",ASRTO:"好的"}
  //Serial用作调试使用；Serial2连接到外部设备的串口，如连接到STM32
  setPinFun(13,SECOND_FUNCTION);
  setPinFun(14,SECOND_FUNCTION);
  Serial.begin(9600);
  setPinFun(5,FORTH_FUNCTION);
  setPinFun(6,FORTH_FUNCTION);
  Serial2.begin(9600);
  //PA3引脚输出高低电平，用于表示当前语音模块的工作状态
  setPinFun(3,FIRST_FUNCTION);
  pinMode(3,output);
  //声明变量

  //{playid:10084,voice:零}
  //{playid:10085,voice:一}
  //{playid:10086,voice:二}
  //{playid:10087,voice:三}
  //{playid:10088,voice:四}
  //{playid:10089,voice:五}
  //{playid:10090,voice:六}
  //{playid:10091,voice:七}
  //{playid:10092,voice:八}
  //{playid:10093,voice:九}
  //{playid:10094,voice:十}
  //{playid:10095,voice:百}
  //{playid:10096,voice:千}
  //{playid:10097,voice:万}
  //{playid:10098,voice:亿}
  //{playid:10099,voice:负}
  //{playid:10100,voice:点}
}
