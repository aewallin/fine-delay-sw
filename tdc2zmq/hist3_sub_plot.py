import zmq
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from scipy.optimize import curve_fit

from scipy.optimize import curve_fit

import tdc_pb2 # generated from .proto by "protoc tdc.proto --python_out=." 
import numpy
import math

context = zmq.Context()
socket = context.socket(zmq.SUB)
# specific server:port to subscribe to
#socket.connect("tcp://localhost:5555")  
#socket.connect("ipc:///tmp/tstamp.pipe")
socket.connect("ipc:///tmp/histogram2.pipe")
# subscription filter, "" means subscribe to everything
socket.setsockopt(zmq.SUBSCRIBE, "") 

fig = plt.figure()
ax1 = fig.add_subplot(1,2,1)
ax2 = fig.add_subplot(1,2,2)

mng = plt.get_current_fig_manager()
mng.resize(*mng.window.maxsize())

#  interval is milliseconds
#a = animation.FuncAnimation(fig,animate, interval=1000)
plt.show(block=False)

#hbins = [1,2,3]
#hvals = [4,5,6]

ftrap = float(2*math.pi*12000048) / float(1e9)

def sinfunc(x,a,phi,b):
    return b+a*numpy.sin( numpy.multiply(ftrap,x)-numpy.multiply(ftrap,phi))

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
		
		histmax = float(h.s + float(h.ps)/1e12) # rightmost bin of histogram
		
		hist_time = 30.0
		binwidth = histmax  / (h.bins)
		hbins = [x*binwidth for x in range(h.bins)]
		hvals = h.hist
		#print hvals
		# normalize
		hsum = sum(hvals)
		hvals = [float(x)/hsum for x in hvals]
		assert( len(hvals) == len(hbins) )
		
		#b0 = 4000
		#a0 = 1000
		#t0 = 10
		# hbins in units of nanoseconds!
		#fitpars, covmat = curve_fit(sinfunc,hbins, hvals, p0=[a0,t0,b0])
		
		#print sum(h.hist)
		#print hvals
		ft_raw = numpy.fft.fft(hvals[50:] - numpy.mean(hvals[50:]) )
		#ft = numpy.fft.fftshift(ft_raw)
		ft = ft_raw
		sample_interval = binwidth
		faxis =  numpy.fft.fftfreq( len(ft), d=sample_interval)
		# update plot
		ax2.clear()
		ax1.clear()
		##print "cleared"
		#ax1.semilogy(hbins,[float(h)/len(hvals) for h in hvals],'o')
		ax1.plot(hbins, hvals,'.')
		
		#plt.plot(hbins, sinfunc(hbins, fitpars[0], fitpars[1],fitpars[2]), 'r-')
		#fit_text = "Fit = A*sin( 2*pi*f*(t-t0) ) + B \n A=%.0f t0=%.1f ns B=%.0f" % (fitpars[0], fitpars[1], fitpars[2])
		
		ax1.set_xlabel('Time (s)')
		ax1.set_ylabel('Counts (a.u.)')
		ax1.set_title('second order correlation')
		ax1.set_ylim((0,0.02))
		
		ax2.set_xlabel('Frequency (Hz)')
		ax2.set_ylabel('abs(FFT)')
		ax2.set_title('FFT')
		ax2.grid()
		print "plot histogram2, counts= %d, max_count=%f" % ( sum(h.hist), max(hvals) )
		#print " max count ", max(hvals)
		#ftext = "histogram-gate = 30 s, histogram_count = %d f_count = %.2f Hz" % (sum(hvals), float(sum(hvals))/hist_time)
		#plt.text(5, 550, ftext )
		#plt.text(5, 1050, fit_text , color='r',size='32')
		#plt.legend(('Data', 'Fit'))
		#plt.text(5, 580, "a_mod = 50 mVpp, f_mod = 12 MHz" )
		ax1.set_ylim((0,0.0025))
		
		ax2.set_ylim((0.001,100))
		#plt.xlim((0,1e9*histmax))
		ax2.semilogy(faxis, abs(ft),'r.')
		#ax2.set_xlim((0.0, 1/(2*float(binwidth))))
		
		#ax2.set_ylim((0,0.02))
		plt.draw()
		filename = "12meg_frame_%03d" % nframe
		nframe = nframe+1
		#plt.savefig(filename)



if __name__ == "__main__":
	run()




