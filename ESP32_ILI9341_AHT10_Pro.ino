#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <time.h>
#include <sys/time.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_AHTX0.h>
#include <lvgl.h>

#if LVGL_VERSION_MAJOR != 8
#error "Hay cai LVGL 8.3.x trong Arduino Library Manager"
#endif

#if LV_COLOR_DEPTH != 16
#error "Mo lv_conf.h va dat LV_COLOR_DEPTH = 16"
#endif

LV_FONT_DECLARE(ui_font_11);
LV_FONT_DECLARE(ui_font_11_bold);
LV_FONT_DECLARE(ui_font_14);
LV_FONT_DECLARE(ui_font_14_bold);
LV_FONT_DECLARE(ui_font_16_bold);
LV_FONT_DECLARE(ui_font_28_bold);
LV_FONT_DECLARE(ui_font_52_bold);

// ===== ESP32 DEVKIT V1 + ILI9341, GIU NGUYEN DAY DANG CHAY =====
#define TFT_CS    27
#define TFT_RST   25
#define TFT_DC    26
#define TFT_MOSI  13
#define TFT_SCK   14
#define TFT_MISO  35
#define AHT_SDA   32
#define AHT_SCL   33

const char *AP_NAME = "ESP32-SmartClock";
const char *AP_PASSWORD = "12345678";
const uint32_t WEATHER_INTERVAL = 10UL * 60UL * 1000UL;
const uint32_t CRYPTO_INTERVAL = 60UL * 1000UL;

SPIClass displaySPI(HSPI);
Adafruit_ILI9341 tft(&displaySPI, TFT_DC, TFT_CS, TFT_RST);
Adafruit_AHTX0 aht;
WebServer server(80);
DNSServer dnsServer;
Preferences prefs;
SemaphoreHandle_t dataMutex;

// ===== CAU HINH =====
String wifiSSID;
String wifiPassword;
String deviceName = "SMART CLOCK";
String weatherLocation = "Ha Noi";
String cryptoSymbols = "BTC,ETH,BNB";
int timezoneHours = 7;
float tempOffset = 0;
float humidityOffset = 0;
float configuredLat = NAN;
float configuredLon = NAN;
bool coordinatesSet = false;
bool portalMode = false;
uint8_t tickerSeconds = 5;
bool pageEnabled[4] = {true, true, true, false};
bool autoPageRotate = true;
uint8_t defaultDisplayPage = 0;
uint8_t pageSeconds = 12;

uint32_t colorDate = 0x20242A;
uint32_t colorHour = 0x15191E;
uint32_t colorMinute = 0x15191E;
uint32_t colorColon = 0x4E80FF;
uint32_t colorWeather = 0x4E80FF;
uint32_t colorTemp = 0x4E80FF;
uint32_t colorHum = 0x3EAFD6;

// ===== DU LIEU =====
struct ForecastDay {
  String date;
  int code = -1;
  float minTemp = NAN;
  float maxTemp = NAN;
  int rain = 0;
  String sunrise;
  String sunset;
};
ForecastDay forecast[7];

struct CryptoData {
  String symbol;
  float price = NAN;
  float change = NAN;
  bool valid = false;
};
CryptoData crypto[3];
uint8_t cryptoCount = 0;

bool weatherOK = false;
bool cryptoOK = false;
volatile bool dataDirty = false;
volatile bool forceWeatherUpdate = false;
float outdoorTemp = NAN;
float apparentTemp = NAN;
float windSpeed = NAN;
float surfacePressure = NAN;
float visibilityKm = NAN;
double weatherLat = 0;
double weatherLon = 0;
int currentWeatherCode = -1;

bool ahtOK = false;
float temperature = NAN;
float humidity = NAN;
float dailyMinTemp = NAN;
float dailyMaxTemp = NAN;
uint32_t lastSensorMs = 0;
uint32_t lastTickerMs = 0;
uint32_t lastWifiTryMs = 0;
uint32_t lastPageMs = 0;
uint8_t tickerPage = 0;
uint8_t activeDisplayPage = 0;
int previousSecond = -1;
int previousDay = -1;

// ===== LVGL =====
// 12 dong du de LVGL ve muot, dong thoi tiet kiem 5 KB DRAM so voi 20 dong.
static const uint16_t LV_DRAW_LINES = 12;
static lv_color_t lvDrawBuffer[320 * LV_DRAW_LINES];
static lv_disp_draw_buf_t lvDrawBuf;
static lv_disp_drv_t lvDispDrv;
static lv_color_t weatherBuffer[56 * 56];
static lv_color_t forecastIconBuffer[7][20 * 20];

lv_obj_t *lblLocation;
lv_obj_t *lblDate;
lv_obj_t *lblWifi;
lv_obj_t *lblHour;
lv_obj_t *lblColon;
lv_obj_t *lblMinute;
lv_obj_t *lblOutdoor;
lv_obj_t *lblWeatherText;
lv_obj_t *weatherCanvas;
lv_obj_t *lblIndoorTemp;
lv_obj_t *lblFeels;
lv_obj_t *lblMax;
lv_obj_t *lblMin;
lv_obj_t *lblHumidity;
lv_obj_t *lblWind;
lv_obj_t *lblPressure;
lv_obj_t *lblVisibility;
lv_obj_t *lblFooterWind;
lv_obj_t *lblFooterRain;
lv_obj_t *lblTicker;
lv_obj_t *displayPages[4];
lv_obj_t *forecastDateLabel[7];
lv_obj_t *forecastWeatherLabel[7];
lv_obj_t *forecastTempLabel[7];
lv_obj_t *forecastRainLabel[7];
lv_obj_t *forecastIconCanvas[7];
lv_obj_t *cryptoSymbolLabel[3];
lv_obj_t *cryptoPriceLabel[3];
lv_obj_t *cryptoChangeLabel[3];
lv_obj_t *sensorTempBig;
lv_obj_t *sensorHumBig;
lv_obj_t *sensorFeelBig;
lv_obj_t *sensorMinBig;
lv_obj_t *sensorMaxBig;
lv_obj_t *sensorState;

const uint32_t C_BG = 0xE7E8EA;
const uint32_t C_CARD = 0xF5F6F7;
const uint32_t C_BORDER = 0xC8CBD0;
const uint32_t C_TEXT = 0x20242A;
const uint32_t C_MUTED = 0x777D86;
const uint32_t C_GREEN = 0x0AA45A;
const uint32_t C_RED = 0xE04444;
const uint32_t C_YELLOW = 0xF2A900;

String htmlEscape(String s) {
  s.replace("&", "&amp;"); s.replace("<", "&lt;"); s.replace(">", "&gt;"); s.replace("\"", "&quot;");
  return s;
}

String jsonEscape(String s) {
  s.replace("\\", "\\\\");s.replace("\"", "\\\"");s.replace("\r", "\\r");s.replace("\n", "\\n");
  return s;
}

uint32_t hexToRgb(String value, uint32_t fallback) {
  if (value.startsWith("#")) value.remove(0, 1);
  if (value.length() != 6) return fallback;
  return strtoul(value.c_str(), nullptr, 16) & 0xFFFFFF;
}

String rgbToHex(uint32_t c) {
  char out[8]; snprintf(out, sizeof(out), "#%06lX", (unsigned long)(c & 0xFFFFFF)); return out;
}

String urlEncode(const String &s) {
  String out; const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.') out += (char)c;
    else if (c == ' ') out += "%20";
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}

String weatherLabel(int code) {
  if (code == 0) return "Trời quang";
  if (code == 1 || code == 2) return "Ít mây";
  if (code == 3) return "Nhiều mây";
  if (code == 45 || code == 48) return "Sương mù";
  if (code >= 51 && code <= 67) return "Có mưa";
  if (code >= 71 && code <= 77) return "Có tuyết";
  if (code >= 80 && code <= 82) return "Mưa rào";
  if (code == 85 || code == 86) return "Tuyết rào";
  if (code >= 95 && code <= 99) return "Có dông";
  return "Thời tiết";
}

// ===== LICH AM VIET NAM =====
double jdFromDate(int d, int m, int y) {
  int a = (14 - m) / 12; y += 4800 - a; m += 12 * a - 3;
  long jd = d + (153 * m + 2) / 5 + 365L * y + y / 4 - y / 100 + y / 400 - 32045;
  if (jd < 2299161) jd = d + (153 * m + 2) / 5 + 365L * y + y / 4 - 32083;
  return jd;
}

int newMoonDay(int k, double tz) {
  double T = k / 1236.85, T2 = T*T, T3 = T2*T, dr = PI/180.0;
  double jd = 2415020.75933 + 29.53058868*k + 0.0001178*T2 - 0.000000155*T3;
  jd += 0.00033*sin((166.56 + 132.87*T - 0.009173*T2)*dr);
  double M = 359.2242 + 29.10535608*k - 0.0000333*T2 - 0.00000347*T3;
  double Mp = 306.0253 + 385.81691806*k + 0.0107306*T2 + 0.00001236*T3;
  double F = 21.2964 + 390.67050646*k - 0.0016528*T2 - 0.00000239*T3;
  double c = (0.1734-0.000393*T)*sin(M*dr)+0.0021*sin(2*M*dr)-0.4068*sin(Mp*dr)-0.0161*sin(2*Mp*dr)-0.0004*sin(3*Mp*dr);
  c += 0.0104*sin(2*F*dr)-0.0051*sin((M+Mp)*dr)-0.0074*sin((M-Mp)*dr)+0.0004*sin((2*F+M)*dr)-0.0004*sin((2*F-M)*dr);
  c -= 0.0006*sin((2*F+Mp)*dr)+0.0010*sin((2*F-Mp)*dr); c += 0.0005*sin((2*Mp+M)*dr);
  double dt = T < -11 ? 0.001+0.000839*T+0.0002261*T2-0.00000845*T3-0.000000081*T*T3 : -0.000278+0.000265*T+0.000262*T2;
  return floor(jd+c-dt+0.5+tz/24.0);
}

double sunLongitude(int jdn, double tz) {
  double T=(jdn-2451545.5-tz/24.0)/36525.0,T2=T*T,dr=PI/180.0;
  double M=357.52910+35999.05030*T-0.0001559*T2-0.00000048*T*T2;
  double L=280.46645+36000.76983*T+0.0003032*T2+(1.914600-0.004817*T-0.000014*T2)*sin(dr*M);
  L+=(0.019993-0.000101*T)*sin(2*dr*M)+0.000290*sin(3*dr*M); L=fmod(L*dr,2*PI);
  return L<0?L+2*PI:L;
}

int lunarMonth11(int y,double tz){int off=jdFromDate(31,12,y)-2415021,k=floor(off/29.530588853),nm=newMoonDay(k,tz);if((int)floor(sunLongitude(nm,tz)/PI*6)>=9)nm=newMoonDay(k-1,tz);return nm;}
int leapMonthOffset(int a11,double tz){int k=floor(0.5+(a11-2415021.076998695)/29.530588853),last=-1,i=1,arc;do{arc=floor(sunLongitude(newMoonDay(k+i,tz),tz)/PI*6);if(arc==last)break;last=arc;i++;}while(i<14);return i-1;}

void solarToLunar(int d,int m,int y,int &ld,int &lm,int &ly,bool &leap){
  const double tz=7.0;int dayNo=jdFromDate(d,m,y),k=floor((dayNo-2415021.076998695)/29.530588853),monthStart=newMoonDay(k+1,tz);
  if(monthStart>dayNo)monthStart=newMoonDay(k,tz);int a11=lunarMonth11(y,tz),b11=a11;
  if(a11>=monthStart){ly=y;a11=lunarMonth11(y-1,tz);}else{ly=y+1;b11=lunarMonth11(y+1,tz);}ld=dayNo-monthStart+1;
  int diff=floor((monthStart-a11)/29.0);lm=diff+11;leap=false;
  if(b11-a11>365){int leapDiff=leapMonthOffset(a11,tz);if(diff>=leapDiff){lm=diff+10;if(diff==leapDiff)leap=true;}}
  if(lm>12)lm-=12;if(lm>=11&&diff<4)ly--;
}

void setCompileTimeFallback() {
  if (time(nullptr) > 1700000000) return;
  const char *months="JanFebMarAprMayJunJulAugSepOctNovDec";char mon[4]={0};int day,year,hour,minute,second;
  sscanf(__DATE__,"%3s %d %d",mon,&day,&year);sscanf(__TIME__,"%d:%d:%d",&hour,&minute,&second);
  const char *p=strstr(months,mon);tm build={};build.tm_year=year-1900;build.tm_mon=p?(p-months)/3:0;build.tm_mday=day;build.tm_hour=hour;build.tm_min=minute;build.tm_sec=second;
  time_t epoch=mktime(&build)-timezoneHours*3600;timeval tv={epoch,0};settimeofday(&tv,nullptr);
}

// ===== THOI TIET / CRYPTO =====
void parseCryptoSymbols() {
  cryptoCount=0;String list=cryptoSymbols;list.toUpperCase();list.replace(" ","");int start=0;
  while(start<(int)list.length()&&cryptoCount<3){int comma=list.indexOf(',',start);if(comma<0)comma=list.length();String s=list.substring(start,comma);bool clean=s.length()>=2&&s.length()<=10;for(size_t i=0;i<s.length();i++)if(!isalnum((uint8_t)s[i]))clean=false;if(clean){crypto[cryptoCount].symbol=s;crypto[cryptoCount].valid=false;cryptoCount++;}start=comma+1;}
}

bool fetchWeather() {
  if (WiFi.status()!=WL_CONNECTED || (!weatherLocation.length()&&!coordinatesSet)) return false;
  WiFiClientSecure secure;secure.setInsecure();HTTPClient http;double lat,lon;String resolved=weatherLocation;
  if(coordinatesSet){lat=configuredLat;lon=configuredLon;}
  else {
    String url="https://geocoding-api.open-meteo.com/v1/search?name="+urlEncode(weatherLocation)+"&count=1&language=vi&format=json";
    if(!http.begin(secure,url))return false;int status=http.GET();if(status!=200){http.end();return false;}JsonDocument geo;auto err=deserializeJson(geo,http.getString());http.end();JsonArray results=geo["results"].as<JsonArray>();if(err||results.isNull()||!results.size())return false;
    lat=geo["results"][0]["latitude"]|0.0;lon=geo["results"][0]["longitude"]|0.0;resolved=String((const char*)(geo["results"][0]["name"]|weatherLocation.c_str()));
  }
  String url="https://api.open-meteo.com/v1/forecast?latitude="+String(lat,5)+"&longitude="+String(lon,5)+"&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m,surface_pressure,visibility&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset&timezone=auto&forecast_days=7";
  if(!http.begin(secure,url))return false;int status=http.GET();if(status!=200){http.end();return false;}JsonDocument doc;auto err=deserializeJson(doc,http.getString());http.end();if(err)return false;
  ForecastDay local[7];for(int i=0;i<7;i++){local[i].date=String((const char*)(doc["daily"]["time"][i]|""));local[i].code=doc["daily"]["weather_code"][i]|-1;local[i].maxTemp=doc["daily"]["temperature_2m_max"][i]|NAN;local[i].minTemp=doc["daily"]["temperature_2m_min"][i]|NAN;local[i].rain=doc["daily"]["precipitation_probability_max"][i]|0;local[i].sunrise=String((const char*)(doc["daily"]["sunrise"][i]|""));local[i].sunset=String((const char*)(doc["daily"]["sunset"][i]|""));}
  xSemaphoreTake(dataMutex,portMAX_DELAY);weatherLat=lat;weatherLon=lon;outdoorTemp=doc["current"]["temperature_2m"]|NAN;apparentTemp=doc["current"]["apparent_temperature"]|NAN;currentWeatherCode=doc["current"]["weather_code"]|-1;windSpeed=doc["current"]["wind_speed_10m"]|NAN;surfacePressure=doc["current"]["surface_pressure"]|NAN;visibilityKm=(doc["current"]["visibility"]|NAN)/1000.0;for(int i=0;i<7;i++)forecast[i]=local[i];weatherOK=true;dataDirty=true;xSemaphoreGive(dataMutex);return true;
}

bool fetchCrypto() {
  if(WiFi.status()!=WL_CONNECTED)return false;CryptoData local[3];bool any=false;WiFiClientSecure secure;secure.setInsecure();HTTPClient http;
  for(uint8_t i=0;i<cryptoCount;i++){local[i].symbol=crypto[i].symbol;String url="https://api.binance.com/api/v3/ticker/24hr?symbol="+local[i].symbol+"USDT";if(!http.begin(secure,url))continue;int status=http.GET();if(status==200){JsonDocument doc;if(!deserializeJson(doc,http.getString())){local[i].price=String((const char*)(doc["lastPrice"]|"nan")).toFloat();local[i].change=String((const char*)(doc["priceChangePercent"]|"nan")).toFloat();local[i].valid=local[i].price>0;any|=local[i].valid;}}http.end();delay(20);}
  xSemaphoreTake(dataMutex,portMAX_DELAY);for(uint8_t i=0;i<cryptoCount;i++)crypto[i]=local[i];cryptoOK=any;dataDirty=true;xSemaphoreGive(dataMutex);return any;
}

void networkTask(void *) {
  uint32_t lastWeather=0,lastCrypto=0;vTaskDelay(pdMS_TO_TICKS(3500));
  for(;;){if(WiFi.status()==WL_CONNECTED){uint32_t now=millis();if(forceWeatherUpdate||lastWeather==0||now-lastWeather>=WEATHER_INTERVAL){forceWeatherUpdate=false;bool ok=fetchWeather();lastWeather=ok?now:now-WEATHER_INTERVAL+30000UL;}if(lastCrypto==0||now-lastCrypto>=CRYPTO_INTERVAL){bool ok=fetchCrypto();lastCrypto=ok?now:now-CRYPTO_INTERVAL+15000UL;}}vTaskDelay(pdMS_TO_TICKS(1000));}
}

// ===== LVGL DRIVER / ICON =====
void lvFlush(lv_disp_drv_t *disp,const lv_area_t *area,lv_color_t *colorP){uint16_t w=area->x2-area->x1+1,h=area->y2-area->y1+1;tft.drawRGBBitmap(area->x1,area->y1,(uint16_t*)colorP,w,h);lv_disp_flush_ready(disp);}

void setPixel(int x,int y,uint32_t color){if(x>=0&&x<56&&y>=0&&y<56)weatherBuffer[y*56+x]=lv_color_hex(color);}
void drawLineRaw(int x0,int y0,int x1,int y1,uint32_t c){int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;for(;;){setPixel(x0,y0,c);if(x0==x1&&y0==y1)break;int e2=2*err;if(e2>=dy){err+=dy;x0+=sx;}if(e2<=dx){err+=dx;y0+=sy;}}}
void fillCircleRaw(int cx,int cy,int r,uint32_t c){for(int y=-r;y<=r;y++)for(int x=-r;x<=r;x++)if(x*x+y*y<=r*r)setPixel(cx+x,cy+y,c);}
void fillRectRaw(int x,int y,int w,int h,uint32_t c){for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++)setPixel(xx,yy,c);}

void drawWeatherIcon(int code){
  for(int i=0;i<56*56;i++)weatherBuffer[i]=lv_color_hex(C_CARD);
  uint32_t blue=colorWeather,yellow=C_YELLOW,cloud=0xB8C7DC,dark=0x7C91AC;
  if(code<0)code=2;
  bool clear=code==0,partly=code==1||code==2,cloudy=code==3;
  bool fog=code==45||code==48;
  bool rain=(code>=51&&code<=67)||(code>=80&&code<=82);
  bool snow=(code>=71&&code<=77)||code==85||code==86;
  bool storm=code>=95&&code<=99;
  if(clear||partly){fillCircleRaw(partly?39:28,partly?15:25,clear?12:9,yellow);for(int a=0;a<8;a++){float q=a*PI/4;int cx=partly?39:28,cy=partly?15:25,r1=partly?13:17,r2=partly?17:21;drawLineRaw(cx+cos(q)*r1,cy+sin(q)*r1,cx+cos(q)*r2,cy+sin(q)*r2,yellow);}}
  if(partly||cloudy||fog||rain||snow||storm){fillCircleRaw(21,32,11,cloud);fillCircleRaw(34,27,14,cloud);fillCircleRaw(45,34,10,cloud);fillRectRaw(16,31,35,14,cloud);drawLineRaw(15,45,51,45,dark);}
  if(fog){drawLineRaw(12,48,45,48,dark);drawLineRaw(20,52,52,52,dark);}
  if(rain){drawLineRaw(22,48,19,54,blue);drawLineRaw(34,48,31,54,blue);drawLineRaw(46,48,43,54,blue);}
  if(snow){for(int x=21;x<=45;x+=12){drawLineRaw(x-3,51,x+3,51,blue);drawLineRaw(x,48,x,54,blue);drawLineRaw(x-2,49,x+2,53,blue);drawLineRaw(x+2,49,x-2,53,blue);}}
  if(storm){drawLineRaw(35,44,29,52,yellow);drawLineRaw(29,52,36,51,yellow);drawLineRaw(36,51,27,56,yellow);}
  lv_obj_invalidate(weatherCanvas);
}

void drawForecastIcon(uint8_t index,int code){
  if(index>=7)return;lv_color_t *buf=forecastIconBuffer[index];for(int i=0;i<400;i++)buf[i]=lv_color_hex(C_CARD);
  auto px=[&](int x,int y,uint32_t c){if(x>=0&&x<20&&y>=0&&y<20)buf[y*20+x]=lv_color_hex(c);};
  auto circle=[&](int cx,int cy,int r,uint32_t c){for(int y=-r;y<=r;y++)for(int x=-r;x<=r;x++)if(x*x+y*y<=r*r)px(cx+x,cy+y,c);};
  auto line=[&](int x0,int y0,int x1,int y1,uint32_t c){int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;for(;;){px(x0,y0,c);if(x0==x1&&y0==y1)break;int e2=2*err;if(e2>=dy){err+=dy;x0+=sx;}if(e2<=dx){err+=dx;y0+=sy;}}};
  bool clear=code==0,partly=code==1||code==2,cloudy=code==3,fog=code==45||code==48;
  bool rain=(code>=51&&code<=67)||(code>=80&&code<=82),snow=(code>=71&&code<=77)||code==85||code==86,storm=code>=95&&code<=99;
  if(code<0)partly=true;
  if(clear||partly){int sx=partly?14:10,sy=partly?6:9;circle(sx,sy,clear?4:3,C_YELLOW);for(int a=0;a<8;a++){float q=a*PI/4;line(sx+cos(q)*(clear?5:4),sy+sin(q)*(clear?5:4),sx+cos(q)*(clear?7:5),sy+sin(q)*(clear?7:5),C_YELLOW);}}
  if(partly||cloudy||fog||rain||snow||storm){circle(6,12,4,0xB8C7DC);circle(11,10,5,0xB8C7DC);circle(15,13,3,0xB8C7DC);for(int y=12;y<=15;y++)for(int x=4;x<=17;x++)px(x,y,0xB8C7DC);line(4,16,17,16,0x7C91AC);}
  if(fog){line(3,17,13,17,0x7C91AC);line(7,19,18,19,0x7C91AC);}
  if(rain){line(7,17,6,19,colorWeather);line(12,17,11,19,colorWeather);line(17,17,16,19,colorWeather);}
  if(snow){for(int x=7;x<=17;x+=5){px(x,18,colorWeather);px(x-1,18,colorWeather);px(x+1,18,colorWeather);px(x,17,colorWeather);px(x,19,colorWeather);}}
  if(storm){line(12,15,9,19,C_YELLOW);line(9,19,14,17,C_YELLOW);}
  lv_obj_invalidate(forecastIconCanvas[index]);
}

lv_obj_t *panel(lv_obj_t *parent,int x,int y,int w,int h,int radius=8){lv_obj_t *o=lv_obj_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_radius(o,radius,0);lv_obj_set_style_bg_color(o,lv_color_hex(C_CARD),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_border_color(o,lv_color_hex(C_BORDER),0);lv_obj_set_style_border_width(o,1,0);lv_obj_set_style_pad_all(o,0,0);return o;}
lv_obj_t *label(lv_obj_t *parent,const char *text,int x,int y,int w,const lv_font_t *font,uint32_t color,lv_text_align_t align=LV_TEXT_ALIGN_LEFT){lv_obj_t *o=lv_label_create(parent);lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);lv_obj_set_style_text_align(o,align,0);lv_label_set_long_mode(o,LV_LABEL_LONG_CLIP);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,font->line_height);lv_label_set_text(o,text);return o;}
void divider(lv_obj_t *parent,int x,int y,int w,int h){lv_obj_t *o=lv_obj_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(o,lv_color_hex(C_BORDER),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_pad_all(o,0,0);}

lv_obj_t *pageContainer(lv_obj_t *parent){lv_obj_t *o=lv_obj_create(parent);lv_obj_set_pos(o,0,23);lv_obj_set_size(o,320,217);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_opa(o,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_pad_all(o,0,0);return o;}

String shortForecastDate(const String &iso){if(iso.length()>=10)return iso.substring(8,10)+"/"+iso.substring(5,7);return "--/--";}

void showDisplayPage(uint8_t requested){
  uint8_t chosen=requested%4;if(!pageEnabled[chosen]){for(uint8_t i=0;i<4;i++)if(pageEnabled[i]){chosen=i;break;}}
  for(uint8_t i=0;i<4;i++){if(i==chosen)lv_obj_clear_flag(displayPages[i],LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(displayPages[i],LV_OBJ_FLAG_HIDDEN);}
  activeDisplayPage=chosen;lastPageMs=millis();
}

void nextDisplayPage(){for(uint8_t step=1;step<=4;step++){uint8_t candidate=(activeDisplayPage+step)%4;if(pageEnabled[candidate]){showDisplayPage(candidate);return;}}}

void buildDashboard(){
  lv_obj_t *scr=lv_scr_act();lv_obj_set_style_bg_color(scr,lv_color_hex(C_BG),0);lv_obj_set_style_bg_opa(scr,LV_OPA_COVER,0);lv_obj_clear_flag(scr,LV_OBJ_FLAG_SCROLLABLE);
  lblLocation=label(scr,"• HÀ NỘI",5,4,58,&ui_font_11_bold,colorWeather);
  lblDate=label(scr,"--",63,2,229,&ui_font_14_bold,colorDate,LV_TEXT_ALIGN_CENTER);lv_obj_set_style_text_letter_space(lblDate,-1,0);
  lblWifi=lv_label_create(scr);lv_label_set_text(lblWifi,LV_SYMBOL_WIFI);lv_obj_set_pos(lblWifi,299,4);lv_obj_set_style_text_color(lblWifi,lv_color_hex(C_GREEN),0);

  for(uint8_t i=0;i<4;i++)displayPages[i]=pageContainer(scr);
  lv_obj_t *overview=displayPages[0];
  lv_obj_t *hero=panel(overview,6,1,308,83,9);lv_obj_set_style_border_color(hero,lv_color_hex(colorWeather),0);divider(hero,169,8,1,65);
  lblHour=label(hero,"22",2,19,70,&ui_font_52_bold,colorHour,LV_TEXT_ALIGN_RIGHT);lv_obj_set_style_text_letter_space(lblHour,-2,0);
  lblColon=label(hero,":",72,19,21,&ui_font_52_bold,colorColon,LV_TEXT_ALIGN_CENTER);
  lblMinute=label(hero,"57",94,19,72,&ui_font_52_bold,colorMinute);lv_obj_set_style_text_letter_space(lblMinute,-2,0);
  label(hero,"NGOÀI TRỜI",178,8,75,&ui_font_11,C_MUTED);
  lblOutdoor=label(hero,"--°C",177,23,76,&ui_font_28_bold,colorWeather);
  lblWeatherText=label(hero,"Đang cập nhật",177,55,78,&ui_font_11,C_MUTED);
  weatherCanvas=lv_canvas_create(hero);lv_canvas_set_buffer(weatherCanvas,weatherBuffer,56,56,LV_IMG_CF_TRUE_COLOR);lv_obj_set_pos(weatherCanvas,247,13);drawWeatherIcon(2);

  label(overview,"CHỈ SỐ MÔI TRƯỜNG",6,85,180,&ui_font_11,C_MUTED);
  lv_obj_t *tempCard=panel(overview,6,101,151,78,8);lv_obj_set_style_border_color(tempCard,lv_color_hex(colorTemp),0);
  label(tempCard,"T",7,5,18,&ui_font_16_bold,colorTemp,LV_TEXT_ALIGN_CENTER);lblIndoorTemp=label(tempCard,"--.-°C",28,3,115,&ui_font_28_bold,C_TEXT);
  label(tempCard,"•",8,35,10,&ui_font_11,colorTemp);label(tempCard,"Cảm nhận",21,34,70,&ui_font_11,C_TEXT);lblFeels=label(tempCard,"--.-°C",91,34,54,&ui_font_11_bold,C_TEXT,LV_TEXT_ALIGN_RIGHT);
  label(tempCard,"↑",8,50,10,&ui_font_11,colorTemp);label(tempCard,"Cao nhất",21,49,70,&ui_font_11,C_TEXT);lblMax=label(tempCard,"--.-°C",91,49,54,&ui_font_11_bold,C_TEXT,LV_TEXT_ALIGN_RIGHT);
  label(tempCard,"↓",8,64,10,&ui_font_11,C_GREEN);label(tempCard,"Thấp nhất",21,63,70,&ui_font_11,C_TEXT);lblMin=label(tempCard,"--.-°C",91,63,54,&ui_font_11_bold,C_TEXT,LV_TEXT_ALIGN_RIGHT);

  lv_obj_t *humCard=panel(overview,163,101,151,78,8);lv_obj_set_style_border_color(humCard,lv_color_hex(colorHum),0);
  label(humCard,"H",7,5,18,&ui_font_16_bold,colorHum,LV_TEXT_ALIGN_CENTER);lblHumidity=label(humCard,"--%",28,3,115,&ui_font_28_bold,C_TEXT);
  label(humCard,"≈",8,35,10,&ui_font_11,colorHum);label(humCard,"Gió",21,34,67,&ui_font_11,C_TEXT);lblWind=label(humCard,"-- km/h",88,34,57,&ui_font_11_bold,C_TEXT,LV_TEXT_ALIGN_RIGHT);
  label(humCard,"◉",8,50,10,&ui_font_11,0xB832C9);label(humCard,"Áp suất",21,49,67,&ui_font_11,C_TEXT);lblPressure=label(humCard,"-- hPa",88,49,57,&ui_font_11_bold,C_TEXT,LV_TEXT_ALIGN_RIGHT);
  label(humCard,"⊙",8,64,10,&ui_font_11,C_YELLOW);label(humCard,"Tầm nhìn",21,63,67,&ui_font_11,C_TEXT);lblVisibility=label(humCard,"-- km",88,63,57,&ui_font_11_bold,C_TEXT,LV_TEXT_ALIGN_RIGHT);

  lv_obj_t *foot=panel(overview,6,182,308,29,8);divider(foot,154,0,1,29);divider(foot,77,0,1,29);
  label(foot,"GIÓ",0,1,77,&ui_font_11,C_MUTED,LV_TEXT_ALIGN_CENTER);lblFooterWind=label(foot,"-- km/h",0,14,77,&ui_font_11_bold,C_TEXT,LV_TEXT_ALIGN_CENTER);
  label(foot,"MƯA",77,1,77,&ui_font_11,C_MUTED,LV_TEXT_ALIGN_CENTER);lblFooterRain=label(foot,"--%",77,14,77,&ui_font_11_bold,C_TEXT,LV_TEXT_ALIGN_CENTER);
  lblTicker=label(foot,"✓ ĐANG KHỞI ĐỘNG",157,8,148,&ui_font_11_bold,C_GREEN,LV_TEXT_ALIGN_CENTER);

  lv_obj_t *forecastPage=displayPages[1];lv_obj_t *fh=panel(forecastPage,6,1,308,29,8);label(fh,"DỰ BÁO THỜI TIẾT 7 NGÀY",8,7,292,&ui_font_14_bold,colorWeather,LV_TEXT_ALIGN_CENTER);
  lv_obj_t *fl=panel(forecastPage,6,34,308,177,8);
  for(uint8_t i=0;i<7;i++){int y=5+i*24;if(i)divider(fl,7,y-4,294,1);forecastDateLabel[i]=label(fl,"--/--",8,y,48,&ui_font_11_bold,C_TEXT);forecastIconCanvas[i]=lv_canvas_create(fl);lv_canvas_set_buffer(forecastIconCanvas[i],forecastIconBuffer[i],20,20,LV_IMG_CF_TRUE_COLOR);lv_obj_set_pos(forecastIconCanvas[i],57,y-3);forecastWeatherLabel[i]=label(fl,"Đang chờ",80,y,74,&ui_font_11,C_MUTED);forecastTempLabel[i]=label(fl,"--° / --°",155,y,82,&ui_font_11_bold,colorWeather,LV_TEXT_ALIGN_RIGHT);forecastRainLabel[i]=label(fl,"--%",246,y,50,&ui_font_11_bold,colorHum,LV_TEXT_ALIGN_RIGHT);drawForecastIcon(i,-1);}

  lv_obj_t *cryptoPage=displayPages[2];lv_obj_t *ch=panel(cryptoPage,6,1,308,29,8);label(ch,"THỊ TRƯỜNG CRYPTO",8,7,292,&ui_font_14_bold,colorWeather,LV_TEXT_ALIGN_CENTER);
  for(uint8_t i=0;i<3;i++){lv_obj_t *card=panel(cryptoPage,6,36+i*55,308,49,8);cryptoSymbolLabel[i]=label(card,"---",10,7,62,&ui_font_14_bold,C_TEXT);cryptoPriceLabel[i]=label(card,"Đang cập nhật",76,7,140,&ui_font_14_bold,colorWeather);cryptoChangeLabel[i]=label(card,"--%",220,7,76,&ui_font_14_bold,C_GREEN,LV_TEXT_ALIGN_RIGHT);label(card,"Giá USDT · biến động 24 giờ",76,27,210,&ui_font_11,C_MUTED);}
  lv_obj_t *cs=panel(cryptoPage,6,204,308,7,4);lv_obj_set_style_bg_color(cs,lv_color_hex(colorWeather),0);lv_obj_set_style_border_width(cs,0,0);

  lv_obj_t *sensorPage=displayPages[3];lv_obj_t *sh=panel(sensorPage,6,1,308,29,8);label(sh,"CẢM BIẾN AHT10 TRONG NHÀ",8,7,292,&ui_font_14_bold,colorWeather,LV_TEXT_ALIGN_CENTER);
  lv_obj_t *st=panel(sensorPage,6,36,151,116,8);lv_obj_set_style_border_color(st,lv_color_hex(colorTemp),0);label(st,"NHIỆT ĐỘ",10,8,130,&ui_font_11_bold,C_MUTED);sensorTempBig=label(st,"--.-°C",10,27,132,&ui_font_28_bold,colorTemp);label(st,"Cảm nhận",10,67,72,&ui_font_11,C_TEXT);sensorFeelBig=label(st,"--.-°C",82,67,59,&ui_font_11_bold,C_TEXT,LV_TEXT_ALIGN_RIGHT);label(st,"Thấp",10,88,30,&ui_font_11,C_TEXT);sensorMinBig=label(st,"--.-°",40,88,40,&ui_font_11_bold,C_GREEN,LV_TEXT_ALIGN_RIGHT);divider(st,84,87,1,15);label(st,"Cao",88,88,22,&ui_font_11,C_TEXT);sensorMaxBig=label(st,"--.-°",110,88,31,&ui_font_11_bold,C_RED,LV_TEXT_ALIGN_RIGHT);
  lv_obj_t *su=panel(sensorPage,163,36,151,116,8);lv_obj_set_style_border_color(su,lv_color_hex(colorHum),0);label(su,"ĐỘ ẨM",10,8,130,&ui_font_11_bold,C_MUTED);sensorHumBig=label(su,"--%",10,27,132,&ui_font_28_bold,colorHum);label(su,"Mức tiện nghi",10,67,130,&ui_font_11,C_TEXT);divider(su,10,86,131,3);label(su,"Mục tiêu: 40 - 70%",10,94,130,&ui_font_11,C_MUTED);
  lv_obj_t *ss=panel(sensorPage,6,159,308,52,8);label(ss,"TRẠNG THÁI",10,7,80,&ui_font_11,C_MUTED);sensorState=label(ss,"Đang kiểm tra cảm biến...",10,27,288,&ui_font_11_bold,C_GREEN);
  showDisplayPage(defaultDisplayPage);
}

float heatIndexC(float t,float rh){if(t<27)return t;float f=t*9/5+32;float hi=-42.379+2.04901523*f+10.14333127*rh-0.22475541*f*rh-0.00683783*f*f-0.05481717*rh*rh+0.00122874*f*f*rh+0.00085282*f*rh*rh-0.00000199*f*f*rh*rh;return(hi-32)*5/9;}
String compactPrice(float price);

void updateDateTime(const tm &now){
  static const char *days[]={"CHỦ NHẬT","THỨ HAI","THỨ BA","THỨ TƯ","THỨ NĂM","THỨ SÁU","THỨ BẢY"};
  char h[3],m[3];snprintf(h,sizeof(h),"%02d",now.tm_hour);snprintf(m,sizeof(m),"%02d",now.tm_min);lv_label_set_text(lblHour,h);lv_label_set_text(lblMinute,m);lv_obj_set_style_opa(lblColon,(now.tm_sec&1)?LV_OPA_20:LV_OPA_COVER,0);
  if(now.tm_mday!=previousDay){previousDay=now.tm_mday;if(!isnan(temperature)){dailyMinTemp=temperature;dailyMaxTemp=temperature;}int ld,lm,ly;bool leap;solarToLunar(now.tm_mday,now.tm_mon+1,now.tm_year+1900,ld,lm,ly,leap);char text[64];snprintf(text,sizeof(text),"%s · %02d/%02d · %02d/%02d ÂL%s",days[now.tm_wday],now.tm_mday,now.tm_mon+1,ld,lm,leap?"*":"");lv_label_set_text(lblDate,text);}
}

void updateSensorUI(){
  char b[24];if(ahtOK&&!isnan(temperature)){snprintf(b,sizeof(b),"%.1f°C",temperature);lv_label_set_text(lblIndoorTemp,b);lv_label_set_text(sensorTempBig,b);snprintf(b,sizeof(b),"%.1f°C",heatIndexC(temperature,humidity));lv_label_set_text(lblFeels,b);lv_label_set_text(sensorFeelBig,b);snprintf(b,sizeof(b),"%.1f°C",dailyMaxTemp);lv_label_set_text(lblMax,b);snprintf(b,sizeof(b),"%.1f°",dailyMaxTemp);lv_label_set_text(sensorMaxBig,b);snprintf(b,sizeof(b),"%.1f°C",dailyMinTemp);lv_label_set_text(lblMin,b);snprintf(b,sizeof(b),"%.1f°",dailyMinTemp);lv_label_set_text(sensorMinBig,b);snprintf(b,sizeof(b),"%.0f%%",humidity);lv_label_set_text(lblHumidity,b);lv_label_set_text(sensorHumBig,b);lv_label_set_text(sensorState,humidity>=40&&humidity<=70?"✓ KHÔNG KHÍ DỄ CHỊU":(humidity>70?"! ĐỘ ẨM CAO - NÊN THÔNG GIÓ":"! KHÔNG KHÍ KHÔ - NÊN TẠO ẨM"));}else{lv_label_set_text(lblIndoorTemp,"--.-°C");lv_label_set_text(lblHumidity,"--%");lv_label_set_text(sensorTempBig,"--.-°C");lv_label_set_text(sensorHumBig,"--%");lv_label_set_text(sensorState,"! KHÔNG TÌM THẤY AHT10");}
}

void updateWeatherUI(){
  xSemaphoreTake(dataMutex,portMAX_DELAY);char b[32];
  if(weatherOK){snprintf(b,sizeof(b),"%.0f°C",outdoorTemp);lv_label_set_text(lblOutdoor,b);lv_label_set_text(lblWeatherText,weatherLabel(currentWeatherCode).c_str());snprintf(b,sizeof(b),"%.0f km/h",windSpeed);lv_label_set_text(lblWind,b);lv_label_set_text(lblFooterWind,b);snprintf(b,sizeof(b),"%.0f hPa",surfacePressure);lv_label_set_text(lblPressure,b);snprintf(b,sizeof(b),"%.0f km",visibilityKm);lv_label_set_text(lblVisibility,b);snprintf(b,sizeof(b),"%d%%",forecast[0].rain);lv_label_set_text(lblFooterRain,b);drawWeatherIcon(currentWeatherCode);for(uint8_t i=0;i<7;i++){lv_label_set_text(forecastDateLabel[i],shortForecastDate(forecast[i].date).c_str());lv_label_set_text(forecastWeatherLabel[i],weatherLabel(forecast[i].code).c_str());snprintf(b,sizeof(b),"%.0f° / %.0f°",forecast[i].minTemp,forecast[i].maxTemp);lv_label_set_text(forecastTempLabel[i],b);snprintf(b,sizeof(b),"%d%%",forecast[i].rain);lv_label_set_text(forecastRainLabel[i],b);drawForecastIcon(i,forecast[i].code);}}
  for(uint8_t i=0;i<3;i++){if(i<cryptoCount){lv_label_set_text(cryptoSymbolLabel[i],crypto[i].symbol.c_str());if(crypto[i].valid){lv_label_set_text(cryptoPriceLabel[i],compactPrice(crypto[i].price).c_str());snprintf(b,sizeof(b),"%+.1f%%",crypto[i].change);lv_label_set_text(cryptoChangeLabel[i],b);lv_obj_set_style_text_color(cryptoChangeLabel[i],lv_color_hex(crypto[i].change>=0?C_GREEN:C_RED),0);}else{lv_label_set_text(cryptoPriceLabel[i],"Chờ dữ liệu");lv_label_set_text(cryptoChangeLabel[i],"--%");}}else{lv_label_set_text(cryptoSymbolLabel[i],"---");lv_label_set_text(cryptoPriceLabel[i],"Chưa cấu hình");lv_label_set_text(cryptoChangeLabel[i],"--%");}}
  xSemaphoreGive(dataMutex);
}

String compactPrice(float price){
  if(price>=1000000)return "$"+String(price/1000000.0f,2)+"M";
  if(price>=1000)return "$"+String(price/1000.0f,price>=10000?1:2)+"K";
  if(price>=1)return "$"+String(price,price>=100?1:2);
  return "$"+String(price,4);
}

String tickerMessage(uint8_t page){
  if(!ahtOK)return "! KIỂM TRA AHT10";if(portalMode)return "! CÀI ĐẶT WI-FI";if(WiFi.status()!=WL_CONNECTED)return "! WI-FI NGOẠI TUYẾN";
  uint8_t slots=3+cryptoCount,slot=page%max((uint8_t)3,slots);
  if(slot>=3&&crypto[slot-3].valid){CryptoData &c=crypto[slot-3];return c.symbol+" "+compactPrice(c.price)+" · "+(c.change>=0?"+":"")+String(c.change,1)+"%";}
  if(slot==0&&weatherOK)return weatherLabel(currentWeatherCode)+" · "+String(outdoorTemp,0)+"°C";
  if(slot==1){if(temperature>30)return "NHIỆT ĐỘ HƠI CAO";if(humidity>70)return "ĐỘ ẨM ĐANG CAO";return "✓ TRONG NHÀ DỄ CHỊU";}
  if(weatherOK){tm now;getLocalTime(&now,5);String event=now.tm_hour<12?forecast[0].sunrise:forecast[0].sunset;int p=event.indexOf('T');if(p>=0)event=event.substring(p+1,p+6);return String(now.tm_hour<12?"BÌNH MINH · ":"HOÀNG HÔN · ")+event;}
  return "✓ HỆ THỐNG SẴN SÀNG";
}

void updateTicker(){xSemaphoreTake(dataMutex,portMAX_DELAY);String msg=tickerMessage(tickerPage++);xSemaphoreGive(dataMutex);lv_label_set_text(lblTicker,msg.c_str());uint32_t c=C_TEXT;if(msg.startsWith("!"))c=C_RED;else if(msg.indexOf(" · -")>=0)c=C_RED;else if(msg.indexOf("+")>=0||msg.indexOf("DỄ CHỊU")>=0||msg.startsWith("✓"))c=C_GREEN;lv_obj_set_style_text_color(lblTicker,lv_color_hex(c),0);}

// ===== WEB CAI DAT =====
String weatherEmoji(int code){
  if(code==0)return "☀️";if(code==1||code==2)return "🌤️";if(code==3)return "☁️";
  if(code==45||code==48)return "🌫️";if((code>=51&&code<=67)||(code>=80&&code<=82))return "🌧️";
  if((code>=71&&code<=77)||code==85||code==86)return "🌨️";if(code>=95&&code<=99)return "⛈️";return "🌡️";
}

String forecastRowsHtml(){
  if(!weatherOK)return "<div class='empty'>Chưa có dữ liệu. Hãy kiểm tra Internet hoặc lưu lại vị trí.</div>";
  String rows="<div class='forecast'><div class='fhead'><span>Ngày</span><span>Thời tiết</span><span>Nhiệt độ</span><span>Mưa</span><span>Mặt trời</span></div>";
  xSemaphoreTake(dataMutex,portMAX_DELAY);
  for(int i=0;i<7;i++){
    String rise=forecast[i].sunrise,sett=forecast[i].sunset;int p=rise.indexOf('T');if(p>=0)rise=rise.substring(p+1,p+6);p=sett.indexOf('T');if(p>=0)sett=sett.substring(p+1,p+6);
    rows+="<div class='frow'><b>"+forecast[i].date+"</b><span class='condition'>"+weatherEmoji(forecast[i].code)+" "+weatherLabel(forecast[i].code)+"</span><span>"+String(forecast[i].minTemp,0)+"–"+String(forecast[i].maxTemp,0)+"°C</span><span>"+String(forecast[i].rain)+"%</span><span>"+rise+" / "+sett+"</span></div>";
  }
  xSemaphoreGive(dataMutex);return rows+"</div>";
}

String settingsPage(){
  String networks;
  // Reserve the complete dashboard once to reduce heap fragmentation.
  String page;page.reserve(26000);
  page=R"HTML(<!doctype html><html lang="vi"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Smart Clock Control</title><style>
*{box-sizing:border-box}body{margin:0;background:#edf0f4;color:#1e2430;font:14px Inter,system-ui,-apple-system,"Segoe UI",sans-serif}.shell{min-height:100vh;display:grid;grid-template-columns:230px minmax(0,1fr)}.side{position:sticky;top:0;height:100vh;padding:24px 16px;background:#111827;color:#fff}.brand{display:flex;gap:12px;align-items:center;padding:0 8px 24px}.logo{display:grid;place-items:center;width:40px;height:40px;border-radius:12px;background:linear-gradient(145deg,#4e80ff,#55c8e8);font-weight:900}.brand b{font-size:16px}.brand small{display:block;color:#9ca7b8;margin-top:3px}.tabs{display:grid;gap:6px}.tab{border:0;background:transparent;color:#aeb8c7;border-radius:11px;padding:12px 13px;text-align:left;font-weight:750;cursor:pointer}.tab:hover{background:#ffffff0d;color:#fff}.tab.on{background:#ffffff16;color:#fff;box-shadow:inset 3px 0 #5f8cff}.sidefoot{position:absolute;left:24px;right:24px;bottom:22px;color:#8f9aad;font-size:11px;line-height:1.6}.main{min-width:0}.top{display:flex;align-items:center;justify-content:space-between;padding:22px 28px;background:#fff;border-bottom:1px solid #dde2e8}.top h1{margin:0;font-size:22px}.muted{color:#747d8b}.live{display:flex;align-items:center;gap:8px;padding:9px 13px;border:1px solid #ccebdc;background:#effaf5;color:#087b46;border-radius:999px;font-weight:800}.dot{width:8px;height:8px;border-radius:50%;background:#0aa45a;box-shadow:0 0 0 4px #0aa45a1d}.content{max-width:1060px;padding:24px 28px 42px}.summary{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin-bottom:18px}.stat{background:#fff;border:1px solid #dce1e7;border-radius:14px;padding:14px 15px}.stat small{display:block;color:#7b8491;font-weight:750;margin-bottom:5px}.stat b{font-size:15px}.panel{display:none}.panel.on{display:block}.box{background:#fff;border:1px solid #dce1e7;border-radius:17px;padding:20px;margin-bottom:16px;box-shadow:0 8px 30px #25324a08}.title{font-size:17px;font-weight:850}.lead{margin:5px 0 18px;color:#747d8b}.grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}.grid3{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}label{display:block;color:#67717f;font-size:11px;font-weight:850;letter-spacing:.065em;margin:13px 0 7px}input,select{width:100%;border:1px solid #ccd3dc;background:#fff;color:#202631;border-radius:11px;padding:12px 13px;outline:none;font:inherit}input:focus,select:focus{border-color:#4e80ff;box-shadow:0 0 0 3px #4e80ff20}input[type=color]{height:46px;padding:4px}input[type=range]{padding:0;accent-color:#4e80ff}input[type=checkbox]{width:18px;height:18px;margin:0;accent-color:#4e80ff}.coloritem{padding:12px;border:1px solid #e0e4e9;border-radius:13px;background:#fafbfc}.checkgrid{display:grid;grid-template-columns:repeat(2,1fr);gap:10px;margin:12px 0 16px}.check{display:flex;align-items:center;gap:10px;margin:0;padding:12px;border:1px solid #dde2e8;border-radius:11px;background:#fafbfc;color:#29313d;font-size:13px;letter-spacing:0}.btn{border:0;border-radius:11px;padding:12px 17px;background:#4e80ff;color:#fff;font-weight:850;cursor:pointer}.btn.secondary{background:#e9efff;color:#356fe0}.actionbar{display:flex;justify-content:space-between;align-items:center;position:sticky;bottom:14px;padding:13px 14px;background:#ffffffed;border:1px solid #d9dee5;border-radius:14px;box-shadow:0 13px 38px #25324a1c;backdrop-filter:blur(10px)}.status{padding:12px 13px;border-radius:11px;background:#edf8f3;color:#087b46;margin-top:14px}.devicepreview{display:grid;grid-template-columns:1.3fr 1fr;gap:10px;padding:13px;border-radius:15px;background:#e7e8ea;border:1px solid #c8cbd0}.previewhero{display:grid;place-items:center;min-height:94px;background:#f5f6f7;border:1px solid %CWEATHER%;border-radius:11px;font-weight:900;font-size:38px;color:%CHOUR%}.previewhero i{color:%CCOLON%;font-style:normal}.previewcards{display:grid;gap:8px}.previewcards div{display:grid;align-content:center;padding:10px;background:#f5f6f7;border:1px solid #c8cbd0;border-radius:10px}.previewcards small{color:#747d8b}.previewcards b{font-size:19px}.forecast{margin-top:18px;border:1px solid #e0e4e9;border-radius:13px;overflow:hidden}.fhead,.frow{display:grid;grid-template-columns:1.05fr 1.55fr 1fr .7fr 1fr;gap:8px;align-items:center;padding:11px 12px}.fhead{background:#f5f7fa;color:#747d8b;font-size:11px;font-weight:850;text-transform:uppercase}.frow{border-top:1px solid #e5e8ec}.condition{white-space:nowrap}.empty{margin-top:16px;padding:18px;text-align:center;border:1px dashed #cbd2db;border-radius:12px;color:#747d8b}.upload{border:1px dashed #aeb8c5;border-radius:14px;padding:24px;text-align:center;background:#fafbfc}.progress{height:9px;border-radius:999px;background:#e3e7ec;overflow:hidden;margin:16px 0 8px}.progress span{display:block;width:0;height:100%;background:linear-gradient(90deg,#4e80ff,#55c8e8);transition:width .2s}.note{font-size:12px;color:#7b8491}.coord{display:inline-flex;align-items:center;gap:6px;padding:7px 10px;border-radius:9px;background:#f0f4fb;color:#4c6383;font-size:12px;font-weight:700;margin-top:10px}@media(max-width:780px){.shell{display:block}.side{position:static;height:auto;padding:14px}.brand{padding:4px 4px 12px}.tabs{display:flex;overflow:auto}.tab{white-space:nowrap}.sidefoot{display:none}.top{padding:17px}.content{padding:16px}.summary{grid-template-columns:1fr}.grid,.grid3,.devicepreview,.checkgrid{grid-template-columns:1fr}.fhead{display:none}.frow{grid-template-columns:1fr 1fr}.actionbar{bottom:8px}.live{font-size:12px}}
</style></head><body><div class="shell"><aside class="side"><div class="brand"><div class="logo">SC</div><div><b>Smart Clock</b><small>LVGL Control Center</small></div></div><nav class="tabs"><button type="button" class="tab on" data-p="display">Hiển thị</button><button type="button" class="tab" data-p="sensor">Cảm biến</button><button type="button" class="tab" data-p="weather">Thời tiết 7 ngày</button><button type="button" class="tab" data-p="crypto">Crypto</button><button type="button" class="tab" data-p="network">Kết nối</button><button type="button" class="tab" data-p="system">Hệ thống</button><button type="button" class="tab" data-p="firmware">Cập nhật firmware</button></nav><div class="sidefoot">ESP32 DevKit V1<br>ILI9341 · 320×240 ngang</div></aside><div class="main"><header class="top"><div><h1>Bảng điều khiển</h1><div class="muted">Tinh chỉnh màn hình và dữ liệu theo thời gian thực</div></div><div class="live"><span class="dot"></span>%IP%</div></header><main class="content"><div class="summary"><div class="stat"><small>WI-FI</small><b>%NETSTATUS%</b></div><div class="stat"><small>CẢM BIẾN</small><b>%AHTSTATUS%</b></div><div class="stat"><small>THỜI TIẾT</small><b>%WXSTATUS%</b></div></div><form id="settings" method="POST" action="/save">
<section id="display" class="panel on"><div class="box"><div class="title">Giao diện màn hình</div><p class="lead">Bố cục ngang 320×240, viền bo tĩnh, dấu hai chấm nhấp nháy và icon WMO theo thời tiết thật.</p><div class="devicepreview"><div class="previewhero">22<i>:</i>57</div><div class="previewcards"><div><small>NHIỆT ĐỘ</small><b style="color:%CTEMP%">29.8°C</b></div><div><small>ĐỘ ẨM</small><b style="color:%CHUM%">64%</b></div></div></div><div class="grid3"><div class="coloritem"><label>MÀU NGÀY / ÂM LỊCH</label><input type="color" name="cdate" value="%CDATE%"></div><div class="coloritem"><label>MÀU GIỜ</label><input type="color" name="chour" value="%CHOUR%"></div><div class="coloritem"><label>MÀU PHÚT</label><input type="color" name="cminute" value="%CMINUTE%"></div><div class="coloritem"><label>MÀU DẤU HAI CHẤM</label><input type="color" name="ccolon" value="%CCOLON%"></div><div class="coloritem"><label>MÀU THỜI TIẾT</label><input type="color" name="cweather" value="%CWEATHER%"></div><div class="coloritem"><label>MÀU NHIỆT ĐỘ</label><input type="color" name="ctemp" value="%CTEMP%"></div><div class="coloritem"><label>MÀU ĐỘ ẨM</label><input type="color" name="chum" value="%CHUM%"></div></div><label>THỜI GIAN CHUYỂN NỘI DUNG FOOTER</label><input id="ticker" type="range" min="2" max="15" name="ticker" value="%TICKER%"><div id="tv" class="coord">%TICKER% giây / mục</div></div><div class="box"><div class="title">Trang trình chiếu trên TFT</div><p class="lead">Chọn các trang được phép hiển thị. Trang Dự báo luôn trình bày đủ cả 7 ngày trên một màn hình.</p><div class="checkgrid"><label class="check"><input type="checkbox" name="page0" %PAGE0%> Tổng quan</label><label class="check"><input type="checkbox" name="page1" %PAGE1%> Dự báo đủ 7 ngày</label><label class="check"><input type="checkbox" name="page2" %PAGE2%> Crypto</label><label class="check"><input type="checkbox" name="page3" %PAGE3%> Cảm biến AHT10</label><label class="check"><input type="checkbox" name="autorotate" %AUTOROTATE%> Tự động chuyển trang</label></div><div class="grid"><div><label>TRANG HIỂN THỊ KHI KHỞI ĐỘNG</label><select name="defaultpage"><option value="0" %DEF0%>Tổng quan</option><option value="1" %DEF1%>Dự báo 7 ngày</option><option value="2" %DEF2%>Crypto</option><option value="3" %DEF3%>Cảm biến</option></select></div><div><label>THỜI GIAN MỖI TRANG</label><input id="pagespeed" type="range" min="5" max="60" name="pagesecs" value="%PAGESECS%"><div id="pv" class="coord">%PAGESECS% giây / trang</div></div></div></div></section>
<section id="displayActions" class="panel on"><div class="box"><div class="title">Điều khiển màn hình ngay</div><p class="lead">Chạm một nút để TFT chuyển trang lập tức, không khởi động lại.</p><div style="display:flex;flex-wrap:wrap;gap:9px"><button class="btn secondary showpage" type="button" data-page="0">TỔNG QUAN</button><button class="btn secondary showpage" type="button" data-page="1">DỰ BÁO 7 NGÀY</button><button class="btn secondary showpage" type="button" data-page="2">CRYPTO</button><button class="btn secondary showpage" type="button" data-page="3">CẢM BIẾN</button><button class="btn" id="toggleRotate" type="button">%ROTATEBUTTON%</button></div><div id="displaymsg" class="coord">Sẵn sàng điều khiển TFT</div></div></section>
<section id="displayMirror" class="panel on"><div class="box"><div class="title">Xem trước đúng màn hình TFT</div><p class="lead">Khung dưới đây đúng tỷ lệ 320×240. Màu sắc thay đổi ngay khi chỉnh, trước khi lưu.</p><div style="overflow:auto;padding:12px;border-radius:14px;background:#15191e"><div id="tftMirror" style="position:relative;width:320px;height:240px;margin:auto;background:#e7e8ea;color:#20242a;font-family:Arial,sans-serif;overflow:hidden"><b id="pvLocation" style="position:absolute;left:5px;top:4px;width:58px;font-size:11px;color:%CWEATHER%;white-space:nowrap;overflow:hidden">• HÀ NỘI</b><b id="pvDate" style="position:absolute;left:63px;top:2px;width:229px;text-align:center;font-size:14px;color:%CDATE%">THỨ HAI · 03/08 · 21/06 ÂL</b><b style="position:absolute;right:7px;top:2px;font-size:17px;color:#0aa45a">⌁</b><div id="pvHero" style="position:absolute;left:6px;top:24px;width:308px;height:83px;border:1px solid %CWEATHER%;border-radius:9px;background:#f5f6f7"><div style="position:absolute;left:2px;top:13px;width:164px;height:60px;display:flex;align-items:center;justify-content:center;font-size:52px;font-weight:900;letter-spacing:-3px"><span id="pvHour" style="color:%CHOUR%">22</span><span id="pvColon" style="width:25px;text-align:center;color:%CCOLON%">:</span><span id="pvMinute" style="color:%CMINUTE%">57</span></div><div style="position:absolute;left:169px;top:8px;width:1px;height:65px;background:#c8cbd0"></div><span style="position:absolute;left:178px;top:8px;font-size:11px;color:#777d86">NGOÀI TRỜI</span><b id="pvOutside" style="position:absolute;left:177px;top:23px;font-size:28px;color:%CWEATHER%">31°C</b><span style="position:absolute;left:177px;top:56px;font-size:11px;color:#777d86">Mưa rào · 68%</span><span style="position:absolute;right:10px;top:18px;font-size:35px">🌦️</span></div><span style="position:absolute;left:6px;top:109px;font-size:11px;color:#777d86">CHỈ SỐ MÔI TRƯỜNG</span><div id="pvTempCard" style="position:absolute;left:6px;top:124px;width:151px;height:78px;padding:6px 9px;border:1px solid %CTEMP%;border-radius:8px;background:#f5f6f7"><b style="font-size:11px;color:#777d86">NHIỆT ĐỘ</b><b id="pvTemp" style="display:block;margin-top:-2px;font-size:28px;color:%CTEMP%">30.9°C</b><div style="font-size:11px;line-height:15px">• Cảm nhận <b style="float:right">36.2°C</b><br>↑ Cao nhất <b style="float:right">31.9°C</b><br><span style="color:#0aa45a">↓</span> Thấp nhất <b style="float:right">30.9°C</b></div></div><div id="pvHumCard" style="position:absolute;left:163px;top:124px;width:151px;height:78px;padding:6px 9px;border:1px solid %CHUM%;border-radius:8px;background:#f5f6f7"><b style="font-size:11px;color:#777d86">ĐỘ ẨM</b><b id="pvHum" style="display:block;margin-top:-2px;font-size:28px;color:%CHUM%">65%</b><div style="font-size:11px;line-height:15px">≈ Gió <b style="float:right">13 km/h</b><br>◉ Áp suất <b style="float:right">1008 hPa</b><br>⊙ Tầm nhìn <b style="float:right">10 km</b></div></div><div style="position:absolute;left:6px;top:205px;width:308px;height:29px;border:1px solid #c8cbd0;border-radius:8px;background:#f5f6f7;display:grid;grid-template-columns:77px 77px 1fr;text-align:center;font-size:10px;overflow:hidden"><div style="padding-top:2px;border-right:1px solid #c8cbd0;color:#777d86">GIÓ<br><b style="color:#20242a">13 km/h</b></div><div style="padding-top:2px;border-right:1px solid #c8cbd0;color:#777d86">MƯA<br><b style="color:#20242a">68%</b></div><b style="padding-top:8px;color:#0aa45a">✓ KHÔNG KHÍ DỄ CHỊU</b></div></div></div></div></section>
<section id="sensor" class="panel"><div class="box"><div class="title">Hiệu chuẩn cảm biến AHT10</div><p class="lead">Dùng số âm để giảm và số dương để tăng kết quả đo.</p><div class="grid"><div><label>BÙ NHIỆT ĐỘ (°C)</label><input type="number" step="0.1" min="-20" max="20" name="toff" value="%TOFF%"></div><div><label>BÙ ĐỘ ẨM (%)</label><input type="number" step="0.1" min="-50" max="50" name="hoff" value="%HOFF%"></div></div><div class="status">%AHTDETAIL%</div></div></section>
<section id="weather" class="panel"><div class="box"><div class="title">Vị trí và dự báo 7 ngày</div><p class="lead">Nhập địa danh hoặc tọa độ. Khi có tọa độ, hệ thống ưu tiên tọa độ để dự báo chính xác hơn.</p><label>TÊN KHU VỰC HIỂN THỊ</label><input id="loc" name="location" value="%LOCATION%" placeholder="Ví dụ: Hà Nội, Đà Nẵng, Quận 1"><div class="grid"><div><label>VĨ ĐỘ</label><input id="lat" name="lat" type="number" min="-90" max="90" step="0.000001" value="%LAT%"></div><div><label>KINH ĐỘ</label><input id="lon" name="lon" type="number" min="-180" max="180" step="0.000001" value="%LON%"></div></div><button class="btn secondary" id="gps" type="button" style="margin-top:14px">LẤY VỊ TRÍ CỦA THIẾT BỊ</button><div id="gpsmsg" class="coord">%COORDSTATUS%</div>%FORECAST%</div></section>
<section id="weatherActions" class="panel"><div class="box"><div class="title">Cập nhật trực tiếp</div><p class="lead">ESP32 sẽ dùng GPS khi trình duyệt cho phép. Nếu GPS bị chặn trên địa chỉ HTTP nội bộ, thiết bị sẽ cập nhật chính xác theo địa danh đã nhập ở trên.</p><button class="btn" id="weatherNow" type="button">CẬP NHẬT THỜI TIẾT NGAY</button><div id="weathermsg" class="coord">Chưa gửi yêu cầu cập nhật</div></div></section>
<section id="crypto" class="panel"><div class="box"><div class="title">Danh mục Crypto</div><p class="lead">Nhập tối đa 3 mã coin. Giá USDT và biến động 24 giờ sẽ chạy luân phiên ở nửa phải footer.</p><label>MÃ COIN, CÁCH NHAU BẰNG DẤU PHẨY</label><input name="crypto" maxlength="32" list="coins" value="%CRYPTO%" placeholder="BTC,ETH,SOL"><datalist id="coins"><option value="BTC,ETH,BNB"><option value="BTC,ETH,SOL"><option value="BTC,DOGE,XRP"><option value="BNB,SOL,ADA"></datalist><div class="status">Nguồn Binance · cập nhật mỗi 60 giây · tối đa 3 mã</div></div></section>
<section id="network" class="panel"><div class="box"><div class="title">Kết nối Wi-Fi</div><p class="lead">Trang mở ngay, chỉ quét mạng khi bạn bấm nút bên dưới.</p><label>TÊN MẠNG</label><input name="ssid" list="networks" value="%SSID%"><datalist id="networks">%NETWORKS%</datalist><button class="btn secondary" id="scanwifi" type="button" style="margin-top:12px">QUÉT MẠNG WI-FI</button><div id="scanmsg" class="coord">Chưa quét · có thể nhập SSID thủ công</div><label>MẬT KHẨU MỚI</label><input type="password" name="pass" autocomplete="new-password" placeholder="Để trống nếu giữ mật khẩu hiện tại"><div class="status">Nếu kết nối thất bại, thiết bị tự phát AP ESP32-SmartClock.</div></div></section>
<section id="system" class="panel"><div class="box"><div class="title">Thiết lập hệ thống</div><div class="grid"><div><label>TÊN THIẾT BỊ</label><input name="name" maxlength="20" value="%NAME%"></div><div><label>MÚI GIỜ UTC</label><input type="number" min="-12" max="14" name="tz" value="%TZ%"></div></div><div class="status">Thời gian đồng bộ NTP khi có Internet; mở trang này cũng đồng bộ từ điện thoại/máy tính.</div></div></section>
<div id="actionbar" class="actionbar"><span class="muted">Thay đổi có hiệu lực sau khi ESP32 khởi động lại.</span><button class="btn" type="submit">LƯU & KHỞI ĐỘNG LẠI</button></div></form>
<section id="firmware" class="panel"><div class="box"><div class="title">Cập nhật firmware OTA</div><p class="lead">Chọn file .bin xuất từ Arduino IDE. Không ngắt nguồn trong quá trình cập nhật.</p><form id="ota" class="upload" method="POST" action="/update" enctype="multipart/form-data"><input type="file" name="firmware" accept=".bin,application/octet-stream" required><div class="progress"><span id="bar"></span></div><div id="otamsg" class="note">Sẵn sàng nhận firmware.</div><br><button class="btn">TẢI LÊN FIRMWARE</button></form><p class="note">Partition Scheme cần hỗ trợ OTA; không chọn Huge APP.</p></div><div class="box"><div class="title">Cập nhật trực tiếp từ GitHub</div><p class="lead">Repo phát hành đã được cố định. Mỗi Release mới chỉ cần có asset tên firmware.bin.</p><label>LINK FILE FIRMWARE .BIN</label><input id="githubUrl" type="url" value="https://github.com/ledinhtien219/dong-ho-chu-nhat/releases/latest/download/firmware.bin"><button class="btn" id="githubUpdate" type="button" style="margin-top:14px">TẢI BẢN MỚI NHẤT TỪ GITHUB</button><div id="githubmsg" class="status">Nguồn: ledinhtien219/dong-ho-chu-nhat · asset firmware.bin</div></div></section></main></div></div>
<script>
const tabs=[...document.querySelectorAll('.tab')],panels=[...document.querySelectorAll('.panel')],action=document.getElementById('actionbar');
function openPanel(id){tabs.forEach(x=>x.classList.toggle('on',x.dataset.p===id));panels.forEach(x=>x.classList.toggle('on',x.id===id||(id==='display'&&(x.id==='displayActions'||x.id==='displayMirror'))||(id==='weather'&&x.id==='weatherActions')));action.style.display=id==='firmware'?'none':'flex'}
tabs.forEach(b=>b.addEventListener('click',()=>openPanel(b.dataset.p)));
const ticker=document.getElementById('ticker'),tickerValue=document.getElementById('tv');ticker.addEventListener('input',()=>tickerValue.textContent=ticker.value+' giây / mục');
const pageSpeed=document.getElementById('pagespeed'),pageValue=document.getElementById('pv');pageSpeed.addEventListener('input',()=>pageValue.textContent=pageSpeed.value+' giây / trang');
document.querySelector('.devicepreview').style.display='none';const colorMap={cdate:['pvDate','color'],chour:['pvHour','color'],cminute:['pvMinute','color'],ccolon:['pvColon','color'],cweather:['pvLocation','color','pvOutside','color','pvHero','borderColor'],ctemp:['pvTemp','color','pvTempCard','borderColor'],chum:['pvHum','color','pvHumCard','borderColor']};Object.entries(colorMap).forEach(([name,map])=>{const input=document.querySelector('[name="'+name+'"]');input.addEventListener('input',()=>{for(let i=0;i<map.length;i+=2)document.getElementById(map[i]).style[map[i+1]]=input.value})});
const gps=document.getElementById('gps'),gpsMessage=document.getElementById('gpsmsg'),latInput=document.getElementById('lat'),lonInput=document.getElementById('lon'),locationInput=document.getElementById('loc');
gps.addEventListener('click',()=>{if(!navigator.geolocation){gpsMessage.textContent='Trình duyệt không hỗ trợ định vị. Hãy nhập tọa độ.';return}gpsMessage.textContent='Đang lấy vị trí...';navigator.geolocation.getCurrentPosition(p=>{latInput.value=p.coords.latitude.toFixed(6);lonInput.value=p.coords.longitude.toFixed(6);gpsMessage.textContent='Đã lấy GPS · sai số khoảng '+Math.round(p.coords.accuracy)+' m'},()=>gpsMessage.textContent='Không lấy được GPS. Hãy nhập tọa độ thủ công.',{enableHighAccuracy:true,timeout:12000,maximumAge:30000})});
locationInput.addEventListener('input',()=>{latInput.value='';lonInput.value='';document.getElementById('pvLocation').textContent='• '+locationInput.value.toUpperCase();gpsMessage.textContent='Đã chuyển sang tìm theo tên địa danh.'});
document.getElementById('pvLocation').textContent='• '+locationInput.value.toUpperCase();
const displayMessage=document.getElementById('displaymsg'),rotateButton=document.getElementById('toggleRotate');let rotateOn=%ROTATEJS%;document.querySelectorAll('.showpage').forEach(b=>b.addEventListener('click',async()=>{displayMessage.textContent='Đang chuyển trang...';try{const r=await fetch('/display?page='+b.dataset.page,{method:'POST'});displayMessage.textContent=r.ok?'TFT đã chuyển sang '+b.textContent.trim()+'.':'Không chuyển được trang.'}catch(e){displayMessage.textContent='Mất kết nối với ESP32.'}}));rotateButton.addEventListener('click',async()=>{rotateOn=!rotateOn;try{const r=await fetch('/display?rotate='+(rotateOn?1:0),{method:'POST'});if(!r.ok)throw 0;rotateButton.textContent=rotateOn?'DỪNG CHUYỂN TRANG':'TIẾP TỤC CHUYỂN TRANG';displayMessage.textContent=rotateOn?'Đã bật tự động chuyển trang.':'Đã dừng tự động chuyển trang.'}catch(e){rotateOn=!rotateOn;displayMessage.textContent='Không thay đổi được chế độ.'}});
const weatherNow=document.getElementById('weatherNow'),weatherMessage=document.getElementById('weathermsg');async function requestWeather(position){try{const place=locationInput.value.trim();const data=position?{lat:position.coords.latitude.toFixed(6),lon:position.coords.longitude.toFixed(6),location:place}:{location:place};if(position){latInput.value=data.lat;lonInput.value=data.lon}const r=await fetch('/weather-now',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});weatherMessage.textContent=r.ok?(position?'Đã nhận GPS chính xác. ESP32 đang tải dữ liệu mới...':'GPS bị chặn bởi HTTP; đang cập nhật theo địa danh “'+place+'”...'):'Không thể cập nhật thời tiết.'}catch(e){weatherMessage.textContent='Mất kết nối với ESP32.'}weatherNow.disabled=false}weatherNow.addEventListener('click',()=>{weatherNow.disabled=true;if(!window.isSecureContext||!navigator.geolocation){requestWeather(null);return}weatherMessage.textContent='Đang lấy GPS điện thoại...';navigator.geolocation.getCurrentPosition(p=>requestWeather(p),()=>requestWeather(null),{enableHighAccuracy:true,timeout:15000,maximumAge:10000})});
const scanButton=document.getElementById('scanwifi'),scanMessage=document.getElementById('scanmsg'),networkList=document.getElementById('networks');scanButton.addEventListener('click',async()=>{scanButton.disabled=true;scanMessage.textContent='Đang quét mạng Wi-Fi...';try{for(let i=0;i<15;i++){const response=await fetch('/scan?t='+Date.now(),{cache:'no-store'});if(response.status===200){const list=await response.json();networkList.innerHTML='';list.forEach(n=>{const o=document.createElement('option');o.value=n.ssid;o.label=n.ssid+' ('+n.rssi+' dBm)';networkList.appendChild(o)});scanMessage.textContent=list.length?'Đã tìm thấy '+list.length+' mạng.':'Không tìm thấy mạng Wi-Fi.';scanButton.disabled=false;return}await new Promise(r=>setTimeout(r,1000))}scanMessage.textContent='Quét quá thời gian. Hãy thử lại.'}catch(e){scanMessage.textContent='Không nhận được kết quả quét.'}scanButton.disabled=false});
const ota=document.getElementById('ota'),bar=document.getElementById('bar'),otaMessage=document.getElementById('otamsg');ota.addEventListener('submit',e=>{e.preventDefault();const x=new XMLHttpRequest();x.open('POST','/update');x.upload.onprogress=p=>{if(p.lengthComputable){const v=Math.round(p.loaded*100/p.total);bar.style.width=v+'%';otaMessage.textContent='Đang tải lên '+v+'%'}};x.onload=()=>{otaMessage.textContent=x.status===200?'Cập nhật thành công, ESP32 đang khởi động lại...':'Cập nhật thất bại. Hãy kiểm tra file .bin.'};x.onerror=()=>otaMessage.textContent='Mất kết nối trong khi tải firmware.';x.send(new FormData(ota))});
const githubUrl=document.getElementById('githubUrl'),githubButton=document.getElementById('githubUpdate'),githubMessage=document.getElementById('githubmsg');githubButton.addEventListener('click',async()=>{const url=githubUrl.value.trim();if(!/^https:\/\/(github\.com|raw\.githubusercontent\.com)\/.+\.bin(?:\?.*)?$/i.test(url)){githubMessage.textContent='Link chưa đúng. Hãy dùng link HTTPS trực tiếp tới file .bin.';return}if(!confirm('Cập nhật firmware từ GitHub ngay bây giờ?'))return;githubButton.disabled=true;githubMessage.textContent='ESP32 đang tải file từ GitHub. Không tắt nguồn...';try{const r=await fetch('/update-url',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({url})});const text=await r.text();githubMessage.textContent=r.ok?'Cập nhật thành công, ESP32 đang khởi động lại...':'Cập nhật lỗi: '+text}catch(e){githubMessage.textContent='Kết nối đã đóng. Nếu ESP32 khởi động lại thì cập nhật đã thành công.'}githubButton.disabled=false});
fetch('/time',{method:'POST',headers:{'Content-Type':'text/plain'},body:String(Math.floor(Date.now()/1000))}).catch(()=>{});
</script></body></html>)HTML";
  page.replace("%IP%",portalMode?WiFi.softAPIP().toString():WiFi.localIP().toString());page.replace("%NETSTATUS%",WiFi.status()==WL_CONNECTED?"Đã kết nối":(portalMode?"AP cài đặt":"Ngoại tuyến"));page.replace("%AHTSTATUS%",ahtOK?"AHT10 hoạt động":"Không tìm thấy AHT10");page.replace("%AHTDETAIL%",ahtOK?"Cảm biến đang hoạt động ổn định.":"Không tìm thấy cảm biến. Kiểm tra SDA GPIO32 và SCL GPIO33.");page.replace("%WXSTATUS%",weatherOK?weatherLabel(currentWeatherCode):"Đang chờ dữ liệu");page.replace("%SSID%",htmlEscape(wifiSSID));page.replace("%NETWORKS%",networks);page.replace("%NAME%",htmlEscape(deviceName));page.replace("%TZ%",String(timezoneHours));page.replace("%TOFF%",String(tempOffset,1));page.replace("%HOFF%",String(humidityOffset,1));page.replace("%LOCATION%",htmlEscape(weatherLocation));page.replace("%LAT%",coordinatesSet?String(configuredLat,6):"");page.replace("%LON%",coordinatesSet?String(configuredLon,6):"");page.replace("%COORDSTATUS%",coordinatesSet?("Đang dùng tọa độ "+String(configuredLat,5)+", "+String(configuredLon,5)):"Đang tìm dự báo theo tên địa danh.");page.replace("%CRYPTO%",htmlEscape(cryptoSymbols));page.replace("%FORECAST%",forecastRowsHtml());page.replace("%CDATE%",rgbToHex(colorDate));page.replace("%CHOUR%",rgbToHex(colorHour));page.replace("%CMINUTE%",rgbToHex(colorMinute));page.replace("%CCOLON%",rgbToHex(colorColon));page.replace("%CWEATHER%",rgbToHex(colorWeather));page.replace("%CTEMP%",rgbToHex(colorTemp));page.replace("%CHUM%",rgbToHex(colorHum));page.replace("%TICKER%",String(tickerSeconds));page.replace("%PAGESECS%",String(pageSeconds));for(uint8_t i=0;i<4;i++){page.replace(String("%PAGE")+String(i)+"%",pageEnabled[i]?"checked":"");page.replace(String("%DEF")+String(i)+"%",defaultDisplayPage==i?"selected":"");}page.replace("%AUTOROTATE%",autoPageRotate?"checked":"");page.replace("%ROTATEBUTTON%",autoPageRotate?"DỪNG CHUYỂN TRANG":"TIẾP TỤC CHUYỂN TRANG");page.replace("%ROTATEJS%",autoPageRotate?"true":"false");return page;
}

void setupWebServer(){
  server.on("/",HTTP_GET,[]{server.sendHeader("Cache-Control","no-store");server.send(200,"text/html; charset=utf-8",settingsPage());});
  server.on("/scan",HTTP_GET,[]{int n=WiFi.scanComplete();if(n==WIFI_SCAN_FAILED){WiFi.scanNetworks(true,true);server.send(202,"application/json","{\"scanning\":true}");return;}if(n==WIFI_SCAN_RUNNING){server.send(202,"application/json","{\"scanning\":true}");return;}String body="[";body.reserve(2+max(0,n)*55);for(int i=0;i<n;i++){if(i)body+=',';body+="{\"ssid\":\""+jsonEscape(WiFi.SSID(i))+"\",\"rssi\":"+String(WiFi.RSSI(i))+"}";}body+=']';WiFi.scanDelete();server.sendHeader("Cache-Control","no-store");server.send(200,"application/json",body);});
  server.on("/display",HTTP_POST,[]{if(server.hasArg("page")){int p=server.arg("page").toInt();if(p<0||p>3){server.send(400,"application/json","{\"ok\":false}");return;}showDisplayPage((uint8_t)p);}if(server.hasArg("rotate")){autoPageRotate=server.arg("rotate")=="1";lastPageMs=millis();prefs.begin("clock",false);prefs.putBool("autorotate",autoPageRotate);prefs.end();}server.send(200,"application/json",String("{\"ok\":true,\"page\":")+String(activeDisplayPage)+",\"rotate\":"+(autoPageRotate?"true":"false")+"}");});
  server.on("/weather-now",HTTP_POST,[]{if(WiFi.status()!=WL_CONNECTED){server.send(503,"application/json","{\"ok\":false,\"error\":\"wifi\"}");return;}String location=server.arg("location");location.trim();bool hasGps=server.hasArg("lat")&&server.hasArg("lon");if(hasGps){float la=server.arg("lat").toFloat(),lo=server.arg("lon").toFloat();if(la<-90||la>90||lo<-180||lo>180){server.send(400,"application/json","{\"ok\":false,\"error\":\"coordinates\"}");return;}configuredLat=la;configuredLon=lo;coordinatesSet=true;}else if(location.length()){coordinatesSet=false;configuredLat=NAN;configuredLon=NAN;}if(location.length())weatherLocation=location;prefs.begin("clock",false);prefs.putBool("hascoord",coordinatesSet);if(coordinatesSet){prefs.putFloat("lat",configuredLat);prefs.putFloat("lon",configuredLon);}if(location.length())prefs.putString("loc",location);prefs.end();forceWeatherUpdate=true;server.send(202,"application/json","{\"ok\":true,\"updating\":true}");});
  server.on("/save",HTTP_POST,[]{String name=server.arg("name");name.trim();if(!name.length())name="SMART CLOCK";String location=server.arg("location");location.trim();if(!location.length())location="HÀ NỘI";String symbols=server.arg("crypto");symbols.trim();if(!symbols.length())symbols="BTC,ETH,BNB";bool pages[4];bool anyPage=false;for(uint8_t i=0;i<4;i++){pages[i]=server.hasArg(String("page")+String(i));anyPage|=pages[i];}if(!anyPage)pages[0]=true;prefs.begin("clock",false);prefs.putString("ssid",server.arg("ssid"));if(server.arg("pass").length())prefs.putString("pass",server.arg("pass"));prefs.putString("name",name);prefs.putInt("tz",constrain(server.arg("tz").toInt(),-12,14));prefs.putFloat("toff",constrain(server.arg("toff").toFloat(),-20.0f,20.0f));prefs.putFloat("hoff",constrain(server.arg("hoff").toFloat(),-50.0f,50.0f));prefs.putString("loc",location);prefs.putString("crypto",symbols);bool hc=server.arg("lat").length()&&server.arg("lon").length();float la=server.arg("lat").toFloat(),lo=server.arg("lon").toFloat();hc=hc&&la>=-90&&la<=90&&lo>=-180&&lo<=180;prefs.putBool("hascoord",hc);if(hc){prefs.putFloat("lat",la);prefs.putFloat("lon",lo);}prefs.putUChar("ticker",constrain(server.arg("ticker").toInt(),2,15));for(uint8_t i=0;i<4;i++)prefs.putBool((String("page")+String(i)).c_str(),pages[i]);prefs.putBool("autorotate",server.hasArg("autorotate"));prefs.putUChar("pagesecs",constrain(server.arg("pagesecs").toInt(),5,60));prefs.putUChar("defaultpage",constrain(server.arg("defaultpage").toInt(),0,3));prefs.putUInt("cdate",hexToRgb(server.arg("cdate"),colorDate));prefs.putUInt("chour",hexToRgb(server.arg("chour"),colorHour));prefs.putUInt("cminute",hexToRgb(server.arg("cminute"),colorMinute));prefs.putUInt("ccolon",hexToRgb(server.arg("ccolon"),colorColon));prefs.putUInt("cweather",hexToRgb(server.arg("cweather"),colorWeather));prefs.putUInt("ctemp",hexToRgb(server.arg("ctemp"),colorTemp));prefs.putUInt("chum",hexToRgb(server.arg("chum"),colorHum));prefs.end();server.send(200,"text/html; charset=utf-8","<body style='font-family:system-ui;text-align:center;padding-top:20vh'><h2>Đã lưu cấu hình</h2><p>ESP32 đang khởi động lại...</p></body>");delay(1000);ESP.restart();});
  server.on("/time",HTTP_POST,[]{time_t epoch=strtoll(server.arg("plain").c_str(),nullptr,10);if(epoch>1700000000){timeval tv={epoch,0};settimeofday(&tv,nullptr);previousDay=-1;server.send(200,"text/plain","OK");}else server.send(400,"text/plain","INVALID");});
  server.on("/update-url",HTTP_POST,[]{String url=server.arg("url");url.trim();bool github=url.startsWith("https://github.com/")||url.startsWith("https://raw.githubusercontent.com/");int binPos=url.indexOf(".bin");if(!github||binPos<0||(binPos+4!=(int)url.length()&&url.charAt(binPos+4)!='?')){server.send(400,"text/plain","Link GitHub .bin khong hop le");return;}if(WiFi.status()!=WL_CONNECTED){server.send(503,"text/plain","Wi-Fi chua ket noi");return;}WiFiClientSecure client;client.setInsecure();HTTPClient http;http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);http.setConnectTimeout(12000);if(!http.begin(client,url)){server.send(500,"text/plain","Khong mo duoc URL");return;}int code=http.GET();if(code!=HTTP_CODE_OK){http.end();server.send(502,"text/plain",String("GitHub HTTP ")+code);return;}int size=http.getSize();if(!Update.begin(size>0?(size_t)size:UPDATE_SIZE_UNKNOWN)){http.end();server.send(500,"text/plain","Khong du bo nho OTA");return;}size_t written=Update.writeStream(*http.getStreamPtr());bool lengthOK=size<=0||written==(size_t)size;bool ok=lengthOK&&Update.end(true)&&Update.isFinished();http.end();if(!ok){Update.abort();server.send(500,"text/plain",String("Ghi firmware loi, da nhan ")+written+" byte");return;}server.send(200,"text/plain","OK");delay(1200);ESP.restart();});
  server.on("/update",HTTP_POST,[]{bool ok=!Update.hasError();server.send(ok?200:500,"text/html; charset=utf-8",ok?"<h2>Cập nhật thành công. ESP32 đang khởi động lại...</h2>":"<h2>Cập nhật thất bại.</h2>");if(ok){delay(1000);ESP.restart();}},[]{HTTPUpload &u=server.upload();if(u.status==UPLOAD_FILE_START)Update.begin(UPDATE_SIZE_UNKNOWN);else if(u.status==UPLOAD_FILE_WRITE)Update.write(u.buf,u.currentSize);else if(u.status==UPLOAD_FILE_END)Update.end(true);else if(u.status==UPLOAD_FILE_ABORTED)Update.abort();});
  server.onNotFound([](){if(portalMode){server.sendHeader("Location","http://192.168.4.1",true);server.send(302,"text/plain","");}else server.send(404,"text/plain","Not found");});server.begin();
}

void loadSettings(){prefs.begin("clock",true);wifiSSID=prefs.getString("ssid","VIETTEL_2.4G_T2");wifiPassword=prefs.getString("pass","88888888");deviceName=prefs.getString("name","SMART CLOCK");timezoneHours=prefs.getInt("tz",7);tempOffset=prefs.getFloat("toff",0);humidityOffset=prefs.getFloat("hoff",0);weatherLocation=prefs.getString("loc","HÀ NỘI");cryptoSymbols=prefs.getString("crypto","BTC,ETH,BNB");coordinatesSet=prefs.getBool("hascoord",false);configuredLat=coordinatesSet?prefs.getFloat("lat",NAN):NAN;configuredLon=coordinatesSet?prefs.getFloat("lon",NAN):NAN;tickerSeconds=constrain(prefs.getUChar("ticker",5),(uint8_t)2,(uint8_t)15);bool anyPage=false;for(uint8_t i=0;i<4;i++){pageEnabled[i]=prefs.getBool((String("page")+String(i)).c_str(),i<3);anyPage|=pageEnabled[i];}if(!anyPage)pageEnabled[0]=true;autoPageRotate=prefs.getBool("autorotate",true);pageSeconds=constrain(prefs.getUChar("pagesecs",12),(uint8_t)5,(uint8_t)60);defaultDisplayPage=constrain(prefs.getUChar("defaultpage",0),(uint8_t)0,(uint8_t)3);colorDate=prefs.getUInt("cdate",colorDate);colorHour=prefs.getUInt("chour",colorHour);colorMinute=prefs.getUInt("cminute",colorHour);colorColon=prefs.getUInt("ccolon",colorColon);colorWeather=prefs.getUInt("cweather",colorWeather);colorTemp=prefs.getUInt("ctemp",colorTemp);colorHum=prefs.getUInt("chum",colorHum);prefs.end();}

void startPortal(){portalMode=true;WiFi.mode(WIFI_AP_STA);WiFi.softAP(AP_NAME,AP_PASSWORD);dnsServer.start(53,"*",WiFi.softAPIP());setupWebServer();Serial.print("AP cai dat: ");Serial.println(WiFi.softAPIP());}
void connectWifi(){if(!wifiSSID.length()){startPortal();return;}WiFi.mode(WIFI_STA);WiFi.begin(wifiSSID.c_str(),wifiPassword.c_str());uint32_t start=millis();while(WiFi.status()!=WL_CONNECTED&&millis()-start<12000)delay(200);if(WiFi.status()==WL_CONNECTED){portalMode=false;configTime(timezoneHours*3600,0,"pool.ntp.org","time.google.com");setupWebServer();Serial.print("Wi-Fi OK, IP: ");Serial.println(WiFi.localIP());}else startPortal();}

void setup(){
  Serial.begin(115200);Serial.println("Smart Clock LVGL khoi dong...");dataMutex=xSemaphoreCreateMutex();loadSettings();setCompileTimeFallback();configTime(timezoneHours*3600,0,"pool.ntp.org","time.google.com");
  displaySPI.begin(TFT_SCK,TFT_MISO,TFT_MOSI,TFT_CS);tft.begin(20000000);tft.setRotation(1);tft.fillScreen(ILI9341_BLACK);
  lv_init();lv_disp_draw_buf_init(&lvDrawBuf,lvDrawBuffer,nullptr,320*LV_DRAW_LINES);lv_disp_drv_init(&lvDispDrv);lvDispDrv.hor_res=320;lvDispDrv.ver_res=240;lvDispDrv.flush_cb=lvFlush;lvDispDrv.draw_buf=&lvDrawBuf;lv_disp_drv_register(&lvDispDrv);buildDashboard();Serial.println("LVGL dashboard OK");
  Wire.begin(AHT_SDA,AHT_SCL);ahtOK=aht.begin();connectWifi();String loc=weatherLocation;if(loc.equalsIgnoreCase("Ha Noi")||loc=="Hà Nội")loc="HÀ NỘI";lv_label_set_text(lblLocation,("• "+loc).c_str());parseCryptoSymbols();updateTicker();
  xTaskCreatePinnedToCore(networkTask,"network",16384,nullptr,1,nullptr,0);
}

void loop(){
  static uint32_t lastTick=millis();uint32_t nowMs=millis();lv_tick_inc(nowMs-lastTick);lastTick=nowMs;
  tm now;if(getLocalTime(&now,5)&&now.tm_sec!=previousSecond){previousSecond=now.tm_sec;updateDateTime(now);}
  if(nowMs-lastSensorMs>=2000){lastSensorMs=nowMs;if(ahtOK){sensors_event_t he,te;aht.getEvent(&he,&te);temperature=te.temperature+tempOffset;humidity=constrain(he.relative_humidity+humidityOffset,0.0f,100.0f);if(isnan(dailyMinTemp)||temperature<dailyMinTemp)dailyMinTemp=temperature;if(isnan(dailyMaxTemp)||temperature>dailyMaxTemp)dailyMaxTemp=temperature;}updateSensorUI();}
  if(dataDirty){xSemaphoreTake(dataMutex,portMAX_DELAY);dataDirty=false;xSemaphoreGive(dataMutex);updateWeatherUI();}
  if(nowMs-lastTickerMs>=tickerSeconds*1000UL){lastTickerMs=nowMs;updateTicker();}
  if(autoPageRotate&&nowMs-lastPageMs>=pageSeconds*1000UL)nextDisplayPage();
  if(WiFi.status()!=WL_CONNECTED&&!portalMode&&nowMs-lastWifiTryMs>=30000){lastWifiTryMs=nowMs;WiFi.reconnect();}
  lv_obj_set_style_text_color(lblWifi,lv_color_hex(WiFi.status()==WL_CONNECTED?C_GREEN:(portalMode?C_YELLOW:C_RED)),0);
  server.handleClient();if(portalMode)dnsServer.processNextRequest();lv_timer_handler();delay(5);
}
