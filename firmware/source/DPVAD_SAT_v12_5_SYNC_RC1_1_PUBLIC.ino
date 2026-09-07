/*
 DPVAD_SAT - firmware preliminar sanitizado para consulta académica.

 Este archivo corresponde a un candidato V12.5 SYNC RC1 recuperado de los
 materiales del proyecto. No se presenta como copia bit a bit certificada de
 la versión exacta usada en todos los ensayos.

 Credenciales, contraseñas y claves API fueron sustituidas.
 Consulte RIGHTS.md antes de cualquier reutilización.
*/

#include <Wire.h>
#include <DHT.h>
#include <DHT_U.h>
#include <MPU9250.h>  // ✅ Correcto para la versión 0.4.8 de Hideakitai
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>
#include <math.h>
#include <Preferences.h>

// --- LCD I2C (backpack PCF8574) ---
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ThingSpeak.h>

#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <time.h>
#include <WebServer.h>
#include <esp_system.h>

// Entradas obligatorias del framework Arduino.
// Se declaran explícitamente para facilitar la validación del sketch principal.
void setup();
void loop();

const char* FIRMWARE_VERSION = "12.5-SYNC-RC1";

// -------------------- Pines --------------------
#define DHTPIN 4
#define DHTTYPE DHT22
#define TRIG 25
#define ECHO 26
#define SDA_PIN 21
#define SCL_PIN 22
#define MQ9_AO 34
// microSD (VSPI)
#define SD_CS 5
#define SD_SCK 18
#define SD_MISO 19
#define SD_MOSI 23

// -------------------- WiFi / AP ----------------
const char* WIFI_SSID = "YOUR_WIFI_SSID"; // "lab_sat";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD"; // "gkqzfbdzubdhadg";
const char* AP_SSID   = "DPVAD/SAT";
const char* AP_PASS   = "CHANGE_ME_AP_PASSWORD";
IPAddress AP_IP(192, 168, 4, 1);

String staIPStr = "-", apIPStr = "-";

// -------------------- Seguridad Web ------------
// Autenticacion HTTP Basic para el portal de administracion.
// IMPORTANTE: cambiar estas credenciales antes de un despliegue definitivo.
const char* WEB_ADMIN_USER = "CHANGE_ME_USER";
const char* WEB_ADMIN_PASSWORD = "CHANGE_ME_PASSWORD";

// Portal independiente de trazabilidad operativa (puerto 8080).
// Estas credenciales son distintas de las del portal de históricos.
const char* LOG_WEB_USER = "CHANGE_ME_USER";
const char* LOG_WEB_PASSWORD = "CHANGE_ME_PASSWORD";
const uint16_t LOG_WEB_PORT = 8080;

// -------------------- ThingSpeak ---------------
/// ONLINE: cada 60 s
// BACKFILL: lotes de hasta 30 registros, 30 s despues de cada respuesta
// OFFLINE: cada 60 s (reintentos sin castigar TS)
unsigned long CHANNEL_ID = 1944055;
const char* WRITE_API_KEY = "YOUR_THINGSPEAK_WRITE_API_KEY";
WiFiClient tsClient;

const unsigned long TS_ONLINE_INTERVAL_MS   = 60000;  // 60 s
const unsigned long TS_BACKFILL_INTERVAL_MS = 30000;  // 30 s despues de finalizar el lote
const unsigned long TS_OFFLINE_INTERVAL_MS  = 60000;  // 60 s

// Bulk Write se utiliza exclusivamente durante BACKFILL.
// ThingSpeak permite lotes mayores, pero 30 registros mantiene un consumo
// de RAM moderado en el ESP32 y reduce de forma importante el tiempo de cola.
const uint16_t TS_BACKFILL_BATCH_SIZE = 30;
const uint32_t TS_BULK_HTTP_TIMEOUT_MS = 25000UL;
const uint32_t TS_BULK_SAMPLE_GUARD_MS = TS_BULK_HTTP_TIMEOUT_MS + 2000UL;

unsigned long lastTS = 0;
uint32_t lastBulkDurationMs = 0;
uint32_t lastBulkPayloadBytes = 0;

// -------------------- Muestreo ------------------
const unsigned long SAMPLE_MS = 60000;  // muestreo + CSV cada 60 s
unsigned long lastSample = 0;

// -------------------- Sensores/vars -------------
DHT dht(DHTPIN, DHTTYPE);
MPU9250 mpu;          // I2C (0x68 por defecto, 0x69 si AD0=HIGH)

Adafruit_BME280 bme;           // begin(addr, &Wire)
Adafruit_BMP280 bmp280(&Wire); // begin(addr) SIN &Wire en la llamada
bool hasBME = false;           // indica si estamos usando BME280
Preferences prefs;             // NVS para R0
Preferences calPrefs;          // NVS para perfil de calibracion general
Preferences systemPrefs;       // NVS para contador de arranques y trazabilidad

// -------------------- Calibracion general -------
// Esta revision conserva la operacion de la v12 y agrega una capa de
// correccion/validacion; esta rama añade únicamente trazabilidad operativa.
struct LinearCalibration {
  float gain;
  float offset;
};

struct CalibrationProfile {
  uint32_t magic;
  uint16_t schemaVersion;
  uint16_t reserved;
  uint32_t generation;

  LinearCalibration dhtTemp;
  LinearCalibration dhtHumidity;
  LinearCalibration envTemp;
  LinearCalibration envHumidity;
  LinearCalibration pressure;
  LinearCalibration distance;

  float gyroBiasX;
  float gyroBiasY;
  float gyroBiasZ;

  float accelBiasX;
  float accelBiasY;
  float accelBiasZ;
  float accelScaleX;
  float accelScaleY;
  float accelScaleZ;

  float seaLevelHpa;
  float knownAltitudeM;
  uint32_t crc32;
};

enum CalibrationState {
  CAL_IDLE = 0,
  CAL_PRECHECK,
  CAL_CAPTURING,
  CAL_COMPUTING,
  CAL_VALIDATING,
  CAL_COMMITTING,
  CAL_COMPLETED,
  CAL_FAILED
};

const uint32_t CAL_PROFILE_MAGIC = 0x44505643UL; // "DPVC"
const uint16_t CAL_PROFILE_SCHEMA = 1;
const char* CALLOG_FILE = "/calibration_events.csv";
const float TEMP_CROSSCHECK_MAX_C = 2.0f;
const float HUM_CROSSCHECK_MAX_PCT = 8.0f;

CalibrationProfile calProfile;
CalibrationState calibrationState = CAL_IDLE;

// Lecturas crudas conservadas para trazabilidad y calibracion.
float raw_dist = NAN;
float raw_dht_t = NAN, raw_dht_h = NAN;
float corrected_dht_t = NAN, corrected_dht_h = NAN;
float raw_env_t = NAN, raw_env_h = NAN, raw_env_p = NAN;
float corrected_env_h = NAN;
float raw_ax = NAN, raw_ay = NAN, raw_az = NAN;
float raw_gx = NAN, raw_gy = NAN, raw_gz = NAN;
float last_temp_delta = NAN, last_hum_delta = NAN;
bool temp_crosscheck_ok = false, hum_crosscheck_ok = false;

float last_dist = NAN, last_t = NAN, last_h = NAN, last_mq9ppm = NAN; // ppm estimadas
float last_ax = NAN, last_ay = NAN, last_az = NAN;
float last_gx = NAN, last_gy = NAN, last_gz = NAN;
float last_pitch = NAN, last_roll = NAN;

// BMP280/BME280
float last_bmp_t = NAN;    // °C corregidos
float last_bme_h = NAN;    // %RH corregidos (solo BME280)
float last_bmp_p = NAN;    // hPa corregidos
float last_bmp_alt = NAN;  // m estimados
bool ok_bmp = false;

// Temperatura "oficial" del SAT (fusionada BME/BMP + DHT22)
float temp_sat         = NAN;  // valor actual
float temp_sat_valid   = NAN;  // último valor válido conocido
bool  temp_sat_is_fresh = false; // indica si viene de una lectura fresca o es reutilizada

// -------------------- Flags resumen -------------
bool ok_dht = false, ok_ultra = false, ok_mq9 = false, ok_mpu = false;
bool ok_sd = false, ok_ap = false, ok_sta = false, ok_ts = false;

// -------------------- MQ-9 Autocalibración ------
// last_mq9ppm es una estimacion basada en Rs/R0 y una curva aproximada.
// No equivale a una medicion certificada contra gas patron.
float mq9_R0 = NAN;
bool mq9_calibrated = false;

// RL es la resistencia de carga física del circuito/módulo MQ-9.
// Debe coincidir con el valor real medido o documentado para el módulo.
const float MQ9_RL_DEFAULT = 10.0f;      // kΩ, respaldo si no existe configuración
float mq9_RL = MQ9_RL_DEFAULT;           // kΩ, valor operativo persistente
const char* MQ9_CAL_FILE = "/mq9_calibration.cfg";

const float MQ9_CLEAN_AIR_RATIO = 9.8f;  // Rs/R0 típico en aire limpio
const float ADC_VREF = 3.3f;
const uint32_t MQ9_WARMUP_MS = 90000;  // 90 s calentamiento
const uint32_t MQ9_SAMPLE_MS = 30000;  // 30 s muestreo

// -------------------- LCD -----------------------
hd44780_I2Cexp lcd;  // auto-detección del backpack I2C
bool ok_lcd = false;

// -------------------- Logging (SD + tiempo) -----
String logFilePath = "";
tm timeinfo;

// ---- Ficheros adicionales (backfill + eventos) ----
const char* BACKFILL_FILE       = "/backfill_queue.csv";
const char* BACKFILL_STATE_FILE = "/backfill_state.cfg";
const char* BACKFILL_STATE_TMP  = "/backfill_state.tmp";
const char* CONNLOG_FILE        = "/conn_events.csv";

// Estadísticas OFFLINE/BACKFILL.
uint32_t offlineSampleCount      = 0;
uint32_t backfillSentCount       = 0;
uint32_t backfillPendingAtStart  = 0;

// ---- Cola persistente de backfill en microSD ----
// El archivo conserva las filas y el cursor indica el siguiente byte por enviar.
uint32_t backfillReadOffset = 0;
uint32_t backfillCount      = 0;
uint32_t backfillSentTotal  = 0;

// Última muestra generada. Permite encolarla si el envío ONLINE falla.
String lastSampleCsvRow = "";
bool lastSampleQueued  = false;

// RAM únicamente como emergencia temporal si la SD no está disponible.
const uint8_t  EMERGENCY_CAPACITY = 60;
const uint16_t BACKFILL_ROW_MAX   = 192;
char emergencyBuf[EMERGENCY_CAPACITY][BACKFILL_ROW_MAX];
uint8_t emergencyHead  = 0;
uint8_t emergencyTail  = 0;
uint8_t emergencyCount = 0;
uint32_t emergencyDropped = 0;

// Lote global para no reservar varios kilobytes en la pila de loop().
struct BackfillBatch {
  char rows[TS_BACKFILL_BATCH_SIZE][BACKFILL_ROW_MAX];
  uint16_t count;
  uint32_t finalOffset;
};
BackfillBatch syncBatch;

unsigned long lastSdRetry = 0;
const unsigned long SD_RETRY_MS = 30000;

void copyRowToEmergencySlot(uint8_t idx, const String& row) {
  size_t n = row.length();
  if (n >= BACKFILL_ROW_MAX) n = BACKFILL_ROW_MAX - 1;
  memcpy(emergencyBuf[idx], row.c_str(), n);
  emergencyBuf[idx][n] = '\0';
}

// -------------------- Web -----------------------
WebServer server(80);          // históricos y mantenimiento
WebServer logServer(LOG_WEB_PORT); // trazabilidad operativa, solo lectura

// -------------------- Máquina de estados --------
enum ConnState {
  CS_ONLINE = 0,
  CS_OFFLINE,
  CS_BACKFILL
};

ConnState connState = CS_OFFLINE;       // estado actual
unsigned long lastWiFiCheck = 0;        // para reintentos
const unsigned long WIFI_RETRY_MS = 15000;

// -------------------- Trazabilidad operativa ----
enum SystemLogSeverity : uint8_t {
  SYSLOG_DEBUG = 0,
  SYSLOG_INFO,
  SYSLOG_WARNING,
  SYSLOG_ERROR,
  SYSLOG_CRITICAL
};

enum SystemLogComponent : uint8_t {
  SYSCOMP_SYSTEM = 0,
  SYSCOMP_NETWORK,
  SYSCOMP_STORAGE,
  SYSCOMP_SYNC,
  SYSCOMP_WEB,
  SYSCOMP_CALIBRATION,
  SYSCOMP_SENSOR_HEALTH
};

const char* SYSTEM_LOG_DIR = "/logs";
const char* SYSTEM_LOG_HEADER =
  "date,time,epoch,time_quality,boot_id,uptime_ms,severity,component,event,from_state,to_state,result,details";
const uint16_t SYSTEM_LOG_MAX_FILES = 90;
const unsigned long HEALTH_SNAPSHOT_INTERVAL_MS = 3600000UL;  // 1 hora
const unsigned long LOG_RETENTION_INTERVAL_MS = 86400000UL;   // 24 horas

struct BufferedSystemEvent {
  char row[320];
  char path[72];
  uint8_t severity;
};

const uint8_t SYSTEM_EVENT_BUFFER_CAPACITY = 24;
BufferedSystemEvent systemEventBuffer[SYSTEM_EVENT_BUFFER_CAPACITY];
uint8_t systemEventBufferCount = 0;
uint32_t systemEventDropped = 0;

uint32_t bootId = 0;
String lastResetReason = "UNKNOWN";
String lastSystemEvent = "NONE";
String lastSystemSeverity = "INFO";
unsigned long lastHealthSnapshotMs = 0;
unsigned long lastLogRetentionMs = 0;
bool timeSyncSuccessLogged = false;
bool timeSyncFailureLogged = false;
bool wifiFailureActive = false;
bool sdFailureActive = false;
bool emergencyBufferActive = false;
uint32_t lastEmergencyDroppedLogged = 0;
uint32_t sdRemountAttempts = 0;
bool thingSpeakStateKnown = false;
bool thingSpeakAvailable = false;
int lastThingSpeakCode = 0;
uint32_t backfillSendFailures = 0;
uint32_t wifiLinkLossCount = 0;
uint32_t thingSpeakFailureCount = 0;
String lastBackfillCause = "NONE";

// --- Prototipos explícitos ---
const char* stateName(ConnState st);
void updateOperationalStatusLine();
const char* wifiStatusName(wl_status_t status);
void setConnState(ConnState newState);
bool backfillHasData();
bool sendBackfillBatch();
int publishLiveToThingSpeak();
void logConnEvent(const char* evName);
uint32_t countBackfillLines();
bool initializeBackfillQueue();
bool saveBackfillState();
bool appendBackfillRow(const String& row);
bool flushEmergencyBackfill();
void processSdRecovery(unsigned long now);
bool readBackfillBatch(BackfillBatch& batch);
bool buildBackfillBulkPayload(const BackfillBatch& batch, String& payload);

// Trazabilidad operativa y portal independiente.
void initializeSystemIdentity();
const char* resetReasonName(esp_reset_reason_t reason);
const char* systemLogSeverityName(uint8_t severity);
const char* systemLogComponentName(uint8_t component);
void bufferSystemLogRow(const String& row,
                        uint8_t severity,
                        const String& path);
void logSystemEvent(uint8_t severity,
                    uint8_t component,
                    const char* event,
                    const char* fromState = "",
                    const char* toState = "",
                    const char* result = "",
                    const String& details = "");
bool flushBufferedSystemEvents();
void processSystemLogMaintenance(unsigned long now);
void maintainSystemLogRetention();
void recordThingSpeakResult(int code, const char* mode);
bool requireLogAuthentication();
void sendSystemLogIndex();
void sendSystemLogList();
void sendSystemLogStatus();
void handleSystemLogDownload();

// Calibracion: persistencia, aplicacion y handlers web.
void setDefaultCalibrationProfile();
bool loadCalibrationProfile();
bool saveCalibrationProfile(const char* reason);
float applyLinearCalibration(float value, const LinearCalibration& cal);
float calculateAltitudeM(float pressureHpa, float seaLevelHpa);
void updateCalibrationCrossChecks();
void handleCalibrationStatus();
void handleCalibrationReference();
void handleCalibrationSet();
void handleCalibrationReset();
void handleCalibrationImuZero();
void handleCalibrationSeaLevel();
bool requireWebAuthentication();

// MQ-9: persistencia y diagnóstico aislados del perfil general.
bool validMq9Rl(float value);
bool validMq9R0(float value);
bool loadMq9Calibration();
bool saveMq9Calibration();
void clearMq9Calibration();
void handleMQ9Status();
void handleMQ9Config();
float adc_to_volt(int raw);
float mq9_rs_from_adc(int raw);
float mq9_ppm_from_ratio(float ratio);

// -------------------- Utilidades ----------------
uint32_t crc32Buffer(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
    }
  }
  return ~crc;
}

uint32_t calibrationProfileCrc(const CalibrationProfile& profile) {
  CalibrationProfile copy = profile;
  copy.crc32 = 0;
  return crc32Buffer(reinterpret_cast<const uint8_t*>(&copy), sizeof(copy));
}

void setDefaultLinear(LinearCalibration& cal) {
  cal.gain = 1.0f;
  cal.offset = 0.0f;
}

void setDefaultCalibrationProfile() {
  memset(&calProfile, 0, sizeof(calProfile));
  calProfile.magic = CAL_PROFILE_MAGIC;
  calProfile.schemaVersion = CAL_PROFILE_SCHEMA;
  calProfile.generation = 1;

  setDefaultLinear(calProfile.dhtTemp);
  setDefaultLinear(calProfile.dhtHumidity);
  setDefaultLinear(calProfile.envTemp);
  setDefaultLinear(calProfile.envHumidity);
  setDefaultLinear(calProfile.pressure);
  setDefaultLinear(calProfile.distance);

  calProfile.accelScaleX = 1.0f;
  calProfile.accelScaleY = 1.0f;
  calProfile.accelScaleZ = 1.0f;
  calProfile.seaLevelHpa = 1013.25f;
  calProfile.knownAltitudeM = NAN;
  calProfile.crc32 = calibrationProfileCrc(calProfile);
}

bool calibrationProfileIsValid(const CalibrationProfile& profile) {
  if (profile.magic != CAL_PROFILE_MAGIC) return false;
  if (profile.schemaVersion != CAL_PROFILE_SCHEMA) return false;
  if (!isfinite(profile.seaLevelHpa) || profile.seaLevelHpa < 800.0f || profile.seaLevelHpa > 1200.0f) return false;
  return profile.crc32 == calibrationProfileCrc(profile);
}

bool loadCalibrationProfile() {
  setDefaultCalibrationProfile();

  CalibrationProfile stored;
  memset(&stored, 0, sizeof(stored));
  calPrefs.begin("sat_cal", true);
  size_t storedSize = calPrefs.getBytesLength("profile");
  size_t readSize = 0;
  if (storedSize == sizeof(stored)) {
    readSize = calPrefs.getBytes("profile", &stored, sizeof(stored));
  }
  calPrefs.end();

  if (readSize == sizeof(stored) && calibrationProfileIsValid(stored)) {
    calProfile = stored;
    Serial.printf("[CAL] Perfil cargado: generacion=%lu schema=%u\n",
                  (unsigned long)calProfile.generation,
                  (unsigned)calProfile.schemaVersion);
    return true;
  }

  Serial.println("[CAL] Sin perfil valido; se usan coeficientes neutros.");
  return false;
}

void logCalibrationEvent(const char* reason) {
  if (!ok_sd) return;

  char datePart[11] = "0000-00-00";
  char timePart[9] = "00:00:00";
  if (getLocalTime(&timeinfo)) {
    strftime(datePart, sizeof(datePart), "%Y-%m-%d", &timeinfo);
    strftime(timePart, sizeof(timePart), "%H:%M:%S", &timeinfo);
  }

  bool createHeader = !SD.exists(CALLOG_FILE);
  File f = SD.open(CALLOG_FILE, FILE_APPEND);
  if (!f) return;
  if (createHeader) {
    f.println("date,time,generation,reason,dht_t_gain,dht_t_offset,dht_h_gain,dht_h_offset,env_t_gain,env_t_offset,env_h_gain,env_h_offset,pressure_gain,pressure_offset,distance_gain,distance_offset,gyro_bx,gyro_by,gyro_bz,sea_level_hpa,known_altitude_m");
  }
  f.printf("%s,%s,%lu,%s,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.4f,%.3f\n",
           datePart, timePart, (unsigned long)calProfile.generation, reason,
           calProfile.dhtTemp.gain, calProfile.dhtTemp.offset,
           calProfile.dhtHumidity.gain, calProfile.dhtHumidity.offset,
           calProfile.envTemp.gain, calProfile.envTemp.offset,
           calProfile.envHumidity.gain, calProfile.envHumidity.offset,
           calProfile.pressure.gain, calProfile.pressure.offset,
           calProfile.distance.gain, calProfile.distance.offset,
           calProfile.gyroBiasX, calProfile.gyroBiasY, calProfile.gyroBiasZ,
           calProfile.seaLevelHpa, calProfile.knownAltitudeM);
  f.close();
}

bool saveCalibrationProfile(const char* reason) {
  calibrationState = CAL_COMMITTING;
  calProfile.magic = CAL_PROFILE_MAGIC;
  calProfile.schemaVersion = CAL_PROFILE_SCHEMA;
  calProfile.generation++;
  calProfile.crc32 = calibrationProfileCrc(calProfile);

  calPrefs.begin("sat_cal", false);
  size_t written = calPrefs.putBytes("profile", &calProfile, sizeof(calProfile));
  calPrefs.end();

  if (written != sizeof(calProfile)) {
    calibrationState = CAL_FAILED;
    Serial.println("[CAL] Error guardando perfil en NVS.");
    logSystemEvent(SYSLOG_ERROR, SYSCOMP_CALIBRATION,
                   "CALIBRATION_UPDATE_FAILED", "", "", "FAIL",
                   String("reason=") + reason);
    return false;
  }

  calibrationState = CAL_COMPLETED;
  logCalibrationEvent(reason);
  logSystemEvent(SYSLOG_INFO, SYSCOMP_CALIBRATION,
                 "CALIBRATION_UPDATED", "", "", "OK",
                 String("reason=") + reason + ";generation=" + String(calProfile.generation));
  Serial.printf("[CAL] Perfil guardado: generacion=%lu motivo=%s\n",
                (unsigned long)calProfile.generation, reason);
  return true;
}

float applyLinearCalibration(float value, const LinearCalibration& cal) {
  if (!isfinite(value)) return NAN;
  if (!isfinite(cal.gain) || !isfinite(cal.offset)) return value;
  return value * cal.gain + cal.offset;
}

float calculateAltitudeM(float pressureHpa, float seaLevelHpa) {
  if (!isfinite(pressureHpa) || pressureHpa <= 0.0f) return NAN;
  if (!isfinite(seaLevelHpa) || seaLevelHpa <= 0.0f) return NAN;
  return 44330.0f * (1.0f - powf(pressureHpa / seaLevelHpa, 0.19029495f));
}

void updateCalibrationCrossChecks() {
  temp_crosscheck_ok = false;
  hum_crosscheck_ok = false;
  last_temp_delta = NAN;
  last_hum_delta = NAN;

  if (isfinite(corrected_dht_t) && isfinite(last_bmp_t)) {
    last_temp_delta = fabsf(corrected_dht_t - last_bmp_t);
    temp_crosscheck_ok = last_temp_delta <= TEMP_CROSSCHECK_MAX_C;
  }

  if (hasBME && isfinite(corrected_dht_h) && isfinite(last_bme_h)) {
    last_hum_delta = fabsf(corrected_dht_h - last_bme_h);
    hum_crosscheck_ok = last_hum_delta <= HUM_CROSSCHECK_MAX_PCT;
  }
}

const char* calibrationStateName(CalibrationState state) {
  switch (state) {
    case CAL_IDLE: return "IDLE";
    case CAL_PRECHECK: return "PRECHECK";
    case CAL_CAPTURING: return "CAPTURING";
    case CAL_COMPUTING: return "COMPUTING";
    case CAL_VALIDATING: return "VALIDATING";
    case CAL_COMMITTING: return "COMMITTING";
    case CAL_COMPLETED: return "COMPLETED";
    case CAL_FAILED: return "FAILED";
    default: return "UNKNOWN";
  }
}

LinearCalibration* calibrationForTarget(const String& target) {
  if (target == "dht_temp") return &calProfile.dhtTemp;
  if (target == "dht_hum") return &calProfile.dhtHumidity;
  if (target == "env_temp") return &calProfile.envTemp;
  if (target == "env_hum") return &calProfile.envHumidity;
  if (target == "pressure") return &calProfile.pressure;
  if (target == "distance") return &calProfile.distance;
  return nullptr;
}

float rawValueForTarget(const String& target) {
  if (target == "dht_temp") return raw_dht_t;
  if (target == "dht_hum") return raw_dht_h;
  if (target == "env_temp") return raw_env_t;
  if (target == "env_hum") return raw_env_h;
  if (target == "pressure") return raw_env_p;
  if (target == "distance") return raw_dist;
  return NAN;
}

String calibrationJsonNumber(float value, unsigned int decimals = 3U) {
  if (!isfinite(value)) return "null";
  return String(value, decimals);
}

void handleCalibrationStatus() {
  if (!requireWebAuthentication()) return;
  String json;
  json.reserve(900);
  json += "{";
  json += "\"state\":\"" + String(calibrationStateName(calibrationState)) + "\",";
  json += "\"generation\":" + String(calProfile.generation) + ",";
  json += "\"gasUnit\":\"ppm_estimated\",";
  json += "\"dhtTempRaw\":" + calibrationJsonNumber(raw_dht_t) + ",";
  json += "\"dhtTempCorrected\":" + calibrationJsonNumber(corrected_dht_t) + ",";
  json += "\"dhtHumidityRaw\":" + calibrationJsonNumber(raw_dht_h) + ",";
  json += "\"dhtHumidityCorrected\":" + calibrationJsonNumber(corrected_dht_h) + ",";
  json += "\"envTempRaw\":" + calibrationJsonNumber(raw_env_t) + ",";
  json += "\"envTempCorrected\":" + calibrationJsonNumber(last_bmp_t) + ",";
  json += "\"envHumidityRaw\":" + calibrationJsonNumber(raw_env_h) + ",";
  json += "\"envHumidityCorrected\":" + calibrationJsonNumber(last_bme_h) + ",";
  json += "\"pressureRaw\":" + calibrationJsonNumber(raw_env_p) + ",";
  json += "\"pressureCorrected\":" + calibrationJsonNumber(last_bmp_p) + ",";
  json += "\"distanceRaw\":" + calibrationJsonNumber(raw_dist) + ",";
  json += "\"distanceCorrected\":" + calibrationJsonNumber(last_dist) + ",";
  json += "\"tempDelta\":" + calibrationJsonNumber(last_temp_delta) + ",";
  json += "\"tempCrosscheckOk\":" + String(temp_crosscheck_ok ? "true" : "false") + ",";
  json += "\"humidityDelta\":" + calibrationJsonNumber(last_hum_delta) + ",";
  json += "\"humidityCrosscheckOk\":" + String(hum_crosscheck_ok ? "true" : "false") + ",";
  json += "\"seaLevelHpa\":" + calibrationJsonNumber(calProfile.seaLevelHpa);
  json += "}";
  server.send(200, "application/json", json);
}

void handleCalibrationReference() {
  if (!requireWebAuthentication()) return;
  if (!server.hasArg("target") || !server.hasArg("reference")) {
    server.send(400, "text/plain", "Use target=<dht_temp|dht_hum|env_temp|env_hum|pressure|distance>&reference=<valor>");
    return;
  }

  String target = server.arg("target");
  float reference = server.arg("reference").toFloat();
  LinearCalibration* cal = calibrationForTarget(target);
  float raw = rawValueForTarget(target);

  if (cal == nullptr || !isfinite(reference) || !isfinite(raw)) {
    server.send(400, "text/plain", "Target, referencia o lectura cruda no validos");
    return;
  }

  calibrationState = CAL_COMPUTING;
  cal->offset = reference - (cal->gain * raw);
  if (!saveCalibrationProfile((String("reference_") + target).c_str())) {
    server.send(500, "text/plain", "No se pudo guardar el perfil");
    return;
  }

  String response = "Calibracion de un punto aplicada. target=" + target +
                    " raw=" + String(raw, 4) +
                    " reference=" + String(reference, 4) +
                    " gain=" + String(cal->gain, 7) +
                    " offset=" + String(cal->offset, 7);
  server.send(200, "text/plain", response);
}

void handleCalibrationSet() {
  if (!requireWebAuthentication()) return;
  if (!server.hasArg("target") || !server.hasArg("gain") || !server.hasArg("offset")) {
    server.send(400, "text/plain", "Use target=<...>&gain=<valor>&offset=<valor>");
    return;
  }

  String target = server.arg("target");
  float gain = server.arg("gain").toFloat();
  float offset = server.arg("offset").toFloat();
  LinearCalibration* cal = calibrationForTarget(target);

  if (cal == nullptr || !isfinite(gain) || !isfinite(offset) || fabsf(gain) < 0.000001f) {
    server.send(400, "text/plain", "Parametros no validos");
    return;
  }

  cal->gain = gain;
  cal->offset = offset;
  if (!saveCalibrationProfile((String("set_") + target).c_str())) {
    server.send(500, "text/plain", "No se pudo guardar el perfil");
    return;
  }
  server.send(200, "text/plain", "Coeficientes guardados");
}

void resetCalibrationTarget(const String& target) {
  if (target == "all") {
    uint32_t previousGeneration = calProfile.generation;
    setDefaultCalibrationProfile();
    calProfile.generation = previousGeneration;
    return;
  }
  LinearCalibration* cal = calibrationForTarget(target);
  if (cal != nullptr) setDefaultLinear(*cal);
  if (target == "imu") {
    calProfile.gyroBiasX = calProfile.gyroBiasY = calProfile.gyroBiasZ = 0.0f;
    calProfile.accelBiasX = calProfile.accelBiasY = calProfile.accelBiasZ = 0.0f;
    calProfile.accelScaleX = calProfile.accelScaleY = calProfile.accelScaleZ = 1.0f;
  }
  if (target == "altitude") {
    calProfile.seaLevelHpa = 1013.25f;
    calProfile.knownAltitudeM = NAN;
  }
}

void handleCalibrationReset() {
  if (!requireWebAuthentication()) return;
  String target = server.hasArg("target") ? server.arg("target") : "all";
  if (target != "all" && target != "imu" && target != "altitude" && calibrationForTarget(target) == nullptr) {
    server.send(400, "text/plain", "Target no valido");
    return;
  }
  resetCalibrationTarget(target);
  if (!saveCalibrationProfile((String("reset_") + target).c_str())) {
    server.send(500, "text/plain", "No se pudo guardar el perfil");
    return;
  }
  server.send(200, "text/plain", String("Calibracion restablecida: ") + target);
}

void handleCalibrationImuZero() {
  if (!requireWebAuthentication()) return;
  if (!ok_mpu) {
    server.send(409, "text/plain", "IMU no disponible");
    return;
  }

  calibrationState = CAL_PRECHECK;
  if (ok_lcd) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Calibrando IMU");
    lcd.setCursor(0, 1); lcd.print("Mantenga inmovil");
  }

  calibrationState = CAL_CAPTURING;
  double sx = 0, sy = 0, sz = 0;
  uint16_t count = 0;
  const uint16_t required = 300;
  uint32_t deadline = millis() + 12000UL;

  while (count < required && millis() < deadline) {
    if (mpu.update()) {
      sx += mpu.getGyroX();
      sy += mpu.getGyroY();
      sz += mpu.getGyroZ();
      count++;
    }
    server.handleClient();
    delay(10);
    yield();
  }

  if (count < 100) {
    calibrationState = CAL_FAILED;
    server.send(500, "text/plain", "Muestras insuficientes para cero de giroscopio");
    return;
  }

  calibrationState = CAL_COMPUTING;
  calProfile.gyroBiasX = sx / count;
  calProfile.gyroBiasY = sy / count;
  calProfile.gyroBiasZ = sz / count;

  if (!saveCalibrationProfile("imu_gyro_zero")) {
    server.send(500, "text/plain", "No se pudo guardar el perfil");
    return;
  }

  String response = "Cero de giroscopio guardado. bx=" + String(calProfile.gyroBiasX, 5) +
                    " by=" + String(calProfile.gyroBiasY, 5) +
                    " bz=" + String(calProfile.gyroBiasZ, 5) +
                    " samples=" + String(count);
  server.send(200, "text/plain", response);
}

void handleCalibrationSeaLevel() {
  if (!requireWebAuthentication()) return;
  if (!server.hasArg("altitude") || !isfinite(last_bmp_p)) {
    server.send(400, "text/plain", "Use altitude=<metros> con una lectura de presion valida");
    return;
  }

  float altitude = server.arg("altitude").toFloat();
  float base = 1.0f - altitude / 44330.0f;
  if (!isfinite(altitude) || base <= 0.0f) {
    server.send(400, "text/plain", "Altitud no valida");
    return;
  }

  float seaLevel = last_bmp_p / powf(base, 5.255f);
  if (!isfinite(seaLevel) || seaLevel < 800.0f || seaLevel > 1200.0f) {
    server.send(400, "text/plain", "Resultado de presion al nivel del mar fuera de rango");
    return;
  }

  calProfile.knownAltitudeM = altitude;
  calProfile.seaLevelHpa = seaLevel;
  if (!saveCalibrationProfile("altitude_reference")) {
    server.send(500, "text/plain", "No se pudo guardar el perfil");
    return;
  }
  server.send(200, "text/plain", "Referencia barometrica guardada. seaLevelHpa=" + String(seaLevel, 3));
}

bool validMq9Rl(float value) {
  return isfinite(value) && value >= 0.1f && value <= 1000.0f;
}

bool validMq9R0(float value) {
  return isfinite(value) && value > 0.05f && value < 100000.0f;
}

bool loadMq9CalibrationFromSd(float& rlOut, float& r0Out) {
  if (!ok_sd || !SD.exists(MQ9_CAL_FILE)) return false;

  File f = SD.open(MQ9_CAL_FILE, FILE_READ);
  if (!f) return false;

  float fileRl = NAN;
  float fileR0 = NAN;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    int separator = line.indexOf('=');
    if (separator <= 0) continue;

    String key = line.substring(0, separator);
    String value = line.substring(separator + 1);
    key.trim();
    value.trim();

    if (key == "RL_KOHM") fileRl = value.toFloat();
    else if (key == "R0_KOHM") fileR0 = value.toFloat();
  }
  f.close();

  bool recovered = false;
  if (validMq9Rl(fileRl)) {
    rlOut = fileRl;
    recovered = true;
  }
  if (validMq9R0(fileR0)) {
    r0Out = fileR0;
    recovered = true;
  }
  return recovered;
}

bool saveMq9CalibrationToSd() {
  if (!ok_sd) return false;

  if (SD.exists(MQ9_CAL_FILE)) SD.remove(MQ9_CAL_FILE);
  File f = SD.open(MQ9_CAL_FILE, FILE_WRITE);
  if (!f) return false;

  f.println("VERSION=2");
  f.print("RL_KOHM=");
  f.println(mq9_RL, 6);
  f.print("R0_KOHM=");
  if (validMq9R0(mq9_R0)) f.println(mq9_R0, 6);
  else f.println("NAN");
  f.flush();
  f.close();
  return true;
}

bool saveMq9Calibration() {
  if (!validMq9Rl(mq9_RL)) {
    Serial.println("[MQ-9] No se guarda: RL fuera de rango.");
    return false;
  }

  prefs.begin("mq9", false);
  size_t rlWritten = prefs.putFloat("RL", mq9_RL);
  size_t r0Written = 0;
  if (validMq9R0(mq9_R0)) {
    r0Written = prefs.putFloat("R0", mq9_R0);
  } else {
    prefs.remove("R0");
  }
  prefs.putUShort("VER", 2);
  prefs.end();

  bool nvsOk = rlWritten == sizeof(float) && (!validMq9R0(mq9_R0) || r0Written == sizeof(float));
  bool sdOk = saveMq9CalibrationToSd();

  Serial.printf("[MQ-9] Config guardada: RL=%.4f kOhm R0=%s NVS=%s SD=%s\n",
                mq9_RL,
                validMq9R0(mq9_R0) ? String(mq9_R0, 4).c_str() : "NO_CAL",
                nvsOk ? "OK" : "FAIL",
                sdOk ? "OK" : "NO");
  logSystemEvent(nvsOk ? SYSLOG_INFO : SYSLOG_ERROR,
                 SYSCOMP_CALIBRATION,
                 "MQ9_CALIBRATION_UPDATED", "", "", nvsOk ? "OK" : "FAIL",
                 String("rl_kohm=") + String(mq9_RL, 4) +
                 ";r0_valid=" + (validMq9R0(mq9_R0) ? "1" : "0") +
                 ";sd_backup=" + (sdOk ? "1" : "0"));
  return nvsOk;
}

bool loadMq9Calibration() {
  float savedRl = NAN;
  float savedR0 = NAN;

  prefs.begin("mq9", true);
  savedRl = prefs.getFloat("RL", NAN);
  savedR0 = prefs.getFloat("R0", NAN);
  prefs.end();

  bool rlFromNvs = validMq9Rl(savedRl);
  bool r0FromNvs = validMq9R0(savedR0);

  float sdRl = NAN;
  float sdR0 = NAN;
  bool sdRecovered = false;
  if (!rlFromNvs || !r0FromNvs) {
    sdRecovered = loadMq9CalibrationFromSd(sdRl, sdR0);
  }

  mq9_RL = rlFromNvs ? savedRl : (validMq9Rl(sdRl) ? sdRl : MQ9_RL_DEFAULT);
  mq9_R0 = r0FromNvs ? savedR0 : (validMq9R0(sdR0) ? sdR0 : NAN);
  mq9_calibrated = validMq9R0(mq9_R0);

  Serial.printf("[MQ-9] Config activa: RL=%.4f kOhm (%s) R0=%s (%s)\n",
                mq9_RL,
                rlFromNvs ? "NVS" : (validMq9Rl(sdRl) ? "SD" : "DEFAULT"),
                mq9_calibrated ? String(mq9_R0, 4).c_str() : "NO_CAL",
                r0FromNvs ? "NVS" : (validMq9R0(sdR0) ? "SD" : "AUSENTE"));

  // Si la SD permitió recuperar un valor perdido en NVS, restaurarlo.
  if (sdRecovered && ((!rlFromNvs && validMq9Rl(sdRl)) || (!r0FromNvs && validMq9R0(sdR0)))) {
    saveMq9Calibration();
  }

  return mq9_calibrated;
}

void clearMq9Calibration() {
  prefs.begin("mq9", false);
  prefs.remove("R0");
  prefs.remove("RL");
  prefs.remove("VER");
  prefs.end();

  if (ok_sd && SD.exists(MQ9_CAL_FILE)) SD.remove(MQ9_CAL_FILE);

  mq9_RL = MQ9_RL_DEFAULT;
  mq9_R0 = NAN;
  mq9_calibrated = false;
  logSystemEvent(SYSLOG_WARNING, SYSCOMP_CALIBRATION,
                 "MQ9_CALIBRATION_RESET", "", "", "OK",
                 "RL/R0 eliminados; se restablece RL por defecto");
}

void handleMQ9Status() {
  if (!requireWebAuthentication()) return;

  int raw = analogRead(MQ9_AO);
  float voltage = adc_to_volt(raw);
  float rs = mq9_rs_from_adc(raw);
  float ratio = (validMq9R0(mq9_R0) && isfinite(rs)) ? rs / mq9_R0 : NAN;
  float ppm = isfinite(ratio) ? mq9_ppm_from_ratio(ratio) : NAN;

  String json;
  json.reserve(320);
  json += "{";
  json += "\"raw\":" + String(raw) + ",";
  json += "\"voltage\":" + calibrationJsonNumber(voltage, 4) + ",";
  json += "\"rlKOhm\":" + calibrationJsonNumber(mq9_RL, 4) + ",";
  json += "\"r0KOhm\":" + calibrationJsonNumber(mq9_R0, 4) + ",";
  json += "\"rsKOhm\":" + calibrationJsonNumber(rs, 4) + ",";
  json += "\"ratio\":" + calibrationJsonNumber(ratio, 5) + ",";
  json += "\"ppmEstimated\":" + calibrationJsonNumber(ppm, 2) + ",";
  json += "\"calibrated\":" + String(mq9_calibrated ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleMQ9Config() {
  if (!requireWebAuthentication()) return;

  bool hasRl = server.hasArg("rl");
  bool hasR0 = server.hasArg("r0");
  if (!hasRl && !hasR0) {
    server.send(400, "text/plain", "Use rl=<kOhm> y/o r0=<kOhm>");
    return;
  }

  float newRl = mq9_RL;
  float newR0 = mq9_R0;

  if (hasRl) {
    newRl = server.arg("rl").toFloat();
    if (!validMq9Rl(newRl)) {
      server.send(400, "text/plain", "RL fuera de rango (0.1 a 1000 kOhm)");
      return;
    }
  }

  if (hasR0) {
    newR0 = server.arg("r0").toFloat();
    if (!validMq9R0(newR0)) {
      server.send(400, "text/plain", "R0 fuera de rango");
      return;
    }
  } else if (hasRl && fabsf(newRl - mq9_RL) > 0.000001f) {
    // R0 depende de la configuración usada para calcular Rs.
    // Cambiar RL sin proporcionar R0 invalida la calibración anterior.
    newR0 = NAN;
  }

  mq9_RL = newRl;
  mq9_R0 = newR0;
  mq9_calibrated = validMq9R0(mq9_R0);

  if (!saveMq9Calibration()) {
    server.send(500, "text/plain", "No se pudo guardar la configuración MQ-9");
    return;
  }

  String response = "MQ-9 actualizado. RL=" + String(mq9_RL, 4) + " kOhm R0=";
  response += mq9_calibrated ? String(mq9_R0, 4) + " kOhm" : "NO_CAL; ejecutar recalibracion";
  server.send(200, "text/plain", response);
}

float adc_to_volt(int raw) {
  return (raw * ADC_VREF) / 4095.0f;
}

float mq9_rs_from_adc(int raw) {
  float v = adc_to_volt(raw);
  if (v <= 0.05f) return 1e6f;
  if (v >= (ADC_VREF - 0.05f)) return 0.1f;
  return mq9_RL * ((ADC_VREF - v) / v);
}

float mq9_ppm_from_ratio(float ratio) {
  ratio = constrain(ratio, 0.01f, 100.0f);
  const float a = -0.42f, b = 1.92f;
  return powf(10.0f, a * log10f(ratio) + b);
}

// Timeout ampliado para llegar a 5 m (≈ 29 ms necesarios)
const unsigned long ULTRA_TIMEOUT_US = 40000;  // 40 ms → ~6.8 m teóricos

// Lectura "cruda" de distancia
float medirDistanciaRawCM() {
  // Pulso de disparo
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);

  // Esperamos el eco hasta ULTRA_TIMEOUT_US
  long dur = pulseIn(ECHO, HIGH, ULTRA_TIMEOUT_US);
  if (dur <= 0) return NAN;

  float dist = (dur * 0.0343f) / 2.0f;  // 0.0343 cm/µs a 20 °C aprox.
  return dist;
}

// Lectura filtrada: rango 2–500 cm + promedio de varias muestras
float medirDistanciaCM() {
  const int N = 5;          // número de muestras internas
  float vals[N];
  int valid = 0;

  for (int i = 0; i < N; i++) {
    float d = medirDistanciaRawCM();

    // Filtro de rango: HC-SR04 típico 2–400 cm, aquí extendemos a 500 cm
    if (isfinite(d) && d >= 2.0f && d <= 500.0f) {
      vals[valid++] = d;
    }

    delay(10);  // pequeña pausa entre disparos
  }

  if (valid == 0) {
    // todas fallaron → devolvemos NaN
    return NAN;
  }

  // Promedio simple de las válidas
  float sum = 0;
  for (int i = 0; i < valid; i++) {
    sum += vals[i];
  }
  return sum / valid;
}

float medirDistanciaEstableCM() {
  static float lastValid = NAN;

  float d = medirDistanciaCM();  // la filtrada

  if (isnan(d)) {
    // Si falla la medida, devolvemos el último valor válido
    return lastValid;
  } else {
    lastValid = d;
    return d;
  }
}

// DEMO MQ-9 (si quisieras usarla aparte)
float mq9_ppm_demo(int adcRaw) {
  return (adcRaw / 4095.0f) * 10000.0f;
}

// Elige la mejor temperatura disponible:
// 1) BME/BMP (last_bmp_t)
// 2) DHT22 (last_t)
// 3) último valor válido (temp_sat_valid) si ambos fallan
void updateTempSat() {
  float candidate = NAN;

  // Prioridad 1: BME/BMP
  if (isfinite(last_bmp_t)) {
    candidate = last_bmp_t;
  }
  // Prioridad 2: DHT22
  else if (isfinite(last_t)) {
    candidate = last_t;
  }

  if (isfinite(candidate)) {
    temp_sat = candidate;
    temp_sat_valid = candidate;
    temp_sat_is_fresh = true;
  } else {
    // No hay lectura nueva válida → reusar última buena si existe
    if (isfinite(temp_sat_valid)) {
      temp_sat = temp_sat_valid;
      temp_sat_is_fresh = false;
    } else {
      temp_sat = NAN;          // nunca hemos tenido una lectura válida
      temp_sat_is_fresh = false;
    }
  }

  // *** CLAVE: entregar el valor fusionado a last_t ***
  if (isfinite(temp_sat)) {
    last_t = temp_sat;
  }
}

// ---- WiFi ----
void connectWiFiSTA() {
  bool wasConnected = (WiFi.status() == WL_CONNECTED);
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[WiFi] Conectando STA");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 15000) {
    delay(250);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    staIPStr = WiFi.localIP().toString();
    ok_sta = true;
    Serial.println(String(" OK @ ") + staIPStr);
    if (!wasConnected || wifiFailureActive) {
      logSystemEvent(SYSLOG_INFO, SYSCOMP_NETWORK,
                     "WIFI_CONNECTED", "", "", "OK",
                     String("ssid=") + WIFI_SSID + ";ip=" + staIPStr +
                     ";rssi=" + String(WiFi.RSSI()));
    }
    wifiFailureActive = false;
  } else {
    ok_sta = false;
    Serial.println(" fallo/timeout");
    if (!wifiFailureActive) {
      logSystemEvent(SYSLOG_WARNING, SYSCOMP_NETWORK,
                     "WIFI_CONNECT_FAILED", "", "", "FAIL",
                     String("ssid=") + WIFI_SSID + ";timeout_ms=15000");
    }
    wifiFailureActive = true;
  }
}

void startAP() {
  ok_ap = WiFi.softAP(AP_SSID, AP_PASS);
  delay(200);
  apIPStr = WiFi.softAPIP().toString();  // normalmente 192.168.4.1
  Serial.println(String("[AP] SSID: ") + AP_SSID + " Pass: " + AP_PASS + " IP: " + apIPStr);
  logSystemEvent(ok_ap ? SYSLOG_INFO : SYSLOG_ERROR,
                 SYSCOMP_NETWORK,
                 ok_ap ? "AP_STARTED" : "AP_START_FAILED",
                 "", "", ok_ap ? "OK" : "FAIL",
                 String("ssid=") + AP_SSID + ";ip=" + apIPStr);
}

// ---- Tiempo (NTP) ----
bool syncTimeIfNeeded() {
  if (getLocalTime(&timeinfo)) {
    if (!timeSyncSuccessLogged) {
      logSystemEvent(SYSLOG_INFO, SYSCOMP_SYSTEM,
                     "TIME_AVAILABLE", "", "", "OK", "source=NTP/system_clock");
      timeSyncSuccessLogged = true;
      timeSyncFailureLogged = false;
    }
    return true;
  }
  configTime(-5 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  for (int i = 0; i < 20; i++) {
    if (getLocalTime(&timeinfo)) {
      if (!timeSyncSuccessLogged) {
        logSystemEvent(SYSLOG_INFO, SYSCOMP_SYSTEM,
                       "TIME_SYNCED", "", "", "OK", "source=NTP");
        timeSyncSuccessLogged = true;
        timeSyncFailureLogged = false;
      }
      return true;
    }
    delay(250);
  }
  if (!timeSyncFailureLogged) {
    logSystemEvent(SYSLOG_WARNING, SYSCOMP_SYSTEM,
                   "TIME_SYNC_FAILED", "", "", "FAIL", "NTP sin respuesta");
    timeSyncFailureLogged = true;
  }
  return false;
}

// ---- SD helpers ----
bool initSD() {
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  Serial.print("[SD] Montando a 8MHz...");
  if (!SD.begin(SD_CS, SPI, 8000000)) {
    Serial.print(" fallo, reintentando a 4MHz...");
    if (!SD.begin(SD_CS, SPI, 4000000)) {
      Serial.println(" ❌");
      return false;
    }
  }
  Serial.println(" OK");
  return true;
}

void ensureLogFileForToday() {
  char datePart[9];  // YYYYMMDD
  strftime(datePart, sizeof(datePart), "%Y%m%d", &timeinfo);
  String wanted = "/data_station101_" + String(datePart) + ".csv";
  if (logFilePath == wanted) return;
  logFilePath = wanted;
  if (!SD.exists(logFilePath)) {
    File f = SD.open(logFilePath, FILE_WRITE);
    if (f) {
      f.println("YYYY-MM-DD,HH:MM:SS,dist_cm,temp_c,hum_pct,pitch_deg,gas_ppm,ax_ms2,gx_dps,ay_ms2,bmp_t_c,bmp_p_hpa,bmp_alt_m");
      f.close();
      Serial.println(String("[SD] Archivo creado: ") + logFilePath);
    } else {
      Serial.println("[SD] No se pudo crear archivo de log");
    }
  }
}

// ---- Cola persistente de backfill en microSD ----
bool saveBackfillState() {
  if (!ok_sd) return false;

  if (SD.exists(BACKFILL_STATE_TMP)) SD.remove(BACKFILL_STATE_TMP);
  File f = SD.open(BACKFILL_STATE_TMP, FILE_WRITE);
  if (!f) {
    Serial.println("[BACKFILL] No se pudo crear el estado temporal");
    return false;
  }

  f.println("VERSION=1");
  f.print("OFFSET=");  f.println(backfillReadOffset);
  f.print("PENDING="); f.println(backfillCount);
  f.print("SENT=");    f.println(backfillSentTotal);
  f.flush();
  f.close();

  if (SD.exists(BACKFILL_STATE_FILE)) SD.remove(BACKFILL_STATE_FILE);
  if (!SD.rename(BACKFILL_STATE_TMP, BACKFILL_STATE_FILE)) {
    Serial.println("[BACKFILL] No se pudo activar el archivo de estado");
    return false;
  }
  return true;
}

uint32_t countQueueLinesFromOffset(uint32_t offset) {
  if (!ok_sd || !SD.exists(BACKFILL_FILE)) return 0;

  File f = SD.open(BACKFILL_FILE, FILE_READ);
  if (!f) return 0;
  if (offset > f.size() || !f.seek(offset)) {
    f.close();
    return 0;
  }

  uint32_t count = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) count++;
    yield();
  }
  f.close();
  return count;
}

bool loadBackfillState() {
  backfillReadOffset = 0;
  backfillCount = 0;
  backfillSentTotal = 0;

  if (!ok_sd || !SD.exists(BACKFILL_STATE_FILE)) return false;

  File f = SD.open(BACKFILL_STATE_FILE, FILE_READ);
  if (!f) return false;

  bool versionOk = false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    int eq = line.indexOf('=');
    if (eq <= 0) continue;
    String key = line.substring(0, eq);
    String value = line.substring(eq + 1);
    if (key == "VERSION") versionOk = (value.toInt() == 1);
    else if (key == "OFFSET") backfillReadOffset = strtoul(value.c_str(), nullptr, 10);
    else if (key == "PENDING") backfillCount = strtoul(value.c_str(), nullptr, 10);
    else if (key == "SENT") backfillSentTotal = strtoul(value.c_str(), nullptr, 10);
  }
  f.close();
  return versionOk;
}

void resetBackfillQueueFiles() {
  if (ok_sd) {
    if (SD.exists(BACKFILL_FILE)) SD.remove(BACKFILL_FILE);
    if (SD.exists(BACKFILL_STATE_FILE)) SD.remove(BACKFILL_STATE_FILE);
    if (SD.exists(BACKFILL_STATE_TMP)) SD.remove(BACKFILL_STATE_TMP);
  }
  backfillReadOffset = 0;
  backfillCount = 0;
}

bool initializeBackfillQueue() {
  if (!ok_sd) return false;

  bool stateLoaded = loadBackfillState();
  if (!SD.exists(BACKFILL_FILE)) {
    resetBackfillQueueFiles();
    Serial.println("[BACKFILL] Cola SD vacía");
    return true;
  }

  File f = SD.open(BACKFILL_FILE, FILE_READ);
  if (!f) {
    Serial.println("[BACKFILL] No se pudo abrir la cola al inicializar");
    return false;
  }
  uint32_t queueSize = f.size();
  f.close();

  if (!stateLoaded || backfillReadOffset > queueSize) {
    Serial.println("[BACKFILL] Estado ausente/inválido; se reconstruye desde el inicio");
    backfillReadOffset = 0;
  }

  // La cantidad real se obtiene del archivo para recuperar escrituras previas
  // a un reinicio ocurrido antes de guardar el estado.
  backfillCount = countQueueLinesFromOffset(backfillReadOffset);

  if (backfillCount == 0) {
    resetBackfillQueueFiles();
  } else {
    saveBackfillState();
  }

  Serial.printf("[BACKFILL] Cola SD recuperada: pending=%lu offset=%lu bytes=%lu\n",
                (unsigned long)backfillCount,
                (unsigned long)backfillReadOffset,
                (unsigned long)queueSize);
  logSystemEvent(SYSLOG_INFO, SYSCOMP_STORAGE,
                 "QUEUE_RECOVERED", "", "", "OK",
                 String("pending=") + String(backfillCount) +
                 ";offset=" + String(backfillReadOffset) +
                 ";bytes=" + String(queueSize));
  return true;
}

bool pushEmergencyBackfill(const String& row) {
  if (emergencyCount == EMERGENCY_CAPACITY) {
    emergencyTail = (emergencyTail + 1) % EMERGENCY_CAPACITY;
    emergencyCount--;
    emergencyDropped++;
    Serial.println("[BACKFILL] RAM emergencia llena; se descarta el registro más antiguo");
    if (emergencyDropped == 1 || emergencyDropped >= lastEmergencyDroppedLogged + 10) {
      lastEmergencyDroppedLogged = emergencyDropped;
      logSystemEvent(SYSLOG_CRITICAL, SYSCOMP_STORAGE,
                     "DATA_DROPPED", "", "", "FAIL",
                     String("reason=emergency_buffer_full;dropped=") + String(emergencyDropped));
    }
  }

  copyRowToEmergencySlot(emergencyHead, row);
  emergencyHead = (emergencyHead + 1) % EMERGENCY_CAPACITY;
  emergencyCount++;
  Serial.printf("[BACKFILL] Registro temporal en RAM de emergencia (%u/%u)\n",
                (unsigned)emergencyCount,
                (unsigned)EMERGENCY_CAPACITY);
  if (!emergencyBufferActive) {
    emergencyBufferActive = true;
    logSystemEvent(SYSLOG_WARNING, SYSCOMP_STORAGE,
                   "EMERGENCY_BUFFER_STARTED", "", "", "DEGRADED",
                   String("capacity=") + String(EMERGENCY_CAPACITY));
  }
  return true;
}

bool appendBackfillRowToSD(const String& row) {
  if (!ok_sd) return false;

  File f = SD.open(BACKFILL_FILE, FILE_APPEND);
  if (!f) {
    Serial.println("[BACKFILL] No se pudo abrir la cola SD para escritura");
    ok_sd = false;
    sdFailureActive = true;
    logSystemEvent(SYSLOG_ERROR, SYSCOMP_STORAGE,
                   "QUEUE_WRITE_FAILED", "", "", "FAIL",
                   "No se pudo abrir /backfill_queue.csv");
    return false;
  }

  size_t written = f.println(row);
  f.flush();
  f.close();
  if (written == 0) {
    Serial.println("[BACKFILL] Escritura vacía en la cola SD");
    ok_sd = false;
    sdFailureActive = true;
    logSystemEvent(SYSLOG_ERROR, SYSCOMP_STORAGE,
                   "QUEUE_WRITE_FAILED", "", "", "FAIL",
                   "La escritura de la fila devolvio cero bytes");
    return false;
  }

  backfillCount++;
  saveBackfillState();
  return true;
}

bool appendBackfillRow(const String& row) {
  offlineSampleCount++;

  if (appendBackfillRowToSD(row)) {
    Serial.printf("[BACKFILL] Registro #%lu persistido en SD (pendientes=%lu)\n",
                  (unsigned long)offlineSampleCount,
                  (unsigned long)backfillCount);
    return true;
  }

  Serial.println("[BACKFILL] SD no disponible; usando RAM de emergencia");
  return pushEmergencyBackfill(row);
}

bool flushEmergencyBackfill() {
  if (!ok_sd || emergencyCount == 0) return emergencyCount == 0;

  uint8_t recordsToFlush = emergencyCount;
  while (emergencyCount > 0) {
    String row = String(emergencyBuf[emergencyTail]);
    row.trim();
    if (row.length() > 0 && !appendBackfillRowToSD(row)) {
      return false;
    }
    emergencyTail = (emergencyTail + 1) % EMERGENCY_CAPACITY;
    emergencyCount--;
    yield();
  }

  Serial.println("[BACKFILL] RAM de emergencia transferida a la cola SD");
  emergencyBufferActive = false;
  logSystemEvent(SYSLOG_INFO, SYSCOMP_STORAGE,
                 "EMERGENCY_BUFFER_FLUSHED", "", "", "OK",
                 String("records=") + String(recordsToFlush));
  return true;
}

void processSdRecovery(unsigned long now) {
  if (ok_sd) {
    if (emergencyCount > 0) flushEmergencyBackfill();
    return;
  }

  if (now - lastSdRetry < SD_RETRY_MS) return;
  lastSdRetry = now;
  sdRemountAttempts++;
  Serial.println("[SD] Reintentando montaje para recuperar la cola...");
  if (sdRemountAttempts == 1) {
    logSystemEvent(SYSLOG_WARNING, SYSCOMP_STORAGE,
                   "SD_REMOUNT_ATTEMPT", "", "", "RETRY",
                   "attempt=1");
  }
  ok_sd = initSD();
  if (ok_sd) {
    sdFailureActive = false;
    if (!SD.exists(SYSTEM_LOG_DIR)) SD.mkdir(SYSTEM_LOG_DIR);
    flushBufferedSystemEvents();
    logSystemEvent(SYSLOG_INFO, SYSCOMP_STORAGE,
                   "SD_RECOVERED", "", "", "OK",
                   String("attempts=") + String(sdRemountAttempts));
    initializeBackfillQueue();
    flushEmergencyBackfill();
    maintainSystemLogRetention();
    sdRemountAttempts = 0;
  }
}

// ---- Log de eventos de conectividad ----
void logConnEvent(const char* evName) {
  if (!ok_sd) return;
  if (!getLocalTime(&timeinfo) && !syncTimeIfNeeded()) {
    Serial.println("[CONNLOG] Sin hora válida, no se registra evento");
    return;
  }
  char datePart[11];
  char timePart[9];
  strftime(datePart, sizeof(datePart), "%Y-%m-%d", &timeinfo);
  strftime(timePart, sizeof(timePart), "%H:%M:%S", &timeinfo);

  File f = SD.open(CONNLOG_FILE, FILE_APPEND);
  if (!f) {
    Serial.println("[CONNLOG] No se pudo abrir archivo de eventos");
    return;
  }
  f.print(datePart);
  f.print(',');
  f.print(timePart);
  f.print(',');
  f.println(evName);
  f.close();
}

// ---- Logging principal (histórico diario + cola persistente) ----
void logSampleToSD() {
  if (ok_sd && logFilePath == "") {
    if (syncTimeIfNeeded()) ensureLogFileForToday();
    if (logFilePath == "") logFilePath = "/pending_timestamp.csv";
  }

  if (ok_sd && getLocalTime(&timeinfo)) ensureLogFileForToday();

  char datePart[11] = "0000-00-00";
  char timePart[9]  = "00:00:00";
  if (getLocalTime(&timeinfo)) {
    strftime(datePart, sizeof(datePart), "%Y-%m-%d", &timeinfo);
    strftime(timePart, sizeof(timePart), "%H:%M:%S", &timeinfo);
  }

  String row;
  row.reserve(160);
  row += datePart; row += ',';
  row += timePart; row += ',';
  row += String(isnan(last_dist)   ? 0 : last_dist);   row += ',';
  row += String(isnan(last_t)      ? 0 : last_t);      row += ',';
  row += String(isnan(last_h)      ? 0 : last_h);      row += ',';
  row += String(isnan(last_pitch)  ? 0 : last_pitch);  row += ',';
  row += String(isnan(last_mq9ppm) ? 0 : last_mq9ppm); row += ',';
  row += String(isnan(last_ax)     ? 0 : last_ax);     row += ',';
  row += String(isnan(last_gx)     ? 0 : last_gx);     row += ',';
  row += String(isnan(last_ay)     ? 0 : last_ay);     row += ',';
  row += String(isnan(last_bmp_t)  ? 0 : last_bmp_t);  row += ',';
  row += String(isnan(last_bmp_p)  ? 0 : last_bmp_p);  row += ',';
  row += String(isnan(last_bmp_alt)? 0 : last_bmp_alt);

  lastSampleCsvRow = row;
  lastSampleQueued = false;

  // Histórico local: independiente de la cola de retransmisión.
  if (ok_sd) {
    File f = SD.open(logFilePath, FILE_APPEND);
    if (!f) {
      Serial.println("[SD] No se pudo abrir el archivo de log");
    } else {
      f.println(row);
      f.flush();
      f.close();
    }
  } else {
    Serial.println("[SD] Histórico no escrito: tarjeta no disponible");
  }

  // En OFFLINE/BACKFILL, o si el enlace WiFi ya cayó aunque el estado aún no
  // se haya actualizado, la muestra queda pendiente de retransmisión.
  if (connState == CS_OFFLINE || connState == CS_BACKFILL ||
      WiFi.status() != WL_CONNECTED) {
    lastSampleQueued = appendBackfillRow(row);
  }
}

// ---- SD stats ----
uint64_t sumUsedBytes(fs::FS& fs, const String& path = "/") {
  uint64_t total = 0;
  File root = fs.open(path);
  if (!root) return 0;
  File f = root.openNextFile();
  while (f) {
    if (f.isDirectory()) {
      total += sumUsedBytes(fs, String("/") + f.name());
    } else {
      total += f.size();
    }
    f = root.openNextFile();
  }
  return total;
}

// ---- Trazabilidad operativa diaria ----
const char* systemLogSeverityName(uint8_t severity) {
  switch (severity) {
    case SYSLOG_DEBUG:    return "DEBUG";
    case SYSLOG_INFO:     return "INFO";
    case SYSLOG_WARNING:  return "WARNING";
    case SYSLOG_ERROR:    return "ERROR";
    case SYSLOG_CRITICAL: return "CRITICAL";
    default:              return "UNKNOWN";
  }
}

const char* systemLogComponentName(uint8_t component) {
  switch (component) {
    case SYSCOMP_SYSTEM:        return "SYSTEM";
    case SYSCOMP_NETWORK:       return "NETWORK";
    case SYSCOMP_STORAGE:       return "STORAGE";
    case SYSCOMP_SYNC:          return "SYNC";
    case SYSCOMP_WEB:           return "WEB";
    case SYSCOMP_CALIBRATION:   return "CALIBRATION";
    case SYSCOMP_SENSOR_HEALTH: return "SENSOR_HEALTH";
    default:                    return "UNKNOWN";
  }
}

const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:   return "POWER_ON";
    case ESP_RST_EXT:       return "EXTERNAL_RESET";
    case ESP_RST_SW:        return "SOFTWARE_RESET";
    case ESP_RST_PANIC:     return "PANIC_EXCEPTION";
    case ESP_RST_INT_WDT:   return "INTERRUPT_WATCHDOG";
    case ESP_RST_TASK_WDT:  return "TASK_WATCHDOG";
    case ESP_RST_WDT:       return "OTHER_WATCHDOG";
    case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO_RESET";
    default:                return "UNKNOWN";
  }
}

void initializeSystemIdentity() {
  systemPrefs.begin("sat_system", false);
  uint32_t previousBoot = systemPrefs.getULong("boot_count", 0);
  bootId = previousBoot + 1;
  systemPrefs.putULong("boot_count", bootId);
  systemPrefs.end();

  esp_reset_reason_t reason = esp_reset_reason();
  lastResetReason = resetReasonName(reason);
}

bool getSystemLogTime(tm& outTm, time_t& epochOut) {
  epochOut = time(nullptr);
  if (epochOut < 1700000000) return false;
  localtime_r(&epochOut, &outTm);
  return true;
}

String csvQuoted(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 4);
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value[i];
    if (c == '"') escaped += "\"\"";
    else if (c == '\r' || c == '\n') escaped += ' ';
    else escaped += c;
  }
  return String("\"") + escaped + "\"";
}

String jsonEscapeText(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value[i];
    if (c == '"' || c == '\\') {
      escaped += '\\';
      escaped += c;
    } else if (c == '\r' || c == '\n') {
      escaped += ' ';
    } else {
      escaped += c;
    }
  }
  return escaped;
}

String systemLogPath(bool timeValid, const tm& eventTm) {
  if (timeValid) {
    char datePart[9];
    strftime(datePart, sizeof(datePart), "%Y%m%d", &eventTm);
    return String(SYSTEM_LOG_DIR) + "/system_" + datePart + ".csv";
  }
  char path[64];
  snprintf(path, sizeof(path), "%s/system_00000000_boot_%06lu_unsynced.csv",
           SYSTEM_LOG_DIR, (unsigned long)bootId);
  return String(path);
}

bool ensureSystemLogDirectory() {
  if (!ok_sd) return false;
  if (SD.exists(SYSTEM_LOG_DIR)) return true;
  return SD.mkdir(SYSTEM_LOG_DIR);
}

bool writeSystemLogRow(const String& row, const String& path) {
  if (!ok_sd || !ensureSystemLogDirectory()) return false;

  bool newFile = !SD.exists(path);
  File f = SD.open(path, FILE_APPEND);
  if (!f) {
    ok_sd = false;
    sdFailureActive = true;
    return false;
  }
  if (newFile) f.println(SYSTEM_LOG_HEADER);
  size_t written = f.println(row);
  f.flush();
  f.close();
  if (written == 0) {
    ok_sd = false;
    sdFailureActive = true;
    return false;
  }
  return true;
}

void bufferSystemLogRow(const String& row,
                        uint8_t severity,
                        const String& path) {
  int target = -1;
  if (systemEventBufferCount < SYSTEM_EVENT_BUFFER_CAPACITY) {
    target = systemEventBufferCount++;
  } else if (severity >= SYSLOG_ERROR) {
    for (uint8_t i = 0; i < systemEventBufferCount; ++i) {
      if (systemEventBuffer[i].severity < SYSLOG_ERROR) {
        target = i;
        break;
      }
    }
  }

  if (target < 0) {
    systemEventDropped++;
    return;
  }
  if (systemEventBufferCount == SYSTEM_EVENT_BUFFER_CAPACITY &&
      severity >= SYSLOG_ERROR) {
    systemEventDropped++;
  }

  size_t n = row.length();
  if (n >= sizeof(systemEventBuffer[target].row)) {
    n = sizeof(systemEventBuffer[target].row) - 1;
  }
  memcpy(systemEventBuffer[target].row, row.c_str(), n);
  systemEventBuffer[target].row[n] = '\0';
  size_t pLen = path.length();
  if (pLen >= sizeof(systemEventBuffer[target].path)) {
    pLen = sizeof(systemEventBuffer[target].path) - 1;
  }
  memcpy(systemEventBuffer[target].path, path.c_str(), pLen);
  systemEventBuffer[target].path[pLen] = '\0';
  systemEventBuffer[target].severity = static_cast<uint8_t>(severity);
}

bool flushBufferedSystemEvents() {
  if (!ok_sd || systemEventBufferCount == 0) return systemEventBufferCount == 0;

  uint8_t retained = 0;
  for (uint8_t i = 0; i < systemEventBufferCount; ++i) {
    String row(systemEventBuffer[i].row);
    String path(systemEventBuffer[i].path);
    if (!writeSystemLogRow(row, path)) {
      if (retained != i) systemEventBuffer[retained] = systemEventBuffer[i];
      retained++;
      for (uint8_t j = i + 1; j < systemEventBufferCount; ++j) {
        if (retained != j) systemEventBuffer[retained] = systemEventBuffer[j];
        retained++;
      }
      systemEventBufferCount = retained;
      return false;
    }
  }
  systemEventBufferCount = 0;
  return true;
}

void logSystemEvent(uint8_t severity,
                    uint8_t component,
                    const char* event,
                    const char* fromState,
                    const char* toState,
                    const char* result,
                    const String& details) {
  tm eventTm = {};
  time_t epochValue = 0;
  bool timeValid = getSystemLogTime(eventTm, epochValue);

  char datePart[11] = "0000-00-00";
  char timePart[9] = "00:00:00";
  if (timeValid) {
    strftime(datePart, sizeof(datePart), "%Y-%m-%d", &eventTm);
    strftime(timePart, sizeof(timePart), "%H:%M:%S", &eventTm);
  }

  String row;
  row.reserve(300);
  row += csvQuoted(datePart); row += ',';
  row += csvQuoted(timePart); row += ',';
  row += String(timeValid ? static_cast<unsigned long>(epochValue) : 0UL); row += ',';
  row += csvQuoted(timeValid ? "NTP" : "UNSYNCED"); row += ',';
  row += String(bootId); row += ',';
  row += String(millis()); row += ',';
  row += csvQuoted(systemLogSeverityName(severity)); row += ',';
  row += csvQuoted(systemLogComponentName(component)); row += ',';
  row += csvQuoted(event ? event : ""); row += ',';
  row += csvQuoted(fromState ? fromState : ""); row += ',';
  row += csvQuoted(toState ? toState : ""); row += ',';
  row += csvQuoted(result ? result : ""); row += ',';
  row += csvQuoted(details);

  lastSystemEvent = event ? event : "";
  lastSystemSeverity = systemLogSeverityName(severity);

  Serial.printf("[SYSLOG][%s][%s] %s %s %s\n",
                systemLogSeverityName(severity),
                systemLogComponentName(component),
                event ? event : "",
                result ? result : "",
                details.c_str());

  String path = systemLogPath(timeValid, eventTm);
  if (!writeSystemLogRow(row, path)) {
    bufferSystemLogRow(row, severity, path);
  }
}

bool isSystemLogBasename(const String& name) {
  if (name.length() == 0 || name.indexOf("..") >= 0 || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0) {
    return false;
  }
  return name.startsWith("system_") && name.endsWith(".csv");
}

String systemLogListJson() {
  if (!ok_sd || !SD.exists(SYSTEM_LOG_DIR)) return "[]";
  File root = SD.open(SYSTEM_LOG_DIR);
  if (!root || !root.isDirectory()) return "[]";

  String json = "[";
  bool first = true;
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String name = file.name();
      int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);
      if (isSystemLogBasename(name)) {
        if (!first) json += ',';
        first = false;
        json += "{\"name\":\"" + jsonEscapeText(name) + "\",\"size\":" + String(file.size()) + "}";
      }
    }
    file = root.openNextFile();
  }
  root.close();
  json += ']';
  return json;
}

void maintainSystemLogRetention() {
  if (!ok_sd || !SD.exists(SYSTEM_LOG_DIR)) return;

  while (true) {
    File root = SD.open(SYSTEM_LOG_DIR);
    if (!root || !root.isDirectory()) return;

    uint16_t count = 0;
    String oldest = "";
    File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        String name = file.name();
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        if (isSystemLogBasename(name)) {
          count++;
          if (oldest.length() == 0 || name < oldest) oldest = name;
        }
      }
      file = root.openNextFile();
    }
    root.close();

    if (count <= SYSTEM_LOG_MAX_FILES || oldest.length() == 0) return;
    String path = String(SYSTEM_LOG_DIR) + "/" + oldest;
    if (!SD.remove(path)) return;
    logSystemEvent(SYSLOG_INFO, SYSCOMP_STORAGE,
                   "LOG_RETENTION_DELETE", "", "", "OK",
                   String("file=") + oldest + ";max_files=" + String(SYSTEM_LOG_MAX_FILES));
  }
}

void recordThingSpeakResult(int code, const char* mode) {
  lastThingSpeakCode = code;
  bool success = (code == 200 || code == 202);

  if (success) {
    if (thingSpeakStateKnown && !thingSpeakAvailable) {
      logSystemEvent(SYSLOG_INFO, SYSCOMP_SYNC,
                     "THINGSPEAK_RECOVERED", "", "", "OK",
                     String("mode=") + mode + ";code=" + String(code));
    }
    thingSpeakAvailable = true;
    thingSpeakStateKnown = true;
    backfillSendFailures = 0;
    return;
  }

  if (!thingSpeakStateKnown || thingSpeakAvailable) {
    logSystemEvent(SYSLOG_WARNING, SYSCOMP_SYNC,
                   "THINGSPEAK_UNAVAILABLE", "", "", "FAIL",
                   String("mode=") + mode + ";code=" + String(code));
  }
  thingSpeakAvailable = false;
  thingSpeakStateKnown = true;
}

void processSystemLogMaintenance(unsigned long now) {
  if (ok_sd && systemEventBufferCount > 0) flushBufferedSystemEvents();

  if (now - lastHealthSnapshotMs >= HEALTH_SNAPSHOT_INTERVAL_MS) {
    lastHealthSnapshotMs = now;
    uint64_t total = ok_sd ? SD.cardSize() : 0;
    uint64_t used = ok_sd ? sumUsedBytes(SD, "/") : 0;
    uint64_t freeBytes = total > used ? total - used : 0;
    String details = String("firmware=") + FIRMWARE_VERSION +
                     ";heap=" + String(ESP.getFreeHeap()) +
                     ";sd_ok=" + (ok_sd ? "1" : "0") +
                     ";sd_free=" + String(static_cast<unsigned long long>(freeBytes)) +
                     ";wifi=" + (WiFi.status() == WL_CONNECTED ? "1" : "0") +
                     ";rssi=" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0) +
                     ";state=" + stateName(connState) +
                     ";pending_sd=" + String(backfillCount) +
                     ";pending_ram=" + String(emergencyCount) +
                     ";backfill_dropped=" + String(emergencyDropped) +
                     ";backfill_cause=" + lastBackfillCause +
                     ";bulk_size=" + String(TS_BACKFILL_BATCH_SIZE) +
                     ";bulk_duration_ms=" + String(lastBulkDurationMs) +
                     ";wifi_losses=" + String(wifiLinkLossCount) +
                     ";ts_failures=" + String(thingSpeakFailureCount) +
                     ";log_buffer=" + String(systemEventBufferCount) +
                     ";log_dropped=" + String(systemEventDropped);
    logSystemEvent(SYSLOG_INFO, SYSCOMP_SYSTEM,
                   "HEALTH_SNAPSHOT", "", "", "OK", details);
  }

  if (now - lastLogRetentionMs >= LOG_RETENTION_INTERVAL_MS) {
    lastLogRetentionMs = now;
    maintainSystemLogRetention();
  }
}

const char PAGE_SYSTEM_LOGS[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>DPVAD/SAT - Trazabilidad</title>
<style>
body{font-family:system-ui,Segoe UI,Arial;margin:24px;color:#111}
h1{margin:0 0 8px} .small{color:#555;font-size:12px}
.card{border:1px solid #ccc;border-radius:8px;padding:12px;margin:12px 0}
table{border-collapse:collapse;width:100%}td,th{border:1px solid #ccc;padding:6px 8px;text-align:left}
button{padding:8px 12px}
</style></head><body>
<h1>Trazabilidad operativa</h1>
<p class="small">Portal independiente, solo lectura. No contiene mediciones de sensores.</p>
<div class="card" id="status">Cargando estado...</div>
<button onclick="loadAll()">Refrescar</button>
<table><thead><tr><th>Archivo</th><th>Tamaño</th><th>Acción</th></tr></thead><tbody id="rows"></tbody></table>
<script>
function fmt(n){if(n<1024)return n+' B';if(n<1048576)return(n/1024).toFixed(1)+' KB';return(n/1048576).toFixed(2)+' MB'}
async function loadStatus(){const r=await fetch('/status');const s=await r.json();document.getElementById('status').innerHTML=
`<b>Firmware:</b> ${s.firmware}<br><b>Boot:</b> ${s.bootId}<br><b>Último reset:</b> ${s.resetReason}<br>`+
`<b>Estado:</b> ${s.state}<br><b>Wi-Fi:</b> ${s.wifi?'Conectado':'Desconectado'}<br>`+
`<b>SD:</b> ${s.sd?'OK':'Falla'}<br><b>Pendientes:</b> ${s.pending}<br>`+
`<b>Causa de BACKFILL:</b> ${s.lastBackfillCause}<br><b>Lote:</b> ${s.backfillBatchSize} registros<br>`+
`<b>Última latencia Bulk:</b> ${s.lastBulkDurationMs} ms<br><b>Pérdidas Wi-Fi:</b> ${s.wifiLinkLossCount}<br>`+
`<b>Fallos ThingSpeak:</b> ${s.thingSpeakFailureCount}<br><b>Último evento:</b> ${s.lastSeverity} / ${s.lastEvent}<br>`+
`<b>Eventos en RAM:</b> ${s.bufferedEvents}<br><b>Eventos perdidos:</b> ${s.droppedEvents}`}
async function loadFiles(){const r=await fetch('/list');const a=await r.json();const rows=document.getElementById('rows');rows.innerHTML='';if(!a.length){rows.innerHTML='<tr><td colspan="3">No hay archivos</td></tr>';return}for(const f of a){const tr=document.createElement('tr');tr.innerHTML=`<td>${f.name}</td><td>${fmt(f.size)}</td><td><a href="/download?file=${encodeURIComponent(f.name)}">Descargar</a></td>`;rows.appendChild(tr)}}
function loadAll(){loadStatus();loadFiles()}loadAll();
</script></body></html>
)HTML";

bool requireLogAuthentication() {
  if (logServer.authenticate(LOG_WEB_USER, LOG_WEB_PASSWORD)) return true;
  logServer.requestAuthentication();
  return false;
}

void sendSystemLogIndex() {
  if (!requireLogAuthentication()) return;
  logServer.send_P(200, "text/html", PAGE_SYSTEM_LOGS);
}

void sendSystemLogList() {
  if (!requireLogAuthentication()) return;
  logServer.send(200, "application/json", systemLogListJson());
}

void sendSystemLogStatus() {
  if (!requireLogAuthentication()) return;
  String json = "{";
  json += "\"firmware\":\"" + jsonEscapeText(FIRMWARE_VERSION) + "\",";
  json += "\"bootId\":" + String(bootId) + ',';
  json += "\"resetReason\":\"" + jsonEscapeText(lastResetReason) + "\",";
  json += "\"state\":\"" + String(stateName(connState)) + "\",";
  json += "\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ',';
  json += "\"sd\":" + String(ok_sd ? "true" : "false") + ',';
  json += "\"pending\":" + String(backfillCount + emergencyCount) + ',';
  json += "\"backfillBatchSize\":" + String(TS_BACKFILL_BATCH_SIZE) + ',';
  json += "\"lastBulkDurationMs\":" + String(lastBulkDurationMs) + ',';
  json += "\"lastBackfillCause\":\"" + jsonEscapeText(lastBackfillCause) + "\",";
  json += "\"wifiLinkLossCount\":" + String(wifiLinkLossCount) + ',';
  json += "\"thingSpeakFailureCount\":" + String(thingSpeakFailureCount) + ',';
  json += "\"bufferedEvents\":" + String(systemEventBufferCount) + ',';
  json += "\"droppedEvents\":" + String(systemEventDropped) + ',';
  json += "\"lastEvent\":\"" + jsonEscapeText(lastSystemEvent) + "\",";
  json += "\"lastSeverity\":\"" + jsonEscapeText(lastSystemSeverity) + "\"}";
  logServer.send(200, "application/json", json);
}

void handleSystemLogDownload() {
  if (!requireLogAuthentication()) return;
  if (!logServer.hasArg("file")) {
    logServer.send(400, "text/plain", "Missing file");
    return;
  }
  String name = logServer.arg("file");
  name.trim();
  if (!isSystemLogBasename(name)) {
    logServer.send(400, "text/plain", "Invalid file");
    return;
  }
  String path = String(SYSTEM_LOG_DIR) + "/" + name;
  if (!ok_sd || !SD.exists(path)) {
    logServer.send(404, "text/plain", "Not found");
    return;
  }
  File f = SD.open(path, FILE_READ);
  if (!f) {
    logServer.send(500, "text/plain", "Open failed");
    return;
  }
  logServer.sendHeader("Content-Disposition", String("attachment; filename=\"") + name + "\"");
  logServer.streamFile(f, "text/csv");
  f.close();
  logSystemEvent(SYSLOG_INFO, SYSCOMP_WEB,
                 "SYSTEM_LOG_DOWNLOADED", "", "", "OK",
                 String("file=") + name);
}

void printBackfillDiag() {
  // --- Memoria RAM disponible ---
  uint32_t freeHeap = ESP.getFreeHeap();
  Serial.printf("[MEM] Heap libre: %u bytes\n", freeHeap);

  // --- Verificación del archivo de pérdida de conexión (CONNLOG_FILE) ---
  if (!ok_sd) {
    Serial.println("[CONNLOG] SD NO OK, no puedo verificar el archivo de eventos.");
    return;
  }

  if (SD.exists(CONNLOG_FILE)) {
    File f = SD.open(CONNLOG_FILE, FILE_READ);
    if (f) {
      size_t sz = f.size();
      f.close();
      Serial.printf("[CONNLOG] %s existe, size=%u bytes\n",
                    CONNLOG_FILE, (unsigned)sz);
    } else {
      Serial.printf("[CONNLOG] %s existe pero NO se pudo abrir\n", CONNLOG_FILE);
    }
  } else {
    Serial.printf("[CONNLOG] %s NO existe aún (ningún evento registrado)\n", CONNLOG_FILE);
  }
}

// Cantidad pendiente total: SD persistente + RAM de emergencia.
uint32_t countBackfillLines() {
  uint32_t total = backfillCount + emergencyCount;
  Serial.printf("[BACKFILL] pending SD=%lu RAM=%u total=%lu\n",
                (unsigned long)backfillCount,
                (unsigned)emergencyCount,
                (unsigned long)total);
  return total;
}

// Sólo archivos diarios data_station101_YYYYMMDD.csv
bool isDataCsv(const String& name) {
  if (!name.endsWith(".csv")) return false;
  if (!name.startsWith("/")) return false;
  return name.indexOf("data_station101_") == 1;
}

// Detectar si un archivo es el del día actual
bool isTodayFile(const String& name) {
  if (!name.startsWith("/data_station101_")) return false;
  if (!name.endsWith(".csv")) return false;

  if (!getLocalTime(&timeinfo)) return false;

  char datePart[9];  // YYYYMMDD
  strftime(datePart, sizeof(datePart), "%Y%m%d", &timeinfo);
  String todayName = "/data_station101_" + String(datePart) + ".csv";

  return name == todayName;
}

String listCsvJSON() {
  String json = "[";
  File root = SD.open("/");
  if (!root) return "[]";
  File file = root.openNextFile();
  bool first = true;
  while (file) {
    String fn = String("/") + file.name();
    if (!file.isDirectory() && isDataCsv(fn)) {
      if (!first) json += ",";
      first = false;
      bool today = isTodayFile(fn);
      json += "{\"name\":\"" + fn + "\",\"size\":" + String(file.size()) +
              ",\"today\":" + (today ? String("true") : String("false")) + "}";
    }
    file = root.openNextFile();
  }
  json += "]";
  return json;
}

// ----------------- Web: página HTML -----------------
const char PAGE_INDEX[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>DPVAD/SAT - SD</title>
<style>
body{font-family:system-ui,Segoe UI,Arial;margin:24px;color:#111}
h1{margin:0 0 12px} button{padding:8px 12px}
table{border-collapse:collapse;margin-top:12px;width:100%}
td,th{border:1px solid #ccc;padding:6px 8px;text-align:left}
.small{color:#555;font-size:12px}
.bad{color:#b00}
</style></head><body>
<h1>Archivos en SD</h1>
<p class="small">Portal administrativo protegido. El navegador solicitará las credenciales de mantenimiento.</p>
<p id="ips" class="small">IPs: ...</p>
<p id="sdinfo" class="small">SD: ...</p>
<div>
<button id="refresh">Refrescar</button>
<button id="downloadAll">Descargar todo y borrar</button>
</div>
<table id="tbl"><thead><tr><th>Archivo</th><th>Tamaño</th><th>Acción</th></tr></thead><tbody></tbody></table>
<script>
function fmt(n){
  if(n<1024) return n+' B';
  if(n<1048576) return (n/1024).toFixed(1)+' KB';
  return (n/1048576).toFixed(2)+' MB';
}

async function load(){
  const tbody = document.querySelector('#tbl tbody');
  tbody.innerHTML = '';
  const response = await fetch('/list');
  if(response.status === 401){
    tbody.innerHTML='<tr><td colspan=3>Autenticación requerida</td></tr>';
    return;
  }
  const list = await response.json().catch(()=>[]);
  if(list.length===0){
    tbody.innerHTML='<tr><td colspan=3>No hay archivos</td></tr>';
    return;
  }
  for(const f of list){
    const tr = document.createElement('tr');
    const file = encodeURIComponent(f.name);
    const href = `/download?file=${file}&del=0`;

    let accionHtml;
    if (f.today) {
      accionHtml = '<span class="small bad">Protegido (hoy)</span> ' +
                   `<a href="${href}" download>Descargar</a>`;
    } else {
      accionHtml = `<a href="${href}" download>Descargar</a>`;
    }

    tr.innerHTML = `
      <td>${f.name}</td>
      <td>${fmt(f.size)}</td>
      <td>${accionHtml}</td>`;
    tbody.appendChild(tr);
  }
}

async function loadStats(){
  const response = await fetch('/stats');
  if(response.status === 401){
    document.getElementById('ips').textContent='Autenticación requerida';
    return;
  }
  const s = await response.json().catch(()=>null);
  if(!s){
    document.getElementById('ips').textContent='IPs: (sin datos)';
    return;
  }
  document.getElementById('ips').textContent =
    `IPs → STA: ${s.staIP}  |  AP: ${s.apIP}`;
  document.getElementById('sdinfo').textContent =
    `SD → Total: ${fmt(s.sdTotal)}  Usado: ${fmt(s.sdUsed)}  Libre: ${fmt(s.sdFree)}`;
}

document.getElementById('refresh').onclick = ()=>{
  load();
  loadStats();
};

document.getElementById('downloadAll').onclick = async ()=>{
  const response = await fetch('/list');
  if(response.status === 401){
    alert('Autenticación requerida.');
    return;
  }
  const list = await response.json().catch(()=>[]);
  if (!Array.isArray(list) || list.length === 0) {
    alert('No hay archivos para descargar.');
    return;
  }

  // Descarga masiva: del=1 borra en el servidor si NO es el archivo de hoy.
  for(const f of list){
    if (f.today) continue;
    const a = document.createElement('a');
    a.href = '/download?file='+encodeURIComponent(f.name)+'&del=1';
    a.download = '';
    document.body.appendChild(a);
    a.click();
    a.remove();
    await new Promise(r=>setTimeout(r,800));
  }
  setTimeout(()=>{
    load();
    loadStats();
  },1500);
};

load();
loadStats();
</script></body></html>
)HTML";

// ---- Web Handlers ----
bool requireWebAuthentication() {
  if (server.authenticate(WEB_ADMIN_USER, WEB_ADMIN_PASSWORD)) return true;
  server.requestAuthentication();
  return false;
}

void sendIndex() {
  if (!requireWebAuthentication()) return;
  server.send_P(200, "text/html", PAGE_INDEX);
}

void sendList() {
  if (!requireWebAuthentication()) return;
  server.send(200, "application/json", listCsvJSON());
}

void sendStats() {
  if (!requireWebAuthentication()) return;
  uint64_t sdTotal = 0, sdUsed = 0, sdFree = 0;
  sdTotal = SD.cardSize();
  sdUsed = sumUsedBytes(SD, "/");
  if (sdTotal > sdUsed) sdFree = sdTotal - sdUsed;
  else sdFree = 0;

  String json = "{";
  json += "\"staIP\":\"" + staIPStr + "\",";
  json += "\"apIP\":\"" + apIPStr + "\",";
  json += "\"sdTotal\":" + String((uint64_t)sdTotal) + ",";
  json += "\"sdUsed\":" + String((uint64_t)sdUsed) + ",";
  json += "\"sdFree\":" + String((uint64_t)sdFree) + ",";
  json += "\"calGeneration\":" + String(calProfile.generation) + ",";
  json += "\"tempDelta\":" + calibrationJsonNumber(last_temp_delta) + ",";
  json += "\"humidityDelta\":" + calibrationJsonNumber(last_hum_delta) + "}";
  server.send(200, "application/json", json);
}

// Descarga con borrado opcional controlado por 'del'
void handleDownload() {
  // Todas las descargas requieren autenticacion administrativa valida
  if (!requireWebAuthentication()) return;
  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "Missing 'file'");
    return;
  }

  String filePath = server.arg("file");
  filePath.trim();
  if (!isDataCsv(filePath)) {
    server.send(400, "text/plain", "Invalid file");
    return;
  }
  if (!SD.exists(filePath)) {
    server.send(404, "text/plain", "Not found");
    return;
  }

  // del=0 → descarga manual (NO borrar)
  // del=1 → descarga masiva (borrar si NO es de hoy)
  bool doDelete = false;
  if (server.hasArg("del")) {
    String delArg = server.arg("del");
    delArg.trim();
    doDelete = (delArg == "1");
  }

  File f = SD.open(filePath, FILE_READ);
  if (!f) {
    server.send(500, "text/plain", "Open failed");
    return;
  }

  String disp = "attachment; filename=\"" + String(f.name()) + "\"";
  server.sendHeader("Content-Disposition", disp);
  size_t sent = server.streamFile(f, "text/csv");
  f.close();

  Serial.printf("[SD] Descarga %s (%u bytes)\n",
                filePath.c_str(), (unsigned)sent);

  // Solo borrar si:
  // - se pidió del=1 (lote)
  // - se envió algo (>0 bytes)
  // - NO es el archivo de hoy
  if (doDelete && sent > 0 && !isTodayFile(filePath)) {
    bool ok = SD.remove(filePath);
    Serial.printf("[SD] Borrado tras lote %s -> %s\n",
                  filePath.c_str(), ok ? "OK" : "FAIL");
  } else if (doDelete && isTodayFile(filePath)) {
    Serial.printf("[SD] Solicitaron borrar el archivo de hoy (%s) -> PROTEGIDO, NO se borra\n",
                  filePath.c_str());
  }
}

// ------ Handlers Web para recalibración MQ-9 -------
void handleMQ9Recal() {
  if (!requireWebAuthentication()) return;

  // --- Opción: limpiar el valor R0 guardado ---
  if (server.hasArg("clear") && server.arg("clear") == "1") {
    clearMq9Calibration();
    server.send(200, "text/plain", "MQ-9: RL/R0 borrados (NVS y SD). Reinicia para recalibrar.");
    Serial.println("[MQ-9] RL/R0 borrados en NVS y SD. Reinicia para recalibrar.");
    if (ok_lcd) {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("MQ-9 RL/R0 borrado");
      lcd.setCursor(0, 1); lcd.print("Reinicie el equipo");
    }
    return;
  }

  // --- Opción: recalibración inmediata (sin reiniciar) ---
  server.send(200, "text/plain", "MQ-9: recalibracion solicitada. Se hará ahora si procede.");
  Serial.println("[MQ-9] Recalibracion solicitada por web.");

  if (!ok_mq9) {
    Serial.println("[MQ-9] No se puede recalibrar: ADC no operativo.");
    return;
  }

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("MQ-9 Calibrando...");
  lcd.setCursor(0, 1); lcd.print("Aire limpio (90s)");

  uint32_t warmEnd = millis() + MQ9_WARMUP_MS;
  while (millis() < warmEnd) {
    server.handleClient();
    delay(1000);
    yield();
  }

  double rsSum = 0;
  uint32_t n = 0;
  uint32_t sampleEnd = millis() + MQ9_SAMPLE_MS;
  while (millis() < sampleEnd) {
    int raw = analogRead(MQ9_AO);
    float rs = mq9_rs_from_adc(raw);
    if (isfinite(rs) && rs > 0.1f && rs < 1e5f) {
      rsSum += rs;
      n++;
    }
    server.handleClient();
    delay(200);
    yield();
  }

  if (n > 5) {
    float rsAvg = rsSum / n;
    mq9_R0 = rsAvg / MQ9_CLEAN_AIR_RATIO;
    mq9_calibrated = true;
    saveMq9Calibration();
    Serial.printf("[MQ-9] Nueva calibracion -> RL=%.4fkΩ R0=%.2fkΩ (Rs_avg=%.2fkΩ)\n",
                  mq9_RL, mq9_R0, rsAvg);
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("MQ-9 Recalibrado");
    lcd.setCursor(0, 1); lcd.printf("R0=%.2fkΩ", mq9_R0);
    delay(3000);
  } else {
    Serial.println("[MQ-9] Error: muestreo insuficiente para recalibrar.");
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("MQ-9 fallo recal");
    lcd.setCursor(0, 1); lcd.print("Sin datos validos");
    delay(3000);
  }
}

// -------- Máquina de estados: helpers --------
const char* stateName(ConnState st) {
  switch (st) {
    case CS_ONLINE:   return "ONLINE";
    case CS_OFFLINE:  return "OFFLINE";
    case CS_BACKFILL: return "BACKFILL";
    default:          return "UNKNOWN";
  }
}

// Primera linea operativa del LCD: estado, fecha/hora y cola de backfill.
// Evita getLocalTime() para no bloquear cuando no existe sincronizacion NTP.
void updateOperationalStatusLine() {
  if (!ok_lcd) return;

  char line[21] = {0};
  const time_t nowEpoch = time(nullptr);
  const bool validTime = nowEpoch >= 1704067200;  // 2024/01/01 UTC

  struct tm localTimeInfo;
  memset(&localTimeInfo, 0, sizeof(localTimeInfo));
  if (validTime) {
    localtime_r(&nowEpoch, &localTimeInfo);
  }

  if (connState == CS_BACKFILL) {
    const uint32_t pending = countBackfillLines();
    if (validTime) {
      snprintf(line, sizeof(line),
               "BF Q:%lu %02d:%02d",
               (unsigned long)pending,
               localTimeInfo.tm_hour,
               localTimeInfo.tm_min);
    } else {
      snprintf(line, sizeof(line),
               "BF Q:%lu --:--",
               (unsigned long)pending);
    }
  } else {
    const char* shortState = connState == CS_ONLINE ? "ON" : "OFF";
    if (validTime) {
      snprintf(line, sizeof(line),
               "%s %04d/%02d/%02d %02d:%02d",
               shortState,
               localTimeInfo.tm_year + 1900,
               localTimeInfo.tm_mon + 1,
               localTimeInfo.tm_mday,
               localTimeInfo.tm_hour,
               localTimeInfo.tm_min);
    } else {
      snprintf(line, sizeof(line),
               "%s --/--/-- --:--",
               shortState);
    }
  }

  lcd.setCursor(0, 0);
  lcd.print("                    ");
  lcd.setCursor(0, 0);
  lcd.print(line);
}

const char* wifiStatusName(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:     return "IDLE";
    case WL_NO_SSID_AVAIL:   return "NO_SSID";
    case WL_SCAN_COMPLETED:  return "SCAN_COMPLETED";
    case WL_CONNECTED:       return "CONNECTED";
    case WL_CONNECT_FAILED:  return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED:    return "DISCONNECTED";
    default:                 return "UNKNOWN";
  }
}

void setConnState(ConnState newState) {
  if (newState == connState) return;
  ConnState previous = connState;
  Serial.printf("[STATE] %s -> %s\n", stateName(previous), stateName(newState));
  connState = newState;

  String details = String("pending_sd=") + String(backfillCount) +
                   ";pending_ram=" + String(emergencyCount) +
                   ";wifi=" + (WiFi.status() == WL_CONNECTED ? "connected" : "disconnected") +
                   ";ts_code=" + String(lastThingSpeakCode) +
                   ";cause=" + lastBackfillCause;
  logSystemEvent(SYSLOG_INFO, SYSCOMP_SYSTEM,
                 "STATE_CHANGE", stateName(previous), stateName(newState), "OK", details);

  if (newState == CS_OFFLINE) {
    logSystemEvent(SYSLOG_WARNING, SYSCOMP_NETWORK,
                   "OFFLINE_ENTERED", stateName(previous), stateName(newState), "DEGRADED", details);
  } else if (newState == CS_BACKFILL) {
    logSystemEvent(SYSLOG_INFO, SYSCOMP_SYNC,
                   "BACKFILL_STARTED", stateName(previous), stateName(newState), "OK", details);
  } else if (newState == CS_ONLINE && previous == CS_BACKFILL) {
    logSystemEvent(SYSLOG_INFO, SYSCOMP_SYNC,
                   "BACKFILL_COMPLETED", stateName(previous), stateName(newState), "OK", details);
  } else if (newState == CS_ONLINE) {
    logSystemEvent(SYSLOG_INFO, SYSCOMP_NETWORK,
                   "ONLINE_ENTERED", stateName(previous), stateName(newState), "OK", details);
  }
}

// ¿Hay datos pendientes en la cola persistente o en RAM de emergencia?
bool backfillHasData() {
  uint32_t lines = countBackfillLines();
  bool has = (lines > 0);
  Serial.printf("[BACKFILL] hasData=%s (pendientes=%lu)\n",
                has ? "TRUE" : "FALSE",
                (unsigned long)lines);
  return has;
}

bool readNextBackfillRow(String& line, uint32_t& nextOffset) {
  line = "";
  nextOffset = backfillReadOffset;
  if (!ok_sd || backfillCount == 0 || !SD.exists(BACKFILL_FILE)) return false;

  File f = SD.open(BACKFILL_FILE, FILE_READ);
  if (!f) return false;
  if (!f.seek(backfillReadOffset)) {
    f.close();
    return false;
  }

  while (f.available()) {
    line = f.readStringUntil('\n');
    nextOffset = f.position();
    line.trim();
    if (line.length() > 0) {
      f.close();
      return true;
    }
  }
  f.close();
  return false;
}

// Leer hasta TS_BACKFILL_BATCH_SIZE filas sin consumir la cola.
bool readBackfillBatch(BackfillBatch& batch) {
  batch.count = 0;
  batch.finalOffset = backfillReadOffset;

  if (!ok_sd || backfillCount == 0 || !SD.exists(BACKFILL_FILE)) return false;

  File f = SD.open(BACKFILL_FILE, FILE_READ);
  if (!f) return false;
  if (!f.seek(backfillReadOffset)) {
    f.close();
    return false;
  }

  const uint16_t wanted = backfillCount < TS_BACKFILL_BATCH_SIZE
    ? static_cast<uint16_t>(backfillCount)
    : TS_BACKFILL_BATCH_SIZE;
  while (f.available() && batch.count < wanted) {
    String line = f.readStringUntil('\n');
    uint32_t nextOffset = f.position();
    line.trim();
    if (line.length() == 0) continue;

    if (line.length() >= BACKFILL_ROW_MAX) {
      Serial.printf("[BACKFILL] Fila excede limite: %u >= %u; lote no consumido\n",
                    (unsigned)line.length(), (unsigned)BACKFILL_ROW_MAX);
      logSystemEvent(SYSLOG_ERROR, SYSCOMP_SYNC,
                     "BACKFILL_ROW_TOO_LONG", "", "", "FAIL",
                     String("length=") + String(line.length()) +
                     ";limit=" + String(BACKFILL_ROW_MAX));
      f.close();
      return false;
    }

    line.toCharArray(batch.rows[batch.count], BACKFILL_ROW_MAX);
    batch.count++;
    batch.finalOffset = nextOffset;
  }
  f.close();
  return batch.count > 0;
}

// Convierte una fila CSV en un objeto JSON para Bulk Write.
bool appendBackfillJsonUpdate(String& payload,
                              const char* csvRow,
                              bool useAbsoluteTime,
                              uint16_t index) {
  String line(csvRow);
  String tokens[13];
  int col = 0;
  int start = 0;
  for (int i = 0; i <= line.length() && col < 13; ++i) {
    if (i == line.length() || line[i] == ',') {
      tokens[col] = line.substring(start, i);
      tokens[col].trim();
      col++;
      start = i + 1;
    }
  }
  if (col < 13) return false;

  const bool validDate = tokens[0].length() == 10 && tokens[0] != "0000-00-00";
  const bool validTime = tokens[1].length() == 8 && tokens[1] != "00:00:00";
  if (useAbsoluteTime && (!validDate || !validTime)) return false;

  if (index > 0) payload += ',';
  payload += '{';
  if (useAbsoluteTime) {
    payload += "\"created_at\":\"";
    payload += tokens[0];
    payload += ' ';
    payload += tokens[1];
    payload += " -0500\"";
  } else {
    payload += "\"delta_t\":";
    payload += String(index == 0 ? 0 : (SAMPLE_MS / 1000UL));
  }

  // Mismo mapeo que el backfill unitario anterior.
  payload += ",\"field1\":";
  payload += tokens[2];
  payload += ",\"field2\":";
  payload += tokens[3];
  payload += ",\"field3\":";
  payload += tokens[4];
  payload += ",\"field4\":";
  payload += tokens[6];
  payload += ",\"field5\":";
  payload += tokens[11];
  payload += ",\"field6\":";
  payload += tokens[12];
  payload += ",\"status\":\"BACKFILL SD BULK\"";
  payload += '}';
  return true;
}

bool buildBackfillBulkPayload(const BackfillBatch& batch, String& payload) {
  bool useAbsoluteTime = true;
  for (uint16_t i = 0; i < batch.count; ++i) {
    String row(batch.rows[i]);
    int firstComma = row.indexOf(',');
    int secondComma = firstComma >= 0 ? row.indexOf(',', firstComma + 1) : -1;
    if (firstComma != 10 || secondComma != 19 || row.startsWith("0000-00-00,00:00:00")) {
      useAbsoluteTime = false;
      break;
    }
  }

  payload = "{\"write_api_key\":\"";
  payload += WRITE_API_KEY;
  payload += "\",\"updates\":[";
  payload.reserve(256 + batch.count * 230U);

  for (uint16_t i = 0; i < batch.count; ++i) {
    if (!appendBackfillJsonUpdate(payload, batch.rows[i], useAbsoluteTime, i)) {
      payload = "";
      return false;
    }
  }
  payload += "]}";
  return true;
}

// Envía un grupo de registros pendientes mediante la API Bulk Write.
bool sendBackfillBatch() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[BACKFILL] WiFi caido; no se consume la cola");
    return false;
  }

  if (emergencyCount > 0 && !flushEmergencyBackfill()) {
    Serial.println("[BACKFILL] No se pudo persistir la RAM de emergencia");
    return false;
  }

  if (backfillCount == 0) {
    Serial.println("[BACKFILL] Cola SD vacia");
    return false;
  }

  if (!readBackfillBatch(syncBatch)) {
    Serial.println("[BACKFILL] No se pudo leer el lote desde la SD");
    return false;
  }

  String payload;
  if (!buildBackfillBulkPayload(syncBatch, payload)) {
    Serial.println("[BACKFILL] No se pudo construir el JSON del lote");
    logSystemEvent(SYSLOG_ERROR, SYSCOMP_SYNC,
                   "BACKFILL_PAYLOAD_FAILED", "", "", "FAIL",
                   String("batch=") + String(syncBatch.count));
    return false;
  }

  WiFiClientSecure secureClient;
  secureClient.setInsecure();  // RC: reemplazar por CA antes de produccion.
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(TS_BULK_HTTP_TIMEOUT_MS);

  String url = String("https://api.thingspeak.com/channels/") +
               String(CHANNEL_ID) + "/bulk_update.json";

  secureClient.stop();
  uint32_t heapBefore = ESP.getFreeHeap();
  uint32_t started = millis();
  bool begun = http.begin(secureClient, url);
  int code = -1;
  String response = "";
  if (begun) {
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Connection", "close");
    code = http.POST(payload);
    if (code > 0) response = http.getString();
    http.end();
  }
  secureClient.stop();

  lastBulkDurationMs = millis() - started;
  lastBulkPayloadBytes = payload.length();
  uint32_t heapAfter = ESP.getFreeHeap();

  String normalized = response;
  normalized.replace(" ", "");
  normalized.replace("\r", "");
  normalized.replace("\n", "");
  const bool bodySuccess = normalized.indexOf("\"success\":true") >= 0;
  const bool accepted = (code == 200 || code == 202) && bodySuccess;

  Serial.printf("[TS BULK] count=%u bytes=%lu HTTP=%d accepted=%s duration=%lums heap=%lu->%lu\n",
                (unsigned)syncBatch.count,
                (unsigned long)lastBulkPayloadBytes,
                code,
                accepted ? "true" : "false",
                (unsigned long)lastBulkDurationMs,
                (unsigned long)heapBefore,
                (unsigned long)heapAfter);
  if (response.length() > 0) {
    Serial.print("[TS BULK] Response: ");
    Serial.println(response);
  }
  recordThingSpeakResult(accepted ? 200 : code, "BACKFILL_BULK");

  if (!accepted) {
    backfillSendFailures++;
    thingSpeakFailureCount++;
    lastBackfillCause = String("THINGSPEAK_BULK_CODE_") + String(code);
    logSystemEvent(SYSLOG_WARNING, SYSCOMP_SYNC,
                   "BACKFILL_BATCH_FAILED", "", "", "FAIL",
                   String("code=") + String(code) +
                   ";count=" + String(syncBatch.count) +
                   ";bytes=" + String(lastBulkPayloadBytes) +
                   ";duration_ms=" + String(lastBulkDurationMs) +
                   ";heap_before=" + String(heapBefore) +
                   ";heap_after=" + String(heapAfter) +
                   ";pending=" + String(backfillCount) +
                   ";wifi_status=" + String(wifiStatusName(WiFi.status())) +
                   ";wifi_code=" + String((int)WiFi.status()) +
                   ";rssi=" + String(WiFi.RSSI()));
    return false;
  }

  backfillReadOffset = syncBatch.finalOffset;
  const uint16_t consumed = syncBatch.count;
  backfillCount = backfillCount > consumed ? backfillCount - consumed : 0;
  backfillSentCount += consumed;
  backfillSentTotal += consumed;
  backfillSendFailures = 0;

  if (backfillCount == 0) resetBackfillQueueFiles();
  else saveBackfillState();

  logSystemEvent(SYSLOG_INFO, SYSCOMP_SYNC,
                 "BACKFILL_BATCH_SENT", "", "", "OK",
                 String("count=") + String(consumed) +
                 ";bytes=" + String(lastBulkPayloadBytes) +
                 ";duration_ms=" + String(lastBulkDurationMs) +
                 ";heap_before=" + String(heapBefore) +
                 ";heap_after=" + String(heapAfter) +
                 ";remaining=" + String(backfillCount) +
                 ";offset=" + String(backfillReadOffset));

  Serial.printf("[BACKFILL] Lote confirmado. enviados=%u restantes=%lu offset=%lu\n",
                (unsigned)consumed,
                (unsigned long)backfillCount,
                (unsigned long)backfillReadOffset);
  return true;
}

// Envío ONLINE normal. Devuelve el código para detectar Internet/ThingSpeak.
int publishLiveToThingSpeak() {
  ThingSpeak.setField(1, last_dist);
  ThingSpeak.setField(2, last_t);
  ThingSpeak.setField(3, last_h);
  if (isfinite(last_mq9ppm)) ThingSpeak.setField(4, last_mq9ppm);
  else ThingSpeak.setField(4, "");
  ThingSpeak.setField(5, last_bmp_p);
  ThingSpeak.setField(6, last_bmp_alt);

  float acc_mod = sqrt(last_ax * last_ax + last_ay * last_ay + last_az * last_az);
  ThingSpeak.setField(7, acc_mod);
  ThingSpeak.setField(8, last_gz);

  ThingSpeak.setStatus(String("WiFi=") + (WiFi.status() == WL_CONNECTED ? "OK" : "NO") +
                       " BMP=" + (ok_bmp ? "1" : "0") +
                       " MQ9=" + (ok_mq9 ? "1" : "0") +
                       " IMU=" + (ok_mpu ? "1" : "0"));

  int code = ThingSpeak.writeFields(CHANNEL_ID, WRITE_API_KEY);
  Serial.print("[TS] writeFields -> ");
  Serial.println(code == 200 ? "OK (200)" : String("Error ") + code);
  recordThingSpeakResult(code, "ONLINE");
  return code;
}

// -------------------- Setup --------------------
void setup() {
  Serial.begin(115200);
  initializeSystemIdentity();

  logSystemEvent(SYSLOG_INFO, SYSCOMP_SYSTEM,
                 "BOOT_START", "", "", "OK",
                 String("firmware=") + FIRMWARE_VERSION + ";boot_id=" + String(bootId));

  esp_reset_reason_t resetReason = esp_reset_reason();
  uint8_t resetSeverity = SYSLOG_INFO;
  if (resetReason == ESP_RST_BROWNOUT) resetSeverity = SYSLOG_CRITICAL;
  else if (resetReason == ESP_RST_PANIC || resetReason == ESP_RST_TASK_WDT ||
           resetReason == ESP_RST_INT_WDT || resetReason == ESP_RST_WDT) resetSeverity = SYSLOG_ERROR;
  logSystemEvent(resetSeverity, SYSCOMP_SYSTEM,
                 "RESET_REASON", "", "", lastResetReason.c_str(),
                 String("code=") + String(static_cast<int>(resetReason)));

  loadCalibrationProfile();

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
  pinMode(MQ9_AO, INPUT);

  analogSetPinAttenuation(MQ9_AO, ADC_11db);

  // I2C antes de cualquier sensor
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  Serial.printf("[I2C] SDA=%d SCL=%d @100kHz\n", SDA_PIN, SCL_PIN);
  delay(50);

  // LCD I2C
  int lcd_status = lcd.begin(20, 4);
  if (lcd_status) {
    ok_lcd = false;
    Serial.printf("LCD FAIL, code=%d\n", lcd_status);
  } else {
    ok_lcd = true;
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print("  DPVAD/SAT - BOOT");
    lcd.setCursor(0, 2);
    lcd.print("        UNAD");
    Serial.println("LCD OK (hd44780_I2Cexp)");
  }

  // DHT22
  dht.begin();
  delay(1500);
  float t0 = dht.readTemperature();
  float h0 = dht.readHumidity();
  ok_dht = isfinite(t0) && isfinite(h0);

  // HC-SR04
  float d0 = medirDistanciaCM();
  ok_ultra = isfinite(d0);

  // MQ-9
  int mq9raw0 = analogRead(MQ9_AO);
  ok_mq9 = (mq9raw0 >= 0);

  // MPU9250
  Serial.println("[MPU9250] Inicializando...");
  ok_mpu = (mpu.setup(0x68) || mpu.setup(0x69));
  Serial.println(ok_mpu ? "[MPU9250] OK" : "[MPU9250] FAIL");

  // BME280 / BMP280
  hasBME = false;
  ok_bmp = false;

  if (!hasBME) hasBME = bme.begin(0x76, &Wire);
  if (!hasBME) hasBME = bme.begin(0x77, &Wire);

  if (hasBME) {
    Serial.println("✅ BME280 detectado");
    bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                    Adafruit_BME280::SAMPLING_X2,   // Temp
                    Adafruit_BME280::SAMPLING_X2,   // Hum
                    Adafruit_BME280::SAMPLING_X16,  // Pres
                    Adafruit_BME280::FILTER_X16,
                    Adafruit_BME280::STANDBY_MS_62_5);
    ok_bmp = true;
  } else {
    if (bmp280.begin(0x76) || bmp280.begin(0x77)) {
      Serial.println("✅ BMP280 detectado");
      bmp280.setSampling(Adafruit_BMP280::MODE_NORMAL,
                         Adafruit_BMP280::SAMPLING_X2,    // Temp
                         Adafruit_BMP280::SAMPLING_X16,   // Pres
                         Adafruit_BMP280::FILTER_X16,
                         Adafruit_BMP280::STANDBY_MS_125);
      ok_bmp = true;
    } else {
      Serial.println("❌ BME/BMP280 no detectados (prueba 0x76/0x77, SDA/SCL/3V3).");
      ok_bmp = false;
    }
  }

  // SD
  ok_sd = initSD();
  Serial.println(ok_sd ? "✅ SD montada" : "❌ SD no montada");
  if (ok_sd) {
    if (!SD.exists(SYSTEM_LOG_DIR)) SD.mkdir(SYSTEM_LOG_DIR);
    flushBufferedSystemEvents();
    logSystemEvent(SYSLOG_INFO, SYSCOMP_STORAGE,
                   "SD_MOUNT_OK", "", "", "OK",
                   String("card_bytes=") + String(static_cast<unsigned long long>(SD.cardSize())));
    initializeBackfillQueue();
    maintainSystemLogRetention();
  } else {
    sdFailureActive = true;
    logSystemEvent(SYSLOG_ERROR, SYSCOMP_STORAGE,
                   "SD_MOUNT_FAILED", "", "", "FAIL", "initSD sin respuesta");
  }

  // WiFi (AP + STA)
  startAP();
  connectWiFiSTA();

  // NTP + log file del día
  if (WiFi.status() == WL_CONNECTED && syncTimeIfNeeded()) ensureLogFileForToday();

  // ThingSpeak
  ThingSpeak.begin(tsClient);
  ok_ts = true;
  Serial.printf("[TS] Ready -> channel=%lu, ONLINE=%lus, BACKFILL=%lus, BATCH=%u\n",
                CHANNEL_ID,
                TS_ONLINE_INTERVAL_MS / 1000,
                TS_BACKFILL_INTERVAL_MS / 1000,
                (unsigned)TS_BACKFILL_BATCH_SIZE);

  // WebServer
  server.on("/", sendIndex);
  server.on("/list", sendList);
  server.on("/stats", sendStats);
  server.on("/download", handleDownload);
  server.on("/mq9/recal", handleMQ9Recal);
  server.on("/mq9/status", handleMQ9Status);
  server.on("/mq9/config", handleMQ9Config);
  server.on("/cal/status", handleCalibrationStatus);
  server.on("/cal/reference", handleCalibrationReference);
  server.on("/cal/set", handleCalibrationSet);
  server.on("/cal/reset", handleCalibrationReset);
  server.on("/cal/imu-zero", handleCalibrationImuZero);
  server.on("/cal/sea-level", handleCalibrationSeaLevel);
  server.onNotFound([]() {
    if (!requireWebAuthentication()) return;
    server.send(404, "text/plain", "Recurso no encontrado");
  });

  server.begin();
  Serial.println("[Web] HTTP server en :80");

  // Portal independiente de trazabilidad: solo lectura, autenticación propia.
  logServer.on("/", sendSystemLogIndex);
  logServer.on("/list", sendSystemLogList);
  logServer.on("/status", sendSystemLogStatus);
  logServer.on("/download", handleSystemLogDownload);
  logServer.onNotFound([]() {
    if (!requireLogAuthentication()) return;
    logServer.send(404, "text/plain", "Recurso no encontrado");
  });
  logServer.begin();
  Serial.printf("[Web] Portal de trazabilidad en :%u\n", LOG_WEB_PORT);
  logSystemEvent(SYSLOG_INFO, SYSCOMP_WEB,
                 "LOG_PORTAL_STARTED", "", "", "OK",
                 String("port=") + String(LOG_WEB_PORT));

  Serial.println(String("[INFO] STA IP: ") + staIPStr + " | AP IP: " + apIPStr);

  // Preferencias MQ-9: recuperar RL y R0 desde NVS, con respaldo en SD.
  loadMq9Calibration();
  if (!mq9_calibrated) {
    Serial.println("[MQ-9] Sin R0 válido; se realizará autocalibración con la RL activa.");
  }

  // Calibración automática MQ-9
  if (ok_mq9 && !mq9_calibrated) {
    Serial.println("[MQ-9] Iniciando autocalibración...");
    if (ok_lcd) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("MQ-9 Calibrando...");
      lcd.setCursor(0, 1);
      lcd.print("Aire limpio (90s)");
    }

    uint32_t warmEnd = millis() + MQ9_WARMUP_MS;
    while (millis() < warmEnd) {
      server.handleClient();
      yield();
      delay(1000);
    }

    double rsSum = 0;
    uint32_t n = 0;
    uint32_t sampleEnd = millis() + MQ9_SAMPLE_MS;
    while (millis() < sampleEnd) {
      int raw = analogRead(MQ9_AO);
      float rs = mq9_rs_from_adc(raw);
      Serial.printf("raw=%d  Rs=%.2fkΩ\n", raw, rs);

      if (isfinite(rs) && rs > 0.1f && rs < 1e6f) {
        rsSum += rs;
        n++;
      }
      server.handleClient();
      yield();
      delay(200);
    }

    if (n > 7) {
      float rsAvg = rsSum / n;
      mq9_R0 = rsAvg / MQ9_CLEAN_AIR_RATIO;
      mq9_calibrated = true;
      saveMq9Calibration();
      Serial.println("[MQ-9] RL/R0 guardados en NVS y respaldados en SD.");
      Serial.printf("[MQ-9] Calibrado: RL=%.4f kΩ R0=%.2f kΩ (promedio Rs=%.2f kΩ)\n",
                    mq9_RL, mq9_R0, rsAvg);
      if (ok_lcd) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("MQ-9 Calibrado");
        lcd.setCursor(0, 1);
        lcd.printf("R0=%.2fkΩ", mq9_R0);
        delay(2000);
      }
    } else {
      mq9_calibrated = false;
      Serial.println("[MQ-9] Error en calibración (datos insuficientes)");
    }
  }


  // Estado inicial en función de WiFi y de si hay backlog
  if (WiFi.status() == WL_CONNECTED) {
    if (backfillHasData()) {
      backfillPendingAtStart = countBackfillLines();
      Serial.printf("[BACKFILL] Pendientes al arranque: %lu registros\n",
                    (unsigned long)backfillPendingAtStart);
      lastBackfillCause = "STARTUP_PENDING_QUEUE";
      connState = CS_BACKFILL;
    } else {
      connState = CS_ONLINE;
    }
  } else {
    connState = CS_OFFLINE;
  }
  Serial.printf("[STATE] Inicial -> %s\n", stateName(connState));
  logSystemEvent(SYSLOG_INFO, SYSCOMP_SYSTEM,
                 "INITIAL_STATE", "", stateName(connState), "OK",
                 String("pending_sd=") + String(backfillCount) +
                 ";pending_ram=" + String(emergencyCount));

  // LCD Startup Sequence
  if (ok_lcd) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("DPVAD/SAT: RSALAZARV");
    lcd.setCursor(0, 1);
    lcd.print("Version: 12.5-SYNC-RC1.1");
    lcd.setCursor(0, 2);
    lcd.print("Inicializando...");
    delay(2500);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Verif. Sensores:");
    lcd.setCursor(0, 1);
    lcd.printf("DHT:%s MPU:%s", ok_dht ? "OK" : "--", ok_mpu ? "OK" : "--");
    lcd.setCursor(0, 2);
    lcd.printf("BMP:%s MQ9:%s", ok_bmp ? "OK" : "--", ok_mq9 ? "OK" : "--");
    lcd.setCursor(0, 3);
    lcd.printf("SD:%s", ok_sd ? "OK" : "--");
    delay(2500);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Conectividad:");
    lcd.setCursor(0, 1);
    lcd.printf("WiFi:%s", ok_sta ? "OK" : "--");
    lcd.setCursor(0, 2);
    lcd.printf("AP:%s TS:%s", ok_ap ? "OK" : "--", ok_ts ? "OK" : "--");
    lcd.setCursor(0, 3);
    lcd.print("Sync reloj...");
    delay(2500);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Operativo:");
    lcd.setCursor(0, 1);
    lcd.print("Iniciando monitoreo.");
    delay(2500);
  }

  // Resumen de arranque
  Serial.println("===== RESUMEN DE DISPOSITIVOS =====");
  Serial.printf("[DHT22]   %s\n", ok_dht ? "OK" : "FALLO");
  Serial.printf("[HC-SR04] %s\n", ok_ultra ? "OK" : "FALLO");
  Serial.printf("[MQ-9]    %s\n", ok_mq9 ? "OK" : "FALLO");
  Serial.printf("[MPU9250] %s\n", ok_mpu ? "OK" : "FALLO");
  Serial.printf("[BMP280]  %s\n", ok_bmp ? "OK" : "FALLO");
  Serial.printf("[LCD]     %s\n", ok_lcd ? "OK" : "FALLO");
  Serial.printf("[microSD] %s\n", ok_sd ? "OK" : "FALLO");
  Serial.printf("[WiFi AP] %s\n", ok_ap ? "OK" : "FALLO");
  Serial.printf("[WiFi STA]%s (%s)\n", ok_sta ? "OK" : "FALLO", staIPStr.c_str());
  Serial.printf("[ThingSpeak] %s\n", ok_ts ? "OK" : "FALLO");
  Serial.printf("[BACKFILL] SD=%lu RAM=%u pendientes=%lu offset=%lu\n",
                (unsigned long)backfillCount,
                (unsigned)emergencyCount,
                (unsigned long)countBackfillLines(),
                (unsigned long)backfillReadOffset);
  Serial.printf("[CAL] Gen=%lu SeaLevel=%.2fhPa Estado=%s\n",
                (unsigned long)calProfile.generation,
                calProfile.seaLevelHpa,
                calibrationStateName(calibrationState));
  Serial.println("===================================");

  String failedSensors = "";
  if (!ok_dht) failedSensors += "DHT22;";
  if (!ok_ultra) failedSensors += "HC-SR04;";
  if (!ok_mq9) failedSensors += "MQ-9;";
  if (!ok_mpu) failedSensors += "IMU;";
  if (!ok_bmp) failedSensors += "BME/BMP280;";
  if (!ok_lcd) failedSensors += "LCD;";
  if (failedSensors.length() > 0) {
    logSystemEvent(SYSLOG_ERROR, SYSCOMP_SENSOR_HEALTH,
                   "SENSOR_INIT_FAILED", "", "", "DEGRADED",
                   String("components=") + failedSensors);
  } else {
    logSystemEvent(SYSLOG_INFO, SYSCOMP_SENSOR_HEALTH,
                   "SENSOR_INIT_OK", "", "", "OK", "all_components_ready=1");
  }

  logSystemEvent(SYSLOG_INFO, SYSCOMP_SYSTEM,
                 "BOOT_COMPLETE", "", stateName(connState), "OK",
                 String("firmware=") + FIRMWARE_VERSION +
                 ";heap=" + String(ESP.getFreeHeap()));
}

// -------------------- Loop --------------------
void loop() {
  server.handleClient();
  logServer.handleClient();

  unsigned long now = millis();
  processSdRecovery(now);
  processSystemLogMaintenance(now);

  // --------- Muestreo de sensores ----------
  if (now - lastSample >= SAMPLE_MS) {
    lastSample = now;

    // HC-SR04: conservar lectura cruda y aplicar perfil lineal.
    raw_dist = medirDistanciaEstableCM();
    last_dist = applyLinearCalibration(raw_dist, calProfile.distance);

    // DHT22: conservar lectura cruda y aplicar perfil lineal.
    raw_dht_h = dht.readHumidity();
    raw_dht_t = dht.readTemperature();
    corrected_dht_h = applyLinearCalibration(raw_dht_h, calProfile.dhtHumidity);
    corrected_dht_t = applyLinearCalibration(raw_dht_t, calProfile.dhtTemp);
    last_h = corrected_dht_h;
    last_t = corrected_dht_t;
    
    // MQ-9 (ADC)
    int mq9Raw = analogRead(MQ9_AO);
    bool mq9_saturado = (mq9Raw < 10 || mq9Raw > 4080);

    if (!mq9_calibrated || mq9_saturado) {
      last_mq9ppm = NAN;
    } else {
      float rs = mq9_rs_from_adc(mq9Raw);
      float ratio = rs / mq9_R0;
      float ppm = mq9_ppm_from_ratio(ratio);
      static float mq9_ema = NAN;
      if (!isfinite(mq9_ema)) mq9_ema = ppm;
      mq9_ema = 0.7f * mq9_ema + 0.3f * ppm;
      if (mq9_ema < 0) mq9_ema = 0;
      if (mq9_ema > 5000) mq9_ema = 5000;
      last_mq9ppm = mq9_ema;
    }

    // IMU (MPU9250): conservar valores crudos y aplicar bias/escala.
    if (ok_mpu && mpu.update()) {
      raw_ax = mpu.getAccX() * 9.80665f;
      raw_ay = mpu.getAccY() * 9.80665f;
      raw_az = mpu.getAccZ() * 9.80665f;
      raw_gx = mpu.getGyroX();
      raw_gy = mpu.getGyroY();
      raw_gz = mpu.getGyroZ();

      last_ax = (raw_ax - calProfile.accelBiasX) * calProfile.accelScaleX;
      last_ay = (raw_ay - calProfile.accelBiasY) * calProfile.accelScaleY;
      last_az = (raw_az - calProfile.accelBiasZ) * calProfile.accelScaleZ;
      last_gx = raw_gx - calProfile.gyroBiasX;
      last_gy = raw_gy - calProfile.gyroBiasY;
      last_gz = raw_gz - calProfile.gyroBiasZ;

      float axg = last_ax / 9.80665f;
      float ayg = last_ay / 9.80665f;
      float azg = last_az / 9.80665f;
      last_pitch = atan2f(-axg, sqrtf(ayg * ayg + azg * azg)) * 180.0f / PI;
      last_roll  = atan2f( ayg, azg) * 180.0f / PI;
    } else if (!ok_mpu) {
      last_ax = last_ay = last_az = last_gx = last_gy = last_gz = last_pitch = last_roll = NAN;
    }

    // Ambiental (BME280/BMP280): conservar crudos y aplicar perfil.
    if (ok_bmp) {
      if (hasBME) {
        raw_env_t = bme.readTemperature();
        raw_env_h = bme.readHumidity();
        raw_env_p = bme.readPressure() / 100.0f;
      } else {
        raw_env_t = bmp280.readTemperature();
        raw_env_h = NAN;
        raw_env_p = bmp280.readPressure() / 100.0f;
      }

      last_bmp_t = applyLinearCalibration(raw_env_t, calProfile.envTemp);
      last_bme_h = applyLinearCalibration(raw_env_h, calProfile.envHumidity);
      last_bmp_p = applyLinearCalibration(raw_env_p, calProfile.pressure);
      last_bmp_alt = calculateAltitudeM(last_bmp_p, calProfile.seaLevelHpa);
    } else {
      raw_env_t = raw_env_h = raw_env_p = NAN;
      last_bmp_t = last_bme_h = last_bmp_p = last_bmp_alt = NAN;
    }

    updateCalibrationCrossChecks();
    updateTempSat();

    // Consola
    Serial.println("----- Lecturas -----");
    Serial.printf("Dist: %.1f cm\n", last_dist);
    Serial.printf("DHT:  %.1f C  %.1f %%\n", last_t, last_h);
    if (mq9_calibrated) {
      int mq9DiagRaw = analogRead(MQ9_AO);
      float mq9DiagV = adc_to_volt(mq9DiagRaw);
      float mq9DiagRs = mq9_rs_from_adc(mq9DiagRaw);
      float mq9DiagRatio = isfinite(mq9DiagRs) ? mq9DiagRs / mq9_R0 : NAN;
      Serial.printf("MQ9: est=%.0f ppm | raw=%d V=%.3fV RL=%.4fkOhm R0=%.4fkOhm Rs=%.4fkOhm ratio=%.4f\n",
                    last_mq9ppm, mq9DiagRaw, mq9DiagV, mq9_RL, mq9_R0,
                    mq9DiagRs, mq9DiagRatio);
    } else {
      Serial.printf("MQ9: sin calibrar | RL=%.4fkOhm\n", mq9_RL);
    }
    if (ok_mpu) {
      Serial.printf("ACC:  ax=%.2f ay=%.2f az=%.2f m/s^2\n", last_ax, last_ay, last_az);
      Serial.printf("GYR:  gx=%.0f gy=%.0f gz=%.0f deg/s\n", last_gx, last_gy, last_gz);
      Serial.printf("Incl: Pitch=%.1f  Roll=%.1f deg\n", last_pitch, last_roll);
    } else {
      Serial.println("MPU9250: (no disponible)");
    }
    if (ok_bmp) {
      Serial.printf("BMP280: T=%.2f C  P=%.2f hPa  Alt=%.1f m\n\n",
                    last_bmp_t, last_bmp_p, last_bmp_alt);
    } else {
      Serial.println("BMP280: (no disponible)\n");
    }

    Serial.printf("[CAL] Gen=%lu | DHT raw T=%.2fC H=%.2f%% | ENV raw T=%.2fC H=%.2f%% P=%.2fhPa\n",
                  (unsigned long)calProfile.generation,
                  raw_dht_t, raw_dht_h, raw_env_t, raw_env_h, raw_env_p);
    if (isfinite(last_temp_delta)) {
      Serial.printf("[CAL] Validacion T: delta=%.2fC -> %s\n",
                    last_temp_delta, temp_crosscheck_ok ? "OK" : "REVISAR");
    }
    if (hasBME && isfinite(last_hum_delta)) {
      Serial.printf("[CAL] Validacion H: delta=%.2f%% -> %s\n",
                    last_hum_delta, hum_crosscheck_ok ? "OK" : "REVISAR");
    }
    Serial.println("[MQ-9] Unidad presentada: ppm estimadas (curva aproximada, no patron metrologico).");

    // LCD
    if (ok_lcd) {
      lcd.clear();
      updateOperationalStatusLine();

      lcd.setCursor(0, 1);
      lcd.print("D:");
      if (isnan(last_dist)) lcd.print("--");
      else {
        lcd.print(last_dist, 0);
        lcd.print("cm ");
      }
      lcd.print("T:");
      if (isnan(last_t)) lcd.print("--");
      else {
        lcd.print(last_t, 1);
        lcd.print("C");
      }

      lcd.setCursor(0, 2);
      lcd.print("H:");
      if (isnan(last_h)) lcd.print("--");
      else {
        lcd.print((int)last_h);
        lcd.print("% ");
      }
      lcd.print("G:");
      if (!mq9_calibrated) {
        lcd.print("CAL");
      } else if (isnan(last_mq9ppm)) {
        lcd.print("--");
      } else {
        lcd.print((int)last_mq9ppm);
      }

      lcd.setCursor(0, 3);
      lcd.print("P:");
      if (isnan(last_bmp_p)) {
        lcd.print("--");
      } else {
        lcd.print((int)last_bmp_p);
        lcd.print("hPa ");
      }
      lcd.print("A:");
      if (isnan(last_bmp_alt)) {
        lcd.print("--");
      } else {
        lcd.print((int)last_bmp_alt);
        lcd.print("m");
      }
    }

    // Histórico local y cola de retransmisión.
    if (ok_sd && getLocalTime(&timeinfo)) ensureLogFileForToday();
    logSampleToSD();
  }

  // --------- Máquina de estados + ThingSpeak ----------
  // Intervalo dinámico:
  // - ONLINE    -> 60 s
  // - BACKFILL  -> lote de hasta 30 registros; cooldown 30 s
  // - OFFLINE   -> Mismo ritmo que ONLINE para reintentos (60 s)
  
  unsigned long tsInterval;
  
  if (connState == CS_ONLINE) {
    tsInterval = TS_ONLINE_INTERVAL_MS;
  } else if (connState == CS_BACKFILL) {
    tsInterval = TS_BACKFILL_INTERVAL_MS;
    } else { // CS_OFFLINE
      tsInterval = TS_OFFLINE_INTERVAL_MS;
    }
  if (now - lastTS >= tsInterval) {
    lastTS = now;

    wl_status_t wst = WiFi.status();

    if (wst != WL_CONNECTED) {
      // Estábamos ONLINE / BACKFILL -> pasamos a OFFLINE si no lo estábamos
      if (connState != CS_OFFLINE) {
        wifiLinkLossCount++;
        lastBackfillCause = "WIFI_LINK_LOST";
        logSystemEvent(SYSLOG_WARNING, SYSCOMP_NETWORK,
                       "WIFI_LINK_LOST", stateName(connState), "OFFLINE", "FAIL",
                       String("wifi_status=") + wifiStatusName(wst) +
                       ";wifi_code=" + String((int)wst) +
                       ";loss_count=" + String(wifiLinkLossCount) +
                       ";pending=" + String(backfillCount));
        setConnState(CS_OFFLINE);
        logConnEvent("OFFLINE");
        backfillSentCount = 0;
      }

      // Reintentos básicos de conexión (bloqueantes pero espaciados)
      if (now - lastWiFiCheck >= WIFI_RETRY_MS) {
        lastWiFiCheck = now;
        connectWiFiSTA();
      }
      return;  // no intentamos TS sin WiFi
    }

    // WiFi está conectado
    if (connState == CS_OFFLINE) {
      // Venimos de OFFLINE → decidimos si hay backlog
      if (backfillHasData()) {
        backfillPendingAtStart = countBackfillLines();
        backfillSentCount      = 0;
        lastBackfillCause = "WIFI_RECOVERED_WITH_PENDING";
        Serial.printf("[BACKFILL] Pendientes al reconectar: %lu registros\n",
                      (unsigned long)backfillPendingAtStart);
        logSystemEvent(SYSLOG_INFO, SYSCOMP_SYNC,
                       "BACKFILL_TRIGGER", "OFFLINE", "BACKFILL", "OK",
                       String("cause=WIFI_RECOVERED_WITH_PENDING;pending=") +
                       String(backfillPendingAtStart) +
                       ";rssi=" + String(WiFi.RSSI()));
        setConnState(CS_BACKFILL);
      } else {
        Serial.println("[BACKFILL] Sin cola pendiente, pasamos directo a ONLINE");
        setConnState(CS_ONLINE);
      }
      logConnEvent("ONLINE");
    }

    if (connState == CS_BACKFILL) {
      // La solicitud Bulk es bloqueante. No se inicia si puede interferir con
      // la siguiente adquisición programada de sensores.
      uint32_t elapsedSinceSample = now - lastSample;
      uint32_t timeUntilNextSample =
        elapsedSinceSample < SAMPLE_MS ? SAMPLE_MS - elapsedSinceSample : 0;

      if (backfillHasData()) {
        if (timeUntilNextSample <= TS_BULK_SAMPLE_GUARD_MS) {
          Serial.printf("[BACKFILL] Lote diferido: faltan %lums para muestreo\n",
                        (unsigned long)timeUntilNextSample);
        } else {
          bool ok = sendBackfillBatch();
          // Los 30 segundos se cuentan desde la finalizacion de la solicitud.
          lastTS = millis();
          if (!ok) Serial.println("[BACKFILL] Lote fallido, se reintentara");
        }
      }
      // Si ya no hay backlog, pasamos a ONLINE
      if (!backfillHasData()) {
        uint32_t remaining = countBackfillLines();
        Serial.printf("[BACKFILL] Cola vacía. Enviados=%lu, Restantes=%lu (de %lu iniciales)\n",
                      (unsigned long)backfillSentCount,
                      (unsigned long)remaining,
                      (unsigned long)backfillPendingAtStart);
        
              // 🔎 Diagnóstico final al terminar el backfill
              printBackfillDiag();

        setConnState(CS_ONLINE);
      }
    } else if (connState == CS_ONLINE) {
      // Publicación normal. Si WiFi existe pero ThingSpeak/Internet falla,
      // la última muestra se conserva en la cola SD y se entra en BACKFILL.
      int code = publishLiveToThingSpeak();
      if (code != 200) {
        thingSpeakFailureCount++;
        lastBackfillCause = String("THINGSPEAK_ONLINE_CODE_") + String(code);
        if (lastSampleCsvRow.length() > 0 && !lastSampleQueued) {
          lastSampleQueued = appendBackfillRow(lastSampleCsvRow);
        }
        if (backfillHasData()) {
          backfillPendingAtStart = countBackfillLines();
          backfillSentCount = 0;
          logSystemEvent(SYSLOG_WARNING, SYSCOMP_SYNC,
                         "BACKFILL_TRIGGER", "ONLINE", "BACKFILL", "DEGRADED",
                         String("cause=THINGSPEAK_FAILURE;code=") + String(code) +
                         ";wifi_status=" + String(wifiStatusName(WiFi.status())) +
                         ";wifi_code=" + String((int)WiFi.status()) +
                         ";rssi=" + String(WiFi.RSSI()) +
                         ";pending=" + String(backfillPendingAtStart));
          setConnState(CS_BACKFILL);
          logConnEvent("THINGSPEAK_UNAVAILABLE");
        }
      }
    }
  }
}