import socket
import threading
import json
from datetime import datetime
from flask import Flask, render_template, jsonify
from collections import deque

app = Flask(__name__)

temperature_data = deque(maxlen=100)
latest_temperature = None
connection_status = "waiting for ESP32 connection..."

TCP_IP = "0.0.0.0"  
TCP_PORT = 9000
BUFFER_SIZE = 1024

def tcp_server_thread():
   
    global latest_temperature, connection_status
    
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.bind((TCP_IP, TCP_PORT))
    server_socket.listen(1)
    
    print(f"\n{'='*50}")
    print(f"TCP Server listening on {TCP_IP}:{TCP_PORT}")
    print(f"{'='*50}\n")
    connection_status = f"listening on port {TCP_PORT}"
    
    while True:
        try:
            print("waiting for ESP32 connection...")
            connection_status = "waiting for ESP32 connection..."
            
            conn, addr = server_socket.accept()
            print(f"connection from: {addr[0]}:{addr[1]}")
            connection_status = f"connected to {addr[0]}:{addr[1]}"
            
            buffer = ""
            while True:
                data = conn.recv(BUFFER_SIZE)
                if not data:
                    print("connection closed by ESP32")
                    connection_status = "waiting for reconnection..."
                    break
            
                buffer += data.decode('utf-8', errors='replace')
                
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    if line.strip():
                        try:
                           
                            temp_reading = json.loads(line)
                            temperature = temp_reading.get('temperature')
                            esp_timestamp = temp_reading.get('timestamp')
                            
                            current_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                            
                            data_point = {
                                'temperature': temperature,
                                'timestamp': current_time,
                                'esp_timestamp': esp_timestamp
                            }
                            
                            temperature_data.append(data_point)
                            latest_temperature = data_point
                          
                            temp_f = (temperature * 9.0/5.0) + 32.0
                            print(f"[{current_time}] Received: {temperature:.2f} °C ({temp_f:.2f} °F) [ESP32 tick: {esp_timestamp}]")
                            
                        except json.JSONDecodeError as e:
                            print(f"JSON decode error: {e}")
                        except Exception as e:
                            print(f"error processing data: {e}")
            
            conn.close()
            
        except Exception as e:
            print(f"TCP Server error: {e}")
            connection_status = f"Error: {str(e)}"

@app.route('/')
def index():

    return render_template('index.html')

@app.route('/api/latest')
def get_latest():

    return jsonify({
        'temperature': latest_temperature,
        'status': connection_status
    })

@app.route('/api/history')
def get_history():

    return jsonify({
        'data': list(temperature_data),
        'status': connection_status
    })

@app.route('/api/stats')
def get_stats():

    if not temperature_data:
        return jsonify({
            'min': None,
            'max': None,
            'avg': None,
            'count': 0
        })
    
    temps = [d['temperature'] for d in temperature_data]
    return jsonify({
        'min': min(temps),
        'max': max(temps),
        'avg': sum(temps) / len(temps),
        'count': len(temps)
    })

if __name__ == '__main__':
    print("\n" + "="*50)
    print("  BMP280 Temperature Monitoring Server")
    print("  CSYE 6550 - IoT/Embedded Development")
    print("="*50)
    
    tcp_thread = threading.Thread(target=tcp_server_thread, daemon=True)
    tcp_thread.start()
    
    print("\n" + "="*50)
    print("Flask Web Dashboard: http://localhost:5000")
    print("="*50 + "\n")
    
    app.run(host='0.0.0.0', port=5000, debug=False, threaded=True)