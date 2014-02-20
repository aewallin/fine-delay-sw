
#pragma once


// yet another time-stamp class
class TS {
public:
	int64_t s;
	int64_t ps;
	int32_t id;
	TS() {s=0;ps=0;id=0;};
	TS(int64_t sec, int64_t pico, int32_t tid=0) { // fixme: use initializer list
		s = sec;
		ps = pico;
		id = tid;
	};
	// http://www.cs.caltech.edu/courses/cs11/material/cpp/donnie/cpp-ops.html
	// subtract t
	TS &operator-=(const TS &t) {
		//std::cout << " first " << s << "." << ps << "\n";
		//std::cout << " second" << t.s << "." << t.ps << "\n";
		//printf("  first: %lli.%012lli \n", s, ps );
		//printf(" second: %lli.%012lli \n", t.s, t.ps );
	    if (ps >= t.ps) {
			ps -= t.ps;
			s -= t.s;
		} else {
			ps = 1e12 + ps - t.ps;
			s  -= t.s+1;
		}
		
		//printf("   diff: %lli.%012lli \n", s, ps );
		return *this;
	};
	
	const TS operator-(TS &t) const {
		return TS(*this) -= t;
	};
	bool operator>(const TS &t) const {
		//printf(">  first: %lli.%012lli %d %d\n", s, ps , s>t.s, ps>t.ps);
		//printf("> second: %lli.%012lli \n", t.s, t.ps );
		if (s > t.s) {
			return true;
		} else if (s < t.s) {
			return false;
		}
		
		assert( s == t.s );
		//assert( 0 );
		if ( ps >= t.ps)
			return true;
		else
			return false;
	};
	TS &operator=(const TS &t) {
			if (this == &t)
			return *this;
		s=t.s;
		ps=t.ps;
		return *this;
	}
};
