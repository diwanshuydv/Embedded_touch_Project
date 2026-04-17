import serial
import time

# Replace with your actual port and baud rate
PORT = '/dev/ttyACM0' 
BAUD = 115200 

try:
    # Open the serial port
    ser = serial.Serial(PORT, BAUD, timeout=1)
    print(f"Connected to {PORT}")

    while True:
        message = "Hello STM32\n\r\0"
        
        # 1. Send data to USART
        ser.write(message.encode('utf-8'))
        print(f"Sent: {message.strip()}")
        # 2. Read the Echo back (what the STM32 sends)
        # This replaces your need for the 'screen' command
        echo = ser.read(1)
        while echo:
            print(echo)
            echo = ser.read(1)

        time.sleep(2) # Interval

except serial.SerialException as e:
    print(f"Error: {e}")
finally:
    if 'ser' in locals() and ser.is_open:
        ser.close()