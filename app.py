from flask import Flask, request, jsonify
from datetime import datetime
import threading
import smtplib
from email.mime.text import MIMEText
import os

app = Flask(__name__)

# global vars
unseen_falls = []
LOG_FILE = "falls.log"
EMAIL_FILE = "email_config.txt"

# email global vars
SENDER_EMAIL = "sender email goes here"
SENDER_PASSWORD = "application password goes here" # app password
RECEIVER_EMAIL = ""

def send_email_alert(timestamp, severity):
    global RECEIVER_EMAIL

    if not RECEIVER_EMAIL:
        print("Alert skipped: No receiver email configured.")
        return

    # create message object
    msg = MIMEText(f"A fall was detected on {timestamp}.\nSeverity (Peak Acceleration): {severity} m/s^2.")
    msg['Subject'] = 'URGENT: Fall Detected!'
    msg['From'] = SENDER_EMAIL
    msg['To'] = RECEIVER_EMAIL

    # send the email
    try:
        with smtplib.SMTP_SSL('smtp.gmail.com', 465) as server:
            server.login(SENDER_EMAIL, SENDER_PASSWORD)
            server.send_message(msg)
            print(f"Alert email sent successfully to {RECEIVER_EMAIL}.")
    except Exception as e:
        print(f"Failed to send email: {e}")

@app.route('/api/fall-detector', methods=['POST'])
def register_fall():
    global unseen_falls
    data = request.get_json() or {}
    severity = data.get('severity', 'Unknown')
    
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    
    # cache the fall to be "popped" by the client on the next GET
    fall_record = {"timestamp": timestamp, "severity": severity}
    unseen_falls.append(fall_record)
    
    # record the fall in the log file
    with open(LOG_FILE, 'a') as f:
        f.write(f"{timestamp} | Severity: {severity}\n")
    
    # send the email
    threading.Thread(target=send_email_alert, args=(timestamp, severity)).start()
    
    return jsonify({"status": "logged"}), 201

@app.route('/api/fall-detector', methods=['GET'])
def check_falls():
    global unseen_falls
    if not unseen_falls:
        return jsonify([]), 200
        
    # make a copy of the list locally to be returned and clear the global list
    current_falls = unseen_falls.copy()
    unseen_falls.clear()
    
    # return the list of cached falls
    return jsonify(current_falls), 200

@app.route('/api/config/email', methods=['POST'])
def update_email():
    global RECEIVER_EMAIL
    data = request.get_json() or {}
    new_email = data.get('email')

    if new_email:
        # edit the file that contains the email
        with open(EMAIL_FILE, 'w') as f:
            f.write(new_email.strip())

        # update the receiver global var
        RECEIVER_EMAIL = new_email.strip()
        return jsonify({"status": "success", "message": "Email configured"}), 200

    return jsonify({"error": "No email provided"}), 400

@app.route('/api/falls', methods=['GET'])
def get_all_falls():
    global RECEIVER_EMAIL

    # read the receiver email and set the global var
    if os.path.exists(EMAIL_FILE):
        with open(EMAIL_FILE, 'r') as f:
            saved_email = f.read().strip()
            if saved_email:
                RECEIVER_EMAIL = saved_email

    falls = []
    # try to read from the log file
    if os.path.exists(LOG_FILE):
        with open(LOG_FILE, 'r') as f:
            for line in f:
                parts = line.strip().split(' | Severity: ')
                if len(parts) == 2:
                    falls.append({"timestamp": parts[0], "severity": parts[1]})
                else:
                    falls.append({"timestamp": parts[0], "severity": "Unknown"})

    # returb both the falls array and the current email
    return jsonify({
        "falls": falls,
        "current_email": RECEIVER_EMAIL
        }), 200

if __name__ == '__main__':
    app.run(host='127.0.0.1', port=5000)
