import zmq
import matplotlib.pyplot as plt
import matplotlib.animation as animation

import tdc_pb2 # generated from .proto by "protoc tdc.proto --python_out=." 
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
mng = plt.get_current_fig_manager()
mng.resize(*mng.window.maxsize())

#  interval is milliseconds
#a = animation.FuncAnimation(fig,animate, interval=1000)
plt.show(block=False)


#hbins = [1,2,3]
#hvals = [4,5,6]


def run():
	# bring in new data
	#xdata.append( xdata[-1]+1 ) # advance time
	#ydata.append( new_data( ydata[-1] ) ) # more data to plot
	nframe = 1
	while True:
		print "waiting.."
		msg = socket.recv()
		h = tdc_pb2.Histogram()
		h.ParseFromString(msg)
		
		binwidth = float(h.s + float(h.ps)/1e12) / (h.bins)
		hbins = [1e6*x*binwidth for x in range(h.bins)]
		hvals = h.hist
		#print sum(h.hist)
		#print hvals
		
		# update plot
		ax1.clear()
		##print "cleared"
		#ax1.semilogy(hbins,[float(h)/len(hvals) for h in hvals],'o')
		ax1.semilogy(hbins, hvals,'o')
		plt.xlabel('Time (us)')
		plt.ylabel('Counts')
		plt.title('TIme-stamp histogram test. AW 2014-02-20')
		print "plotted %d histogram" % (sum(hvals))
		print " max count ", max(hvals)
		plt.ylim((1e0,1e4))
		plt.draw()
		filename = "frame_%03d" % nframe
		nframe = nframe+1
		plt.savefig(filename)



if __name__ == "__main__":
	run()



