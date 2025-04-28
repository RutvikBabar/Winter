import csv
import json
import time
from datetime import datetime
import zmq

CSV_FILE = '2021_Market_Data_RAW.csv'
TIME_COLUMN = 'Time'
SYMBOL_COLUMN = 'Symbol'

# Monitored symbols from your pairs
MONITORED_SYMBOLS = {
    "PLUG", "TPGY",
    "ROL", "APHA",
    "SMSI", "ALL",
    "ALTR", "ATH",
    "CPB", "MSFT",
    "USAC", "TPR",
    "CC", "HSY",
    "GRTS", "FDX",
    "BILI", "GME",
    "NLSN"
}

def parse_time(t):
    try:
        return datetime.strptime(t, "%H:%M:%S.%f")
    except ValueError:
        return datetime.strptime(t, "%H:%M:%S")

def main():
    context = zmq.Context()
    socket = context.socket(zmq.PUB)
    socket.bind('tcp://127.0.0.1:5555')
    print("Publisher ready on tcp://127.0.0.1:5555...")

    market_open_system_time = time.time()
    market_open_csv_time = None

    with open(CSV_FILE, newline='') as csvfile:
        reader = csv.DictReader(csvfile)

        for idx, row in enumerate(reader):
            try:
                current_csv_time = parse_time(row[TIME_COLUMN])
            except ValueError:
                print(f"Skipping bad time format at row {idx}")
                continue

            if market_open_csv_time is None:
                market_open_csv_time = current_csv_time
                print(f"Market open time (CSV): {market_open_csv_time.time()}")
                print(f"Market replay starts at system time: {datetime.fromtimestamp(market_open_system_time).time()}")

            elapsed_csv = (current_csv_time - market_open_csv_time).total_seconds()
            target_system_time = market_open_system_time + elapsed_csv

            now = time.time()
            sleep_time = target_system_time - now
            if sleep_time > 0:
                time.sleep(sleep_time)

            data_to_send = {k: v for k, v in row.items()}
            message = json.dumps(data_to_send)
            socket.send_string(message)

            # Only print if the symbol is in our monitored list
            # if row.get(SYMBOL_COLUMN) in MONITORED_SYMBOLS:
            #     
            print(f"Published @ {current_csv_time.time()}: {json.dumps(data_to_send)}")

    print("✅ All data published.")

if __name__ == '__main__':
    main()
