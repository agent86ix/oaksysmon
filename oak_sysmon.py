import serial
import sys
import psutil
import threading
import time
import json
import os

class OakSysMon(threading.Thread):
	def __init__(self, serial_port):
		threading.Thread.__init__(self)
		self.serial_port = serial_port
		self.running = True
		self.cpu_count = psutil.cpu_count
		self.ser = None

	def reset_port(self):
		if(self.ser != None):
			self.ser.close()
			self.ser = None
		try:
			self.ser = serial.Serial(self.serial_port, 115200)
		except serial.SerialException:
			print("Can't open serial port, retrying...")

	def run(self):
		# the first time, this does nothing
		psutil.cpu_percent()
		time.sleep(.5)
		report_end = bytearray(1)

		while(self.running):
			if(self.ser == None):
				self.reset_port()
				if(self.ser == None):
					time.sleep(5)
					continue
			report_dict = {}

			# get the timestamp
			report_dict['t'] = time.strftime("%Y-%m-%d %H:%M:%S")

			# Get CPU usage
			cpu_pct = psutil.cpu_percent(percpu=True)
			report_dict['c'] = []
			for i in range(len(cpu_pct)):
				report_dict['c'].append(cpu_pct[i])

			# Get memory usage
			mem = psutil.virtual_memory()
			report_dict['m'] = {'t':mem.total, 'a':mem.available}

			# Get disk usage
			diskparts = psutil.disk_partitions()
			report_dict['d'] = []
			#print diskparts
			for part in diskparts:
				if os.name == 'nt':
					if 'cdrom' in part.opts or part.fstype == '':
						# skip optical drives on windows
						continue
				usage = psutil.disk_usage(part.mountpoint)
				report_dict['d'].append({'m':part.mountpoint, 't':usage.total, 'u':usage.used})


			# Serialize to JSON
			report = json.dumps(report_dict)
			print report
			# Write to the serial port
			try:
				self.ser.write(report)
				self.ser.write(report_end)
			except serial.SerialException:
				print("Can't write to serial port, trying to reopen...")
				self.reset_port()

			time.sleep(5)



sys_mon_thread = OakSysMon(sys.argv[1])
sys_mon_thread.daemon = True
sys_mon_thread.start()

while True:
	time.sleep(1)