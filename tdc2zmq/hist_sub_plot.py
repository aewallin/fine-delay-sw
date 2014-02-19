import zmq
import matplotlib.pyplot as plt
import matplotlib.animation as animation

import tdc_pb2 # generated from .proto by "protc tdc.proto --python_out=." 
import numpy

context = zmq.Context()
socket = context.socket(zmq.SUB)
# specific server:port to subscribe to
#socket.connect("tcp://localhost:5555")  
#socket.connect("ipc:///tmp/tstamp.pipe")
socket.connect("ipc:///tmp/histogram.pipe")
# subscription filter, "" means subscribe to everything
socket.setsockopt(zmq.SUBSCRIBE, "") 

fig = plt.figure()
ax1 = fig.add_subplot(1,1,1)

hbins = [1,2,3]
hvals = [4,5,6]

def animate(i):
	# bring in new data
	#xdata.append( xdata[-1]+1 ) # advance time
	#ydata.append( new_data( ydata[-1] ) ) # more data to plot
	msg = socket.recv()
	h = tdc_pb2.Histogram()
	h.ParseFromString(msg)
	
	hbins = range(h.bins)
	hvals = h.hist
	print sum(h.hist)
	print hvals
	
	# update plot
	ax1.clear()
	ax1.bar(hbins,hvals)
	#plt.draw()

#  interval is milliseconds
a = animation.FuncAnimation(fig,animate, interval=1000)
plt.show()





