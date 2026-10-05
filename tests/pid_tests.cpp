#include "pid.hpp"
#include <cassert>
#include <cmath>
#include <limits>

static bool near(float a, float b) { return std::fabs(a-b) < 1e-4f; }
int main() {
    PID p(2, 1, .5f, 100, 3);
    p.ref=2; assert(p.UpdateResult(.5f));
    assert(near(p.result,7)); // 4 + 1 + 2
    assert(p.UpdateResult(.5f)); assert(near(p.result,6));
    for(int i=0;i<20;++i) assert(p.UpdateResult(.5f));
    assert(near(p.iResult,3)); assert(near(p.result,7));
    p.maxOut=1; assert(p.UpdateResult(.5f)); assert(near(p.result,1));
    p.ref=-100; assert(p.UpdateResult(.5f)); assert(near(p.result,-1));
    assert(near(p.iResult,-3));
    p.ref=std::numeric_limits<float>::quiet_NaN();
    assert(!p.UpdateResult(.1f)); assert(p.result==0);
    assert(!p.UpdateResult(0));
    p.kp=std::numeric_limits<float>::infinity(); assert(!p.UpdateResult(.1f));
    PID pos(.2f,.3f,.04f,10000,100), inc(.2f,.3f,.04f,10000,100,PID_DELTA);
    for(int i=0;i<100;++i) {
        pos.ref=inc.ref=std::sin(i*.1f);
        pos.fdb=inc.fdb=.2f*std::cos(i*.2f);
        const float dt=(i%2==0)?.01f:.02f;
        assert(pos.UpdateResult(dt)); assert(inc.UpdateResult(dt));
        assert(near(pos.result,inc.result));
    }
    inc.maxIOut=.1f; inc.ref=100; inc.kd=0;
    for(int i=0;i<100;++i) assert(inc.UpdateResult(.1f));
    assert(near(inc.iResult,.1f));
    inc.Clear(); assert(inc.result==0 && inc.iResult==0 && inc.err[0]==0);
}
