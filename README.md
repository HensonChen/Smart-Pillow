# Smart Pillow 

An intelligent sleep-tracking system that automatically monitors key physiological metrics and delivers actionable health insights. Built using an ESP32, custom sensor arrays, and a responsive web dashboard, the Smart Pillow maps your sleep patterns to help improve sleep quality.

<img width="80%" alt="smart_pillow_poster" src="https://github.com/user-attachments/assets/1d4437ff-99f6-4e47-b464-f8840af70f0c" />

## Functional Prototype

<img width="45%" alt="final_implementation" src="https://github.com/user-attachments/assets/4d71b90c-83c6-4c33-b084-15cf6e45c254" />
<img width="45%" alt="enclosure" src="https://github.com/user-attachments/assets/2324d91d-f6c4-4842-b013-180ec3686c65" />

<img width="45%" alt="dashboard" src="https://github.com/user-attachments/assets/ba266052-f1f4-423e-8890-032be75f2d84" />
<img width="45%" alt="sleep history" src="https://github.com/user-attachments/assets/55b239ab-f267-4ec3-a1a6-bf5b6b655bda" />

<br/>Left: The Live Dashboard interface showing pressure maps, temperature tracking, and live system state values.

Right: Historical analysis window rendering sleep timeline charts, efficiency scores, and posture shift counters.

---

## Key Features

*  **Comprehensive Analytics Dashboard:** Visualizes sleep data using interactive timelines, pressure heatmaps, and automated insights such as sleep efficiency percentages and tossing frequency.

*  **Real-Time Posture Tracking:** Utilizes a custom 3x4 Velostat pressure matrix to detect body posture, calculate your center of mass, and instantly update the dashboard layout.

*  **Multi-Modal Biometrics:** Integrates NTC thermistors and sound sensors to continuously track temperature levels and snoring patterns throughout the night.

*  **Dual Data Architecture:** Streams live sensor metrics via WebSockets for instant visualization while simultaneously saving logs to a local SD card for long-term historical analysis.

---

## Video Demo

Click the image below to watch the full demo:

[<img width="473" height="313" alt="demo link to youtube" src="https://github.com/user-attachments/assets/7a905b44-1cc4-42c2-9e17-e2dda1394ce8" />](https://youtu.be/1KgjinQBqYA)

---

## System Architecture

<img width="90%" alt="system diagram" src="https://github.com/user-attachments/assets/d6fc5aff-b957-42af-b9bf-e6686f083af4" />


### Software Stack

* **Firmware:** C/C++ (ESP32 Environment)

* **Communication Protocol:** WebSockets (Wi-Fi) for ultra-low latency streaming

* **Web Application:** Dashboard featuring live data streaming, time-series sleep history charts, device calibration tools, and Wi-Fi configuration utilities.

### Hardware Components

* **Microcontroller:** ESP32 (Wi-Fi enabled) 


* **Pressure Sensor:** 3x4 Velostat Matrix (Force Sensing Resistor grid) 


* **Temperature Sensor:** NTC Thermistors 


* **Audio Sensor:** Sound Sensor / Microphone (housed inside a custom red enclosure) 


* **Storage Module:** SD Card Module for offline local data logging 


---

## Dashboard & Interface Preview


Figure 1: Calibration Mode to mimimize errors

<img width="40%" alt="image" src="https://github.com/user-attachments/assets/31d65818-116e-44a3-9fc8-2e5a61ff2387" />
<img width="40%" alt="image" src="https://github.com/user-attachments/assets/b8a312e2-959f-443c-8bdb-6b575c8460ec" />

<br/>Figure 2: Live posture, body temperature, and snoring detection

<img width="30%" alt="image" src="https://github.com/user-attachments/assets/d576e858-264b-4101-b596-9716e20996cb" />
<img width="30%"  alt="image" src="https://github.com/user-attachments/assets/a2194a85-7730-47c5-be3a-c78fa0275dff" />
<img width="30%"  alt="image" src="https://github.com/user-attachments/assets/56f418df-06d2-44c5-bac0-e01c263c5fbe" />

<br/>Figure 3: Historical sleep analysis

<img width="40%" alt="image" src="https://github.com/user-attachments/assets/7acf7e60-3d93-45f2-abd9-340764e945df" />
<img width="40%" alt="sleep history" src="https://github.com/user-attachments/assets/55b239ab-f267-4ec3-a1a6-bf5b6b655bda" />

---

## How It Works (Usage Guide)

1. Explore the Live Dashboard 

*  **Action:** Apply pressure with your hand or rest your head anywhere on the pillow surface.


*  **Result:** The web interface instantly populates a dynamic live stream indicating pressure distribution map overlays, thermal data points, and general system states.



2. Test Posture Tracking 

*  **Action:** Alternately shift pressure across different physical zones (Left, Center, or Right).


*  **Result:** The controller processes the spatial weight distribution matrix, computes your relative center of mass, and live-updates your position profile on the UI.



3. Simulate a Snore 

*  **Action:** Produce a rhythmic, repetitive clicking or snoring audio cue near the microphone element on the red hardware enclosure.


*  **Result:** Once the onboard frequency pattern matches the filter criteria, the web client fires an immediate visual warning alert on screen.



4. Dive into Sleep History Analytics 

*  **Action:** Open the "Sleep History" tab and load any previously recorded `.log` file from the repository.


*  **Result:** The application parses historical records from the local SD card data stream to generate cumulative analytics including heatmaps, sleep efficiency trends, and overall movement charts.

---

## Functionality Details

The Smart Pillow is built for individuals looking to track their sleep patterns and better understand how sleep impacts their overall health. With many people experiencing sleep disturbances like insomnia or sleep apnea, this system automatically monitors key physiological metrics throughout the night, including body posture, temperature, and snoring activity.

### Configuration

Before initial use, the device requires a brief network setup. When first plugging it in, users connect to a local wireless access point named “SmartPillow_Network” using the password “sleepdata.” From there, they can link the pillow to their local Wi-Fi network via a configuration portal. To ensure accurate sleep tracking, users must also calibrate the system on the sleeping dashboard by recording a few baseline states: “empty bed,” “tossing left,” “tossing center,” and "tossing right."

### Live data
The dashboard allows users to monitor live, continuous data streams coming from the pressure, temperature, and sound sensors. Once the initial calibration is complete, users can toggle a calibrated mode that displays adjusted pressure readings, actively filtering out sensor noise and the empty weight of the pillow.

### Sleeping log
The system automatically begins recording a sleep log when a user lies down for longer than five seconds, capturing data continuously until they get out of bed. If the user's head is lifted from the pillow for more than 10 seconds, the device enters a 20-minute grace period. If they return to bed within this window, for instance, after getting up to use the restroom, the system resumes recording on the same log file. Otherwise, logging terminates. During active tracking, the system logs body posture, temperature, and both raw and mass pressure data at 30-second intervals.

### Sleep history
Users can review their sleep history through a comprehensive visual dashboard. This historical data is pulled directly from the sleep logs saved on the local SD card and populated into several dynamic charts. These visualizations include a state and posture timeline, continuous metrics for mass, thermal changes, and posture-linked snoring, a sleep playback heatmap, and automated insights detailing sleep efficiency and the number of times the user tossed and turned.

---

## Implementation Details

### Hardware

The system is driven by an ESP32 microcontroller, which interfaces with three types of sensors: force-sensing resistors (Velostat), NTC thermistors, and a sound sensor. To monitor pressure, we arranged 12 force-sensing resistors in a 3x4 matrix at the bottom of the pillow. Because the ESP32 has limited analog input pins, the system utilizes a matrix scanning technique to capture all 12 pressure data points in real time.

Specifically, the microcontroller scans the entire matrix every 250 milliseconds (4 Hz). During each scan, it sequentially sets each of the four columns to HIGH while simultaneously reading the analog outputs of the three rows. Because the rows and columns are separated by Velostat material at each intersection, the system calculates the resistance at specific points by mapping the active column to the corresponding row. For instance, if column 3 is set to HIGH and row 2 registers a low resistance value, we know pressure is being applied at index (2, 3). This approach effectively allows us to collect live pressure readings across all 12 points simultaneously.

<img width="70%" alt="image" src="https://github.com/user-attachments/assets/34c98217-451a-4c54-9d13-8bea0a5a33b6" />

On the other hand, the NTC thermistors and sound sensors are relatively straightforward, connecting directly to the ESP32 inputs without requiring complex routing logic.

To streamline the hardware setup, I routed the copper wires into a unified “single port.” This port connects all the matrix rows and columns to the enclosed ESP32 using alligator clips. I dedicated significant time to designing and refining this copper wiring layout to minimize signal interference while keeping the connection points organized and reliable.

<img width="70%" alt="image" src="https://github.com/user-attachments/assets/b131da3b-1a23-4935-bf8d-03430aa047c4" />
<img width="70%"  alt="image" src="https://github.com/user-attachments/assets/f209043a-a50b-474c-a909-f95af3dcf4ee" />

#### Circuit Diagram

<img width="70%"  alt="schematic" src="https://github.com/user-attachments/assets/3cde2fc4-6f33-453f-9a58-01ae63dcdf3e" />

### Software

The ESP32 uses WebSockets to stream live pressure matrix, body temperature, and snoring data directly to the client-side web interface. During the “empty bed” calibration, the system calculates a five-second average of the ambient sensor noise and the pillow’s base weight for each matrix index. It then subtracts this baseline from incoming pressure readings to produce an accurate, calibrated pressure value.

To determine body posture, users calibrate standard pressure profiles by lying on the pillow in left, center, and right positions. The software then calculates the Manhattan distance between the current pressure readings at each index and these three calibrated standards. The posture with the minimum aggregate distance is classified as the user’s current sleep position.

Rhythmic snoring detection relies on a specific heuristic pattern. To trigger the “snoring” state within a 20-second rolling window, the sound sensor’s digital output must toggle between HIGH and LOW more than four times but fewer than eight times, with the total duration of these triggers lasting between 4 and 15 seconds.

I also designed the software to scan and connect to the local Wi-Fi network automatically. This ensures users only need to connect directly to the ESP32’s local network during the initial setup, creating a much more seamless nightly experience.
Another critical component is the SD card I/O. I implemented robust error-handling methods to detect if the SD card becomes unreadable, prompting users to easily remount it without disrupting the system.

For the sleep history dashboard, I utilized the open-source Chart.js library for data visualization. To ensure the dashboard remains fully functional offline, the chart.js file is stored directly on the SD card. It must be placed in the root directory prior to the first initialization.
Below is the directory structure of the SD card, along with the data structure for the stored .csv sleep logs and calibration metadata:

#### SD card directory structure

##### Initial

```
SD Card (Root)
│
└── chart.js                  # JavaScript library used for chart data visualization
```

##### After first run

```
SD Card (Root)
│
├── chart.js                  # JavaScript library used for chart data visualization
│
├── calibration_metadata.csv  # Calibrated reference standards
│
├── wifi_config.csv           # Wi-Fi credentials (SSID/Password), if configured
│
└── logs/                     # Directory for system data logging
    │
    ├── s_[date]_[time].csv   # Sleeping data log (when synced with internet time)
    │
    └── s_uptime_[num].csv    # Fallback log (when internet/standard time unavailable)
```

#### CSV data structure

**Sleep log: `s_[date]_[time].csv or s_uptime_[num].csv`**

##### **Header Row**

```Time,State,Posture,Mass,TempL,TempC,TempR,SnoreCount,P0,P1,P2,P3,P4,P5,P6,P7,P8,P9,P10,P11,CalibrationID```

##### **Field Descriptions**

* **Time:** The timestamp of the log entry. It uses real-world time (YYYY-MM-DD HH:MM:SS) if connected to Wi-Fi/NTP, or falls back to system uptime (e.g., Uptime\_120s) if offline.  
* **State:** The current sleeping state of the user, recorded as either Sleeping or Empty.  
* **Posture:** The classified sleeping position determined by the system (Left Side, Right Side, Center Back, Bed Empty, or Needs Calibrating).  
* **Mass:** The total pressure force value. In Raw Mode, this represents the sum of all raw sensor values. In Calibrated Mode, it displays the net value (Raw Sum – Noise Sum).  
* **TempL / TempC / TempR:** The 5-second moving average temperatures (in °C) captured from the left, center, and right thermistors respectively.  
* **SnoreCount:** The running tally of detected snoring events accumulated within the current logging minute.  
* **P0 to P11:** The 12 individual raw pressure sensor readings mapped across the 3\*4 physical grid layout of the pillow.  
* **CalibrationID:** The specific numerical profile identifier used to evaluate the metrics of this log entry, tying it directly to a unique row in the calibration metadata registry.

**Calibration: `calibration_metadata.csv`**

##### **Header Structure (Implicit Data Format)**

```calibration_id,start_time,noise_threshold(12 fields),left_calibration(12 fields),center_calibration(12 fields),right_calibration(12 fields)```

(Note: This file does not write a text header line to the SD card; it saves snapshots directly as comma-separated values matching this exact column sequence).

##### **Field Descriptions**

* **calibration\_id:** A unique, auto-incrementing integer key used to version and track configuration changes every time a calibration step or a full system reset occurs.  
* **start\_time:** The timestamp indicating exactly when that specific configuration snapshot was written to the SD card.  
* **noise\_threshold (Next 12 fields):** The sequential baseline noise values recorded for each cell in the 3\*4 grid during an empty bed calibration. Defaults to 0 if empty.  
* **left\_calibration (Next 12 fields):** The benchmark target pressure values recorded for each cell in the matrix while calibrating the user's side-sleeping position on the left.  
* **center\_calibration (Next 12 fields):** The benchmark target pressure values recorded for each cell in the matrix while calibrating the user's back-sleeping position in the center.  
* **right\_calibration (Final 12 fields):** The benchmark target pressure values recorded for each cell in the matrix while calibrating the user's side-sleeping position on the right.
