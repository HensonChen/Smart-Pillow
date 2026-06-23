/*
 * Most of the codes are co-authored by Google Gemini through iterative development processes
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <SPI.h>
#include <SD.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include "time.h"  // Required for the real-time clock

// --- WI-FI CREDENTIALS & CAPTIVE PORTAL CONFIGURATIONS ---
const char* ap_ssid = "SmartPillow_Network";
const char* ap_password = "sleepdata";
String ssid = "";
String password = "";
bool AP_MODE = false;

DNSServer dnsServer;
const byte DNS_PORT = 53;

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// --- NETWORKING TRANSITION STATE MACHINE VARIABLES ---
bool pendingNetworkSwitch = false;
unsigned long switchTriggerTime = 0;
bool connectionAttemptActive = false;
unsigned long connectionStartTime = 0;
String pendingSsid = "";
String pendingPassword = "";

// --- PIN CONFIGURATIONS ---
const int NUM_ROWS = 3;
const int NUM_COLS = 4;

const int colPins[NUM_COLS] = { 13, 14, 26, 27 };
const int rowPins[NUM_ROWS] = { 34, 35, 36 };

const int pinThermLeft = 39;
const int pinThermCenter = 32;
const int pinThermRight = 33;
const int pinSound = 25;
const int pinSD_CS = 5;

// --- TIME & LOGGING PARAMETERS ---
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -28800;    // Pacific Standard Time (UTC-8)
const int daylightOffset_sec = 3600;  // +1 hour for PDT

// --- STATE MACHINE ---
enum SystemState { STATE_EMPTY,
                   STATE_SLEEPING };
SystemState sysState = STATE_EMPTY;

enum CalibState { CALIB_IDLE,
                  CALIB_EMPTY,
                  CALIB_LEFT,
                  CALIB_CENTER,
                  CALIB_RIGHT };
CalibState currentCalib = CALIB_IDLE;

// --- SESSION LOGGING ARCHITECTURE CONTROLS ---
enum SessionState { SESS_IDLE,
                    SESS_ACTIVE,
                    SESS_PAUSED };
SessionState sessState = SESS_IDLE;

String currentSessionFile = "";
unsigned long sessionStartTime = 0;
unsigned long sessionActiveDurationMs = 0;
unsigned long lastActiveTimeMs = 0;
unsigned long pauseStartTimeMs = 0;

const unsigned long GRACE_PERIOD_MS = 20UL * 60UL * 1000UL;          // 20 Minutes Grace Period
const unsigned long MIN_SESSION_DURATION_MS = 15UL * 60UL * 1000UL;  // 15 Minutes Minimum

// --- ALGORITHM PARAMETERS ---
const unsigned long SAMPLE_RATE_MS = 250;  // 4Hz (250ms per tick)
const unsigned long TIMEOUT_MS = 10000;

// --- LOGGING TO SD CARD ---
const unsigned long LOGGING_INTERVAL_MS = 30000;  // 30 seconds inside session
unsigned long lastLogTime = 0;
int snoreCountThisMinute = 0;  // Acts as binary indicator for the 30s log window (1 if snore detected, 0 otherwise)

int noiseThreshold[NUM_ROWS][NUM_COLS];
int profLeft[NUM_ROWS][NUM_COLS];
int profCenter[NUM_ROWS][NUM_COLS];
int profRight[NUM_ROWS][NUM_COLS];
bool hasProfiles = false;

// --- EXTENDED PROFILES PERSISTENCE VARIABLES ---
int currentCalibrationId = 0;
bool hasLeftProfile = false;
bool hasCenterProfile = false;
bool hasRightProfile = false;

// --- DIAGNOSTICS HARDWARE MONITORING ---
bool sdReady = false;

// --- GLOBAL VARIABLES ---
int pillowMatrix[NUM_ROWS][NUM_COLS];
long calibAccumulator[NUM_ROWS][NUM_COLS];
int calibSampleCount = 0;
int presenceCounter = 0;
unsigned long lastSampleTime = 0;
unsigned long lastPressureTime = 0;
String currentPosture = "Unknown";

// --- REQUIREMENT 1: 5-SECOND THERMAL AVERAGING BUFFERS ---
const int TEMP_BUFFER_SIZE = 20;  // 20 samples @ 250ms = 5 seconds
float tempBufferL[TEMP_BUFFER_SIZE] = { 0 };
float tempBufferC[TEMP_BUFFER_SIZE] = { 0 };
float tempBufferR[TEMP_BUFFER_SIZE] = { 0 };
int tempIdx = 0;
int tempCount = 0;
float avgTempLeft = 0.0, avgTempCenter = 0.0, avgTempRight = 0.0;

// --- REQUIREMENT 2: 10-SECOND SNORE PATTERN BUFFER ---
const int SNORE_BUFFER_SIZE = 80;  // 80 samples @ 250ms = 20 seconds
bool soundBuffer[SNORE_BUFFER_SIZE] = { false };
int soundIdx = 0;
bool snoreDetectedThisWindow = false;
bool isRhythmicSnoring = false;

// Thermistor Constants
const float SERIES_RESISTOR = 10000.0;
const float THERMISTOR_NOMINAL = 10000.0;
const float TEMPERATURE_NOMINAL = 25.0;
const float B_COEFFICIENT = 3950.0;

// --- HTML & JS DASHBOARD ---
const char index_html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>Smart Pillow Dashboard</title>
  <script src="/chart.js" defer></script>
  <style>
    body { background: #121212; color: #ececec; font-family: -apple-system, BlinkMacSystemFont, sans-serif; margin: 0; padding: 15px; text-align: center; }
    h1 { color: #4DA8DA; font-size: 22px; margin-bottom: 5px; }
    .status { font-size: 12px; color: #888; margin-bottom: 15px; }
    .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
    .card { background: #1e1e1e; border-radius: 10px; padding: 15px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
    .card.full { grid-column: span 2; }
    .label { font-size: 11px; text-transform: uppercase; letter-spacing: 1px; color: #aaa; margin-bottom: 8px;}
    .val { font-size: 22px; font-weight: bold; }
    .snore { color: #ff4c4c; font-weight: bold; font-size: 16px; display: none; margin-top: 5px; }
    
    /* Continuous Hardware Alert Banner Styles */
    .hardware-banner { display: none; background: #c0392b; color: #fff; padding: 12px 20px; font-weight: bold; font-size: 13px; border-radius: 8px; margin-bottom: 15px; align-items: center; justify-content: space-between; border: 1px solid #e74c3c; box-shadow: 0 4px 10px rgba(0,0,0,0.4); }
    .btn-retry { background: #fff; color: #c0392b; border: none; padding: 6px 14px; border-radius: 4px; font-weight: bold; cursor: pointer; font-size: 12px; transition: opacity 0.2s; }
    .btn-retry:hover { opacity: 0.9; }

    /* Matrix Styles */
    .matrix-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 5px; margin-top: 10px; }
    .cell { background: #333; color: #fff; padding: 10px 0; border-radius: 6px; font-size: 12px; font-weight: bold; transition: background 0.1s; }
    
    /* UI Buttons & Animation */
    .nav-bar { display: flex; justify-content: center; gap: 10px; margin-bottom: 15px; flex-wrap: wrap; }
    .btn-settings { background: #333; color: #fff; border: 1px solid #555; padding: 8px 15px; border-radius: 6px; cursor: pointer; font-weight: bold; transition: background 0.2s; }
    .btn-settings:hover { background: #444; }
    .btn-eye { background: #2a2a2a; color: #fff; border: 1px solid #555; padding: 12px; border-radius: 6px; cursor: pointer; font-weight: bold; }
    
    button.cal-btn { position: relative; overflow: hidden; background: #4DA8DA; color: #fff; border: none; padding: 12px 15px; border-radius: 6px; font-weight: bold; font-size: 14px; cursor: pointer; width: 100%; }
    button.cal-btn:disabled { background: #555; color: #888; cursor: not-allowed; }
    button.cal-btn .progress { position: absolute; left: 0; top: 0; height: 100%; background: rgba(76, 175, 80, 0.6); width: 0; pointer-events: none; }
    
    .btn-reset { background: #ff4c4c !important; margin-top: 15px !important;}
    hr { border: 0; border-top: 1px solid #333; margin: 15px 0; }
    .step-text { font-size: 14px; text-align: left; margin: 10px 5%; color: #ddd; }
    .flex-row { display: flex; align-items: center; justify-content: space-between; gap: 8px; margin: 5px 5%; }

    /* Inline Control Button & Trash Icon Elements */
    .btn-delete-icon { background: transparent; border: none; cursor: pointer; padding: 6px; display: inline-flex; align-items: center; justify-content: center; transition: background 0.2s, border-radius 0.2s; }
    .btn-delete-icon:hover { background: rgba(231, 76, 60, 0.2); border-radius: 50%; }
    .btn-delete-icon svg { fill: #e74c3c; transition: fill 0.2s; }
    .btn-delete-icon:hover svg { fill: #ff4c4c; }

    /* Compact Left/Right Toggle Switch Styles */
    .toggle-container { display: flex; align-items: center; justify-content: flex-end; gap: 6px; margin-top: 10px; padding-right: 5px; }
    .toggle-label { font-weight: bold; font-size: 11px; text-transform: uppercase; letter-spacing: 0.5px; transition: color 0.2s; }
    .switch { position: relative; display: inline-block; width: 36px; height: 18px; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #333; transition: .2s; border-radius: 18px; border: 1px solid #555; }
    .slider:before { position: absolute; content: ""; height: 10px; width: 10px; left: 3px; bottom: 3px; background-color: #4DA8DA; transition: .2s; border-radius: 50%; }
    input:checked + .slider { background-color: #1a2f1d; border-color: #4caf50; }
    input:checked + .slider:before { transform: translateX(18px); background-color: #4caf50; }

    /* Sleep History Subpage Styles */
    .history-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(280px, 1fr)); gap: 15px; margin-top: 15px; text-align: left; }
    .session-card { background: #1e1e1e; border: 1px solid #333; border-radius: 10px; padding: 15px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); transition: transform 0.2s, border-color 0.2s; }
    .session-card:hover { transform: translateY(-2px); border-color: #4DA8DA; }
    .session-card .card-title { color: #4DA8DA; font-size: 15px; font-weight: bold; letter-spacing: 0.5px; }
    .session-card .card-meta { font-size: 12px; color: #aaa; margin: 4px 0; }
    .session-card .card-duration { font-size: 14px; font-weight: bold; color: #fff; margin-top: 12px; border-top: 1px solid #2a2a2a; padding-top: 8px; }
    .btn-analytics { background: #4DA8DA; color: #fff; border: none; padding: 10px 12px; border-radius: 6px; font-weight: bold; font-size: 13px; cursor: pointer; width: 100%; margin-top: 12px; transition: background 0.2s; text-align: center; }
    .btn-analytics:hover { background: #3a97c9; }

    /* Responsive Chart Canvas Workspace Styling */
    .analytics-view { display: none; text-align: left; background: #1e1e1e; border-radius: 10px; padding: 15px; margin-top: 15px; }
    .chart-container { position: relative; width: 100%; margin-bottom: 25px; background: #151515; border: 1px solid #2a2a2a; border-radius: 8px; padding: 10px; box-sizing: border-box; }
    .chart-container.top-timeline { height: 180px; }
    .chart-container.bottom-metrics { height: 340px; }
    .analytics-header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid #333; padding-bottom: 10px; margin-bottom: 15px; flex-wrap: wrap; gap: 10px; }
    
    /* Overlay Modal Framework Layouts */
    .modal-overlay { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.85); align-items: center; justify-content: center; z-index: 9999; padding: 20px; box-sizing: border-box; }
    .modal-card { background: #1e1e1e; border-radius: 10px; padding: 20px; box-shadow: 0 8px 24px rgba(0,0,0,0.5); width: 100%; max-width: 380px; text-align: left; box-sizing: border-box; border: 1px solid #333; }
    select { width: 100%; padding: 12px; background: #2a2a2a; border: 1px solid #444; border-radius: 6px; color: #fff; box-sizing: border-box; font-size: 14px; margin-bottom: 4px; }
    select:focus { border-color: #4DA8DA; outline: none; }
    input[type="password"] { width: 100%; padding: 12px; background: #2a2a2a; border: 1px solid #444; border-radius: 6px; color: #fff; box-sizing: border-box; font-size: 14px; }
    input[type="password"]:focus { border-color: #4DA8DA; outline: none; }
    .btn-action { background: #4DA8DA; color: #fff; border: none; padding: 12px; border-radius: 6px; font-weight: bold; font-size: 14px; cursor: pointer; width: 100%; margin-top: 15px; transition: background 0.2s; }
    .btn-action:hover { background: #3a97c9; }
  </style>
</head>
<body>
  <div class="hardware-banner" id="sdAlertBanner">
    <span>&#x26A0; <b>SD Storage Fault:</b> SD card cannot be accessed or is disconnected. Real-time data tracking is limited.</span>
    <button class="btn-retry" onclick="triggerSdRemountRecovery()">Retry Initialization</button>
  </div>

  <h1>Smart Pillow Dashbaord</h1>
  <div class="status" id="connStatus">Connecting...</div>
  
  <div class="nav-bar">
    <button class="btn-settings" id="btnNavLive" onclick="switchToView('live')" style="display: none; background: #4DA8DA;">Live Dashboard</button>
    <button class="btn-settings" id="btnNavHistory" onclick="switchToView('history')">Sleep History</button>
    <button class="btn-settings" id="btnToggleSettings" onclick="toggleSettings()">Settings & Calibration</button>
    <button class="btn-settings" id="btnToggleNetwork" onclick="toggleNetworkModal()">&#x1F4F6; Wi-Fi Setup</button>
    <button class="btn-settings" style="background: #1f1f1f; border-color: #3f3f3f; color: white;" onmouseover="this.style.background='#c0392b'" onmouseout="this.style.background='#1f1f1f'" onclick="triggerDeepSleep()">&#x1F4A4; Deep Sleep</button>
  </div>
  
  <div id="liveWorkspace" class="grid">
    <div id="calibrationPanel" class="card full" style="display: none; border: 1px dashed #555;">
      <div class="label">Calibration & Setup</div>
      
      <div class="flex-row">
        <button class="cal-btn" onclick="triggerCal('EMPTY', this)">1. Calibrate Empty Bed (5s)<div class="progress"></div></button>
        <button class="btn-eye" onclick="viewCalProfile('noise')" title="View Values">View</button>
      </div>
      <hr>
      <div class="label">Body Position Profiles</div>
      
      <div class="step-text">A. Toss <b>LEFT</b> then press start:</div>
      <div class="flex-row">
        <button class="cal-btn" onclick="triggerCal('LEFT', this)">Calibrate Left (5s)<div class="progress"></div></button>
        <button class="btn-eye" onclick="viewCalProfile('pLeft')" title="View Values">View</button>
      </div>
      
      <div class="step-text">B. Toss <b>CENTER</b> then press start:</div>
      <div class="flex-row">
        <button class="cal-btn" onclick="triggerCal('CENTER', this)">Calibrate Center (5s)<div class="progress"></div></button>
        <button class="btn-eye" onclick="viewCalProfile('pCenter')" title="View Values">View</button>
      </div>
      
      <div class="step-text">C. Toss <b>RIGHT</b> then press start:</div>
      <div class="flex-row">
        <button class="cal-btn" onclick="triggerCal('RIGHT', this)">Calibrate Right (5s)<div class="progress"></div></button>
        <button class="btn-eye" onclick="viewCalProfile('pRight')" title="View Values">View</button>
      </div>
      
      <div id="profileViewer" style="display:none; background:#111; border:1px solid #4DA8DA; border-radius:6px; padding:10px; margin:15px 5%; font-family:monospace; font-size:12px; text-align:left; color:#ececec;">
        <div style="font-weight:bold; color:#4DA8DA; margin-bottom:5px;" id="profileViewerTitle">Profile Target:</div>
        <div id="profileViewerContent" style="line-height: 1.5;"></div>
      </div>
      
      <hr>
      <button class="cal-btn btn-reset" onclick="triggerReset()">Reset All Calibrations</button>
    </div>

    <div class="card full" style="background: #112233; border: 1px solid #4DA8DA; position: relative;">
      <div class="label">System State</div>
      <div class="val" id="sysState" style="color:#4DA8DA;">Empty</div>
      <div class="label" style="margin-top:10px;">Detected Posture</div>
      <div class="val" id="posture" style="font-size:28px; color:#fff;">--</div>
      
      <div class="toggle-container">
        <span id="rawLabel" class="toggle-label" style="color: #4DA8DA;">Raw</span>
        <label class="switch">
          <input type="checkbox" id="modeCheckbox" onchange="toggleDataWorkspaceMode()">
          <span class="slider"></span>
        </label>
        <span id="calLabel" class="toggle-label" style="color: #666;">Calibrated</span>
      </div>

      <div class="snore" id="snoreAlert">RHYTHMIC SNORE DETECTED!</div>
    </div>

    <div class="card full">
      <div class="label">Live Pressure Matrix</div>
      <div class="matrix-grid" id="matrixContainer"></div>
    </div>

    <div class="card"><div class="label">Total Mass</div><div class="val" id="mass">0</div></div>
    <div class="card"><div class="label">Center of Mass</div><div class="val" style="font-size:18px;"><span id="xPos">0.0</span>, <span id="yPos">0.0</span></div></div>
    
    <div class="card full">
      <div class="label">5s Avg Thermals (°C)</div>
      <div class="val" style="font-size: 16px;">L: <span id="tL">-</span> | C: <span id="tC">-</span> | R: <span id="tR">-</span></div>
    </div>
  </div>

  <div id="historyWorkspace" class="card full" style="display: none; text-align: left;">
    <div class="label" style="font-size: 14px; color: #4DA8DA;">Historical Sleep Sessions</div>
    
    <div id="sessionCardsContainer" class="history-grid"></div>

    <div id="analyticsView" class="analytics-view">
      <div class="analytics-header">
        <div>
          <div id="analyticsTitle" style="font-size: 16px; font-weight: bold; color: #4DA8DA;">Session Analytics Plot</div>
          <div id="analyticsMeta" style="font-size: 12px; color: #aaa; margin-top: 2px;">File Index Target: --</div>
        </div>
        <div style="display: flex; gap: 8px; align-items: center;">
          <button class="btn-delete-icon" id="btnDeleteCurrentAnalyticsLog" style="background: #2a2a2a; border: 1px solid #444; border-radius: 6px; padding: 8px;" title="Purge This Record Permanently">
            <svg viewBox="0 0 24 24" width="20" height="20"><path d="M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z"/></svg>
          </button>
          <button class="btn-settings" onclick="closeAnalyticsView()" style="background: #333;">&larr; Exit Analytics</button>
        </div>
      </div>

      <div class="label" style="margin-bottom: 5px;">1. State &amp; Posture Timeline Chart</div>
      <div class="chart-container top-timeline">
        <canvas id="timelineChart"></canvas>
      </div>

      <div class="label" style="margin-bottom: 5px;">2. Continuous Metrics (Mass, Thermals, &amp; Posture-Linked Snoring)</div>
      <div class="chart-container bottom-metrics">
        <canvas id="metricsChart"></canvas>
      </div>

      <div class="grid" style="margin-top: 20px;">
        <div class="card">
          <div class="label" style="color: #4DA8DA;">3. Sleep Playback Scrubber Heatmap (<span id="playbackTime">--:--:--</span>)</div>
          <div class="matrix-grid" id="historyMatrixContainer"></div>
        </div>
        <div class="card">
          <div class="label" style="color: #4DA8DA;">4. Automated Sleep Architecture Insights</div>
          <div style="text-align: left; margin: 12px 5%; font-size: 13px; line-height: 1.6;">
            <div>&#x1F4CA; <b>Total Sleep Efficiency:</b> <span id="insightEfficiency" style="color: #4caf50; font-weight: bold; font-size: 16px;">--</span>%</div>
            <div>&#x1F504; <b>Toss &amp; Turn Count:</b> <span id="insightTossTurn" style="color: #ff4c4c; font-weight: bold; font-size: 16px;">--</span> position shifts</div>
          </div>
          <div style="position: relative; width: 100%; height: 130px; margin-top: 5px;">
            <canvas id="pieChart"></canvas>
          </div>
        </div>
      </div>
    </div>
  </div>

  <div id="networkModal" class="modal-overlay">
    <div class="modal-card">
      <div class="label" style="color: #4DA8DA; font-size: 13px; margin: 0 0 10px 0;">Network Configuration</div>
      <div id="modalWifiStatus" style="font-size: 12px; color: #888; margin-bottom: 15px; font-weight: bold;">Initializing...</div>
      
      <label class="label" style="margin-top: 10px;">Select Airwave SSID</label>
      <select id="modalSsidSelect"></select>
      
      <div style="text-align: right; margin-bottom: 12px;">
        <button class="btn-settings" style="font-size: 11px; padding: 4px 10px;" onclick="scanWifiNetworks()">&#x1F504; Rescan Networks</button>
      </div>
      
      <label class="label" style="margin-top: 10px;">WPA2 Preshared Key</label>
      <input type="password" id="modalPasswordKey" placeholder="Enter network password">
      
      <button class="btn-action" onclick="commitDashboardWifi()">Commit Credentials</button>
      <button class="btn-settings" style="background: #333; margin-top: 10px; width: 100%; box-sizing: border-box;" onclick="toggleNetworkModal()">Exit Setup</button>
    </div>
  </div>

  <script>
    // --- ESTABLISH CORE WEBSOCKET CHANNEL IMMEDIATELY ---
    var connection = new WebSocket('ws://' + location.hostname + ':81/', ['arduino']);
    var globalCacheFrame = null;
    var UI_WORKSPACE_MODE = "raw"; 

    var timelineChartInstance = null;
    var metricsChartInstance = null;
    var pieChartInstance = null;

    connection.onopen = function () {
      document.getElementById('connStatus').innerText = 'Connected via WebSockets';
      document.getElementById('connStatus').style.color = '#4caf50';
    };

    // --- CUSTOM CHART.JS PLUGIN REGISTERED SAFELY ---
    window.addEventListener('DOMContentLoaded', () => {
      if (typeof Chart !== 'undefined') {
        const postureBackgroundPlugin = {
          id: 'postureBackground',
          beforeDatasetsDraw(chart, args, options) {
            const { ctx, chartArea: { top, bottom, height }, scales: { x } } = chart;
            if (!options.postures || options.postures.length === 0) return;
            
            const meta = chart.getDatasetMeta(0);
            if (!meta || !meta.data || meta.data.length === 0) return;
            
            const totalPoints = chart.data.labels.length;
            ctx.save();
            
            for (let i = 0; i < totalPoints; i++) {
              const currentPoint = meta.data[i];
              if (!currentPoint) continue;
              
              let nextX = x.right;
              if (i < totalPoints - 1 && meta.data[i + 1]) {
                nextX = meta.data[i + 1].x;
              }
              
              const currentX = currentPoint.x;
              const blockWidth = nextX - currentX;
              if (blockWidth <= 0) continue;
              
              const posture = options.postures[i];
              let color = 'transparent';
              
              if (posture === "Center Back") {
                color = 'rgba(76, 175, 80, 0.12)';
              } else if (posture === "Left Side") {
                color = 'rgba(77, 168, 218, 0.12)';
              } else if (posture === "Right Side") {
                color = 'rgba(155, 89, 182, 0.12)';
              }
              
              if (color !== 'transparent') {
                ctx.fillStyle = color;
                ctx.fillRect(currentX, top, blockWidth, height);
              }
            }
            ctx.restore();
          }
        };
        Chart.register(postureBackgroundPlugin);
      }
    });

    // --- DYNAMIC MATRIX GENERATION ---
    var matrixContainer = document.getElementById('matrixContainer');
    for(let i=0; i<12; i++) { matrixContainer.innerHTML += '<div class="cell" id="cell-' + i + '">0</div>'; }
    
    var historyMatrixContainer = document.getElementById('historyMatrixContainer');
    for(let i=0; i<12; i++) { historyMatrixContainer.innerHTML += '<div class="cell" id="hist-cell-' + i + '">0</div>'; }

    function toggleSettings() {
      let panel = document.getElementById('calibrationPanel');
      panel.style.display = (panel.style.display === 'none') ? 'block' : 'none';
    }

    // --- NETWORK MODAL LOGIC UI HANDLERS ---
    function toggleNetworkModal() {
      let modal = document.getElementById('networkModal');
      if (modal.style.display === 'none' || modal.style.display === '') {
        modal.style.display = 'flex';
        scanWifiNetworks();
      } else {
        modal.style.display = 'none';
      }
    }

    function scanWifiNetworks() {
      let statusBox = document.getElementById('modalWifiStatus');
      let selectBox = document.getElementById('modalSsidSelect');
      statusBox.innerText = "Scanning 2.4GHz Airwaves...";
      statusBox.style.color = "#f1c40f";
      selectBox.innerHTML = '<option value="">-- Scanning Transmissions --</option>';
      
      fetch('/api/wifi_scan')
        .then(res => res.json())
        .then(networks => {
          statusBox.innerText = "Scan Complete (" + networks.length + " networks discovered)";
          statusBox.style.color = "#4caf50";
          selectBox.innerHTML = '';
          if (networks.length === 0) {
            selectBox.innerHTML = '<option value="">No Active Networks Found</option>';
            return;
          }
          networks.forEach(net => {
            let optionNode = document.createElement('option');
            optionNode.value = net.ssid;
            optionNode.innerText = net.ssid + " (" + net.rssi + " dBm)";
            selectBox.appendChild(optionNode);
          });
        })
        .catch(err => {
          console.error("Scanning pipeline trace error:", err);
          statusBox.innerText = "Scan Handshake Error Encountered";
          statusBox.style.color = "#ff4c4c";
          selectBox.innerHTML = '<option value="">Error Intercepting Scan Matrix</option>';
        });
    }

    function commitDashboardWifi() {
      let selectBox = document.getElementById('modalSsidSelect');
      let passField = document.getElementById('modalPasswordKey');
      let statusBox = document.getElementById('modalWifiStatus');
      
      let targetSsid = selectBox.value;
      let targetPass = passField.value;
      
      if (!targetSsid) {
        alert("Selection Fault: Please choose a valid network SSID target vector.");
        return;
      }
      
      statusBox.innerText = "Writing Config Profiles to Cluster...";
      statusBox.style.color = "#f1c40f";
      
      var argumentsBuffer = new URLSearchParams();
      argumentsBuffer.append('ssid', targetSsid);
      argumentsBuffer.append('password', targetPass);
      
      fetch('/api/save_wifi', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: argumentsBuffer.toString()
      })
      .then(res => res.json())
      .then(data => {
        if (data.success) {
          statusBox.innerText = "Credentials committed! Core is re-binding infrastructure layers. Check http://smartpillow.local shortly.";
          statusBox.style.color = "#4caf50";
          setTimeout(() => { toggleNetworkModal(); }, 4000);
        } else {
          statusBox.innerText = "Write Failure: " + data.error;
          statusBox.style.color = "#ff4c4c";
        }
      })
      .catch(err => {
        console.error("Error committing credentials:", err);
        statusBox.innerText = "Transmission tracking module failure.";
        statusBox.style.color = "#ff4c4c";
      });
    }

    function toggleDataWorkspaceMode() {
      let chk = document.getElementById('modeCheckbox');
      let rawLbl = document.getElementById('rawLabel');
      let calLbl = document.getElementById('calLabel');
      
      if (chk.checked) {
        UI_WORKSPACE_MODE = "calibrated";
        rawLbl.style.color = "#666";
        calLbl.style.color = "#4caf50";
      } else {
        UI_WORKSPACE_MODE = "raw";
        rawLbl.style.color = "#4DA8DA";
        calLbl.style.color = "#666";
      }
      if(globalCacheFrame) parseRenderTelemetry(globalCacheFrame);
    }

    function switchToView(view) {
      let liveWs = document.getElementById('liveWorkspace');
      let histWs = document.getElementById('historyWorkspace');
      let btnLive = document.getElementById('btnNavLive');
      let btnHist = document.getElementById('btnNavHistory');
      let btnSettings = document.getElementById('btnToggleSettings');
      let calPanel = document.getElementById('calibrationPanel');

      if (view === 'history') {
        liveWs.style.display = 'none';
        calPanel.style.display = 'none';
        histWs.style.display = 'block';
        btnLive.style.display = 'inline-block';
        btnHist.style.display = 'none';
        btnSettings.style.display = 'none';
        closeAnalyticsView();
        fetchSessions();
      } else {
        liveWs.style.display = 'grid';
        histWs.style.display = 'none';
        btnLive.style.display = 'none';
        btnHist.style.display = 'inline-block';
        btnSettings.style.display = 'inline-block';
      }
    }

    // --- EXECUTE METADATA REST API LOG PURGE ---
    function purgeSessionRecord(fileName, exitToWorkspaceAfterPurge) {
      if (confirm("Confirm Deletion Request: Are you completely sure you want to permanently erase the historical dataset log file [" + fileName + "] from internal storage hardware?")) {
        fetch('/api/delete_session?file=' + fileName, { method: 'GET' })
          .then(res => res.json())
          .then(reply => {
            if (reply.success) {
              alert("Storage Status: Specified sleep profile record has been purged successfully.");
              if (exitToWorkspaceAfterPurge) {
                closeAnalyticsView();
              }
              fetchSessions();
            } else {
              alert("Operational Halt: System error encountered during deletion command processing -> " + reply.error);
            }
          })
          .catch(err => {
            console.error("Hardware pipeline interruption during purge trace call:", err);
            alert("Connection Pipeline Error: Unable to complete deletion handshake verification with the ESP32 chip module.");
          });
      }
    }

    // --- REMOUNT STORAGE MODULE DISCOVERY COMMAND LOOP ---
    function triggerSdRemountRecovery() {
      fetch('/api/sd_retry')
        .then(res => res.json())
        .then(status => {
          if (status.success) {
            document.getElementById('sdAlertBanner').style.display = 'none';
            alert("Hardware Status Update: Storage controller re-mounted and validated successfully.");
            if (document.getElementById('historyWorkspace').style.display === 'block') {
              fetchSessions();
            }
          } else {
            alert("Hardware Error: Remount execution failed. Ensure standard formatted MicroSD structure card is securely docked in the physical slot pinout.");
          }
        })
        .catch(err => console.error("Handshake fail during processing remount loop:", err));
    }

    function fetchSessions() {
      let container = document.getElementById('sessionCardsContainer');
      container.style.display = 'grid';
      container.innerHTML = '<div style="color: #aaa; padding: 20px; font-family: monospace;">Polling SD Card Session Index...</div>';
      
      fetch('/api/sessions')
        .then(response => response.json())
        .then(data => {
          container.innerHTML = '';
          if (data.length === 0) {
            container.innerHTML = '<div style="color: #888; padding: 20px;">No historical sleep sessions detected.</div>';
            return;
          }
          
          data.forEach(session => {
            let durationText = "Unknown Duration";
            if (session.start && session.end) {
              let startDt = new Date(session.start.replace(' ', 'T'));
              let endDt = new Date(session.end.replace(' ', 'T'));
              let diffMs = endDt - startDt;
              if (!isNaN(diffMs) && diffMs >= 0) {
                let diffMins = Math.floor(diffMs / 60000);
                let hrs = Math.floor(diffMins / 60);
                let mins = diffMins % 60;
                durationText = hrs + "h " + mins + "m";
              }
            }
            
            let displayDate = "Session Log";
            if (session.file.startsWith("s_")) {
              let parts = session.file.split("_");
              if (parts[1] && parts[1].length === 8) {
                let y = parts[1].substring(0, 4);
                let m = parts[1].substring(4, 6);
                let d = parts[1].substring(6, 8);
                displayDate = y + "-" + m + "-" + d;
              }
            } else if (session.start) {
              displayDate = session.start.split(' ')[0];
            }
            
            let cardHtml = '<div class="session-card">' +
              '<div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 8px;">' +
                '<div class="card-title">Date ID: ' + displayDate + '</div>' +
                '<button class="btn-delete-icon" onclick="purgeSessionRecord(\'' + session.file + '\', false)" title="Purge Record Safely">' +
                  '<svg viewBox="0 0 24 24" width="18" height="18"><path d="M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z"/></svg>' +
                '</button>' +
              '</div>' +
              '<div class="card-meta"><b>File Target:</b> ' + session.file + '</div>' +
              '<div class="card-meta"><b>Start Clock:</b> ' + (session.start ? session.start : '--') + '</div>' +
              '<div class="card-meta"><b>Stop Clock:</b> ' + (session.end ? session.end : '--') + '</div>' +
              '<div class="card-duration">Total Duration: ' + durationText + '</div>' +
              '<button class="btn-analytics" onclick="openSessionAnalytics(\'' + session.file + '\')">Open Analytics</button>' +
              '</div>';
              
            container.innerHTML += cardHtml;
          });
        })
        .catch(err => {
          console.error("API error:", err);
          container.innerHTML = '<div style="color: #ff4c4c; padding: 20px;">Failed to fetch history from server.</div>';
        });
    }

    // --- TIMELINE PLAYBACK SCRUBBER ENGINE LINK ---
    function updatePlaybackFrame(index, dataset) {
      let frame = dataset[index];
      if (!frame) return;
      
      document.getElementById('playbackTime').innerText = frame.time.split(' ')[1] || frame.time;
      
      let baselineNoise = (globalCacheFrame && globalCacheFrame.noise) ? globalCacheFrame.noise : Array(12).fill(0);
      
      for (let i = 0; i < 12; i++) {
        let rawVal = frame.matrix[i];
        let noiseVal = baselineNoise[i];
        let calVal = Math.max(0, rawVal - noiseVal);
        
        let cell = document.getElementById('hist-cell-' + i);
        if (cell) {
          cell.innerText = calVal;
          let intensity = Math.min(255, Math.floor((calVal / 4095) * 255));
          cell.style.background = (calVal > 400) ? 'rgb(0, ' + (100 + intensity/2) + ', ' + intensity + ')' : '#333';
        }
      }
    }

    function openSessionAnalytics(fileName) {
      if (typeof Chart === 'undefined') {
        alert("System Notice: Chart library asset transfer is still completing in the background or failed to load. Please try again in a few seconds.");
        return;
      }

      document.getElementById('sessionCardsContainer').style.display = 'none';
      let viewZone = document.getElementById('analyticsView');
      viewZone.style.display = 'block';
      
      document.getElementById('analyticsTitle').innerText = "Analyzing Sleep Signature";
      document.getElementById('analyticsMeta').innerText = "File Path Resource: /logs/" + fileName;

      // Link Active Instance Multi-View Trashcan Handler
      document.getElementById('btnDeleteCurrentAnalyticsLog').onclick = function() {
        purgeSessionRecord(fileName, true);
      };

      if(timelineChartInstance) { timelineChartInstance.destroy(); timelineChartInstance = null; }
      if(metricsChartInstance) { metricsChartInstance.destroy(); metricsChartInstance = null; }
      if(pieChartInstance) { pieChartInstance.destroy(); pieChartInstance = null; }

      fetch('/api/session_data?file=' + fileName)
        .then(res => res.json())
        .then(data => {
          if(!data || data.length === 0) {
            document.getElementById('analyticsTitle').innerText = "Error: File Empty/Inaccessible";
            return;
          }

          document.getElementById('analyticsTitle').innerText = "Data Visualization Workspace Loaded";

          // --- ARCHITECTURE INSIGHT ENGINE COMPUTATION ---
          let sleepFrames = data.filter(f => f.state === "Sleeping").length;
          let totalFrames = data.length;
          let sleepEfficiency = totalFrames > 0 ? ((sleepFrames / totalFrames) * 100).toFixed(1) : "0.0";
          document.getElementById('insightEfficiency').innerText = sleepEfficiency;

          let tossTurnCount = 0;
          for(let i = 1; i < data.length; i++) {
            if(data[i].posture !== data[i-1].posture && data[i].state === "Sleeping") {
              tossTurnCount++;
            }
          }
          document.getElementById('insightTossTurn').innerText = tossTurnCount;

          let timelineLabels = [];
          let postureNumericMap = [];
          let rawPosturesArray = [];
          let massSeries = [];
          let tempLSeries = [];
          let tempCSeries = [];
          let tempRSeries = [];
          let snoreSeries = [];

          let leftDuration = 0, centerDuration = 0, rightDuration = 0;

          data.forEach((frame, idx) => {
            let tClock = frame.time.split(' ')[1] || frame.time;
            timelineLabels.push(tClock);
            
            let posVal = 0; 
            if(frame.posture === "Left Side") { posVal = 1; leftDuration++; }
            else if(frame.posture === "Center Back") { posVal = 2; centerDuration++; }
            else if(frame.posture === "Right Side") { posVal = 3; rightDuration++; }
            
            postureNumericMap.push(posVal);
            rawPosturesArray.push(frame.posture);
            
            massSeries.push(frame.mass);
            tempLSeries.push(frame.tempL);
            tempCSeries.push(frame.tempC);
            tempRSeries.push(frame.tempR);
          });

          // --- 5-MINUTE INTERVAL SNORE DATA AGGREGATION (10 logs per 5-min window) ---
          for (let i = 0; i < data.length; i++) {
            let blockStart = Math.floor(i / 10) * 10;
            let blockEnd = Math.min(data.length, blockStart + 10);
            let sum = 0;
            for (let j = blockStart; j < blockEnd; j++) {
              sum += data[j].snore;
            }
            snoreSeries.push(sum);
          }

          updatePlaybackFrame(0, data);

          // Render Spatial Allocation Distribution Pie Chart
          const ctxPie = document.getElementById('pieChart').getContext('2d');
          pieChartInstance = new Chart(ctxPie, {
            type: 'doughnut',
            data: {
              labels: ['Left Side', 'Center Back', 'Right Side'],
              datasets: [{
                data: [leftDuration, centerDuration, rightDuration],
                backgroundColor: ['#4DA8DA', '#4caf50', '#9b59b6'],
                borderColor: '#1e1e1e',
                borderWidth: 2
              }]
            },
            options: {
              responsive: true,
              maintainAspectRatio: false,
              plugins: {
                legend: {
                  position: 'right',
                  labels: { color: '#aaa', font: { size: 10 } }
                }
              }
            }
          });

          // Render Line Plots
          const ctxTimeline = document.getElementById('timelineChart').getContext('2d');
          timelineChartInstance = new Chart(ctxTimeline, {
            type: 'line',
            data: {
              labels: timelineLabels,
              datasets: [{
                label: 'Posture State Timeline',
                data: postureNumericMap,
                borderColor: '#4DA8DA',
                borderWidth: 3,
                stepped: 'before',
                fill: false,
                pointRadius: 0,
                pointHoverRadius: 4
              }]
            },
            options: {
              responsive: true,
              maintainAspectRatio: false,
              interaction: { mode: 'index', intersect: false },
              plugins: {
                legend: { display: false },
                tooltip: { mode: 'index', intersect: false }
              },
              onHover: (event, activeElements) => {
                if (activeElements && activeElements.length > 0) {
                  let idx = activeElements[0].index;
                  updatePlaybackFrame(idx, data);
                }
              },
              scales: {
                x: { display: false }, 
                y: {
                  min: 0,
                  max: 3,
                  ticks: {
                    stepSize: 1,
                    color: '#aaa',
                    callback: function(val) {
                      return ["Empty", "Left Side", "Center Back", "Right Side"][val];
                    }
                  },
                  grid: { color: '#2a2a2a' }
                }
              }
            }
          });

          const ctxMetrics = document.getElementById('metricsChart').getContext('2d');
          metricsChartInstance = new Chart(ctxMetrics, {
            data: {
              labels: timelineLabels,
              datasets: [
                {
                  type: 'line',
                  label: 'Total Mass',
                  data: massSeries,
                  borderColor: '#ffffff',
                  borderWidth: 2,
                  fill: false,
                  pointRadius: 0,
                  yAxisID: 'yMass'
                },
                {
                  type: 'line',
                  label: 'Temp Left',
                  data: tempLSeries,
                  borderColor: 'rgba(231, 76, 60, 0.45)', 
                  borderWidth: 1.5,
                  fill: false,
                  pointRadius: 0,
                  yAxisID: 'yTemp'
                },
                {
                  type: 'line',
                  label: 'Temp Center',
                  data: tempCSeries,
                  borderColor: 'rgba(241, 196, 15, 0.45)', 
                  borderWidth: 1.5,
                  fill: false,
                  pointRadius: 0,
                  yAxisID: 'yTemp'
                },
                {
                  type: 'line',
                  label: 'Temp Right',
                  data: tempRSeries,
                  borderColor: 'rgba(230, 126, 34, 0.45)', 
                  borderWidth: 1.5,
                  fill: false,
                  pointRadius: 0,
                  yAxisID: 'yTemp'
                },
                {
                  type: 'bar',
                  label: 'Snore Events (5-Min Aggregated)',
                  data: snoreSeries,
                  backgroundColor: '#ff4c4c',
                  barPercentage: 1.0,
                  categoryPercentage: 1.0,
                  yAxisID: 'ySnore'
                }
              ]
            },
            options: {
              responsive: true,
              maintainAspectRatio: false,
              interaction: { mode: 'index', intersect: false },
              plugins: {
                legend: { labels: { color: '#ececec', boxWidth: 10 } },
                tooltip: { mode: 'index', intersect: false },
                postureBackground: { postures: rawPosturesArray } 
              },
              onHover: (event, activeElements) => {
                if (activeElements && activeElements.length > 0) {
                  let idx = activeElements[0].index;
                  updatePlaybackFrame(idx, data);
                }
              },
              scales: {
                x: {
                  ticks: { color: '#aaa', maxRotation: 45, autoSkip: true, maxTicksLimit: 20 },
                  grid: { color: '#252525' }
                },
                yMass: {
                  type: 'linear',
                  position: 'right',
                  title: { display: true, text: 'Mass Loading Units', color: '#fff' },
                  ticks: { color: '#fff' },
                  grid: { color: '#2a2a2a' }
                },
                yTemp: {
                  type: 'linear',
                  position: 'left',
                  title: { display: true, text: 'Thermals (&#xb0;C)', color: '#f1c40f' },
                  ticks: { color: '#f1c40f' },
                  grid: { display: false }
                },
                ySnore: {
                  type: 'linear',
                  position: 'right',
                  min: 0,
                  suggestedMax: 10,
                  title: { display: true, text: 'Snore Density / 5-Min Chunk', color: '#ff4c4c' },
                  ticks: { color: '#ff4c4c', stepSize: 1 },
                  grid: { display: false }
                }
              }
            }
          });
        })
        .catch(err => {
          console.error("Historical trace engine fetch error:", err);
          document.getElementById('analyticsTitle').innerText = "Failed parsing data file arrays.";
        });
    }

    function closeAnalyticsView() {
      if(timelineChartInstance) { timelineChartInstance.destroy(); timelineChartInstance = null; }
      if(metricsChartInstance) { metricsChartInstance.destroy(); metricsChartInstance = null; }
      if(pieChartInstance) { pieChartInstance.destroy(); pieChartInstance = null; }
      
      document.getElementById('analyticsView').style.display = 'none';
      document.getElementById('sessionCardsContainer').style.display = 'grid';
    }

    function viewCalProfile(profileName) {
      let viewer = document.getElementById('profileViewer');
      let content = document.getElementById('profileViewerContent');
      let title = document.getElementById('profileViewerTitle');
      
      if(!globalCacheFrame || !globalCacheFrame[profileName]) {
        alert("Waiting for connection matrix stream mapping...");
        return;
      }
      
      if(viewer.style.display === 'block' && viewer.dataset.active === profileName) {
        viewer.style.display = 'none';
        return;
      }
      
      viewer.style.display = 'block';
      viewer.dataset.active = profileName;
      
      let labels = { 'noise': 'Baseline Noise Data', 'pLeft': 'Left Posture Mapping', 'pCenter': 'Center Posture Mapping', 'pRight': 'Right Posture Mapping' };
      title.innerText = labels[profileName] + ":";
      
      let arr = globalCacheFrame[profileName];
      let tableOut = "";
      for(let r=0; r<3; r++) {
        let rowBuffer = [];
        for(let c=0; c<4; c++) {
          rowBuffer.push(arr[r * 4 + c]);
        }
        tableOut += rowBuffer.join("\t|\t") + "<br>";
      }
      content.innerHTML = tableOut;
    }

    function triggerCal(type, btnElement) {
      connection.send("CMD:" + type);
      let prog = btnElement.querySelector('.progress');
      prog.style.transition = 'none';
      prog.style.width = '0%';
      setTimeout(() => {
        prog.style.transition = 'width 5s linear';
        prog.style.width = '100%';
      }, 50);
    }

    function triggerReset() {
      if(confirm("Are you sure you want to reset all threshold and posture data?")) {
        connection.send("CMD:RESET");
        let viewer = document.getElementById('profileViewer');
        if(viewer) viewer.style.display = 'none';
      }
    }

    function triggerDeepSleep() {
      if(confirm("Put Smart Pillow into Deep Sleep? This will close all active file logs and disconnect the dashboard.")) {
        connection.send("CMD:SLEEP");
        document.getElementById('connStatus').innerText = 'ESP32 is Sleeping. Power cycle device to wake up.';
        document.getElementById('connStatus').style.color = '#ff4c4c';
      }
    }

    connection.onmessage = function (e) {
      var data = JSON.parse(e.data);
      globalCacheFrame = data; 
      parseRenderTelemetry(data);
    };

    function parseRenderTelemetry(data) {
      let btns = document.querySelectorAll('.cal-btn');
      if (data.calState !== "IDLE") {
        btns.forEach(b => b.disabled = true);
        document.getElementById('connStatus').innerText = 'CALIBRATING ' + data.calState + '... PLEASE WAIT';
        document.getElementById('connStatus').style.color = '#f1c40f';
      } else {
        btns.forEach(b => {
          b.disabled = false;
          let prog = b.querySelector('.progress');
          if(prog) { prog.style.transition = 'none'; prog.style.width = '0%'; }
        });
        document.getElementById('connStatus').innerText = 'Connected - Idle';
        document.getElementById('connStatus').style.color = '#4caf50';
      }

      document.getElementById('sysState').innerText = data.sysState;
      document.getElementById('posture').innerText = data.posture;
      document.getElementById('tL').innerText = data.tempL.toFixed(1);
      document.getElementById('tC').innerText = data.tempC.toFixed(1);
      document.getElementById('tR').innerText = data.tempR.toFixed(1);
      document.getElementById('snoreAlert').style.display = (data.snore === 1) ? 'block' : 'none';

      // Dynamically toggle physical error banner container using broadcast flag diagnostics
      if (data.sdReady === false) {
        document.getElementById('sdAlertBanner').style.display = 'flex';
      } else {
        document.getElementById('sdAlertBanner').style.display = 'none';
      }

      if (UI_WORKSPACE_MODE === "raw") {
        document.getElementById('mass').innerText = data.mass;
        
        let rawW_X = 0, rawW_Y = 0, rawSum = 0;
        for(let i=0; i<12; i++) {
          let row = Math.floor(i / 4);
          let col = i % 4;
          let cellVal = data.matrix[i];
          if(cellVal > 0) {
            rawW_X += ((col + 1) * cellVal);
            rawW_Y += ((row + 1) * cellVal);
            rawSum += cellVal;
          }
        }
        if(rawSum > 0) {
          document.getElementById('xPos').innerText = (rawW_X / rawSum).toFixed(2);
          document.getElementById('yPos').innerText = (rawW_Y / rawSum).toFixed(2);
        } else {
          document.getElementById('xPos').innerText = '--';
          document.getElementById('yPos').innerText = '--';
        }

        for(let i = 0; i < 12; i++) {
          let rawVal = data.matrix[i];
          let noiseVal = data.noise[i];
          let cell = document.getElementById('cell-' + i);
          cell.innerText = rawVal;
          
          let colorThreshold = noiseVal + 100;
          let intensity = Math.min(255, Math.floor((rawVal / 4095) * 255));
          cell.style.background = (rawVal > colorThreshold) ? 'rgb(0, ' + (100 + intensity/2) + ', ' + intensity + ')' : '#333';
        }

      } else {
        let noiseSum = data.noise.reduce((a, b) => a + b, 0);
        let calibratedMass = Math.max(0, data.mass - noiseSum);
        document.getElementById('mass').innerText = calibratedMass;
        
        if(data.mass > 0) {
          document.getElementById('xPos').innerText = data.x.toFixed(2);
          document.getElementById('yPos').innerText = data.y.toFixed(2);
        } else {
          document.getElementById('xPos').innerText = '--';
          document.getElementById('yPos').innerText = '--';
        }

        for(let i = 0; i < 12; i++) {
          let calVal = data.matrix[i] - data.noise[i];
          let displayVal = Math.max(0, calVal);
          let cell = document.getElementById('cell-' + i);
          cell.innerText = displayVal;
          
          let intensity = Math.min(255, Math.floor((Math.max(0, calVal) / 4095) * 255));
          cell.style.background = (calVal > 500) ? 'rgb(0, ' + (100 + intensity/2) + ', ' + intensity + ')' : '#333';
        }
      }
    }
  </script>
</body>
</html>
)=====";

// --- LIGHTWEIGHT SANDBOXED CAPTIVE PORTAL INTERFACE FOR QUICK WIFI SETUP ---
const char captive_html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>Smart Pillow - Network Setup</title>
  <style>
    body { background: #121212; color: #ececec; font-family: -apple-system, BlinkMacSystemFont, sans-serif; margin: 0; padding: 20px; text-align: center; }
    h1 { color: #4DA8DA; font-size: 24px; margin-bottom: 20px; font-weight: bold; }
    .card { background: #1e1e1e; border-radius: 10px; padding: 20px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); max-width: 360px; margin: 0 auto; text-align: left; }
    .label { font-size: 11px; text-transform: uppercase; letter-spacing: 1px; color: #aaa; margin-bottom: 6px; margin-top: 16px; display: block; }
    input[type="text"], input[type="password"] { width: 100%; padding: 12px; background: #2a2a2a; border: 1px solid #444; border-radius: 6px; color: #fff; box-sizing: border-box; font-size: 14px; }
    input[type="text"]:focus, input[type="password"]:focus { border-color: #4DA8DA; outline: none; }
    select { width: 100%; padding: 12px; background: #2a2a2a; border: 1px solid #444; border-radius: 6px; color: #fff; box-sizing: border-box; font-size: 14px; }
    select:focus { border-color: #4DA8DA; outline: none; }
    .btn { background: #4DA8DA; color: #fff; border: none; padding: 14px; border-radius: 6px; font-weight: bold; font-size: 14px; cursor: pointer; width: 100%; margin-top: 24px; box-sizing: border-box; transition: background 0.2s; }
    .btn:hover { background: #3a97c9; }
    .status-box { background: #112233; border: 1px solid #4DA8DA; color: #4DA8DA; padding: 12px; border-radius: 6px; font-size: 13px; margin-bottom: 20px; font-weight: bold; text-align: center; }
  </style>
</head>
<body>
  <h1>Smart Pillow</h1>
  <div class="card">
    <div class="status-box" id="statusBox">Network Provisioning Mode</div>
    <form id="provisionForm">
      <label class="label">Network SSID</label>
      <div style="display: flex; gap: 8px; margin-bottom: 4px;">
        <input type="text" id="ssidField" name="ssid" required placeholder="Type or choose network" style="flex: 1;">
        <select id="ssidSelect" style="width: auto; max-width: 130px; margin: 0;" onchange="if(this.value) document.getElementById('ssidField').value = this.value;">
          <option value="">-- Scan --</option>
        </select>
      </div>
      
      <label class="label">WPA2 Password</label>
      <input type="password" id="passwordField" name="password" placeholder="Enter network password">
      
      <button type="submit" class="btn">Commit Configurations</button>
      <button type="button" class="btn" style="background: #333; margin-top: 12px;" onclick="proceedOfflineAP()">Proceed to Telemetry Dashboard</button>
    </form>
  </div>
  <script>
    // --- REQUIREMENT 2: ZERO-FRICTION CLIENT CLOCK AGGREGATION HANDSHAKE ---
    document.addEventListener("DOMContentLoaded", function() {
      var localEpoch = Math.floor(Date.now() / 1000);
      fetch('/api/sync_time', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: localEpoch.toString()
      })
      .then(response => console.log('Time synchronization loop accepted.'))
      .catch(error => console.error('Time handshake error:', error));

      // Trigger asynchronous background scan for the local landing option dropdown
      var dropdown = document.getElementById('ssidSelect');
      fetch('/api/wifi_scan')
        .then(res => res.json())
        .then(networks => {
          dropdown.innerHTML = '<option value="">-- Scanned --</option>';
          networks.forEach(net => {
            let opt = document.createElement('option');
            opt.value = net.ssid;
            opt.innerText = net.ssid + " (" + net.rssi + " dBm)";
            dropdown.appendChild(opt);
          });
        })
        .catch(err => {
          dropdown.innerHTML = '<option value="">Scan Error</option>';
        });
    });

    function proceedOfflineAP() {
      window.location.href = "http://192.168.4.1/";
    }

    document.getElementById('provisionForm').addEventListener('submit', function(event) {
      event.preventDefault();
      var targetSid = document.getElementById('ssidField').value;
      var targetPass = document.getElementById('passwordField').value;
      var infoCard = document.getElementById('statusBox');
      
      infoCard.innerText = "Writing Configurations...";
      infoCard.style.color = "#f1c40f";
      infoCard.style.borderColor = "#f1c40f";
      infoCard.style.background = "#222211";
      
      var argumentsBuffer = new URLSearchParams();
      argumentsBuffer.append('ssid', targetSid);
      argumentsBuffer.append('password', targetPass);

      // --- REQUIREMENT 3: COMPATIBLE STANDARD FORM ENCODED POST REQUEST ---
      fetch('/api/save_wifi', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: argumentsBuffer.toString()
      })
      .then(res => res.json())
      .then(data => {
        if(data.success) {
          // --- CONNECTION LOOP OPTIMIZATION: REWRITTEN FEEDBACK BUFFER SCREEN LAYOUT ---
          infoCard.innerText = "Connecting to your home network. Ensure your phone reconnects to your home Wi-Fi and then navigate to http://smartpillow.local to access your dashboard.";
          infoCard.style.color = "#4caf50";
          infoCard.style.borderColor = "#4caf50";
          infoCard.style.background = "#112211";
        } else {
          infoCard.innerText = "Write Failure: " + data.error;
          infoCard.style.color = "#ff4c4c";
          infoCard.style.borderColor = "#ff4c4c";
          infoCard.style.background = "#221111";
        }
      })
      .catch(err => {
        infoCard.innerText = "Transmission tracking error.";
        infoCard.style.color = "#ff4c4c";
        infoCard.style.borderColor = "#ff4c4c";
        infoCard.style.background = "#221111";
      });
    });
  </script>
</body>
</html>
)=====";

// --- CSV UTILITY PARSER ---
String getValue(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = { 0, -1 };
  int maxIndex = data.length() - 1;
  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

// --- SAVE METADATA TO SD CARD ---
void saveCalibrationMetadata() {
  if (!sdReady) return;
  String timeStr = "";
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeBuffer[25];
    strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    timeStr = String(timeBuffer);
  } else {
    unsigned long upSecs = millis() / 1000;
    timeStr = "Uptime_" + String(upSecs) + "s";
  }

  String line = String(currentCalibrationId) + "," + timeStr;

  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) {
      line += "," + String(noiseThreshold[r][c]);
    }
  }

  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) {
      line += ",";
      if (hasLeftProfile) line += String(profLeft[r][c]);
    }
  }

  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) {
      line += ",";
      if (hasCenterProfile) line += String(profCenter[r][c]);
    }
  }

  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) {
      line += ",";
      if (hasRightProfile) line += String(profRight[r][c]);
    }
  }

  File file = SD.open("/calibration_metadata.csv", FILE_APPEND);
  if (file) {
    file.println(line);
    file.close();
    Serial.println("Saved configurations to snapshot registry metadata ID: " + String(currentCalibrationId));
  } else {
    Serial.println("ERROR: Failed to open metadata for appending");
  }
}

// --- LOAD METADATA FROM SD CARD ---
void loadCalibrationMetadata() {
  if (!SD.exists("/calibration_metadata.csv")) {
    Serial.println("Calibration registry metadata not found. Building defaults...");
    File file = SD.open("/calibration_metadata.csv", FILE_WRITE);
    if (file) {
      String defaultLine = "0,Unknown";
      for (int i = 0; i < 12; i++) defaultLine += ",0";
      for (int i = 0; i < 36; i++) defaultLine += ",";
      file.println(defaultLine);
      file.close();
    }
    currentCalibrationId = 0;
    hasProfiles = false;
    hasLeftProfile = false;
    hasCenterProfile = false;
    hasRightProfile = false;
    return;
  }

  File file = SD.open("/calibration_metadata.csv", FILE_READ);
  if (!file) {
    Serial.println("ERROR: Failed to access calibration register storage logs.");
    return;
  }

  String lastLine = "";
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      lastLine = line;
    }
  }
  file.close();

  if (lastLine.length() > 0) {
    Serial.println("Loading active baseline calibrations: " + lastLine);

    currentCalibrationId = getValue(lastLine, ',', 0).toInt();
    int tokenIdx = 2;

    for (int r = 0; r < NUM_ROWS; r++) {
      for (int c = 0; c < NUM_COLS; c++) {
        String token = getValue(lastLine, ',', tokenIdx++);
        noiseThreshold[r][c] = (token.length() > 0) ? token.toInt() : 0;
      }
    }

    hasLeftProfile = true;
    for (int r = 0; r < NUM_ROWS; r++) {
      for (int c = 0; c < NUM_COLS; c++) {
        String token = getValue(lastLine, ',', tokenIdx++);
        token.trim();
        if (token.length() > 0) {
          profLeft[r][c] = token.toInt();
        } else {
          hasLeftProfile = false;
        }
      }
    }

    hasCenterProfile = true;
    for (int r = 0; r < NUM_ROWS; r++) {
      for (int c = 0; c < NUM_COLS; c++) {
        String token = getValue(lastLine, ',', tokenIdx++);
        token.trim();
        if (token.length() > 0) {
          profCenter[r][c] = token.toInt();
        } else {
          hasCenterProfile = false;
        }
      }
    }

    hasRightProfile = true;
    for (int r = 0; r < NUM_ROWS; r++) {
      for (int c = 0; c < NUM_COLS; c++) {
        String token = getValue(lastLine, ',', tokenIdx++);
        token.trim();
        if (token.length() > 0) {
          profRight[r][c] = token.toInt();
        } else {
          hasRightProfile = false;
        }
      }
    }

    if (hasLeftProfile && hasCenterProfile && hasRightProfile) {
      hasProfiles = true;
    } else {
      hasProfiles = false;
    }
    Serial.printf("Profiles validation flag check status: %s (ID Match: %d)\n", hasProfiles ? "TRUE" : "FALSE", currentCalibrationId);
  }
}

// --- SD CARD HANDLER ---
void initSDCard() {
  if (!SD.begin(pinSD_CS)) {
    Serial.println("CRITICAL ERROR: SD Card Mount Failed. Check wiring.");
    sdReady = false;
    return;
  }

  sdReady = true;
  loadCalibrationMetadata();

  if (!SD.exists("/logs")) {
    SD.mkdir("/logs");
    Serial.println("Created logging subdirectory channel folder: /logs");
  }
}

void logDataToSD(long totalMass) {
  if (!sdReady || currentSessionFile == "") return;

  String timeStr = "";
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeBuffer[25];
    strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    timeStr = String(timeBuffer);
  } else {
    unsigned long upSecs = millis() / 1000;
    timeStr = "Uptime_" + String(upSecs) + "s";
  }

  String logData = timeStr + "," + String(sysState == STATE_SLEEPING ? "Sleeping" : "Empty") + "," + currentPosture + "," + String(totalMass) + "," + String(avgTempLeft, 2) + "," + String(avgTempCenter, 2) + "," + String(avgTempRight, 2) + "," + String(snoreCountThisMinute);

  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) {
      logData += "," + String(pillowMatrix[r][c]);
    }
  }

  logData += "," + String(currentCalibrationId);

  File file = SD.open(currentSessionFile, FILE_APPEND);
  if (file) {
    file.println(logData);
    file.close();
    Serial.println("Logged to Session SD: " + logData);
  } else {
    Serial.println("ERROR: Failed to open active session logs for appending: " + currentSessionFile);
  }
}

// --- HTTP HANDLER 1: LIST SESSIONS METADATA ---
void handleGetSessions() {
  if (!sdReady || !SD.exists("/logs")) {
    server.send(200, "application/json", "[]");
    return;
  }

  File dir = SD.open("/logs");
  if (!dir || !dir.isDirectory()) {
    server.send(500, "application/json", "{\"error\":\"Failed to read logs channel mapping\"}");
    return;
  }

  String json = "[";
  bool firstEntry = true;
  File file = dir.openNextFile();

  while (file) {
    if (!file.isDirectory()) {
      String name = String(file.name());

      if (name.startsWith("/logs/")) {
        name = name.substring(6);
      } else if (name.startsWith("logs/")) {
        name = name.substring(5);
      }

      File f = SD.open("/logs/" + name, FILE_READ);
      if (f) {
        String header = f.readStringUntil('\n');
        String firstLine = f.readStringUntil('\n');
        firstLine.trim();

        String lastLine = "";
        while (f.available()) {
          String activeLine = f.readStringUntil('\n');
          activeLine.trim();
          if (activeLine.length() > 0) {
            lastLine = activeLine;
          }
        }
        f.close();

        String startTime = getValue(firstLine, ',', 0);
        String endTime = (lastLine.length() > 0) ? getValue(lastLine, ',', 0) : startTime;

        if (startTime.length() > 0) {
          if (!firstEntry) json += ",";
          json += "{";
          json += "\"file\":\"" + name + "\",";
          json += "\"start\":\"" + startTime + "\",";
          json += "\"end\":\"" + endTime + "\"";
          json += "}";
          firstEntry = false;
        }
      }
    }
    file = dir.openNextFile();
  }
  dir.close();
  json += "]";

  server.send(200, "application/json", json);
}

// --- HTTP HANDLER 2: MEMORY-OPTIMIZED DATA CHUNK STREAMING ---
void handleGetSessionData() {
  if (!sdReady || !server.hasArg("file")) {
    server.send(400, "application/json", "{\"error\":\"Missing execution parameter target data context\"}");
    return;
  }

  String fileName = server.arg("file");
  fileName.replace("/", "");
  fileName.replace("\\", "");

  String targetPath = "/logs/" + fileName;
  if (!SD.exists(targetPath)) {
    server.send(404, "application/json", "{\"error\":\"Target session data profile not found\"}");
    return;
  }

  File file = SD.open(targetPath, FILE_READ);
  if (!file) {
    server.send(500, "application/json", "{\"error\":\"Failed to pull hardware streaming handler context\"}");
    return;
  }

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[\n");

  String header = file.readStringUntil('\n');
  bool firstRow = true;

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    String timeStr = getValue(line, ',', 0);
    String stateStr = getValue(line, ',', 1);
    String postureStr = getValue(line, ',', 2);
    String massStr = getValue(line, ',', 3);
    String tempLStr = getValue(line, ',', 4);
    String tempCStr = getValue(line, ',', 5);
    String tempRStr = getValue(line, ',', 6);
    String snoreStr = getValue(line, ',', 7);

    String outputChunk = "";
    if (!firstRow) outputChunk += ",\n";

    outputChunk += "{";
    outputChunk += "\"time\":\"" + timeStr + "\",";
    outputChunk += "\"state\":\"" + stateStr + "\",";
    outputChunk += "\"posture\":\"" + postureStr + "\",";
    outputChunk += "\"mass\":" + massStr + ",";
    outputChunk += "\"tempL\":" + tempLStr + ",";
    outputChunk += "\"tempC\":" + tempCStr + ",";
    outputChunk += "\"tempR\":" + tempRStr + ",";
    outputChunk += "\"snore\":" + snoreStr + ",";

    outputChunk += "\"matrix\":[";
    for (int i = 0; i < 12; i++) {
      outputChunk += getValue(line, ',', 8 + i);
      if (i < 11) outputChunk += ",";
    }
    outputChunk += "],";
    outputChunk += "\"calId\":" + getValue(line, ',', 20);
    outputChunk += "}";

    server.sendContent(outputChunk);
    firstRow = false;
  }

  file.close();
  server.sendContent("\n]");
  server.sendContent("");
}

// --- HTTP HANDLER 3: STORAGE REMOUNT DIAGNOSTICS RETRY ---
void handleSdRemountRetry() {
  if (SD.begin(pinSD_CS)) {
    sdReady = true;
    loadCalibrationMetadata();
    if (!SD.exists("/logs")) {
      SD.mkdir("/logs");
    }
    server.send(200, "application/json", "{\"success\":true}");
    Serial.println("Diagnostics Handshake: Storage module remounted successfully via API request.");
  } else {
    sdReady = false;
    server.send(200, "application/json", "{\"success\":false}");
    Serial.println("Diagnostics Handshake Fail: Remount API recovery loop failed to discover mount.");
  }
}

// --- HTTP HANDLER 4: STORAGE TARGET RECORD LOG PURGE ERASE ---
void handlePurgeSessionFile() {
  if (!sdReady || !server.hasArg("file")) {
    server.send(400, "application/json", "{\"error\":\"Missing target path identification resource data attribute.\"}");
    return;
  }

  String fileName = server.arg("file");
  fileName.replace("/", "");
  fileName.replace("\\", "");
  String targetPath = "/logs/" + fileName;

  if (SD.exists(targetPath)) {
    if (SD.remove(targetPath)) {
      server.send(200, "application/json", "{\"success\":true}");
      Serial.println("Storage File Alteration: Purged record from disk -> " + targetPath);
    } else {
      server.send(500, "application/json", "{\"error\":\"Hardware IO block failure during file wipe operation.\"}");
    }
  } else {
    server.send(404, "application/json", "{\"error\":\"Specified log resource path not found on SD registry.\"}");
  }
}

// --- CLOCK ENVELOPE SYNCHRONIZATION ON DEVICE CONNECTION ---
void handleSyncTime() {
  if (server.hasArg("plain")) {
    String bufferBody = server.arg("plain");
    time_t structuralEpoch = (time_t)bufferBody.toInt();

    // Validate bounds constraint before committing to structural clock tracking
    if (structuralEpoch > 1700000000) {
      struct timeval timeEnvelope;
      timeEnvelope.tv_sec = structuralEpoch;
      timeEnvelope.tv_usec = 0;
      settimeofday(&timeEnvelope, NULL);

      Serial.print("Internal System Real-Time Clock Synchronized via client handshake: ");
      Serial.println(structuralEpoch);
      server.send(200, "application/json", "{\"success\":true}");
      return;
    }
  }
  server.send(400, "application/json", "{\"success\":false,\"error\":\"Malformed transaction payload.\"}");
}

// --- COMPATIBLE WI-FI PERSISTENT WRITE ROUTINE ---
void handleSaveWifi() {
  if (!server.hasArg("ssid")) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"SSID field is completely empty.\"}");
    return;
  }

  pendingSsid = server.arg("ssid");
  pendingPassword = server.hasArg("password") ? server.arg("password") : "";

  if (sdReady) {
    String fileContent = "";
    bool updated = false;

    // Read and merge any existing configurations to preserve other separate access keys
    if (SD.exists("/wifi_config.csv")) {
      File file = SD.open("/wifi_config.csv", FILE_READ);
      while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        int commaIdx = line.indexOf(',');
        if (commaIdx >= 0) {
          String s = line.substring(0, commaIdx);
          String p = line.substring(commaIdx + 1);
          s.trim();
          p.trim();
          if (s == pendingSsid) {
            fileContent += pendingSsid + "," + pendingPassword + "\n";
            updated = true;
          } else {
            fileContent += s + "," + p + "\n";
          }
        }
      }
      file.close();
    }

    if (!updated) {
      fileContent += pendingSsid + "," + pendingPassword + "\n";
    }

    File configurationRegisterFile = SD.open("/wifi_config.csv", FILE_WRITE);
    if (configurationRegisterFile) {
      configurationRegisterFile.print(fileContent);
      configurationRegisterFile.close();

      Serial.println("Storage Mutation Successful: Wi-Fi credentials mapped persistently in database.");
      // --- DELAYED TRANSITION STEP 1: IMMEDIATELY RESPOND WITH SUCCESS SCREEN PAYLOAD ---
      server.send(200, "application/json", "{\"success\":true}");

      // Set tracking state flags for asynchronous switch execution inside loop()
      pendingNetworkSwitch = true;
      switchTriggerTime = millis();
    } else {
      server.send(500, "application/json", "{\"success\":false,\"error\":\"Hardware storage open operation failed.\"}");
    }
  } else {
    server.send(500, "application/json", "{\"success\":false,\"error\":\"MicroSD module disconnected or unmounted.\"}");
  }
}

// --- WI-FI BACKGROUND SURVEY ENVIRONMENT TRACKING REST ENDPOINT ---
void handleWifiScan() {
  int discoveredNetworksCount = WiFi.scanNetworks();
  String jsonResponse = "[";

  if (discoveredNetworksCount > 0) {
    for (int i = 0; i < discoveredNetworksCount; ++i) {
      jsonResponse += "{";
      jsonResponse += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
      jsonResponse += "\"rssi\":" + String(WiFi.RSSI(i));
      jsonResponse += "}";
      if (i < discoveredNetworksCount - 1) {
        jsonResponse += ",";
      }
    }
  }
  jsonResponse += "]";

  // Clean structure arrays from background processor buffer to prevent heap leaks
  WiFi.scanDelete();

  server.send(200, "application/json", jsonResponse);
}

// --- WEBSOCKET INCOMING COMMAND HANDLER ---
void startCalibration(CalibState newState) {
  currentCalib = newState;
  calibSampleCount = 0;
  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) { calibAccumulator[r][c] = 0; }
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_TEXT) {
    String msg = (char*)payload;
    if (msg == "CMD:EMPTY") startCalibration(CALIB_EMPTY);
    if (msg == "CMD:LEFT") startCalibration(CALIB_LEFT);
    if (msg == "CMD:CENTER") startCalibration(CALIB_CENTER);
    if (msg == "CMD:RIGHT") startCalibration(CALIB_RIGHT);

    if (msg == "CMD:RESET") {
      for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) {
          noiseThreshold[r][c] = 0;
        }
      }
      hasProfiles = false;
      hasLeftProfile = false;
      hasCenterProfile = false;
      hasRightProfile = false;

      currentCalibrationId++;
      saveCalibrationMetadata();
      Serial.println("System Calibrations Reset to Zero State.");
    }

    if (msg == "CMD:SLEEP") {
      Serial.println("WEB API EXECUTION: Deep Sleep Command Received.");

      // Clean up active logging files if open
      if (sessState == SESS_ACTIVE || sessState == SESS_PAUSED) {
        Serial.println("Closing active session structures cleanly...");
        sessState = SESS_IDLE;
      }

      // Safely unmount filesystem before power-down execution
      if (sdReady) {
        SD.end();
        sdReady = false;
        Serial.println("SD card safely unmounted.");
      }

      Serial.println("ESP32 Entering Deep Sleep now. Goodnight!");
      delay(500);  // Small margin to allow WebSockets broadcast buffers to finish firing

      esp_deep_sleep_start();
    }
  }
}

// --- FUNCTION PROTOTYPES ---
void scanPressureMatrix();
float readTemperature(int pin);
void processCalibrationTick();
void processSleepStateLogic(long totalMass);
void classifyPosture();
void streamTelemetry(long totalMass, float tempL, float tempC, float tempR);

void setup() {
  Serial.begin(115200);

  for (int c = 0; c < NUM_COLS; c++) {
    pinMode(colPins[c], OUTPUT);
    digitalWrite(colPins[c], LOW);
  }

  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) { noiseThreshold[r][c] = 0; }
  }

  pinMode(pinSound, INPUT);

  // Establish standard micro-storage bus configuration early
  initSDCard();

  bool foundKnownNetwork = false;
  if (sdReady) {
    if (!SD.exists("/wifi_config.csv")) {
      Serial.println("Wi-Fi configuration profile registry absent. Initializing empty table layout...");
      File file = SD.open("/wifi_config.csv", FILE_WRITE);
      if (file) { file.close(); }
      AP_MODE = true;
    } else {
      Serial.println("Scanning airwaves to cross-reference multi-network profiles from persistent memory...");
      WiFi.mode(WIFI_STA);
      int n = WiFi.scanNetworks();
      if (n > 0) {
        for (int i = 0; i < n; i++) {
          String scannedSsid = WiFi.SSID(i);
          File file = SD.open("/wifi_config.csv", FILE_READ);
          while (file.available()) {
            String line = file.readStringUntil('\n');
            line.trim();
            int commaIdx = line.indexOf(',');
            if (commaIdx >= 0) {
              String storedSsid = line.substring(0, commaIdx);
              String storedPassword = line.substring(commaIdx + 1);
              storedSsid.trim();
              storedPassword.trim();

              if (scannedSsid == storedSsid) {
                ssid = storedSsid;
                password = storedPassword;
                foundKnownNetwork = true;
                break;
              }
            }
          }
          file.close();
          if (foundKnownNetwork) break;
        }
      }
      WiFi.scanDelete();
      if (!foundKnownNetwork) {
        AP_MODE = true;
      }
    }
  } else {
    Serial.println("SD Interface unavailable. Enforcing standalone default AP backup layer.");
    AP_MODE = true;
  }

  if (AP_MODE) {
    Serial.println("Booting Active Wi-Fi Access Point & Intercept Captive Portal...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_password);
    // Intercept wildcards on standard port 53 and bounce them locally
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    Serial.println("Captive Portal DNS Redirector initialized.");
  } else {
    Serial.println("\nExecuting network linking via active profile parameters...");
    WiFi.mode(WIFI_STA);
    if (password.length() == 0) {
      Serial.println("Establishing binding to unsecured public router interface...");
      WiFi.begin(ssid.c_str());
    } else {
      Serial.println("Establishing binding via encrypted infrastructure rules...");
      WiFi.begin(ssid.c_str(), password.c_str());
    }

    int connectionAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && connectionAttempts < 20) {
      delay(500);
      Serial.print(".");
      connectionAttempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n--- WI-FI CONNECTED ---");
      Serial.print("Station Mode IP Address Mapping assigned: ");
      Serial.println(WiFi.localIP());

      // Bind device interface resolution token to smartpillow.local
      if (MDNS.begin("smartpillow")) {
        Serial.println("mDNS Responder configured successfully. Hostname reference: smartpillow.local");
        MDNS.addService("http", "tcp", 80);
      } else {
        Serial.println("CRITICAL FAULT: mDNS service failure.");
      }
    } else {
      Serial.println("\nStation Mode link timeout. Falling back to internal AP Captive Portal configuration workspace.");
      AP_MODE = true;
      WiFi.mode(WIFI_AP);
      WiFi.softAP(ap_ssid, ap_password);
      Serial.print("Fallback Access Point IP address: ");
      Serial.println(WiFi.softAPIP());
      dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    }
  }

  // Only bind real-time structural clock routines if an upstream gateway router is accessible
  if (!AP_MODE) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  }

  // --- COMPATIBLE CONTEXT-AWARE INDEX DELIVERY ROUTING ---
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", index_html);
  });

  server.on("/wifi", HTTP_GET, []() {
    server.send(200, "text/html", captive_html);
  });

  server.on("/api/sessions", HTTP_GET, handleGetSessions);
  server.on("/api/session_data", HTTP_GET, handleGetSessionData);
  server.on("/api/sd_retry", HTTP_GET, handleSdRemountRetry);
  server.on("/api/delete_session", HTTP_GET, handlePurgeSessionFile);
  server.on("/api/sync_time", HTTP_POST, handleSyncTime);
  server.on("/api/save_wifi", HTTP_POST, handleSaveWifi);
  server.on("/api/wifi_scan", HTTP_GET, handleWifiScan);

  server.on("/chart.js", HTTP_GET, []() {
    if (sdReady && SD.exists("/chart.js")) {
      File file = SD.open("/chart.js", FILE_READ);
      server.streamFile(file, "application/javascript");
      file.close();
      Serial.println("chart.js loaded");
    } else {
      server.send(404, "text/plain", "Chart.js asset not found on SD card storage filesystem.");
    }
  });

  // Structural Sandbox OS redirection hook configuration mapping
  server.onNotFound([]() {
    if (AP_MODE) {
      server.sendHeader("Location", String("http://192.168.4.1/wifi"), true);
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "text/plain", "Not Found");
    }
  });

  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}


void loop() {
  server.handleClient();
  webSocket.loop();

  // Route incoming probe traces if captive portal interface hooks are running
  if (AP_MODE) {
    dnsServer.processNextRequest();
  }

  if (digitalRead(pinSound) == HIGH) {
    snoreDetectedThisWindow = true;
  }

  unsigned long currentTime = millis();

  // --- DELAYED TRANSITION STEP 2: EXECUTE DELAYED CAPTIVE AP PAUSE AND NETWORK PROFILE LOOKUP ---
  if (pendingNetworkSwitch && (currentTime - switchTriggerTime >= 2000)) {
    pendingNetworkSwitch = false;
    dnsServer.stop();
    WiFi.softAPdisconnect(true);

    WiFi.mode(WIFI_STA);
    if (pendingPassword.length() == 0) {
      WiFi.begin(pendingSsid.c_str());
    } else {
      WiFi.begin(pendingSsid.c_str(), pendingPassword.c_str());
    }

    connectionAttemptActive = true;
    connectionStartTime = currentTime;
    Serial.println("Captive Provisioning Engine: Initiating non-blocking background network handoff...");
  }

  // --- ASYNCHRONOUS STATE MACHINE LOOP MONITORING THE 15-SECOND NETWORKS SAFETY NET ---
  if (connectionAttemptActive) {
    if (WiFi.status() == WL_CONNECTED) {
      connectionAttemptActive = false;
      AP_MODE = false;
      ssid = pendingSsid;
      password = pendingPassword;
      Serial.println("\n--- BACKEND HANDOFF SUCCESSFUL: HOME STATION ONLINE ---");
      Serial.print("Assigned IP Target Vector: ");
      Serial.println(WiFi.localIP());

      if (MDNS.begin("smartpillow")) {
        Serial.println("mDNS Responder configuration bound: smartpillow.local");
        MDNS.addService("http", "tcp", 80);
      } else {
        Serial.println("CRITICAL FAULT: mDNS registration error.");
      }
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    }
    // --- FALLBACK RESTORATION: REVERT CONFIGURATIONS UPON TIMEOUT OR REJECTION ---
    else if (currentTime - connectionStartTime >= 15000) {
      connectionAttemptActive = false;
      Serial.println("\nHandoff target timed out. Reverting to Sandboxed AP loop profile (Preserving file table values)...");

      ssid = "";
      password = "";
      AP_MODE = true;

      WiFi.mode(WIFI_AP);
      WiFi.softAP(ap_ssid, ap_password);
      dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
      Serial.println("Sandboxed Access Point restored. Captive configuration loop ready for retry handshake.");
    }
  }

  if (sessState == SESS_ACTIVE) {
    sessionActiveDurationMs += (currentTime - lastActiveTimeMs);
    lastActiveTimeMs = currentTime;

    if (currentTime - lastLogTime >= LOGGING_INTERVAL_MS) {
      lastLogTime = currentTime;
      long totalMass = 0;
      for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) {
          totalMass += pillowMatrix[r][c];
        }
      }
      logDataToSD(totalMass);
      snoreCountThisMinute = 0;  // Clear binary logging flag for the upcoming 30s logging window
    }
  } else {
    lastLogTime = currentTime;
  }

  if (sessState == SESS_PAUSED) {
    if (currentTime - pauseStartTimeMs >= GRACE_PERIOD_MS) {
      Serial.println("20-minute session grace period expired. Formally closing session data file framework.");
      if (sessionActiveDurationMs < MIN_SESSION_DURATION_MS) {
        Serial.printf("Session rejected: Active duration (%lu s) below 15-minute floor. Deleting junk file.\n", sessionActiveDurationMs / 1000);
        if (sdReady && SD.exists(currentSessionFile)) {
          SD.remove(currentSessionFile);
        }
      } else {
        Serial.println("Session data verified. Maintained storage layout path: " + currentSessionFile);
      }
      sessState = SESS_IDLE;
      currentSessionFile = "";
      sessionActiveDurationMs = 0;
    }
  }

  if (currentTime - lastSampleTime >= SAMPLE_RATE_MS) {
    lastSampleTime = currentTime;
    scanPressureMatrix();

    if (currentCalib != CALIB_IDLE) {
      processCalibrationTick();
    } else {
      long totalMass = 0;
      for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) { totalMass += pillowMatrix[r][c]; }
      }

      processSleepStateLogic(totalMass);

      if (sysState == STATE_SLEEPING) classifyPosture();
      else currentPosture = "Bed Empty";

      // THERMISTOR CODES
      tempBufferL[tempIdx] = readTemperature(pinThermLeft);
      tempBufferC[tempIdx] = readTemperature(pinThermCenter);
      tempBufferR[tempIdx] = readTemperature(pinThermRight);

      tempIdx = (tempIdx + 1) % TEMP_BUFFER_SIZE;
      if (tempCount < TEMP_BUFFER_SIZE) tempCount++;

      avgTempLeft = 0.0;
      avgTempCenter = 0.0;
      avgTempRight = 0.0;
      for (int i = 0; i < tempCount; i++) {
        avgTempLeft += tempBufferL[i];
        avgTempCenter += tempBufferC[i];
        avgTempRight += tempBufferR[i];
      }
      avgTempLeft /= tempCount;
      avgTempCenter /= tempCount;
      avgTempRight /= tempCount;

      // SOUND SENSOR 20s RHYTHM LOGIC
      // Store current window into 20s rolling buffer
      soundBuffer[soundIdx] = snoreDetectedThisWindow;
      soundIdx = (soundIdx + 1) % SNORE_BUFFER_SIZE;

      int trueCount = 0;
      int transitions = 0;
      for (int i = 0; i < SNORE_BUFFER_SIZE; i++) {
        if (soundBuffer[i]) trueCount++;
        int prev = (i == 0) ? SNORE_BUFFER_SIZE - 1 : i - 1;
        if (soundBuffer[i] && !soundBuffer[prev]) transitions++;
      }
      // Rhythm Heuristic: 4 to 8 breath bursts in 20s, NOT constantly loud (pure noise)
      isRhythmicSnoring = (transitions >= 4 && transitions <= 8 && trueCount > 16 && trueCount < 60);

      // Latch snoring flag: If rhythmic snoring heuristic evaluates true anytime during this 30s logging slice, flag it as 1
      if (isRhythmicSnoring) {
        snoreCountThisMinute = 1;
      }

      streamTelemetry(totalMass, avgTempLeft, avgTempCenter, avgTempRight);
    }
    snoreDetectedThisWindow = false;
  }
}

// --- HARDWARE LOGIC FUNCTIONS ---

void scanPressureMatrix() {
  for (int c = 0; c < NUM_COLS; c++) {
    digitalWrite(colPins[c], HIGH);
    delayMicroseconds(15);
    for (int r = 0; r < NUM_ROWS; r++) { pillowMatrix[r][c] = analogRead(rowPins[r]); }
    digitalWrite(colPins[c], LOW);
  }
}

void processCalibrationTick() {
  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) { calibAccumulator[r][c] += pillowMatrix[r][c]; }
  }
  calibSampleCount++;

  if (calibSampleCount >= 20) {
    for (int r = 0; r < NUM_ROWS; r++) {
      for (int c = 0; c < NUM_COLS; c++) {
        int avg = calibAccumulator[r][c] / 20;
        if (currentCalib == CALIB_EMPTY) noiseThreshold[r][c] = avg;
        else if (currentCalib == CALIB_LEFT) {
          profLeft[r][c] = avg;
          hasLeftProfile = true;
        } else if (currentCalib == CALIB_CENTER) {
          profCenter[r][c] = avg;
          hasCenterProfile = true;
        } else if (currentCalib == CALIB_RIGHT) {
          profRight[r][c] = avg;
          hasRightProfile = true;
        }
      }
    }
    currentCalibrationId++;
    if (hasLeftProfile && hasCenterProfile && hasRightProfile) {
      hasProfiles = true;
    } else {
      hasProfiles = false;
    }
    saveCalibrationMetadata();
    currentCalib = CALIB_IDLE;
    streamTelemetry(0, 0, 0, 0);
  }
}

void processSleepStateLogic(long totalMass) {
  long dynamicThreshold = 500;
  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) { dynamicThreshold += noiseThreshold[r][c]; }
  }

  SystemState oldState = sysState;

  if (totalMass > dynamicThreshold) {
    lastPressureTime = millis();
    presenceCounter++;
    if (presenceCounter >= 20 && sysState == STATE_EMPTY) {
      sysState = STATE_SLEEPING;
    }
  } else {
    if (sysState == STATE_SLEEPING && (millis() - lastPressureTime >= TIMEOUT_MS)) {
      sysState = STATE_EMPTY;
      presenceCounter = 0;
    } else if (sysState == STATE_EMPTY) {
      presenceCounter = 0;
    }
  }

  if (oldState == STATE_EMPTY && sysState == STATE_SLEEPING) {
    if (sessState == SESS_IDLE) {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        char timeBuffer[35];
        strftime(timeBuffer, sizeof(timeBuffer), "/logs/s_%Y%m%d_%H%M.csv", &timeinfo);
        currentSessionFile = String(timeBuffer);
      } else {
        currentSessionFile = "/logs/s_uptime_" + String(millis() / 1000) + ".csv";
      }

      if (sdReady) {
        File file = SD.open(currentSessionFile, FILE_WRITE);
        if (file) {
          file.println("Time,State,Posture,Mass,TempL,TempC,TempR,SnoreCount,P0,P1,P2,P3,P4,P5,P6,P7,P8,P9,P10,P11,CalibrationID");
          file.close();
          Serial.println("Initialized active file profile target: " + currentSessionFile);
        }
      }
      sessionStartTime = millis();
      sessionActiveDurationMs = 0;
      lastActiveTimeMs = millis();
      sessState = SESS_ACTIVE;
    } else if (sessState == SESS_PAUSED) {
      Serial.println("User returned before grace window expired. Retaining matching structural file logging resource target: " + currentSessionFile);
      lastActiveTimeMs = millis();
      sessState = SESS_ACTIVE;
    }
  } else if (oldState == STATE_SLEEPING && sysState == STATE_EMPTY) {
    if (sessState == SESS_ACTIVE) {
      sessionActiveDurationMs += (millis() - lastActiveTimeMs);
      pauseStartTimeMs = millis();
      sessState = SESS_PAUSED;
      Serial.println("Bedding registered empty. Entering 20-minute structural grace pause window.");
    }
  }
}

void classifyPosture() {
  if (!hasProfiles) {
    currentPosture = "Needs Calibrating";
    return;
  }

  long distL = 0, distC = 0, distR = 0;
  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) {
      int val = pillowMatrix[r][c];
      distL += abs(val - profLeft[r][c]);
      distC += abs(val - profCenter[r][c]);
      distR += abs(val - profRight[r][c]);
    }
  }

  if (distL <= distC && distL <= distR) currentPosture = "Left Side";
  else if (distR <= distC && distR <= distL) currentPosture = "Right Side";
  else currentPosture = "Center Back";
}

void streamTelemetry(long totalMass, float tempL, float tempC, float tempR) {
  long weightedXSum = 0;
  long weightedYSum = 0;

  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) {
      int val = pillowMatrix[r][c];
      if (val > noiseThreshold[r][c]) {
        weightedXSum += ((c + 1) * val);
        weightedYSum += ((r + 1) * val);
      }
    }
  }

  float headX = (totalMass > 0) ? (float)weightedXSum / totalMass : 0.0;
  float headY = (totalMass > 0) ? (float)weightedYSum / totalMass : 0.0;

  String json = "{";
  json += "\"sysState\":\"" + String(sysState == STATE_SLEEPING ? "Sleeping" : "Empty") + "\",";

  String calStr = "IDLE";
  if (currentCalib == CALIB_EMPTY) calStr = "EMPTY";
  else if (currentCalib == CALIB_LEFT) calStr = "LEFT";
  else if (currentCalib == CALIB_CENTER) calStr = "CENTER";
  else if (currentCalib == CALIB_RIGHT) calStr = "RIGHT";
  json += "\"calState\":\"" + calStr + "\",";

  json += "\"posture\":\"" + currentPosture + "\",";
  json += "\"mass\":" + String(totalMass) + ",";
  json += "\"x\":" + String(headX, 2) + ",";
  json += "\"y\":" + String(headY, 2) + ",";
  json += "\"tempL\":" + String(tempL, 1) + ",";
  json += "\"tempC\":" + String(tempC, 1) + ",";
  json += "\"tempR\":" + String(tempR, 1) + ",";
  json += "\"snore\":" + String(isRhythmicSnoring ? 1 : 0) + ",";
  json += "\"sdReady\":" + String(sdReady ? "true" : "false") + ",";

  json += "\"matrix\":[";
  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) {
      json += String(pillowMatrix[r][c]);
      if (r != NUM_ROWS - 1 || c != NUM_COLS - 1) json += ",";
    }
  }
  json += "],";

  json += "\"noise\":[";
  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) {
      json += String(noiseThreshold[r][c]);
      if (r != NUM_ROWS - 1 || c != NUM_COLS - 1) json += ",";
    }
  }
  json += "],";

  json += "\"pLeft\":[";
  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) {
      json += String(profLeft[r][c]);
      if (r != NUM_ROWS - 1 || c != NUM_COLS - 1) json += ",";
    }
  }
  json += "],";

  json += "\"pCenter\":[";
  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) {
      json += String(profCenter[r][c]);
      if (r != NUM_ROWS - 1 || c != NUM_COLS - 1) json += ",";
    }
  }
  json += "],";

  json += "\"pRight\":[";
  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) {
      json += String(profRight[r][c]);
      if (r != NUM_ROWS - 1 || c != NUM_COLS - 1) json += ",";
    }
  }
  json += "]";
  json += "}";

  webSocket.broadcastTXT(json);
}

float readTemperature(int pin) {
  int rawADC = analogRead(pin);
  if (rawADC == 0) return 0.0;
  float resistance = SERIES_RESISTOR * ((4095.0 / (float)rawADC) - 1.0);
  float steinhart = resistance / THERMISTOR_NOMINAL;
  steinhart = log(steinhart);
  steinhart /= B_COEFFICIENT;
  steinhart += 1.0 / (TEMPERATURE_NOMINAL + 273.15);
  steinhart = 1.0 / steinhart;
  steinhart -= 273.15;
  return steinhart;
}