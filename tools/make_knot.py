# Usage: python tools/make_knot.py models/torus_knot.obj
import math, sys
P, Q = 2, 3
N, M = 720, 32          # segments along the knot, around the tube
R_TUBE = 0.38
def curve(t):
    r = 2 + math.cos(Q * t)
    return (r * math.cos(P * t), -math.sin(Q * t), r * math.sin(P * t))  # y = up
def sub(a,b): return tuple(x-y for x,y in zip(a,b))
def add(a,b): return tuple(x+y for x,y in zip(a,b))
def mul(a,s): return tuple(x*s for x in a)
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def cross(a,b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def norm(a): l = math.sqrt(dot(a,a)); return mul(a, 1/l)
pts = [curve(2*math.pi*i/N) for i in range(N)]
tans = [norm(sub(pts[(i+1)%N], pts[i-1])) for i in range(N)]
# parallel-transport frame
n = norm(cross(tans[0], (0,1,0.3)))
frames = []
for i in range(N):
    t = tans[i]
    n = norm(sub(n, mul(t, dot(n, t))))
    frames.append((n, cross(t, n)))
n_end = norm(sub(frames[-1][0], mul(tans[0], dot(frames[-1][0], tans[0]))))
twist = math.atan2(dot(cross(n_end, frames[0][0]), tans[0]), dot(n_end, frames[0][0]))
out = open(sys.argv[1], "w")
out.write("# (2,3) torus knot, generated\n")
for i in range(N):
    nrm, bin_ = frames[i]
    extra = twist * i / N
    for j in range(M):
        a = 2*math.pi*j/M + extra
        d = add(mul(nrm, math.cos(a)), mul(bin_, math.sin(a)))
        p = add(pts[i], mul(d, R_TUBE))
        out.write("v %.5f %.5f %.5f\n" % p)
        out.write("vn %.5f %.5f %.5f\n" % d)
def idx(i, j): return (i % N) * M + (j % M) + 1
for i in range(N):
    for j in range(M):
        a, b, c, d = idx(i,j), idx(i+1,j), idx(i+1,j+1), idx(i,j+1)
        out.write("f %d//%d %d//%d %d//%d\n" % (a,a,c,c,b,b))
        out.write("f %d//%d %d//%d %d//%d\n" % (a,a,d,d,c,c))
out.close()
