import struct,sys,os
P='/Users/segrob/git/swchess/original/win3x/cd/BBWB.ANX'
d=open(P,'rb').read()
n=struct.unpack_from('<I',d,0)[0]
print('frame count @0x00 =',n)
offs=[struct.unpack_from('<I',d,4+4*i)[0] for i in range((0x70c-4)//4)]
print('table slots=',len(offs))
print('first 12 offs:',[hex(x) for x in offs[:12]])
print('offs[n-1..n+3]:',[hex(x) for x in offs[n-1:n+4]])
print('unique offs in first n:',len(set(offs[:n])))
tail=set(offs[n:]); print('distinct tail values:',[hex(x) for x in list(tail)[:5]],'count',len(tail))
BASE=0x70c
def hdr(o):
    a=BASE+o
    sz,w,h,pl,bc=struct.unpack_from('<IiiHH',d,a)
    comp,szimg,xp,yp,clru,clri=struct.unpack_from('<IIiiII',d,a+16)
    return dict(at=a,sz=sz,w=w,h=h,pl=pl,bc=bc,comp=comp,szimg=szimg,clru=clru)
h0=hdr(offs[0]); print('frame0 hdr:',{k:(hex(v) if k in("at","comp") else v) for k,v in h0.items()})
# survey all distinct records
uniq=sorted(set(offs[:n]))
bad=0
for o in uniq:
    hh=hdr(o)
    if hh['sz']!=40 or hh['bc']!=8 or hh['pl']!=1: bad+=1; print('ODD',hex(o),hh)
    if hh['szimg']!=hh['w']*hh['h']: print('szimg!=w*h',hex(o),hh)
print('distinct records',len(uniq),'bad',bad)
