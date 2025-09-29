#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <time.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <Update.h>

// Primary WiFi credentials
const char* ssid = "UPC6628674";
const char* password = "Ar6jxnrurxhe";

// Secondary WiFi credentials (to be set later)
const char* ssid2 = "5GTowerTest";
const char* password2 = "stopcham";

// Admin credentials for protected endpoints
const char* adminUsername = "admin";
const char* adminPassword = "weather2025!"; // Change this to your preferred password

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
const unsigned long DATA_LOG_INTERVAL = 600000; // Log every 10 minutes

// Persistent storage configuration
const char* DATA_FILE = "/sensor_data.json";
const char* CONFIG_FILE = "/config.json";
const int SAVE_BATCH_SIZE = 5; // Save to flash every 5 new data points
int unsavedDataCount = 0;

// Serial Monitor Buffer for web interface
struct SerialMessage {
    unsigned long timestamp;
    String message;
};

const int MAX_SERIAL_MESSAGES = 100; // Store last 100 serial messages
SerialMessage serialBuffer[MAX_SERIAL_MESSAGES];
int serialIndex = 0;
int serialCount = 0;

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
                <button class="btn" onclick="window.open('/serial', '_blank')" style="margin: 5px;">Serial Monitor</button>
                <button class="btn" onclick="exportData('json')" style="margin: 5px;">Export JSON</button>
                <button class="btn" onclick="exportData('csv')" style="margin: 5px;">Export CSV</button>
                <button class="btn" onclick="clearAllData()" style="margin: 5px; background-color: #dc3545;">Clear All Data</button>
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
        
        // Clear all data function
        function clearAllData() {
            if (confirm('⚠️ WARNING: This will permanently delete ALL stored weather data!\\n\\nAre you sure you want to continue?')) {
                if (confirm('This action cannot be undone. Delete all data?')) {
                    // Prompt for admin credentials
                    const username = prompt('Admin Username:');
                    const password = prompt('Admin Password:');
                    
                    if (!username || !password) {
                        alert('❌ Authentication required!');
                        return;
                    }
                    
                    // Create authorization header
                    const credentials = btoa(username + ':' + password);
                    
                    fetch('/cleardata', { 
                        method: 'POST',
                        headers: {
                            'Authorization': 'Basic ' + credentials
                        }
                    })
                        .then(response => {
                            if (response.status === 401) {
                                alert('❌ Invalid credentials!');
                                return Promise.reject('Unauthorized');
                            }
                            return response.text();
                        })
                        .then(result => {
                            alert('✅ All data has been cleared successfully!');
                            // Refresh the page to show empty charts
                            location.reload();
                        })
                        .catch(error => {
                            if (error !== 'Unauthorized') {
                                alert('❌ Error clearing data: ' + error);
                            }
                        });
                }
            }
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
void logToSerial(String message);
void handleSerialMonitor();
void handleSerialData();
void handleClearData();
bool initializeStorage();
bool saveDataToFlash();
bool loadDataFromFlash();
void handleExport();
bool connectToWiFi();
bool requireAuth();
void setupOTA();
void handleOTAUpdate();
String getOTAUpdatePage();
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
    
    logToSerial("Starting ESP32-S3 Weather Station...");

    // Initialize I2C with ESP32-S3 available pins
    Wire.begin(21, 20); // SDA = GPIO21, SCL = GPIO20
    logToSerial("I2C initialized on SDA=21, SCL=20");

    // Scan for I2C devices
    logToSerial("Scanning for I2C devices...");
    byte error, address;
    int nDevices = 0;
    for(address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        if (error == 0) {
            String addressStr = "I2C device found at address 0x";
            if (address < 16) addressStr += "0";
            addressStr += String(address, HEX);
            logToSerial(addressStr);
            nDevices++;
        }
    }
    if (nDevices == 0) {
        logToSerial("No I2C devices found!");
    } else {
        String deviceCountStr = "Found " + String(nDevices);
        logToSerial(deviceCountStr + " I2C device(s)");
    }

    // Initialize BME280 sensor
    logToSerial("Initializing BME280 sensor...");
    
    if (!bme.begin(0x76)) {
        logToSerial("BME280 not found at 0x76, trying 0x77...");
        if (!bme.begin(0x77)) {
            logToSerial("Could not find a valid BME280 sensor, check wiring!");
            logToSerial("Make sure:");
            logToSerial("- VCC -> 3.3V");
            logToSerial("- GND -> GND");
            logToSerial("- SDA -> GPIO 21");
            logToSerial("- SCL -> GPIO 20");
            while (1);
        } else {
            logToSerial("BME280 found at address 0x77!");
        }
    } else {
        logToSerial("BME280 found at address 0x76!");
    }

    // Configure BME280 settings
    bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                    Adafruit_BME280::SAMPLING_X2,
                    Adafruit_BME280::SAMPLING_X16,
                    Adafruit_BME280::SAMPLING_X16,
                    Adafruit_BME280::FILTER_X16,
                    Adafruit_BME280::STANDBY_MS_500);

    logToSerial("BME280 sensor initialized successfully!");

    // Initialize LittleFS for persistent storage
    if (!initializeStorage()) {
        logToSerial("Failed to initialize storage system!");
        logToSerial("Continuing without persistent storage...");
    }

    // Connect to WiFi with retry logic
    if (!connectToWiFi()) {
        logToSerial("Failed to connect to any WiFi network!");
        logToSerial("Please check your WiFi credentials and network availability.");
        // Continue anyway for development/testing
    }

    // Initialize SSE client array
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        sseClients[i].active = false;
        sseClients[i].lastPing = 0;
    }
    sseClientCount = 0;
    logToSerial("SSE client array initialized");

    // Initialize NTP time
    logToSerial("Setting up NTP time...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    // Wait for time to be set
    struct tm timeinfo;
    int attempts = 0;
    while (!getLocalTime(&timeinfo) && attempts < 10) {
        logToSerial("Failed to obtain time, retrying...");
        delay(1000);
        attempts++;
    }
    if (attempts < 10) {
        logToSerial("Time synchronized successfully!");
        char timeString[64];
        strftime(timeString, sizeof(timeString), "Current time: %A, %B %d %Y %H:%M:%S", &timeinfo);
        logToSerial(String(timeString));
    } else {
        logToSerial("Failed to sync time, using millis() fallback");
    }

    // Set up web server routes
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.on("/history", handleHistory);
    server.on("/memory", handleMemory);
    server.on("/events", handleEvents);
    server.on("/export", handleExport);
    server.on("/serial", handleSerialMonitor);
    server.on("/serialdata", handleSerialData);
    server.on("/cleardata", []() {
        if (!requireAuth()) return;
        handleClearData();
    });
    server.on("/update", HTTP_GET, []() {
        if (!requireAuth()) return;
        server.send(200, "text/html", getOTAUpdatePage());
    });
    server.on("/update", HTTP_POST, []() {
        if (!requireAuth()) return;
        server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        ESP.restart();
    }, []() {
        if (!requireAuth()) return;
        handleOTAUpdate();
    });
    server.onNotFound(handleNotFound);

    // Setup OTA
    setupOTA();

    // Start server
    server.begin();
    logToSerial("HTTP server started");
    logToSerial("Access the weather station at: http://" + WiFi.localIP().toString());
    logToSerial("OTA update available at: http://" + WiFi.localIP().toString() + "/update");
    
    // Take initial sensor reading and log data immediately for testing
    logToSerial("Taking initial sensor reading...");
    readSensorData();
    logSensorData();
    
    // Set up for next data log
    lastDataLog = millis();
}

void loop() {
    // put your main code here, to run repeatedly:
    ArduinoOTA.handle();
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
    
    // Periodic auto-save every 5 minutes (in case of unexpected restart)
    static unsigned long lastAutoSave = 0;
    if (millis() - lastAutoSave > 300000 && unsavedDataCount > 0) { // 5 minutes
        logToSerial("Periodic auto-save triggered (unsaved: " + String(unsavedDataCount) + " points)");
        if (saveDataToFlash()) {
            logToSerial("Periodic auto-save successful!");
            unsavedDataCount = 0;
        }
        lastAutoSave = millis();
    }
}

// put function definitions here:
bool requireAuth() {
    if (!server.authenticate(adminUsername, adminPassword)) {
        server.requestAuthentication(DIGEST_AUTH, "Weather Station Admin", "Authentication Required");
        return false;
    }
    return true;
}

void setupOTA() {
    ArduinoOTA.setHostname("ESP32-WeatherStation");
    ArduinoOTA.setPassword("weather123"); // Change this to your preferred password
    
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "sketch";
        } else { // U_SPIFFS
            type = "filesystem";
        }
        logToSerial("Start updating " + type);
    });
    
    ArduinoOTA.onEnd([]() {
        logToSerial("OTA Update completed successfully!");
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static unsigned long lastLog = 0;
        unsigned long now = millis();
        if (now - lastLog > 1000) { // Log every second
            String progressMsg = "Progress: " + String((progress / (total / 100))) + "%";
            logToSerial(progressMsg);
            lastLog = now;
        }
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        String errorMsg = "Error[" + String(error) + "]: ";
        if (error == OTA_AUTH_ERROR) errorMsg += "Auth Failed";
        else if (error == OTA_BEGIN_ERROR) errorMsg += "Begin Failed";
        else if (error == OTA_CONNECT_ERROR) errorMsg += "Connect Failed";
        else if (error == OTA_RECEIVE_ERROR) errorMsg += "Receive Failed";
        else if (error == OTA_END_ERROR) errorMsg += "End Failed";
        logToSerial(errorMsg);
    });
    
    ArduinoOTA.begin();
    logToSerial("OTA Ready");
}

void handleOTAUpdate() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        logToSerial("Update Start: " + upload.filename);
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            logToSerial("Update Success: " + String(upload.totalSize) + " bytes");
        } else {
            Update.printError(Serial);
        }
    }
}

String getOTAUpdatePage() {
    return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32 Weather Station - Firmware Update</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body { 
            font-family: Arial, sans-serif; 
            margin: 0; 
            padding: 20px; 
            background-color: #f0f0f0; 
        }
        .container { 
            max-width: 600px; 
            margin: 0 auto; 
            background: white; 
            padding: 30px; 
            border-radius: 10px; 
            box-shadow: 0 2px 10px rgba(0,0,0,0.1); 
        }
        h1 { 
            color: #333; 
            text-align: center; 
            margin-bottom: 30px; 
        }
        .upload-section {
            background: #f8f9fa;
            padding: 20px;
            border-radius: 8px;
            margin: 20px 0;
        }
        input[type="file"] {
            width: 100%;
            padding: 10px;
            margin: 10px 0;
            border: 2px dashed #007bff;
            border-radius: 5px;
            background: white;
        }
        .upload-btn {
            background: #007bff;
            color: white;
            border: none;
            padding: 12px 24px;
            border-radius: 5px;
            cursor: pointer;
            font-size: 16px;
            width: 100%;
            margin-top: 10px;
        }
        .upload-btn:hover { background: #0056b3; }
        .warning {
            background: #fff3cd;
            border: 1px solid #ffeaa7;
            color: #856404;
            padding: 15px;
            border-radius: 5px;
            margin: 20px 0;
        }
        .info {
            background: #d1ecf1;
            border: 1px solid #b6d4db;
            color: #0c5460;
            padding: 15px;
            border-radius: 5px;
            margin: 20px 0;
        }
        .progress {
            width: 100%;
            height: 20px;
            background: #e9ecef;
            border-radius: 10px;
            overflow: hidden;
            margin: 10px 0;
            display: none;
        }
        .progress-bar {
            height: 100%;
            background: #28a745;
            width: 0%;
            transition: width 0.3s;
        }
        .back-link {
            display: inline-block;
            margin-top: 20px;
            color: #007bff;
            text-decoration: none;
        }
        .back-link:hover { text-decoration: underline; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🔄 Firmware Update</h1>
        
        <div class="info">
            <strong>Current Device:</strong> ESP32-S3 Weather Station<br>
            <strong>IP Address:</strong> <span id="deviceIP"></span><br>
            <strong>Free Space:</strong> <span id="freeSpace"></span>
        </div>
        
        <div class="warning">
            ⚠️ <strong>Warning:</strong> Do not power off the device during firmware update. 
            The process may take 1-2 minutes to complete.
        </div>
        
        <div class="upload-section">
            <h3>Select Firmware File (.bin)</h3>
            <form method="POST" action="/update" enctype="multipart/form-data" id="uploadForm">
                <input type="file" name="update" accept=".bin" required id="fileInput">
                <div class="progress" id="progressContainer">
                    <div class="progress-bar" id="progressBar"></div>
                </div>
                <div id="progressText" style="text-align: center; margin: 10px 0; display: none;">
                    Uploading: 0%
                </div>
                <button type="submit" class="upload-btn" id="uploadBtn">
                    📤 Upload Firmware
                </button>
            </form>
        </div>
        
        <div class="info">
            <strong>Instructions:</strong><br>
            1. Compile your firmware in PlatformIO<br>
            2. Locate the .bin file in .pio/build/esp32-s3-devkitc-1/<br>
            3. Select the firmware.bin file above<br>
            4. Click "Upload Firmware" and wait for completion<br>
            5. Device will restart automatically
        </div>
        
        <a href="/" class="back-link">← Back to Weather Station</a>
    </div>

    <script>
        // Display device info
        document.getElementById('deviceIP').textContent = window.location.hostname;
        
        // Get free space info
        fetch('/memory')
            .then(response => response.json())
            .then(data => {
                document.getElementById('freeSpace').textContent = 
                    Math.round(data.freeRAM / 1024) + ' KB RAM';
            })
            .catch(error => {
                document.getElementById('freeSpace').textContent = 'Unknown';
            });

        // Handle form submission with progress
        document.getElementById('uploadForm').addEventListener('submit', function(e) {
            e.preventDefault();
            
            const fileInput = document.getElementById('fileInput');
            const file = fileInput.files[0];
            
            if (!file) {
                alert('Please select a firmware file!');
                return;
            }
            
            if (!file.name.endsWith('.bin')) {
                alert('Please select a .bin firmware file!');
                return;
            }
            
            const formData = new FormData();
            formData.append('update', file);
            
            const uploadBtn = document.getElementById('uploadBtn');
            const progressContainer = document.getElementById('progressContainer');
            const progressBar = document.getElementById('progressBar');
            const progressText = document.getElementById('progressText');
            
            uploadBtn.disabled = true;
            uploadBtn.textContent = '⏳ Uploading...';
            progressContainer.style.display = 'block';
            progressText.style.display = 'block';
            
            const xhr = new XMLHttpRequest();
            
            xhr.upload.addEventListener('progress', function(e) {
                if (e.lengthComputable) {
                    const percentComplete = (e.loaded / e.total) * 100;
                    progressBar.style.width = percentComplete + '%';
                    progressText.textContent = 'Uploading: ' + Math.round(percentComplete) + '%';
                }
            });
            
            xhr.addEventListener('load', function() {
                if (xhr.status === 200) {
                    progressText.textContent = '✅ Upload successful! Device restarting...';
                    uploadBtn.textContent = '✅ Success';
                    setTimeout(() => {
                        window.location.href = '/';
                    }, 3000);
                } else {
                    progressText.textContent = '❌ Upload failed!';
                    uploadBtn.disabled = false;
                    uploadBtn.textContent = '📤 Upload Firmware';
                }
            });
            
            xhr.addEventListener('error', function() {
                progressText.textContent = '❌ Upload error!';
                uploadBtn.disabled = false;
                uploadBtn.textContent = '📤 Upload Firmware';
            });
            
            xhr.open('POST', '/update');
            xhr.send(formData);
        });
    </script>
</body>
</html>
)rawliteral";
}

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

    // Uncomment for debugging sensor readings:
    // logToSerial("=== Sensor Readings ===");
    // logToSerial("Temperature: " + String(temperature) + " °C");
    // logToSerial("Pressure: " + String(pressure) + " hPa");
    // logToSerial("Humidity: " + String(humidity) + " %");
    // logToSerial();
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
    
    // Simplified logging - just show essential info
    logToSerial("Data logged: " + String(dataCount) + " points, " + String(timeStr) + 
                  " [" + String(temperature, 1) + "°C, " + String(humidity, 1) + "%, " + String(pressure, 1) + "hPa]");
    
    // Uncomment for detailed debugging:
    // logToSerial("=== Data Logged ===");
    // logToSerial("Timestamp: " + String(timestamp) + " (" + String(timeStr) + ")");
    // logToSerial("Index: " + String(dataIndex));
    // logToSerial("Temperature: " + String(temperature, 2) + "°C");
    // logToSerial("Pressure: " + String(pressure, 2) + "hPa");
    // logToSerial("Humidity: " + String(humidity, 2) + "%");
    
    dataIndex = (dataIndex + 1) % MAX_DATA_POINTS;
    if (dataCount < MAX_DATA_POINTS) {
        dataCount++;
    }
    
    // Uncomment for detailed debugging:
    // logToSerial("Total data points: " + String(dataCount));
    // logToSerial("Next index: " + String(dataIndex));
    
    // Increment unsaved data counter
    unsavedDataCount++;
    
    // Save immediately for first few data points, then use batch saves
    bool shouldSave = false;
    if (dataCount <= 3) {
        // Save first 3 data points immediately to ensure persistence
        shouldSave = true;
        logToSerial("Saving initial data point immediately...");
    } else if (unsavedDataCount >= SAVE_BATCH_SIZE) {
        // Regular batch save
        shouldSave = true;
        logToSerial("Saving data batch to flash storage...");
    }
    
    if (shouldSave) {
        if (saveDataToFlash()) {
            logToSerial("Data saved to flash successfully! (" + String(dataCount) + " total points)");
            unsavedDataCount = 0;
        } else {
            logToSerial("Failed to save data to flash!");
        }
    }
    
    // Uncomment for memory usage debugging:
    // size_t freeRAM = ESP.getFreeHeap();
    // size_t totalRAM = ESP.getHeapSize();
    // logToSerial("Free RAM: " + String(freeRAM) + " bytes (" + String(freeRAM/1024) + " KB)");
    // logToSerial("RAM usage: " + String((float)(totalRAM - freeRAM) * 100.0 / totalRAM, 1) + "%");
    // logToSerial("========================");
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
    
    logToSerial("=== History Request ===");
    logToSerial("Range: " + range);
    logToSerial("Total data count: " + String(dataCount));
    logToSerial("Data index: " + String(dataIndex));
    
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
    
    logToSerial("Points to return: " + String(pointsToReturn));
    
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
        
        logToSerial("Start index: " + String(startIndex));
        
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
    logToSerial("JSON length: " + String(json.length()));
    logToSerial("========================");
    
    server.send(200, "application/json", json);
}

void handleMemory() {
    // Uncomment for debugging memory requests:
    // logToSerial("=== Memory Info Request ===");
    String memInfo = getMemoryInfo();
    // logToSerial("Memory info: " + memInfo);
    // logToSerial("===========================");
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
    
    // Note: Removed logToSerial call here to prevent recursion
    
    // Send initial connection message
    String initialMessage = "data: {\"type\":\"connected\",\"message\":\"SSE connection established\"}\n\n";
    client.print(initialMessage);
    client.flush(); // Ensure data is sent immediately
}

void broadcastSSE(String message) {
    if (sseClientCount == 0) return;
    
    String sseMessage = "data: " + message + "\n\n";
    
    // Note: Removed logToSerial call here to prevent infinite recursion
    
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
                    // Note: Removed logToSerial call here to prevent recursion
                }
            } else {
                // Client disconnected
                sseClients[i].active = false;
                sseClients[i].client.stop();
                sseClientCount--;
                // Note: Removed logToSerial call here to prevent recursion
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
                // Note: Removed logToSerial call here to prevent recursion
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
                        // Note: Removed logToSerial call here to prevent recursion
                    }
                }
            }
        }
    }
    
    // Note: Removed SSE cleanup complete message to prevent recursion
}

bool initializeStorage() {
    logToSerial("Initializing LittleFS...");
    if (!LittleFS.begin(true)) {
        logToSerial("LittleFS mount failed!");
        return false;
    }
    
    logToSerial("LittleFS mounted successfully!");
    
    // Print storage info
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    logToSerial("Storage Info:");
    logToSerial("  Total: " + String(totalBytes / 1024) + " KB");
    logToSerial("  Used: " + String(usedBytes / 1024) + " KB");
    logToSerial("  Free: " + String((totalBytes - usedBytes) / 1024) + " KB");
    
    // Try to load existing data
    loadDataFromFlash();
    
    return true;
}

bool saveDataToFlash() {
    // Ensure directory exists (though LittleFS doesn't really have directories)
    logToSerial("Attempting to save " + String(dataCount) + " data points to flash...");
    
    File dataFile = LittleFS.open(DATA_FILE, "w", true); // true = create if not exists
    if (!dataFile) {
        logToSerial("Failed to open data file for writing!");
        logToSerial("LittleFS info: Total=" + String(LittleFS.totalBytes()) + ", Used=" + String(LittleFS.usedBytes()));
        return false;
    }
    
    logToSerial("Data file opened successfully for writing");
    
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
        logToSerial("Data saved: " + String(bytesWritten) + " bytes, " + String(dataCount) + " data points");
        return true;
    } else {
        logToSerial("Failed to write data to file!");
        return false;
    }
}

bool loadDataFromFlash() {
    if (!LittleFS.exists(DATA_FILE)) {
        logToSerial("No existing data file found - starting fresh");
        return true; // This is OK for first run
    }
    
    File dataFile = LittleFS.open(DATA_FILE, "r");
    if (!dataFile) {
        logToSerial("Failed to open data file for reading!");
        return false;
    }
    
    // Read and parse JSON
    DynamicJsonDocument doc(32768); // 32KB for JSON document
    DeserializationError error = deserializeJson(doc, dataFile);
    dataFile.close();
    
    if (error) {
        logToSerial("Failed to parse JSON data file!");
        logToSerial("Error: " + String(error.c_str()));
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
    
    logToSerial("Loaded " + String(dataCount) + " data points from flash storage");
    if (dataCount > 0) {
        logToSerial("Data range: " + String(dataBuffer[0].timestamp) + " to " + String(dataBuffer[dataCount-1].timestamp));
    }
    
    return true;
}

void handleExport() {
    logToSerial("Export request received");
    
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

void logToSerial(String message) {
    // Store message in circular buffer
    serialBuffer[serialIndex].timestamp = millis() / 1000; // Use fast millis() for serial logging
    serialBuffer[serialIndex].message = message;
    
    serialIndex = (serialIndex + 1) % MAX_SERIAL_MESSAGES;
    if (serialCount < MAX_SERIAL_MESSAGES) {
        serialCount++;
    }
    
    // Also print to actual serial
    Serial.println(message);
    
    // NO SSE broadcasting here to prevent infinite recursion
}

void handleSerialData() {
    String json = "[";
    
    for (int i = 0; i < serialCount; i++) {
        int index;
        if (serialCount < MAX_SERIAL_MESSAGES) {
            index = i;
        } else {
            index = (serialIndex + i) % MAX_SERIAL_MESSAGES;
        }
        
        if (i > 0) json += ",";
        json += "{";
        json += "\"timestamp\":" + String(serialBuffer[index].timestamp) + ",";
        json += "\"message\":\"" + serialBuffer[index].message + "\"";
        json += "}";
    }
    
    json += "]";
    server.send(200, "application/json", json);
}

void handleSerialMonitor() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Serial Monitor - ESP32 Weather Station</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body { 
            font-family: 'Courier New', monospace; 
            margin: 0; 
            padding: 20px; 
            background-color: #1e1e1e; 
            color: #d4d4d4;
        }
        .header {
            background-color: #2d2d30;
            padding: 15px;
            border-radius: 5px;
            margin-bottom: 20px;
            text-align: center;
        }
        .serial-output {
            background-color: #0c0c0c;
            border: 1px solid #3e3e42;
            border-radius: 5px;
            padding: 15px;
            height: 70vh;
            overflow-y: auto;
            font-size: 12px;
            line-height: 1.4;
        }
        .log-entry {
            margin-bottom: 5px;
            word-wrap: break-word;
        }
        .timestamp {
            color: #569cd6;
            margin-right: 10px;
        }
        .controls {
            margin-bottom: 10px;
            text-align: center;
        }
        .btn {
            background: #0e639c;
            color: white;
            border: none;
            padding: 8px 15px;
            margin: 0 5px;
            border-radius: 3px;
            cursor: pointer;
            font-size: 12px;
        }
        .btn:hover { background: #1177bb; }
        .status {
            color: #4ec9b0;
            font-size: 11px;
            margin-top: 5px;
        }
    </style>
</head>
<body>
    <div class="header">
        <h2>🖥️ Serial Monitor - ESP32 Weather Station</h2>
        <div class="controls">
            <button class="btn" onclick="refreshLogs()">Refresh</button>
            <button class="btn" onclick="clearLogs()">Clear</button>
            <button class="btn" onclick="toggleAutoUpdate()">Auto Update: <span id="auto-status">ON</span></button>
            <button class="btn" onclick="window.close()">Close</button>
        </div>
        <div class="status" id="status">Connected • Last update: <span id="last-update">Never</span></div>
    </div>
    
    <div class="serial-output" id="serial-output">
        Loading serial data...
    </div>

    <script>
        let autoUpdate = true;
        let updateInterval;

        function formatTimestamp(timestamp) {
            const date = new Date(timestamp * 1000);
            return date.toLocaleTimeString();
        }

        function refreshLogs() {
            fetch('/serialdata')
                .then(response => response.json())
                .then(data => {
                    const output = document.getElementById('serial-output');
                    output.innerHTML = '';
                    
                    data.forEach(entry => {
                        const div = document.createElement('div');
                        div.className = 'log-entry';
                        div.innerHTML = `<span class="timestamp">[${formatTimestamp(entry.timestamp)}]</span>${entry.message}`;
                        output.appendChild(div);
                    });
                    
                    // Auto-scroll to bottom
                    output.scrollTop = output.scrollHeight;
                    
                    // Update status
                    document.getElementById('last-update').textContent = new Date().toLocaleTimeString();
                })
                .catch(error => {
                    console.error('Error fetching serial data:', error);
                    document.getElementById('status').textContent = 'Error loading data';
                });
        }

        function clearLogs() {
            document.getElementById('serial-output').innerHTML = '<div class="log-entry">--- Logs cleared locally ---</div>';
        }

        function toggleAutoUpdate() {
            autoUpdate = !autoUpdate;
            document.getElementById('auto-status').textContent = autoUpdate ? 'ON' : 'OFF';
            
            if (autoUpdate) {
                updateInterval = setInterval(refreshLogs, 2000); // Update every 2 seconds
            } else {
                clearInterval(updateInterval);
            }
        }

        // Initialize
        document.addEventListener('DOMContentLoaded', function() {
            refreshLogs();
            updateInterval = setInterval(refreshLogs, 2000); // Auto-refresh every 2 seconds
        });
    </script>
</body>
</html>
)rawliteral";
    
    server.send(200, "text/html", html);
}

void handleClearData() {
    // Check if this is a POST request
    if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Method Not Allowed - Use POST");
        return;
    }
    
    logToSerial("⚠️ CLEARING ALL DATA - User requested data wipe");
    
    // Reset all data counters and indices
    dataCount = 0;
    dataIndex = 0;
    unsavedDataCount = 0;
    firstDataLog = 0;
    
    // Clear the data buffer
    for (int i = 0; i < MAX_DATA_POINTS; i++) {
        dataBuffer[i].timestamp = 0;
        dataBuffer[i].temperature = 0.0;
        dataBuffer[i].pressure = 0.0;
        dataBuffer[i].humidity = 0.0;
    }
    
    // Clear persistent storage
    bool storageCleared = false;
    if (LittleFS.remove(DATA_FILE)) {
        logToSerial("✅ Persistent storage file deleted successfully");
        storageCleared = true;
    } else {
        logToSerial("⚠️ No persistent storage file found or failed to delete");
        storageCleared = true; // Not an error if file doesn't exist
    }
    
    if (storageCleared) {
        logToSerial("✅ All weather data cleared successfully!");
        server.send(200, "text/plain", "SUCCESS: All data cleared successfully!");
        
        // Broadcast data clear event to SSE clients
        broadcastSSE("{\"type\":\"dataCleared\",\"message\":\"All data has been cleared\"}");
    } else {
        logToSerial("❌ Failed to clear some data");
        server.send(500, "text/plain", "ERROR: Failed to clear all data");
    }
}

bool connectToWiFi() {
    // Try primary WiFi first (3 attempts)
    logToSerial("Attempting to connect to primary WiFi: " + String(ssid));
    for (int attempt = 1; attempt <= 3; attempt++) {
        logToSerial("Primary WiFi attempt " + String(attempt) + "/3");
        WiFi.begin(ssid, password);
        
        // Wait up to 20 seconds for connection
        int timeout = 10; // 40 * 500ms = 20 seconds
        while (WiFi.status() != WL_CONNECTED && timeout > 0) {
            delay(500);
            Serial.print(".");
            timeout--;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            logToSerial("");
            logToSerial("Primary WiFi connected successfully!");
            logToSerial("IP address: " + WiFi.localIP().toString());
            return true;
        }
        
        logToSerial("");
        logToSerial("Primary WiFi connection failed on attempt " + String(attempt));
        WiFi.disconnect();
        delay(500); // Wait 2 seconds before next attempt
    }
    
    // If primary WiFi failed, try secondary WiFi (if configured)
    if (strlen(ssid2) > 0) {
        logToSerial("Attempting to connect to secondary WiFi: " + String(ssid2));
        for (int attempt = 1; attempt <= 3; attempt++) {
            logToSerial("Secondary WiFi attempt " + String(attempt) + "/3");
            WiFi.begin(ssid2, password2);
            
            // Wait up to 20 seconds for connection
            int timeout = 10; // 40 * 500ms = 20 seconds
            while (WiFi.status() != WL_CONNECTED && timeout > 0) {
                delay(500);
                Serial.print(".");
                timeout--;
            }
            
            if (WiFi.status() == WL_CONNECTED) {
                logToSerial("");
                logToSerial("Secondary WiFi connected successfully!");
                logToSerial("IP address: " + WiFi.localIP().toString());
                return true;
            }
            
            logToSerial("");
            logToSerial("Secondary WiFi connection failed on attempt " + String(attempt));
            WiFi.disconnect();
            delay(500); // Wait 2 seconds before next attempt
        }
    } else {
        logToSerial("No secondary WiFi configured. Please provide secondary WiFi credentials if needed.");
    }
    
    logToSerial("All WiFi connection attempts failed!");
    return false;
}