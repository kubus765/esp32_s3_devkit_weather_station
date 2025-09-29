#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <time.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// Primary WiFi credentials
const char* ssid = "UPC6628674";
const char* password = "Ar6jxnrurxhe";

// Secondary WiFi credentials (to be set later)
const char* ssid2 = "5GTowerTest";
const char* password2 = "stopcham";

// NTP Configuration
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;     // GMT+1 for Central Europe (adjust for your timezone)
const int daylightOffset_sec = 3600; // Daylight saving time offset

// Create BME280 object
Adafruit_BME280 bme;

// Create web server on port 80
WebServer server(80);

// Sensor readings
float temperature = 0.0;
float pressure = 0.0;
float humidity = 0.0;

// Data logging system
struct SensorData {
    unsigned long timestamp;
    float temperature;
    float pressure;
    float humidity;
};

const int MAX_DATA_POINTS = 4320; // Store 4320 readings (30 days at 10-minute intervals)
SensorData dataBuffer[MAX_DATA_POINTS];
int dataIndex = 0;
int dataCount = 0;
unsigned long lastDataLog = 0;
unsigned long firstDataLog = 0; // Track first data point timestamp for runtime calculation
const unsigned long DATA_LOG_INTERVAL = 600000/20; // Log every 10 minutes

// Persistent storage configuration
const char* DATA_FILE = "/sensor_data.json";
const char* CONFIG_FILE = "/config.json";
const int SAVE_BATCH_SIZE = 10; // Save to flash every 10 new data points
int unsavedDataCount = 0;

// Server-Sent Events
struct SSEClient {
    WiFiClient client;
    unsigned long lastPing;
    bool active;
};
const int MAX_SSE_CLIENTS = 5;
SSEClient sseClients[MAX_SSE_CLIENTS];
int sseClientCount = 0;

// HTML webpage with charts
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>BME280 Weather Station</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        body { 
            font-family: Arial, sans-serif; 
            margin: 0; 
            padding: 10px; 
            background-color: #f0f0f0; 
        }
        .container { 
            max-width: 1200px; 
            margin: 0 auto; 
            padding: 0 10px;
        }
        .dashboard { 
            display: grid; 
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); 
            gap: 15px; 
            margin-bottom: 20px; 
        }
        .card { 
            background-color: white; 
            padding: 15px; 
            border-radius: 10px; 
            box-shadow: 0 2px 10px rgba(0,0,0,0.1); 
        }
        h1 { 
            color: #333; 
            text-align: center; 
            margin-bottom: 20px; 
            font-size: 2em;
        }
        .current-time {
            text-align: center;
            color: #666;
            margin-bottom: 20px;
            font-size: 16px;
            background: white;
            padding: 10px;
            border-radius: 8px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        
        /* Mobile responsiveness */
        @media (max-width: 768px) {
            body { padding: 5px; }
            .container { padding: 0 5px; }
            h1 { font-size: 1.8em; margin-bottom: 15px; }
            .dashboard { 
                grid-template-columns: 1fr; 
                gap: 10px; 
                margin-bottom: 15px; 
            }
            .card { padding: 12px; }
            .reading { padding: 12px; margin: 8px 0; }
            .value { font-size: 20px; }
            .chart-container { height: 250px; }
            .nav-buttons { 
                margin: 15px 0;
                display: flex;
                justify-content: center;
                flex-wrap: wrap;
                gap: 8px;
            }
            .btn { 
                padding: 12px 16px; 
                margin: 4px; 
                font-size: 14px; 
                display: inline-block;
                min-width: 100px;
                text-align: center;
            }
        }
        
        @media (max-width: 480px) {
            h1 { font-size: 1.5em; }
            .stats { grid-template-columns: repeat(2, 1fr); gap: 10px; }
            .value { font-size: 18px; }
            .chart-container { height: 200px; }
            .nav-buttons {
                flex-direction: column;
                align-items: center;
                gap: 8px;
            }
            .btn { 
                display: block; 
                margin: 3px auto; 
                width: 85%;
                max-width: 200px;
                padding: 12px 16px;
                font-size: 14px;
            }
        }
        .reading { 
            background-color: #f9f9f9; 
            padding: 15px; 
            margin: 10px 0; 
            border-radius: 8px; 
            border-left: 5px solid #007bff; 
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        .reading.temperature { border-left-color: #dc3545; }
        .reading.pressure { border-left-color: #28a745; }
        .reading.altitude { border-left-color: #ffc107; }
        .reading.humidity { border-left-color: #17a2b8; }
        .value { 
            font-size: 24px; 
            font-weight: bold; 
            color: #007bff; 
        }
        .unit { 
            font-size: 14px; 
            color: #666; 
        }
        .label { 
            font-size: 16px; 
            color: #333; 
            margin-bottom: 5px; 
            font-weight: bold;
        }
        .chart-container { 
            position: relative; 
            height: 300px; 
            margin-top: 20px; 
        }
        .info { 
            text-align: center; 
            color: #666; 
            margin-top: 20px; 
            font-size: 13px; 
            line-height: 1.4;
            background: white;
            padding: 15px;
            border-radius: 8px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        .info div {
            margin: 3px 0;
        }
        .nav-buttons {
            text-align: center;
            margin: 20px 0;
        }
        .btn {
            background: #007bff;
            color: white;
            border: none;
            padding: 10px 20px;
            margin: 0 5px;
            border-radius: 5px;
            cursor: pointer;
            font-size: 14px;
        }
        .btn:hover { background: #0056b3; }
        .btn.active { background: #28a745; }
        .stats {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin: 20px 0;
        }
        .stat-item {
            background: #f8f9fa;
            padding: 15px;
            border-radius: 8px;
            text-align: center;
        }
        .stat-value {
            font-size: 20px;
            font-weight: bold;
            color: #007bff;
        }
        .stat-label {
            font-size: 12px;
            color: #666;
            margin-top: 5px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>BME280 Weather Station</h1>
        
        <div class="current-time" id="current-time">
            Loading current time...
        </div>
        
        <div class="dashboard">
            <div class="card">
                <div class="reading temperature">
                    <div class="label">Temperature</div>
                    <span class="value" id="temp">TEMP_VALUE</span>
                    <span class="unit">&deg;C</span>
                </div>
                
                <div class="reading pressure">
                    <div class="label">Pressure</div>
                    <span class="value" id="pressure">PRESSURE_VALUE</span>
                    <span class="unit">hPa</span>
                </div>
                
                <div class="reading humidity">
                    <div class="label">Humidity</div>
                    <span class="value" id="humidity">HUMIDITY_VALUE</span>
                    <span class="unit">%</span>
                </div>
            </div>
            
            <div class="card">
                <h3>Statistics (DATA_POINTS points)</h3>
                <div class="stats">
                    <div class="stat-item">
                        <div class="stat-value" id="temp-avg">--</div>
                        <div class="stat-label">Avg Temp (&deg;C)</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-value" id="temp-min">--</div>
                        <div class="stat-label">Min Temp (&deg;C)</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-value" id="temp-max">--</div>
                        <div class="stat-label">Max Temp (&deg;C)</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-value" id="humid-avg">--</div>
                        <div class="stat-label">Avg Humidity (%)</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-value" id="runtime">--</div>
                        <div class="stat-label">Runtime</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-value" id="data-points">--</div>
                        <div class="stat-label">Data Points</div>
                    </div>
                </div>
            </div>
            
            <div class="card">
                <h3>Memory Usage</h3>
                <div class="stats">
                    <div class="stat-item">
                        <div class="stat-value" id="ram-total">--</div>
                        <div class="stat-label">Total RAM (KB)</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-value" id="ram-used">--</div>
                        <div class="stat-label">Used RAM (%)</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-value" id="ram-free">--</div>
                        <div class="stat-label">Free RAM (KB)</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-value" id="max-points">--</div>
                        <div class="stat-label">Est. Max Points</div>
                    </div>
                </div>
            </div>
        </div>
        
        <div class="nav-buttons">
            <button class="btn active" onclick="showChart('6h')">Last 6 Hours</button>
            <button class="btn" onclick="showChart('24h')">Last 24 Hours</button>
            <button class="btn" onclick="showChart('3d')">Last 3 Days</button>
            <button class="btn" onclick="showChart('7d')">Last 7 Days</button>
            <button class="btn" onclick="showChart('30d')">Last 30 Days</button>
            <button class="btn" onclick="showChart('all')">All Data</button>
        </div>
        
        <div class="card">
            <h3>Temperature & Humidity History</h3>
            <div class="chart-container">
                <canvas id="tempHumidChart"></canvas>
            </div>
        </div>
        
        <div class="card">
            <h3>Pressure History</h3>
            <div class="chart-container">
                <canvas id="pressureChart"></canvas>
            </div>
        </div>
        
        <div class="info">
            <div>ESP32-S3 with BME280 Sensor</div>
            <div>A.J. 70490 | L.P. 72810</div>
            <div style="margin-top: 15px;">
                <button class="btn" onclick="exportData('json')" style="margin: 5px;">Export JSON</button>
                <button class="btn" onclick="exportData('csv')" style="margin: 5px;">Export CSV</button>
            </div>
        </div>
    </div>

    <script>
        let tempHumidChart, pressureChart;
        let currentTimeframe = '6h';
        
        function initCharts() {
            // Temperature & Humidity Chart
            const ctx1 = document.getElementById('tempHumidChart').getContext('2d');
            tempHumidChart = new Chart(ctx1, {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [{
                        label: 'Temperature (°C)',
                        data: [],
                        borderColor: '#dc3545',
                        backgroundColor: 'rgba(220, 53, 69, 0.1)',
                        tension: 0.4,
                        yAxisID: 'y',
                        pointRadius: 0, // Hide points for better performance with large datasets
                        pointHoverRadius: 4
                    }, {
                        label: 'Humidity (%)',
                        data: [],
                        borderColor: '#17a2b8',
                        backgroundColor: 'rgba(23, 162, 184, 0.1)',
                        tension: 0.4,
                        yAxisID: 'y1',
                        pointRadius: 0,
                        pointHoverRadius: 4
                    }]
                },
                options: {
                    responsive: true,
                    maintainAspectRatio: false,
                    interaction: { 
                        intersect: false,
                        mode: 'index'
                    },
                    animation: {
                        duration: 300 // Faster animations for better UX
                    },
                    plugins: {
                        legend: {
                            display: true,
                            position: 'top'
                        },
                        tooltip: {
                            callbacks: {
                                title: function(context) {
                                    // Show full date/time in tooltip
                                    if (context[0] && context[0].parsed) {
                                        const dataIndex = context[0].dataIndex;
                                        const dataset = context[0].chart.data.datasets[0];
                                        if (dataset.timestamps && dataset.timestamps[dataIndex]) {
                                            const date = new Date(dataset.timestamps[dataIndex] * 1000);
                                            return date.toLocaleString();
                                        }
                                    }
                                    return context[0].label;
                                }
                            }
                        }
                    },
                    scales: {
                        x: { 
                            display: true, 
                            title: { display: true, text: 'Time' },
                            ticks: {
                                maxTicksLimit: 10, // Limit number of x-axis labels
                                autoSkip: true
                            }
                        },
                        y: { 
                            type: 'linear', 
                            display: true, 
                            position: 'left', 
                            title: { display: true, text: 'Temperature (°C)' },
                            grid: { color: 'rgba(220, 53, 69, 0.1)' }
                        },
                        y1: { 
                            type: 'linear', 
                            display: true, 
                            position: 'right', 
                            title: { display: true, text: 'Humidity (%)' }, 
                            grid: { drawOnChartArea: false },
                            min: 0,
                            max: 100
                        }
                    }
                }
            });
            
            // Pressure Chart
            const ctx2 = document.getElementById('pressureChart').getContext('2d');
            pressureChart = new Chart(ctx2, {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [{
                        label: 'Pressure (hPa)',
                        data: [],
                        borderColor: '#28a745',
                        backgroundColor: 'rgba(40, 167, 69, 0.1)',
                        tension: 0.4,
                        pointRadius: 0,
                        pointHoverRadius: 4
                    }]
                },
                options: {
                    responsive: true,
                    maintainAspectRatio: false,
                    interaction: { 
                        intersect: false,
                        mode: 'index'
                    },
                    animation: {
                        duration: 300
                    },
                    plugins: {
                        legend: {
                            display: true,
                            position: 'top'
                        },
                        tooltip: {
                            callbacks: {
                                title: function(context) {
                                    if (context[0] && context[0].parsed) {
                                        const dataIndex = context[0].dataIndex;
                                        const dataset = context[0].chart.data.datasets[0];
                                        if (dataset.timestamps && dataset.timestamps[dataIndex]) {
                                            const date = new Date(dataset.timestamps[dataIndex] * 1000);
                                            return date.toLocaleString();
                                        }
                                    }
                                    return context[0].label;
                                }
                            }
                        }
                    },
                    scales: {
                        x: { 
                            display: true, 
                            title: { display: true, text: 'Time' },
                            ticks: {
                                maxTicksLimit: 10,
                                autoSkip: true
                            }
                        },
                        y: { 
                            type: 'linear', 
                            display: true, 
                            position: 'left', 
                            title: { display: true, text: 'Pressure (hPa)' }
                        }
                    }
                }
            });
        }
        
        function showChart(timeframe) {
            currentTimeframe = timeframe;
            document.querySelectorAll('.btn').forEach(btn => btn.classList.remove('active'));
            event.target.classList.add('active');
            loadHistoricalData();
        }
        
        function loadHistoricalData() {
            console.log('Loading historical data for timeframe:', currentTimeframe);
            fetch('/history?range=' + currentTimeframe)
                .then(response => response.json())
                .then(data => {
                    console.log('Received data points:', data.length);
                    console.log('Sample data:', data.slice(0, 3));
                    updateCharts(data);
                    updateStats(data);
                })
                .catch(error => {
                    console.error('Error loading historical data:', error);
                });
        }
        
        function updateCharts(data) {
            console.log('Updating charts with', data.length, 'data points');
            if (data.length === 0) {
                console.log('No data to display');
                return;
            }
            
            // Store timestamps for tooltip display
            const timestamps = data.map(d => d.timestamp);
            
            const labels = data.map(d => {
                const date = new Date(d.timestamp * 1000);
                
                // Format labels based on timeframe and data density
                if (currentTimeframe === '6h' || currentTimeframe === '24h') {
                    // Show time only for short periods
                    return date.toLocaleTimeString([], {hour: '2-digit', minute: '2-digit'});
                } else if (currentTimeframe === '3d') {
                    // Show day and time for 3 days
                    return date.toLocaleDateString([], {month: 'short', day: 'numeric'}) + ' ' + 
                           date.toLocaleTimeString([], {hour: '2-digit', minute: '2-digit'});
                } else {
                    // Show date only for longer periods
                    return date.toLocaleDateString([], {month: 'short', day: 'numeric', hour: '2-digit'});
                }
            });
            
            console.log('Sample labels:', labels.slice(0, 3));
            
            tempHumidChart.data.labels = labels;
            tempHumidChart.data.datasets[0].data = data.map(d => d.temperature);
            tempHumidChart.data.datasets[0].timestamps = timestamps; // Store for tooltips
            tempHumidChart.data.datasets[1].data = data.map(d => d.humidity);
            tempHumidChart.data.datasets[1].timestamps = timestamps;
            tempHumidChart.update('none'); // Skip animation for better performance
            
            pressureChart.data.labels = labels;
            pressureChart.data.datasets[0].data = data.map(d => d.pressure);
            pressureChart.data.datasets[0].timestamps = timestamps;
            pressureChart.update('none'); // Skip animation for better performance
        }
        
        function updateStats(data) {
            if (data.length === 0) return;
            
            const temps = data.map(d => d.temperature);
            const humids = data.map(d => d.humidity);
            
            document.getElementById('temp-avg').textContent = (temps.reduce((a,b) => a+b) / temps.length).toFixed(1);
            document.getElementById('temp-min').textContent = Math.min(...temps).toFixed(1);
            document.getElementById('temp-max').textContent = Math.max(...temps).toFixed(1);
            document.getElementById('humid-avg').textContent = (humids.reduce((a,b) => a+b) / humids.length).toFixed(1);
            
            // Update data range info
            if (data.length > 0) {
                const startDate = new Date(data[0].timestamp * 1000);
                const endDate = new Date(data[data.length - 1].timestamp * 1000);
                const rangeText = `${startDate.toLocaleDateString()} to ${endDate.toLocaleDateString()}`;
                
                // Update chart titles with date range - target the correct chart cards
                const chartCards = document.querySelectorAll('.card');
                const tempHumidCard = chartCards[3]; // 4th card (Temperature & Humidity History)
                const pressureCard = chartCards[4]; // 5th card (Pressure History)
                
                if (tempHumidCard && tempHumidCard.querySelector('h3')) {
                    tempHumidCard.querySelector('h3').textContent = `Temperature & Humidity History (${rangeText})`;
                }
                if (pressureCard && pressureCard.querySelector('h3')) {
                    pressureCard.querySelector('h3').textContent = `Pressure History (${rangeText})`;
                }
            }
        }
        
        function updateCurrentReadings() {
            fetch('/data')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('temp').textContent = data.temperature.toFixed(1);
                    document.getElementById('pressure').textContent = data.pressure.toFixed(1);
                    document.getElementById('humidity').textContent = data.humidity.toFixed(1);
                    if (data.currentTime) {
                        document.getElementById('current-time').textContent = 'Current Time: ' + data.currentTime;
                    }
                    if (data.runtime) {
                        document.getElementById('runtime').textContent = data.runtime;
                    }
                    if (data.dataPoints !== undefined) {
                        document.getElementById('data-points').textContent = data.dataPoints;
                    }
                })
                .catch(error => {
                    console.error('Error updating readings:', error);
                });
        }
        
        function updateMemoryInfo() {
            fetch('/memory')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('ram-total').textContent = (data.totalRAM / 1024).toFixed(0);
                    document.getElementById('ram-used').textContent = data.memoryUsagePercent.toFixed(1) + '%';
                    document.getElementById('ram-free').textContent = (data.freeRAM / 1024).toFixed(0);
                    document.getElementById('max-points').textContent = data.estimatedMaxPossible;
                    
                    console.log('Memory info updated:', data);
                })
                .catch(error => {
                    console.error('Error updating memory info:', error);
                });
        }
        
        // Server-Sent Events for real-time updates
        let eventSource;
        let reconnectInterval;
        
        function connectSSE() {
            if (eventSource) {
                eventSource.close();
            }
            
            eventSource = new EventSource('/events');
            
            eventSource.onopen = function(event) {
                console.log('SSE connection opened');
                if (reconnectInterval) {
                    clearInterval(reconnectInterval);
                    reconnectInterval = null;
                }
            };
            
            eventSource.onmessage = function(event) {
                console.log('Received SSE data:', event.data);
                try {
                    const data = JSON.parse(event.data);
                    if (data.type === 'newData') {
                        // Update current readings immediately
                        updateCurrentReadings();
                        // Refresh charts with new data
                        loadHistoricalData();
                        // Update memory info
                        updateMemoryInfo();
                        console.log('Real-time update triggered');
                    }
                } catch (e) {
                    console.error('Error parsing SSE data:', e);
                }
            };
            
            eventSource.onerror = function(event) {
                console.log('SSE connection error, attempting reconnection...');
                eventSource.close();
                if (!reconnectInterval) {
                    reconnectInterval = setInterval(connectSSE, 5000); // Reconnect every 5 seconds
                }
            };
        }
        
        // Initialize everything
        document.addEventListener('DOMContentLoaded', function() {
            initCharts();
            loadHistoricalData();
            updateCurrentReadings();
            updateMemoryInfo();
            
            // Connect to Server-Sent Events for real-time updates
            connectSSE();
            
            // Keep existing intervals as fallback
            // Update current readings every 30 seconds
            setInterval(updateCurrentReadings, 30000);
            // Update charts every 2 minutes
            setInterval(loadHistoricalData, 120000);
            // Update memory info every minute
            setInterval(updateMemoryInfo, 60000);
        });
        
        // Export data function
        function exportData(format) {
            const url = '/export?format=' + format;
            const filename = 'weather_data_' + new Date().toISOString().slice(0,10) + '.' + format;
            
            // Create a temporary link element and trigger download
            const link = document.createElement('a');
            link.href = url;
            link.download = filename;
            document.body.appendChild(link);
            link.click();
            document.body.removeChild(link);
            
            console.log('Exporting data in ' + format.toUpperCase() + ' format');
        }
        
        // Clean up on page unload
        window.addEventListener('beforeunload', function() {
            if (eventSource) {
                eventSource.close();
            }
        });
    </script>
</body>
</html>
)rawliteral";

// put function declarations here:
bool initializeStorage();
bool saveDataToFlash();
bool loadDataFromFlash();
void handleExport();
bool connectToWiFi();
unsigned long getCurrentTimestamp();
String getMemoryInfo();
void readSensorData();
void logSensorData();
void handleRoot();
void handleData();
void handleHistory();
void handleMemory();
void handleNotFound();
void handleEvents();
void broadcastSSE(String message);
void cleanupSSEClients();

void setup() {
    // put your setup code here, to run once:
    Serial.begin(115200);
    delay(2000); // Give time for serial monitor to connect
    
    Serial.println("Starting ESP32-S3 Weather Station...");

    // Initialize I2C with ESP32-S3 available pins
    Wire.begin(21, 20); // SDA = GPIO21, SCL = GPIO20
    Serial.println("I2C initialized on SDA=21, SCL=20");

    // Scan for I2C devices
    Serial.println("Scanning for I2C devices...");
    byte error, address;
    int nDevices = 0;
    for(address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        if (error == 0) {
            Serial.print("I2C device found at address 0x");
            if (address < 16) Serial.print("0");
            Serial.println(address, HEX);
            nDevices++;
        }
    }
    if (nDevices == 0) {
        Serial.println("No I2C devices found!");
    } else {
        Serial.print("Found ");
        Serial.print(nDevices);
        Serial.println(" I2C device(s)");
    }

    // Initialize BME280 sensor
    Serial.println("Initializing BME280 sensor...");
    
    if (!bme.begin(0x76)) {
        Serial.println("BME280 not found at 0x76, trying 0x77...");
        if (!bme.begin(0x77)) {
            Serial.println("Could not find a valid BME280 sensor, check wiring!");
            Serial.println("Make sure:");
            Serial.println("- VCC -> 3.3V");
            Serial.println("- GND -> GND");
            Serial.println("- SDA -> GPIO 21");
            Serial.println("- SCL -> GPIO 20");
            while (1);
        } else {
            Serial.println("BME280 found at address 0x77!");
        }
    } else {
        Serial.println("BME280 found at address 0x76!");
    }

    // Configure BME280 settings
    bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                    Adafruit_BME280::SAMPLING_X2,
                    Adafruit_BME280::SAMPLING_X16,
                    Adafruit_BME280::SAMPLING_X16,
                    Adafruit_BME280::FILTER_X16,
                    Adafruit_BME280::STANDBY_MS_500);

    Serial.println("BME280 sensor initialized successfully!");

    // Initialize LittleFS for persistent storage
    if (!initializeStorage()) {
        Serial.println("Failed to initialize storage system!");
        Serial.println("Continuing without persistent storage...");
    }

    // Connect to WiFi with retry logic
    if (!connectToWiFi()) {
        Serial.println("Failed to connect to any WiFi network!");
        Serial.println("Please check your WiFi credentials and network availability.");
        // Continue anyway for development/testing
    }

    // Initialize SSE client array
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        sseClients[i].active = false;
        sseClients[i].lastPing = 0;
    }
    sseClientCount = 0;
    Serial.println("SSE client array initialized");

    // Initialize NTP time
    Serial.println("Setting up NTP time...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    // Wait for time to be set
    struct tm timeinfo;
    int attempts = 0;
    while (!getLocalTime(&timeinfo) && attempts < 10) {
        Serial.println("Failed to obtain time, retrying...");
        delay(1000);
        attempts++;
    }
    if (attempts < 10) {
        Serial.println("Time synchronized successfully!");
        Serial.println(&timeinfo, "Current time: %A, %B %d %Y %H:%M:%S");
    } else {
        Serial.println("Failed to sync time, using millis() fallback");
    }

    // Set up web server routes
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.on("/history", handleHistory);
    server.on("/memory", handleMemory);
    server.on("/events", handleEvents);
    server.on("/export", handleExport);
    server.onNotFound(handleNotFound);

    // Start server
    server.begin();
    Serial.println("HTTP server started");
    Serial.println("Access the weather station at: http://" + WiFi.localIP().toString());
    
    // Take initial sensor reading and log data immediately for testing
    Serial.println("Taking initial sensor reading...");
    readSensorData();
    logSensorData();
    
    // Set up for next data log
    lastDataLog = millis();
}

void loop() {
    // put your main code here, to run repeatedly:
    server.handleClient();

    // Read sensor data every 5 seconds
    static unsigned long lastReading = 0;
    if (millis() - lastReading > 5000) {
        readSensorData();
        lastReading = millis();
    }
    
    // Log data every 2 minutes
    if (millis() - lastDataLog > DATA_LOG_INTERVAL) {
        logSensorData();
        lastDataLog = millis();
        
        // Broadcast new data event to all connected SSE clients
        broadcastSSE("{\"type\":\"newData\",\"timestamp\":" + String(getCurrentTimestamp()) + "}");
    }
    
    // Clean up disconnected SSE clients every 30 seconds
    static unsigned long lastSSECleanup = 0;
    if (millis() - lastSSECleanup > 30000) {
        cleanupSSEClients();
        lastSSECleanup = millis();
    }
}

// put function definitions here:
unsigned long getCurrentTimestamp() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        // Convert to Unix timestamp
        return mktime(&timeinfo);
    } else {
        // Fallback to millis() if NTP time is not available
        return millis() / 1000;
    }
}

String getMemoryInfo() {
    // Get memory information
    size_t totalRAM = ESP.getHeapSize();
    size_t freeRAM = ESP.getFreeHeap();
    size_t usedRAM = totalRAM - freeRAM;
    
    // Calculate data buffer usage
    size_t bufferSize = sizeof(dataBuffer);
    size_t singleDataSize = sizeof(SensorData);
    
    // Calculate theoretical maximum data points that could fit in available RAM
    // (This is rough estimation - actual usable memory is less due to other program needs)
    size_t availableForData = freeRAM / 2; // Use only half of free RAM for safety
    size_t maxPossiblePoints = availableForData / singleDataSize;
    
    String info = "{";
    info += "\"totalRAM\":" + String(totalRAM) + ",";
    info += "\"usedRAM\":" + String(usedRAM) + ",";
    info += "\"freeRAM\":" + String(freeRAM) + ",";
    info += "\"bufferSize\":" + String(bufferSize) + ",";
    info += "\"singleDataSize\":" + String(singleDataSize) + ",";
    info += "\"currentDataPoints\":" + String(dataCount) + ",";
    info += "\"maxDataPoints\":" + String(MAX_DATA_POINTS) + ",";
    info += "\"estimatedMaxPossible\":" + String(maxPossiblePoints) + ",";
    info += "\"memoryUsagePercent\":" + String((float)usedRAM * 100.0 / totalRAM, 1);
    info += "}";
    
    return info;
}

void readSensorData() {
    temperature = bme.readTemperature();
    pressure = bme.readPressure() / 100.0; // Convert Pa to hPa
    humidity = bme.readHumidity();

    Serial.println("=== Sensor Readings ===");
    Serial.println("Temperature: " + String(temperature) + " °C");
    Serial.println("Pressure: " + String(pressure) + " hPa");
    Serial.println("Humidity: " + String(humidity) + " %");
    Serial.println();
}

void logSensorData() {
    unsigned long timestamp = getCurrentTimestamp();
    
    // Track the first data point for runtime calculation
    if (firstDataLog == 0) {
        firstDataLog = timestamp;
    }
    
    dataBuffer[dataIndex].timestamp = timestamp;
    dataBuffer[dataIndex].temperature = temperature;
    dataBuffer[dataIndex].pressure = pressure;
    dataBuffer[dataIndex].humidity = humidity;
    
    // Convert timestamp to readable format for debugging
    time_t rawtime = timestamp;
    struct tm * timeinfo = localtime(&rawtime);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
    
    Serial.println("=== Data Logged ===");
    Serial.println("Timestamp: " + String(timestamp) + " (" + String(timeStr) + ")");
    Serial.println("Index: " + String(dataIndex));
    Serial.println("Temperature: " + String(temperature, 2) + "°C");
    Serial.println("Pressure: " + String(pressure, 2) + "hPa");
    Serial.println("Humidity: " + String(humidity, 2) + "%");
    
    dataIndex = (dataIndex + 1) % MAX_DATA_POINTS;
    if (dataCount < MAX_DATA_POINTS) {
        dataCount++;
    }
    
    Serial.println("Total data points: " + String(dataCount));
    Serial.println("Next index: " + String(dataIndex));
    
    // Increment unsaved data counter
    unsavedDataCount++;
    
    // Save to flash storage periodically (batch saves for efficiency)
    if (unsavedDataCount >= SAVE_BATCH_SIZE) {
        Serial.println("Saving data batch to flash storage...");
        if (saveDataToFlash()) {
            Serial.println("Data batch saved to flash successfully!");
            unsavedDataCount = 0;
        } else {
            Serial.println("Failed to save data batch to flash!");
        }
    }
    
    // Memory information
    size_t freeRAM = ESP.getFreeHeap();
    size_t totalRAM = ESP.getHeapSize();
    Serial.println("Free RAM: " + String(freeRAM) + " bytes (" + String(freeRAM/1024) + " KB)");
    Serial.println("RAM usage: " + String((float)(totalRAM - freeRAM) * 100.0 / totalRAM, 1) + "%");
    Serial.println("========================");
}

void handleRoot() {
    String html = htmlPage;
    html.replace("TEMP_VALUE", String(temperature, 1));
    html.replace("PRESSURE_VALUE", String(pressure, 1));
    html.replace("HUMIDITY_VALUE", String(humidity, 1));
    server.send(200, "text/html", html);
}

void handleData() {
    struct tm timeinfo;
    String currentTime = "Time not available";
    if (getLocalTime(&timeinfo)) {
        char timeStr[64];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        currentTime = String(timeStr);
    }
    
    // Calculate runtime
    unsigned long runtimeSeconds = 0;
    String runtimeString = "Not available";
    if (firstDataLog > 0) {
        runtimeSeconds = getCurrentTimestamp() - firstDataLog;
        unsigned long days = runtimeSeconds / 86400;
        unsigned long hours = (runtimeSeconds % 86400) / 3600;
        unsigned long minutes = (runtimeSeconds % 3600) / 60;
        
        if (days > 0) {
            runtimeString = String(days) + "d " + String(hours) + "h " + String(minutes) + "m";
        } else if (hours > 0) {
            runtimeString = String(hours) + "h " + String(minutes) + "m";
        } else {
            runtimeString = String(minutes) + "m " + String(runtimeSeconds % 60) + "s";
        }
    }
    
    String json = "{";
    json += "\"temperature\":" + String(temperature, 1) + ",";
    json += "\"pressure\":" + String(pressure, 1) + ",";
    json += "\"humidity\":" + String(humidity, 1) + ",";
    json += "\"timestamp\":" + String(getCurrentTimestamp()) + ",";
    json += "\"currentTime\":\"" + currentTime + "\",";
    json += "\"runtimeSeconds\":" + String(runtimeSeconds) + ",";
    json += "\"runtime\":\"" + runtimeString + "\",";
    json += "\"dataPoints\":" + String(dataCount);
    json += "}";
    server.send(200, "application/json", json);
}

void handleHistory() {
    String range = server.arg("range");
    int pointsToReturn = dataCount;
    
    Serial.println("=== History Request ===");
    Serial.println("Range: " + range);
    Serial.println("Total data count: " + String(dataCount));
    Serial.println("Data index: " + String(dataIndex));
    
    // Filter data based on requested timeframe (10-minute intervals)
    if (range == "6h") {
        pointsToReturn = min(dataCount, 36); // 6 hours = 36 points (10-min intervals)
    } else if (range == "24h") {
        pointsToReturn = min(dataCount, 144); // 24 hours = 144 points
    } else if (range == "3d") {
        pointsToReturn = min(dataCount, 432); // 3 days = 432 points
    } else if (range == "7d") {
        pointsToReturn = min(dataCount, 1008); // 7 days = 1008 points
    } else if (range == "30d") {
        pointsToReturn = min(dataCount, 4320); // 30 days = 4320 points (all data)
    }
    // "all" returns all available data
    
    Serial.println("Points to return: " + String(pointsToReturn));
    
    String json = "[";
    
    if (dataCount > 0) {
        int startIndex;
        if (dataCount < MAX_DATA_POINTS) {
            // Buffer not full yet, start from beginning
            startIndex = max(0, dataCount - pointsToReturn);
        } else {
            // Buffer is full, calculate circular buffer start
            startIndex = (dataIndex - pointsToReturn + MAX_DATA_POINTS) % MAX_DATA_POINTS;
        }
        
        Serial.println("Start index: " + String(startIndex));
        
        for (int i = 0; i < pointsToReturn; i++) {
            int index = (startIndex + i) % MAX_DATA_POINTS;
            if (i > 0) json += ",";
            json += "{";
            json += "\"timestamp\":" + String(dataBuffer[index].timestamp) + ",";
            json += "\"temperature\":" + String(dataBuffer[index].temperature, 2) + ",";
            json += "\"pressure\":" + String(dataBuffer[index].pressure, 2) + ",";
            json += "\"humidity\":" + String(dataBuffer[index].humidity, 2);
            json += "}";
        }
    }
    
    json += "]";
    Serial.println("JSON length: " + String(json.length()));
    Serial.println("========================");
    
    server.send(200, "application/json", json);
}

void handleMemory() {
    Serial.println("=== Memory Info Request ===");
    String memInfo = getMemoryInfo();
    Serial.println("Memory info: " + memInfo);
    Serial.println("===========================");
    server.send(200, "application/json", memInfo);
}

void handleNotFound() {
    server.send(404, "text/plain", "404: Page not found");
}

void handleEvents() {
    WiFiClient client = server.client();
    
    // Find an available slot for the new SSE client
    int clientIndex = -1;
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (!sseClients[i].active) {
            clientIndex = i;
            break;
        }
    }
    
    if (clientIndex == -1) {
        // No available slots, reject the connection
        server.send(503, "text/plain", "SSE: Too many clients");
        return;
    }
    
    // Send SSE headers manually using the client connection
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/event-stream");
    client.println("Cache-Control: no-cache");
    client.println("Connection: keep-alive");
    client.println("Access-Control-Allow-Origin: *");
    client.println(); // Empty line to end headers
    
    // Store the client
    sseClients[clientIndex].client = client;
    sseClients[clientIndex].lastPing = millis();
    sseClients[clientIndex].active = true;
    sseClientCount++;
    
    Serial.println("New SSE client connected. Total clients: " + String(sseClientCount));
    
    // Send initial connection message
    String initialMessage = "data: {\"type\":\"connected\",\"message\":\"SSE connection established\"}\n\n";
    client.print(initialMessage);
    client.flush(); // Ensure data is sent immediately
}

void broadcastSSE(String message) {
    if (sseClientCount == 0) return;
    
    String sseMessage = "data: " + message + "\n\n";
    
    Serial.println("Broadcasting SSE message to " + String(sseClientCount) + " clients: " + message);
    
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (sseClients[i].active) {
            if (sseClients[i].client.connected()) {
                size_t written = sseClients[i].client.print(sseMessage);
                sseClients[i].client.flush(); // Ensure immediate delivery
                if (written == 0) {
                    // Failed to write, mark as inactive
                    sseClients[i].active = false;
                    sseClients[i].client.stop();
                    sseClientCount--;
                    Serial.println("SSE client " + String(i) + " disconnected (write failed)");
                }
            } else {
                // Client disconnected
                sseClients[i].active = false;
                sseClients[i].client.stop();
                sseClientCount--;
                Serial.println("SSE client " + String(i) + " disconnected");
            }
        }
    }
}

void cleanupSSEClients() {
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (sseClients[i].active) {
            if (!sseClients[i].client.connected()) {
                sseClients[i].active = false;
                sseClients[i].client.stop();
                sseClientCount--;
                Serial.println("Cleaned up disconnected SSE client " + String(i));
            } else {
                // Send keepalive ping every 30 seconds
                if (millis() - sseClients[i].lastPing > 30000) {
                    String pingMessage = "data: {\"type\":\"ping\"}\n\n";
                    size_t written = sseClients[i].client.print(pingMessage);
                    sseClients[i].client.flush(); // Ensure immediate delivery
                    if (written > 0) {
                        sseClients[i].lastPing = millis();
                    } else {
                        // Failed to send ping, client is probably disconnected
                        sseClients[i].active = false;
                        sseClients[i].client.stop();
                        sseClientCount--;
                        Serial.println("SSE client " + String(i) + " removed (ping failed)");
                    }
                }
            }
        }
    }
    
    if (sseClientCount > 0) {
        Serial.println("SSE cleanup complete. Active clients: " + String(sseClientCount));
    }
}

bool initializeStorage() {
    Serial.println("Initializing LittleFS...");
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed!");
        return false;
    }
    
    Serial.println("LittleFS mounted successfully!");
    
    // Print storage info
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    Serial.println("Storage Info:");
    Serial.println("  Total: " + String(totalBytes / 1024) + " KB");
    Serial.println("  Used: " + String(usedBytes / 1024) + " KB");
    Serial.println("  Free: " + String((totalBytes - usedBytes) / 1024) + " KB");
    
    // Try to load existing data
    loadDataFromFlash();
    
    return true;
}

bool saveDataToFlash() {
    File dataFile = LittleFS.open(DATA_FILE, "w");
    if (!dataFile) {
        Serial.println("Failed to open data file for writing!");
        return false;
    }
    
    // Create JSON document
    DynamicJsonDocument doc(32768); // 32KB for JSON document
    JsonArray dataArray = doc.createNestedArray("data");
    
    // Save metadata
    doc["version"] = "1.0";
    doc["totalPoints"] = dataCount;
    doc["maxPoints"] = MAX_DATA_POINTS;
    doc["firstDataLog"] = firstDataLog;
    doc["lastUpdate"] = getCurrentTimestamp();
    
    // Add all data points to JSON
    for (int i = 0; i < dataCount; i++) {
        int index;
        if (dataCount < MAX_DATA_POINTS) {
            index = i; // Linear array, not circular yet
        } else {
            index = (dataIndex + i) % MAX_DATA_POINTS; // Circular buffer
        }
        
        JsonObject dataPoint = dataArray.createNestedObject();
        dataPoint["timestamp"] = dataBuffer[index].timestamp;
        dataPoint["temperature"] = dataBuffer[index].temperature;
        dataPoint["pressure"] = dataBuffer[index].pressure;
        dataPoint["humidity"] = dataBuffer[index].humidity;
    }
    
    // Write JSON to file
    size_t bytesWritten = serializeJson(doc, dataFile);
    dataFile.close();
    
    if (bytesWritten > 0) {
        Serial.println("Data saved: " + String(bytesWritten) + " bytes, " + String(dataCount) + " data points");
        return true;
    } else {
        Serial.println("Failed to write data to file!");
        return false;
    }
}

bool loadDataFromFlash() {
    if (!LittleFS.exists(DATA_FILE)) {
        Serial.println("No existing data file found - starting fresh");
        return true; // This is OK for first run
    }
    
    File dataFile = LittleFS.open(DATA_FILE, "r");
    if (!dataFile) {
        Serial.println("Failed to open data file for reading!");
        return false;
    }
    
    // Read and parse JSON
    DynamicJsonDocument doc(32768); // 32KB for JSON document
    DeserializationError error = deserializeJson(doc, dataFile);
    dataFile.close();
    
    if (error) {
        Serial.println("Failed to parse JSON data file!");
        Serial.println("Error: " + String(error.c_str()));
        return false;
    }
    
    // Load metadata
    if (doc.containsKey("firstDataLog")) {
        firstDataLog = doc["firstDataLog"];
    }
    
    // Load data points
    JsonArray dataArray = doc["data"];
    int loadedPoints = 0;
    
    for (JsonObject dataPoint : dataArray) {
        if (loadedPoints >= MAX_DATA_POINTS) break;
        
        dataBuffer[loadedPoints].timestamp = dataPoint["timestamp"];
        dataBuffer[loadedPoints].temperature = dataPoint["temperature"];
        dataBuffer[loadedPoints].pressure = dataPoint["pressure"];
        dataBuffer[loadedPoints].humidity = dataPoint["humidity"];
        
        loadedPoints++;
    }
    
    dataCount = loadedPoints;
    dataIndex = dataCount % MAX_DATA_POINTS;
    
    Serial.println("Loaded " + String(dataCount) + " data points from flash storage");
    if (dataCount > 0) {
        Serial.println("Data range: " + String(dataBuffer[0].timestamp) + " to " + String(dataBuffer[dataCount-1].timestamp));
    }
    
    return true;
}

void handleExport() {
    Serial.println("Export request received");
    
    String format = server.arg("format");
    if (format == "" || format == "json") {
        // Export as JSON
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "application/json", "");
        
        server.sendContent("{\"data\":[");
        
        for (int i = 0; i < dataCount; i++) {
            int index;
            if (dataCount < MAX_DATA_POINTS) {
                index = i;
            } else {
                index = (dataIndex + i) % MAX_DATA_POINTS;
            }
            
            if (i > 0) server.sendContent(",");
            
            String dataPoint = "{";
            dataPoint += "\"timestamp\":" + String(dataBuffer[index].timestamp) + ",";
            dataPoint += "\"temperature\":" + String(dataBuffer[index].temperature, 2) + ",";
            dataPoint += "\"pressure\":" + String(dataBuffer[index].pressure, 2) + ",";
            dataPoint += "\"humidity\":" + String(dataBuffer[index].humidity, 2);
            dataPoint += "}";
            
            server.sendContent(dataPoint);
        }
        
        String footer = "],\"metadata\":{";
        footer += "\"totalPoints\":" + String(dataCount) + ",";
        footer += "\"exportTime\":" + String(getCurrentTimestamp()) + ",";
        footer += "\"device\":\"ESP32-S3 Weather Station\"";
        footer += "}}";
        
        server.sendContent(footer);
    } else if (format == "csv") {
        // Export as CSV
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "text/csv", "");
        server.sendContent("timestamp,temperature,pressure,humidity\n");
        
        for (int i = 0; i < dataCount; i++) {
            int index;
            if (dataCount < MAX_DATA_POINTS) {
                index = i;
            } else {
                index = (dataIndex + i) % MAX_DATA_POINTS;
            }
            
            String line = String(dataBuffer[index].timestamp) + ",";
            line += String(dataBuffer[index].temperature, 2) + ",";
            line += String(dataBuffer[index].pressure, 2) + ",";
            line += String(dataBuffer[index].humidity, 2) + "\n";
            
            server.sendContent(line);
        }
    } else {
        server.send(400, "text/plain", "Invalid format. Use ?format=json or ?format=csv");
    }
}

bool connectToWiFi() {
    // Try primary WiFi first (3 attempts)
    Serial.println("Attempting to connect to primary WiFi: " + String(ssid));
    for (int attempt = 1; attempt <= 3; attempt++) {
        Serial.println("Primary WiFi attempt " + String(attempt) + "/3");
        WiFi.begin(ssid, password);
        
        // Wait up to 20 seconds for connection
        int timeout = 10; // 40 * 500ms = 20 seconds
        while (WiFi.status() != WL_CONNECTED && timeout > 0) {
            delay(500);
            Serial.print(".");
            timeout--;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println();
            Serial.println("Primary WiFi connected successfully!");
            Serial.print("IP address: ");
            Serial.println(WiFi.localIP());
            return true;
        }
        
        Serial.println();
        Serial.println("Primary WiFi connection failed on attempt " + String(attempt));
        WiFi.disconnect();
        delay(2000); // Wait 2 seconds before next attempt
    }
    
    // If primary WiFi failed, try secondary WiFi (if configured)
    if (strlen(ssid2) > 0) {
        Serial.println("Attempting to connect to secondary WiFi: " + String(ssid2));
        for (int attempt = 1; attempt <= 3; attempt++) {
            Serial.println("Secondary WiFi attempt " + String(attempt) + "/3");
            WiFi.begin(ssid2, password2);
            
            // Wait up to 20 seconds for connection
            int timeout = 40; // 40 * 500ms = 20 seconds
            while (WiFi.status() != WL_CONNECTED && timeout > 0) {
                delay(500);
                Serial.print(".");
                timeout--;
            }
            
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println();
                Serial.println("Secondary WiFi connected successfully!");
                Serial.print("IP address: ");
                Serial.println(WiFi.localIP());
                return true;
            }
            
            Serial.println();
            Serial.println("Secondary WiFi connection failed on attempt " + String(attempt));
            WiFi.disconnect();
            delay(2000); // Wait 2 seconds before next attempt
        }
    } else {
        Serial.println("No secondary WiFi configured. Please provide secondary WiFi credentials if needed.");
    }
    
    Serial.println("All WiFi connection attempts failed!");
    return false;
}