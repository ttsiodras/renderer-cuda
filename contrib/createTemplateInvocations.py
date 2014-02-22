def h(p):
    if p=="!": return "false"
    else: return "true"

op = ("!","")
for a in op:
    for b in op:
	for c in op:
	    for d in op:
		for e in op:
		    print "if (%sg_bUseSpecular && %sg_bUsePhongInterp && %sg_bUseReflections && %sg_bUseShadows && %sg_bUseAntialiasing) {" % (a,b,c,d,e)
		    print "   PAINT(", h(a), ",", h(b), ",", h(c), ",", h(d), ",", h(e), ")"
		    print "} else",
