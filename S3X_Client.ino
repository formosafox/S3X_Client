// ************************************************************************
// 修改日期：2017-05-26
// ************************************************************************
// S3X_Client：與 Proxy 連線
// ************************************************************************
// 零件清單：ESP8266、LED、R220電阻
// ************************************************************************
// http://arduino.tw/allarticlesindex/2009-09-06-18-37-08/169-arduinohd.html
#include <EEPROM.h> 
// https://github.com/JChristensen/Timer
// http://playground.arduino.cc/Code/Timer 
// http://yehnan.blogspot.tw/2012/03/arduino.html
#include "Timer.h"      
//-------------------------------------------------------
// http://yhhuang1966.blogspot.tw/2016/09/arduino_11.html
#include <Bounce2.h>  
//-------------------------------------------------------
#include <Wire.h> 
// https://github.com/agnunez/ESP8266-I2C-LCD1602
// http://www.instructables.com/id/I2C-LCD-on-NodeMCU-V2-With-Arduino-IDE/
// https://github.com/fdebrabander/Arduino-LiquidCrystal-I2C-library
#include <LiquidCrystal_I2C.h> 
LiquidCrystal_I2C LCD_16x2(0x3F,16,2);  // 16x2 LCD,I2C 位址：0x3F,0X27
//-------------------------------------------------------
#include "Blink.h"
#include "SysMode.h"
//-------------------------------------------------------
#include <ESP8266WiFi.h>
#include <Ticker.h>
//-------------------------------------------------------
// http://esp8266.github.io/Arduino/versions/2.0.0/doc/ota_updates/ota_updates.html#classic-ota-configuration
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
//-------------------------------------------------------
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
//-------------------------------------------------------
// https://bblanchon.github.io/ArduinoJson/
// https://github.com/bblanchon/ArduinoJson
#include <ArduinoJson.h>
// ************************************************************************
struct StoreStruct {
  byte Head;                                              // 資料開頭：0~255 亂數
  byte Disable_AP;                                        // 停用 Soft-AP
  char WiFi_SSID[16+1];                                   // WiFi SSID 名稱
  char WiFi_PASS[16+1];                                   // WiFi 連線密碼
  byte Tail;                                              // 資料結尾：資料開頭的反向值
} myConfig;
//-------------------------------------------------------
struct S3X_Info_Struct {
  String SlaveName;                                       // 名稱 S31、S32、S33 
  byte NowTemperature;                                    // 目前水溫
  byte SetTemperature;                                    // 水溫設定
  byte SystemStatusByte;                                  // 系統狀態(Byte)
  unsigned long PacketErrorCount;                         // 封包錯誤計數
} myS3X_Info = {"", 0x00, 0x00, 0x00, 0LU};
//-------------------------------------------------------
const byte Message_Boot             = 0;                  // 訊息：開機
const byte Message_WiFi             = 1;                  // 訊息：WiFi
const byte Message_Info             = 2;                  // 訊息：Info
const byte Message_S3X_Proxy        = 3;                  // 訊息：S3X Proxy
//-------------------------------------------------------
const byte Pin_WakeUp_Sensor        = 13;                 // GPIO 腳位：喚醒感應 使用 HC_SR505(迷你型人體感應模組)或 RCWL-0516(微波雷達感應開關模塊)
//-------------------------------------------------------
const byte Pin_74HC595_Data         = 16;                 // GPIO 腳位：74HC595 Data  Pin
const byte Pin_74HC595_Latch        = 15;                 // GPIO 腳位：74HC595 Latch Pin
const byte Pin_74HC595_Clock        = 14;                 // GPIO 腳位：74HC595 Clock Pin
//-------------------------------------------------------
const byte Pin_WiFi_LED             = 12;                 // GPIO 腳位：WiFi 燈
const byte Pin_Buzzer               = 0;                  // GPIO 腳位：蜂鳴器
const byte Pin_Button               = 2;                  // GPIO 腳位：按鍵
//-------------------------------------------------------
Bounce Button_ForcedHeating = Bounce(Pin_Button,5);       // 按鍵：強制加熱
//-------------------------------------------------------
Timer Timer_S3X_Proxy_Link;                               // 計時器：S3X Proxy 連線
const unsigned long S3X_Proxy_Link_Interval = 5 * 1000;   // 間隔： 5 秒
const byte S3X_Proxy_JSON_String_MaxLen = 64;             // S3X Proxy 傳送過來的 JSON 最大長度
//-------------------------------------------------------
boolean Send_ForcedHeating = false;                       // 發送 強制加熱
byte Send_SetTemperature = 0x00;                          // 發送 水溫設定 [0]不動作 [30~75]更改水溫設定
//-------------------------------------------------------
SysMode mySysMode(10 * 1000);                             // 系統模式：10秒未動作則進入閒置模式【關閉背光】
//-------------------------------------------------------
Timer Timer_SysCheck;                                     // 計時器：系統檢查...
const unsigned long SysCheck_Interval = 500;              // 間隔： 0.5 秒
//-------------------------------------------------------
Blink Link_Blink;                                         // 閃爍：連線 閃爍
//-------------------------------------------------------
Ticker WiFi_LED_Ticker;                                   // Ticker：WiFi 燈
//-------------------------------------------------------
int status = WL_IDLE_STATUS;
WiFiClient WiFi_Client;
//-------------------------------------------------------
char AP_SSID[32+1];                                       // Soft AP 的 SSID
byte WiFi_MAC[6];                                         // WiFi 的 MAC 資料
//-------------------------------------------------------
ESP8266WebServer WebServer(80);                           // Web Server
//-------------------------------------------------------
char mDNS_Name[8+1] = "S3X-XXXX";                         // mDNS 名稱
//-------------------------------------------------------
String S3X_Info_HttpLink="";
String S3X_ForcedHeating_HttpLink="";
String S3X_SetTemperature_HttpLink="";
// ************************************************************************
void setup() {
  Serial.begin(115200);
  //-------------------------------------------------------
                                                          // mDNS 名稱：S3X-Client-XXXX
  sprintf(mDNS_Name,"S3X-%04lu",ESP.getChipId() % 10000LU);
  //-------------------------------------------------------
  IO_Init();                                              // IO 設置
  LCD_Init();                                             // LCD 設置
  //-------------------------------------------------------
  LED_ON_OFF(Pin_WiFi_LED,500,5);
  LED_ON_OFF(Pin_WiFi_LED,100,25);
  //-------------------------------------------------------
  LoadConfig();                                           // 載入設定
  //-------------------------------------------------------
  ShowMessage(Message_Boot);
  //-------------------------------------------------------
  WiFi_init();                                            // WiFi 設置
  //-------------------------------------------------------
  ShowMessage(Message_S3X_Proxy);
  S3X_HttpLink_Init();
  //-------------------------------------------------------
  OTA_Init();                                             // OTA 設置
  //-------------------------------------------------------
  WebServer_Init();                                       // Web Server 設置
  //-------------------------------------------------------
  ShowMessage(Message_Info);
  //-------------------------------------------------------
  mySysMode.Living();                                     // 先執行一次更新時間紀錄
  //-------------------------------------------------------
  Timer_SysCheck.every(SysCheck_Interval,SystemCheck);    // 計時器：系統檢查
                                                          // 計時器：S3X Proxy 連線
  Timer_S3X_Proxy_Link.every(S3X_Proxy_Link_Interval,S3X_Proxy_Link); 
  //-------------------------------------------------------
  S3X_Proxy_Link();                                       // 不等計時器，先執行一次
  //-------------------------------------------------------
}
// ************************************************************************
void loop() {
  //-------------------------------------------------------
  ArduinoOTA.handle();
  //-------------------------------------------------------
  WebServer_Run();                                        // Web Server 執行
  //-------------------------------------------------------
  Timer_S3X_Proxy_Link.update();                          // 計時器：JSON Link 資料接收
  //-------------------------------------------------------
  Timer_SysCheck.update();                                // 計時器：系統檢查
  //-------------------------------------------------------
  Button_Check();                                         // 按鍵檢查 
  //-------------------------------------------------------
}
// ************************************************************************
// Web Server 運行
void WebServer_Run() {
  unsigned long KeepTime = micros();
  WebServer.handleClient();
                                                          // 沒有 Client 要求上傳資料時，其時間差大都在 100 之內
  if((micros()-KeepTime) > 200) mySysMode.Living();       // 活動中
  //Serial.println(micros()-KeepTime);
}
// ************************************************************************
void S3X_HttpLink_Init() {
  //-------------------------------------------------------
  if (WiFi.status() != WL_CONNECTED) return;              // 網路未連線？
  //-------------------------------------------------------
  String IP_Str = mDNS_IP_String("S3X-Proxy");
  if (IP_Str != "") {
    S3X_Info_HttpLink = "http://" + IP_Str + "/Info";
    S3X_ForcedHeating_HttpLink = "http://" + IP_Str + "/ForcedHeating";
    S3X_SetTemperature_HttpLink = "http://" + IP_Str + "/SetTem?SetTemperature=";
    LCD_Print(14,0,"OK");
    Beep(500,1);
    LCD_Print(1,1,IP_Str);
    delay(5000);
  } else {
    S3X_Info_HttpLink = "";
    S3X_ForcedHeating_HttpLink = "";
    S3X_SetTemperature_HttpLink = "";
  }
  //-------------------------------------------------------
}
// ************************************************************************
// 按鍵檢查
void Button_Check() {
  //-------------------------------------------------------
  if (WiFi.status() != WL_CONNECTED) return;              // 網路未連線？
  //-------------------------------------------------------
  if (S3X_ForcedHeating_HttpLink == "") return;           // 沒有設定 S3X_ForcedHeating_HttpLink
  //-------------------------------------------------------
  if (Button_ForcedHeating.update() == false)  return;    // 按鍵狀態沒有變化
  //-------------------------------------------------------
  mySysMode.Living();                                     // 活動中
  if (Button_ForcedHeating.fell()) {                      // 按鍵是否按下後放開
    if (mySysMode.Mode()!=SysMode_Idle)                   // 不處於閒置模式
      Send_ForcedHeating = true;
  }
  //-------------------------------------------------------
}
// ************************************************************************
// 命令：強制加熱
void Command_ForcedHeating() {
  //-------------------------------------------------------
  if (WiFi.status() != WL_CONNECTED) return;              // 網路未連線？
  //-------------------------------------------------------
  if (S3X_ForcedHeating_HttpLink == "") return;           // 沒有設定 S3X_ForcedHeating_HttpLink
  //-------------------------------------------------------
  digitalWrite(Pin_Buzzer, HIGH);                         // 蜂鳴器：開
  //-------------------------------------------------------
  HTTPClient http;       
  http.begin(S3X_ForcedHeating_HttpLink);
  http.GET();
  //-------------------------------------------------------
  delay(100);
  digitalWrite(Pin_Buzzer, LOW);                          // 蜂鳴器：關
}
// ************************************************************************
// 命令：更改水溫設定
void Command_SetTemperature() {
  //-------------------------------------------------------
  if (WiFi.status() != WL_CONNECTED) return;              // 網路未連線？
  //-------------------------------------------------------
  if (S3X_SetTemperature_HttpLink == "") return;          // 沒有設定 S3X_SetTemperature_HttpLink
  //-------------------------------------------------------
  digitalWrite(Pin_Buzzer, HIGH);                         // 蜂鳴器：開
  //-------------------------------------------------------
  HTTPClient http;       
  http.begin((String)S3X_SetTemperature_HttpLink+Send_SetTemperature);
  http.GET();
  //-------------------------------------------------------
  delay(100);
  digitalWrite(Pin_Buzzer, LOW);                          // 蜂鳴器：關
}
// ************************************************************************
// 顯示訊息
void ShowMessage(byte index) {
  Beep(500,1);
  LCD_16x2.clear();
  switch (index) {
    case Message_Boot   :
      LCD_Print(0,0,(String)">>> " + mDNS_Name + " <<<");
      for (byte CX=0; CX<8; CX++)
        LCD_Char(CX*2,1,CX);
      break;
    case Message_WiFi :
      LCD_Print(0,0,(String)"SSID:" + myConfig.WiFi_SSID);
      LCD_Print(0,1,">");
      break;
    case Message_Info :
      LCD_Print(0,0,"TEM:--[--------]");
      LCD_Print(0,1,"SET:--[    ]:0");
      LCD_Print(7,1,(char *)(mDNS_Name+4));
      break;
    case Message_S3X_Proxy :
      LCD_Print(0,0,"S3X Proxy ... ");
      LCD_Print(0,1,">");
      break;
  }
  delay(3000);
}
// ************************************************************************
// 系統檢查
void SystemCheck() {
  //-------------------------------------------------------
  if (S3X_Info_HttpLink!="" && WiFi.status()!=WL_CONNECTED) {
    S3X_Info_HttpLink="";
    S3X_ForcedHeating_HttpLink="";
    S3X_SetTemperature_HttpLink="";
  }
  //-------------------------------------------------------
  if (S3X_Info_HttpLink=="" && WiFi.status()==WL_CONNECTED)
    S3X_HttpLink_Init();
  //-------------------------------------------------------
                                                          // 有接收到感應時 SysMode 活動
  if (digitalRead(Pin_WakeUp_Sensor)==HIGH) mySysMode.Living();
  //-------------------------------------------------------
  switch (mySysMode.Check_WakeUp_Sleep()) {               // 檢查 SysMode 模式
    case  SysMode_doWakeUp  :                             // 喚醒...
        LCD_16x2.backlight();                             // 16x2 LCD：開啟背光
        mySysMode.To_Normal_Idle();
        break;
    case  SysMode_doSleep   :                             // 睡眠...
        LCD_16x2.noBacklight();                           // 16x2 LCD：關閉背光
        mySysMode.To_Normal_Idle();
        break;
  }
  //-------------------------------------------------------
  if (Send_ForcedHeating==true) {                         // 設定要強制加熱【開/關】？
    Command_ForcedHeating();                              // 執行命令：強制加熱
    Send_ForcedHeating = false;                           // 務必false，免得一直傳送命令
  }
  //-------------------------------------------------------
  if (Send_SetTemperature!=0x00) {                        // 設定要更改水溫設定？
    Command_SetTemperature();                             // 執行命令：更改水溫設定
    Send_SetTemperature = 0x00;                           // 務必歸零，免得一直傳送命令
  }
  //-------------------------------------------------------
  if (Link_Blink.isActive()) {                            // 閃爍動作中？
    LCD_Char(12,1,Link_Blink.GetLowHigh()?' ':3);
    digitalWrite(Pin_WiFi_LED,!Link_Blink.GetLowHigh());
    Link_Blink.Update();
  }
}
// ************************************************************************
// OTA 設置
void OTA_Init() {
  ArduinoOTA.setHostname(mDNS_Name);
  ArduinoOTA.onStart([]() {
    WiFi_LED_Ticker.attach(0.5, WiFi_LED_Blink);          // WiFi LED 慢速閃爍
  });
  ArduinoOTA.onEnd([]() {
    WiFi_LED_Ticker.detach();                             // WiFi LED 停止閃爍
    digitalWrite(Pin_WiFi_LED, HIGH);                     // 設定 WiFi 燈：ON
    ESP.restart();
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
  });
  ArduinoOTA.onError([](ota_error_t error) {
    /*
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
    */
  });
  ArduinoOTA.begin();
}
// ************************************************************************
// S3X Proxy 連線
void S3X_Proxy_Link() {
  //-------------------------------------------------------
  //if (WiFi.status() != WL_CONNECTED) return;              // 網路未連線？
  //-------------------------------------------------------
  //if (S3X_Info_HttpLink == "") return;                    // 沒有設定 S3X_Info_HttpLink
  //-------------------------------------------------------
  if (mySysMode.Mode()==SysMode_Idle) {                     // 閒置模式時不連線更新
    Link_Blink.Active();
    return;
  }
  //-------------------------------------------------------
  myS3X_Info = {"", 0x00, 0x00, 0x00, 0LU};
  //-------------------------------------------------------
  HTTPClient http;
  http.begin(S3X_Info_HttpLink);
  if(http.GET() == HTTP_CODE_OK) Parse_S3X_Info(http.getString());
  //-------------------------------------------------------
  DisplayInfo();
  //-------------------------------------------------------
  Link_Blink.Active();
  //-------------------------------------------------------
}
// ************************************************************************
void Parse_S3X_Info (String JSON_Str) {
  //-------------------------------------------------------
  StaticJsonBuffer<S3X_Proxy_JSON_String_MaxLen*2> jsonBuffer;
  JsonObject& JSON_Root = jsonBuffer.parseObject(JSON_Str);
  //-------------------------------------------------------
  if (!JSON_Root.success()) return;                       // 無法解析
  //-------------------------------------------------------
  const char* SlaveName = JSON_Root["S3X"];
  myS3X_Info.SlaveName = String(SlaveName);
  myS3X_Info.NowTemperature = (byte)JSON_Root["TEM"];
  myS3X_Info.SetTemperature = (byte)JSON_Root["SET"];
  myS3X_Info.SystemStatusByte = (byte)JSON_Root["SSB"];
  myS3X_Info.PacketErrorCount = (unsigned long)JSON_Root["PEC"];
  //-------------------------------------------------------
}
// ************************************************************************
// LED 閃爍
void LED_ON_OFF(byte Pin_LED, unsigned int DelayTime, byte Count) {
  for (unsigned int CX=0; CX<Count; CX++) {
    digitalWrite(Pin_LED, HIGH);                          // 設定LED：ON
    delay(DelayTime/2);
    digitalWrite(Pin_LED, LOW);                           // 設定LED：OFF
    delay(DelayTime/2);
  }
}
// ************************************************************************
// Web Server 設置
void WebServer_Init() {
  WebServer.on("/", Web_Root);                            // 系統資訊
  WebServer.on("/Setup", Web_Setup);                      // 系統設置
  WebServer.on("/SetTem", Web_SetTemperature);            // 水溫設定
  //-------------------------------------------------------
  WebServer.on("/Reboot", [](){                           // 重新開機
    WebServer.send(200, "text/html", "<meta charset='UTF-8' http-equiv='refresh' content='10;url=/'>重新開機<br>稍待 10 秒 返回主頁面...");
    ESP.restart();
  });
  //-------------------------------------------------------
  WebServer.on("/ForcedHeating", [](){                    // 強制加熱
    Send_ForcedHeating = true;
                                                          // 5秒後轉回主頁面
    WebServer.send(200, "text/html", "<meta charset='UTF-8' http-equiv='refresh' content='5;url=/'>強制加熱【開/關】<br>稍待 5秒 返回主頁面...");
  });
  //-------------------------------------------------------
  WebServer.on("/Info", [](){                         // 系統資訊
    String JSON_Str="{";
    JSON_Str += (String) "\"S3X\":\"" + myS3X_Info.SlaveName + "\",";
    JSON_Str += (String) "\"TEM\":" + myS3X_Info.NowTemperature + ",";
    JSON_Str += (String) "\"SET\":" + myS3X_Info.SetTemperature + ",";
    JSON_Str += (String) "\"SSB\":" + myS3X_Info.SystemStatusByte + ",";
    JSON_Str += (String) "\"PEC\":" + myS3X_Info.PacketErrorCount + "}";
    WebServer.send(200, "text/html", JSON_Str);
  });
  //-------------------------------------------------------
  WebServer.begin();
}
// ************************************************************************
// IP 轉字串
String IP_To_String(IPAddress ip){
  String IP_Str="";
  for (byte CX=0; CX<4; CX++)
    IP_Str += (CX>0 ? "." + String(ip[CX]) : String(ip[CX]));
  return(IP_Str);
}
// ************************************************************************
// Web 網頁：【/】
void Web_Root() {
  String content="";
  content += "<script>";
  content += "var myVar=setInterval(myTimer,1000);";
  content += "var SecCount=0;";
  content += "function myTimer(){";
  content += "SecCount=(SecCount+1)%60;";
  content += "document.getElementById('myProgress').value=Math.floor(100*SecCount/60);";
  content += "}";
  content += "</script>";
  content += "<meta charset='UTF-8' http-equiv='refresh' content='60'>";   // 1分鐘(60秒) 自動 refresh
  content += "<html><head><title>" + myS3X_Info.SlaveName + "：" + Temperature_String(myS3X_Info.NowTemperature) + " ℃ </title></head>";
  content += "<body style='font-family:Consolas'>";
  content += "<table cellpadding='6' border='1'>";
  content += (String)"<tr style='background-color:#A9D0F5'><td colspan='2' align='center'>" + mDNS_Name + "</td></tr>";
  content += "<tr style='background-color:#DCDCDC'><td colspan='2' align='center'><progress id='myProgress' value='0' max='100' width='100%'></progress></td></tr>";
  content += "<tr style='background-color:#DCDCDC'><td align='center'>目前水溫</td><td align='center'>水溫設定</td></tr>";
  content += "<tr><td align='center'><font size='6' color='red'><strong>" + Temperature_String(myS3X_Info.NowTemperature) + "</strong></font></td>";
  content += "<td align='center'><font size='6' color='blue'><strong>" + Temperature_String(myS3X_Info.SetTemperature) + "</strong></font></td></tr>";
  content += "<tr style='background-color:#DCDCDC'><td colspan='2'>主機(P31)狀態</td></tr>";
  content += "<tr><td colspan='2'>";
  content += (String)"<font color='" + (myS3X_Info.SystemStatusByte & 0x01<<7 ?"red":"black") + "'>" + (myS3X_Info.SystemStatusByte & 0x01<<7 ?"●":"○") + ".8：控制箱未連線</font><br>";
  content += (String)"<font color='" + (myS3X_Info.SystemStatusByte & 0x01<<6 ?"red":"black") + "'>" + (myS3X_Info.SystemStatusByte & 0x01<<6 ?"●":"○") + ".7：溫度異常</font><br>";
  content += (String)"<font color='" + (myS3X_Info.SystemStatusByte & 0x01<<5 ?"red":"black") + "'>" + (myS3X_Info.SystemStatusByte & 0x01<<5 ?"●":"○") + ".6：無法加熱</font><br>";
  content += (String)"<font color='" + (myS3X_Info.SystemStatusByte & 0x01<<4 ?"red":"black") + "'>" + (myS3X_Info.SystemStatusByte & 0x01<<4 ?"●":"○") + ".5：未知 #5</font><br>";
  content += (String)"<font color='" + (myS3X_Info.SystemStatusByte & 0x01<<3 ?"red":"black") + "'>" + (myS3X_Info.SystemStatusByte & 0x01<<3 ?"●":"○") + ".4：加熱中</font><br>";
  content += (String)"<font color='" + (myS3X_Info.SystemStatusByte & 0x01<<2 ?"red":"black") + "'>" + (myS3X_Info.SystemStatusByte & 0x01<<2 ?"●":"○") + ".3：強制加熱模式</font><br>";
  content += (String)"<font color='" + (myS3X_Info.SystemStatusByte & 0x01<<1 ?"red":"black") + "'>" + (myS3X_Info.SystemStatusByte & 0x01<<1 ?"●":"○") + ".2：水位過低</font><br>";
  content += (String)"<font color='" + (myS3X_Info.SystemStatusByte & 0x01<<0 ?"red":"black") + "'>" + (myS3X_Info.SystemStatusByte & 0x01<<0 ?"●":"○") + ".1：未知 #1</font><br>";
  content += "</td></tr>";
  content += (String)"<tr><td colspan='2'>封包錯誤：" + myS3X_Info.PacketErrorCount + "</td></tr>";
  content += "<tr><td colspan='2'>IP：" + IP_To_String(WiFi.localIP()) + "</td></tr>";
  content += "<tr style='background-color:#DCDCDC'><td colspan='2' align='center'>";
  content += "<input type='button' value='強制加熱' onclick=\"location.href='/ForcedHeating'\">&emsp;";
  content += "<input type='button' value='水溫設定' onclick=\"location.href='/SetTem'\">";
  content += "</td></tr>";
  content += "</table><br>";
  content += "<input type='button' value='設定' onclick=\"location.href='/Setup'\">&emsp;";
  content += "<input type='button' value='重開機' onclick=\"location.href='/Reboot'\">&emsp;";
  content += "<input type='button' value='Info' onclick=\"location.href='/Info'\">";
  content += "</body></html>";
  WebServer.send(200, "text/html", content);  
}
// ************************************************************************
// Web 網頁：【/SetTem】
void Web_SetTemperature() {
  static byte Sim_SetTemperature;
  String msg="";
  String content="";
  //-------------------------------------------------------
   if (WebServer.args()==2 || WebServer.args()==1) {      // 按下【儲存設定】送出表單 或者 http://s3x-XXXX.local/SetTem?SetTemperature=40
    Sim_SetTemperature = WebServer.arg("SetTemperature").toInt();
    Send_SetTemperature = Sim_SetTemperature;
    msg = "更改水溫設定完畢<br>";
   } else {
    Sim_SetTemperature = constrain(myS3X_Info.SetTemperature,30,75);
   }
  //-------------------------------------------------------
  content += "<meta charset='UTF-8'>";
  content += "<html><body style='font-family:Consolas'><form action='/SetTem' method='POST'>";
  content += "<table cellpadding='6' border='1'>";
  content += (String)"<tr style='background-color:#A9D0F5'><td colspan='2' align='center'>" + mDNS_Name + "</td></tr>";
  content += "<tr style='background-color:#DCDCDC'><td colspan='2'>水溫設定</td></tr>";
  content += (String)"<tr><td>溫度</td><td><input type='number' min='30' max='75' maxlength='2' name='SetTemperature' placeholder='30~75' value='" + Sim_SetTemperature + "'></td></tr>";
  content += "<tr style='background-color:#DCDCDC'><td colspan='2' align='center'>";
  content += "<input type='submit' name='SUBMIT' value='確認更改'>";
  content += (String)"[" + WebServer.args() + "]";
  content += "</td></tr></table></form>" + msg;
  content += "<input type='button' value='返回主畫面' onclick=\"location.href='/'\">";
  content += "</body></html>";
  WebServer.send(200, "text/html", content);  
  //-------------------------------------------------------
}
// ************************************************************************
// Web 網頁：【/Setup】
void Web_Setup() {
  String msg="";
  String content="";
  //-------------------------------------------------------
  // http://www.blueshop.com.tw/board/FUM200410061525290EW/BRD20050926131412GIB.html
  // checkbox的特性....就是有打勾的才會送出
  //-------------------------------------------------------
  if (WebServer.args()==3 || WebServer.args()==4) {     // 按下【儲存設定】送出表單
    //-------------------------------------------------------
    myConfig.Disable_AP = WebServer.arg("Disable_AP").toInt();
    //-------------------------------------------------------
    msg = WebServer.arg("WiFi_SSID");
    msg.trim();
    msg.toCharArray(myConfig.WiFi_SSID,sizeof(myConfig.WiFi_SSID));
    //-------------------------------------------------------
    msg = WebServer.arg("WiFi_PASS");
    msg.trim();
    msg.toCharArray(myConfig.WiFi_PASS,sizeof(myConfig.WiFi_PASS));
    //-------------------------------------------------------
    SaveConfig();                                       // 儲存設定
    msg = "儲存完畢 & 請重開機<br>";
  }
  //-------------------------------------------------------
  content += "<meta charset='UTF-8'>";
  content += "<html><body style='font-family:Consolas'><form action='/Setup' method='POST'>";
  content += "<table cellpadding='6' border='1'>";
  content += (String)"<tr style='background-color:#A9D0F5'><td colspan='2' align='center'>" + mDNS_Name + "</td></tr>";
  content += "<tr style='background-color:#DCDCDC'><td colspan='2'>無線網路</td></tr>";
  content += (String)"<tr><td>Soft AP</td><td><input type='checkbox' name='Disable_AP' value='1'" + (myConfig.Disable_AP>0?" checked":"") + ">停用 AP</td></tr>";
  content += (String)"<tr><td>SSID</td><td><input type='text' maxlength='16' name='WiFi_SSID' placeholder='WiFi SSID 名稱' value='" + myConfig.WiFi_SSID + "'></td></tr>";
  content += (String)"<tr><td>密碼</td><td><input type='password' maxlength='16' name='WiFi_PASS' placeholder='WiFi 密碼' value='" + myConfig.WiFi_PASS + "'></td></tr>";
  content += "<tr style='background-color:#DCDCDC'><td colspan='2' align='right'>";
  content += "<input type='submit' name='SUBMIT' value='儲存設定'>";
  content += (String)"[" + WebServer.args() + "]";
  content += "</td></tr></table></form>" + msg;
  content += "<input type='button' value='返回主畫面' onclick=\"location.href='/'\">";
  content += "</body></html>";
  WebServer.send(200, "text/html", content);  
}
// ************************************************************************
// Wifi LED 閃爍
void WiFi_LED_Blink() {
  digitalWrite(Pin_WiFi_LED, !digitalRead(Pin_WiFi_LED)); 
}
// ************************************************************************
// WiFi 事件
void WiFiEvent(WiFiEvent_t event) {
    switch(event) {
        case WIFI_EVENT_STAMODE_GOT_IP:           // 取得 IP
            WiFi_LED_Ticker.detach();             // WiFi LED 停止閃爍
            digitalWrite(Pin_WiFi_LED, HIGH);     // WiFi LED：ON
            if (myConfig.Disable_AP != 0 && WiFi.softAPIP() != 0)
              WiFi.mode(WIFI_STA);
            break;
        case WIFI_EVENT_STAMODE_DISCONNECTED:     // 離線
                                                  // WiFi LED 慢速閃爍
            WiFi_LED_Ticker.attach(0.5, WiFi_LED_Blink);
            break;
    }
}
// ************************************************************************
// WiFi 設置
void WiFi_init() {
  //-------------------------------------------------------
  ShowMessage(Message_WiFi);
  //-------------------------------------------------------
  WiFi.macAddress(WiFi_MAC);                      // 取得 WiFi MAC 資料
                                                  // 設置 Soft AP 的 SSID
  sprintf(AP_SSID,"%s_%02X%02X%02X",mDNS_Name,WiFi_MAC[3],WiFi_MAC[4],WiFi_MAC[5]);
  //-------------------------------------------------------
  if (myConfig.Disable_AP > 0) {
    WiFi.mode(WIFI_STA);  
  } else {
    WiFi.mode(WIFI_AP_STA);                       // AP + Station 模式
    WiFi.softAP(AP_SSID);                         // 啟用 Soft AP 
  }
  //-------------------------------------------------------                                         
  WiFi.disconnect(true);                          // 一定要做，不燃 WiFi.status() 不會變化
  delay(1000);                                    // 延遲 1 秒
  WiFi.onEvent(WiFiEvent);                        // 設置 WiFi 事件
  WiFi_LED_Ticker.attach(0.5, WiFi_LED_Blink);    // WiFi LED 慢速閃爍
                                                  // 開始連接 WiFi 分享器
  WiFi.begin(myConfig.WiFi_SSID, myConfig.WiFi_PASS);
  //-------------------------------------------------------
  for (byte CX=0; CX<10; CX++) {                  // 10 秒內檢查是否連線成功
    LCD_Print(5+CX,1,".");
    delay(1000);
    if (WiFi.status() == WL_CONNECTED) break;     // 已連線
  }
  //-------------------------------------------------------
  if (WiFi.status() != WL_CONNECTED) {            // 未連線
    WiFi.mode(WIFI_AP_STA);                       // AP + Station 模式
    WiFi.softAP(AP_SSID);                         // 啟用 Soft AP 
  } else {
    LCD_Print(2,1,"OK");
    Beep(500,1);
    LCD_Print(1,1,IP_To_String(WiFi.localIP()));
    delay(5000);
  }
  //-------------------------------------------------------
  MDNS.begin(mDNS_Name);                        // 設置 mDNS
  //-------------------------------------------------------
}
// ************************************************************************
// 設定值：載入
void LoadConfig() {
  //-------------------------------------------------------
  #if defined(ARDUINO_ARCH_ESP8266)   
    EEPROM.begin(sizeof(myConfig));
  #endif
  //-------------------------------------------------------
  for (byte CX=0; CX<sizeof(myConfig); CX++)
      *((byte*)&myConfig + CX) = EEPROM.read( 0 + CX);
  //-------------------------------------------------------
  if ((byte)~myConfig.Head != (byte)myConfig.Tail)// 頭、尾檢核資料有誤
    memset(&myConfig,0,sizeof(myConfig));         // 清除設定資料
  //-------------------------------------------------------
}
// ************************************************************************
// 設定值：儲存
void SaveConfig() {
  #if defined(ARDUINO_ARCH_ESP8266)   
    EEPROM.begin(sizeof(myConfig));
  #endif
  //-------------------------------------------------------
  myConfig.Head = random(255+1);                  // 取亂數 0~255
  myConfig.Tail = (~myConfig.Head);               // 將值反向(NOT)
  //-------------------------------------------------------
  for (byte CX=0; CX<sizeof(myConfig); CX++)
      EEPROM.write(0 + CX, *((byte*)&myConfig + CX));
  //-------------------------------------------------------
  #if defined(ARDUINO_ARCH_ESP8266)
    EEPROM.commit();                            // 更新至 EEPROM
  #endif
  //-------------------------------------------------------
}
// ************************************************************************
// 初始化：16x2 LCD 
void LCD_Init() {
  //-------------------------------------------------------
  uint8_t bell[8]  = {0x4, 0xe, 0xe, 0xe, 0x1f, 0x0, 0x4};
  uint8_t note[8]  = {0x2, 0x3, 0x2, 0xe, 0x1e, 0xc, 0x0};
  uint8_t clock[8] = {0x0, 0xe, 0x15, 0x17, 0x11, 0xe, 0x0};
  uint8_t heart[8] = {0x0, 0xa, 0x1f, 0x1f, 0xe, 0x4, 0x0};
  uint8_t duck[8]  = {0x0, 0xc, 0x1d, 0xf, 0xf, 0x6, 0x0};
  uint8_t check[8] = {0x0, 0x1 ,0x3, 0x16, 0x1c, 0x8, 0x0};
  uint8_t cross[8] = {0x0, 0x1b, 0xe, 0x4, 0xe, 0x1b, 0x0};
  uint8_t retarrow[8] = {  0x1, 0x1, 0x5, 0x9, 0x1f, 0x8, 0x4};
  //-------------------------------------------------------
  //LCD_16x2.begin(0,2);                            // In ESP8266, SDA=0, SCL=2 
  //LCD_16x2.begin(4,5);                            // In ESP8266, SDA=4, SCL=5 
  LCD_16x2.begin();                             // 使用 GPIO 4、5
  LCD_16x2.backlight();                           // 16x2 LCD：開啟背光
  //-------------------------------------------------------
  LCD_16x2.createChar(0, bell);
  LCD_16x2.createChar(1, note);
  LCD_16x2.createChar(2, clock);
  LCD_16x2.createChar(3, heart);
  LCD_16x2.createChar(4, duck);
  LCD_16x2.createChar(5, check);
  LCD_16x2.createChar(6, cross);
  LCD_16x2.createChar(7, retarrow);
  //-------------------------------------------------------
}
// ************************************************************************
// IO 腳位設置
void IO_Init() {
  //-------------------------------------------------------
  pinMode(Pin_WiFi_LED, OUTPUT);                  // 設定腳位：連線燈
  digitalWrite(Pin_WiFi_LED, LOW);                // 設定連線燈：OFF
  //-------------------------------------------------------
  pinMode(Pin_74HC595_Data, OUTPUT);              // 設定腳位：74HC595 Data  Pin
  pinMode(Pin_74HC595_Latch, OUTPUT);             // 設定腳位：74HC595 Latch Pin
  pinMode(Pin_74HC595_Clock, OUTPUT);             // 設定腳位：74HC595 Clock Pin
  LED_74HC595(0x00);                              // 關閉燈號  
  //-------------------------------------------------------
  pinMode(Pin_Buzzer, OUTPUT);                    // 設定腳位：蜂鳴器
  pinMode(Pin_Button, INPUT_PULLUP);              // 設定腳位：按鍵 INPUT_PULLUP
  pinMode(Pin_WakeUp_Sensor, INPUT);              // 設定腳位：喚醒感應
  digitalWrite(Pin_WakeUp_Sensor,LOW);            // 設定腳位：喚醒感應
  //-------------------------------------------------------
}
// ************************************************************************
// 溫度值轉為字串
String Temperature_String(byte value) {
  String str;

  if (value==0xFF || value==0x00) {
    str = "--";
  } else {
    if (value>99) value=99;
    str = String(value/10) + String(value%10);
  }
  return(str);
}
// ************************************************************************
// 嗶嗶聲：每次響停時間，響停次數
void Beep(unsigned int DelayTime, byte Count) {
  for (unsigned int CX=0; CX<Count; CX++) {
    digitalWrite(Pin_Buzzer, HIGH);               // 蜂鳴器：開
    delay(DelayTime/2);
    digitalWrite(Pin_Buzzer, LOW);                // 蜂鳴器：關
    delay(DelayTime/2);
  }
}
// ************************************************************************
// 16x2 LCD 顯示字串
void LCD_Print(byte col, byte row, String str) {
    LCD_16x2.setCursor(col,row);
    LCD_16x2.print(str);
}
// ************************************************************************
// 16x2 LCD 顯示字元
void LCD_Char(byte col, byte row, byte ch) {
    LCD_16x2.setCursor(col,row);
    LCD_16x2.write(ch);
}
// ************************************************************************
// 顯示 8 個 LED 狀態燈
void LED_74HC595(byte value) {
  digitalWrite(Pin_74HC595_Latch, LOW);
  //shiftOut(Pin_74HC595_Data, Pin_74HC595_Clock, MSBFIRST, value);
  shiftOut(Pin_74HC595_Data, Pin_74HC595_Clock, LSBFIRST, value);
  digitalWrite(Pin_74HC595_Latch, HIGH);
}
// ************************************************************************
String mDNS_IP_String(String hostname) {
  //-------------------------------------------------------
  if (WiFi.status() != WL_CONNECTED) return("");  // 網路未連線？
  //-------------------------------------------------------
  int n = MDNS.queryService("http", "tcp");     // Send out query for esp tcp services
  for (int i = 0; i < n; ++i) {
    if (MDNS.hostname(i).equalsIgnoreCase(hostname)) return(IP_To_String(MDNS.IP(i)));
  }
  //-------------------------------------------------------
  return("");
  //-------------------------------------------------------
}
// ************************************************************************
// 顯示資訊
void DisplayInfo() {
  char str[8+1];
  char SC[8]={'1','W','F','H','5','N','T','C'};
  byte BD;
  byte CX;
  //-------------------------------------------------------
  BD=myS3X_Info.SystemStatusByte;                 // 取得狀態值
  LED_74HC595(BD);                                // 輸出至 74HC595 LED
  for (CX=0; CX<8; CX++) {
    str[CX]=(bitRead(BD,7-CX)==0?'-':SC[7-CX]);   // 高位元->低位元排列狀態字串
  }
  str[8]=0x00;
  LCD_Print(7,0,str); 
  //-------------------------------------------------------
  BD=myS3X_Info.NowTemperature;                   // 取得目前水溫
  LCD_Print(4,0,Temperature_String(BD));
  //-------------------------------------------------------
  BD=myS3X_Info.SetTemperature;                   // 取得水溫設定
  LCD_Print(4,1,Temperature_String(BD));
  //-------------------------------------------------------
  sprintf(str, "%-3lu",myS3X_Info.PacketErrorCount%1000);
  LCD_Print(13,1,str);
  //-------------------------------------------------------
  //LCD_Print(7,1,myS3X_Info.SlaveName);
  //-------------------------------------------------------
 }
 // ************************************************************************
