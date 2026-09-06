import struct,glob,os,zlib,collections,sys
CD='/Users/segrob/git/swchess/original/win3x/cd'
OUT='/private/tmp/claude-501/-Users-segrob-git-swchess/1acda6bc-a3ae-4c95-bdad-9a3280d9aaba/scratchpad/fable-review/frames'
BASE=0x70c
def png(path,w,h,rgb_rows):
    raw=b''.join(b'\x00'+r for r in rgb_rows)
    def ch(t,b): c=t+b; return struct.pack('>I',len(b))+c+struct.pack('>I',zlib.crc32(c))
    open(path,'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+ch(b'IDAT',zlib.compress(raw,6))+ch(b'IEND',b''))
def decode_record(d,a,end):
    sz,w,h,pl,bc=struct.unpack_from('<IiiHH',d,a)
    comp,szimg,xp,yp,clru,clri=struct.unpack_from('<IIiiII',d,a+16)
    esc=comp>>16; pal=d[a+40:a+40+clru*4]; i=a+40+clru*4
    need=w*h; out=bytearray()
    while len(out)<need and i<end:
        b=d[i]; i+=1
        if b==esc:
            v=d[i]; c=d[i+1]; i+=2; out+=bytes([v])*c
        else: out.append(b)
    tail=d[i:end]
    return dict(w=w,h=h,esc=esc,szimg=szimg,stride=(w+3)&~3,pal=pal,px=bytes(out),ok_len=len(out)==need,tail=tail,tail_zero=all(t==0 for t in tail),consumed=i-a)
def load(p):
    d=open(p,'rb').read(); n=struct.unpack_from('<I',d,0)[0]
    offs=[struct.unpack_from('<I',d,4+4*i)[0] for i in range(n)]
    uniq=sorted(set(offs)); starts=[BASE+o for o in uniq]+[len(d)]
    recs={}
    for k,o in enumerate(uniq): recs[o]=decode_record(d,BASE+o,starts[k+1])
    return n,offs,recs
def to_rgb_rows(r):
    pal=r['pal']; px=r['px']; w=r['w']; h=r['h']
    rows=[b''.join(bytes((pal[p*4+2],pal[p*4+1],pal[p*4])) for p in px[y*w:(y+1)*w]) for y in range(h)]
    rows.reverse(); return rows
def sheet(p,name,maxframes=None):
    n,offs,recs=load(p)
    idx=list(range(n)) if maxframes is None else list(range(0,n,max(1,n//maxframes)))[:maxframes]
    cw=max(recs[o]['w'] for o in recs); chh=max(recs[o]['h'] for o in recs)
    cols=10; rowsn=(len(idx)+cols-1)//cols
    W=cols*cw; H=rowsn*chh
    canvas=[bytearray(b'\x30\x30\x30'*W) for _ in range(H)]
    for j,fi in enumerate(idx):
        r=recs[offs[fi]]; rr=to_rgb_rows(r); cx=(j%cols)*cw; cy=(j//cols)*chh
        for y,row in enumerate(rr):
            canvas[cy+chh-r['h']+y][cx*3:(cx+r['w'])*3]=row
    png(os.path.join(OUT,name),W,H,[bytes(c) for c in canvas])
    print('sheet',name,W,'x',H,'frames',len(idx))
if __name__=='__main__':
    tot=0; badlen=0; badtail=0; longtail=0; escs=collections.Counter(); esc_in_px=0; reuse=0; tl=0
    for p in sorted(glob.glob(CD+'/*.ANX')):
        n,offs,recs=load(p); tl+=n; reuse+=n-len(recs)
        for o,r in recs.items():
            tot+=1; escs[r['esc']]+=1
            if not r['ok_len']: badlen+=1; print('LEN',os.path.basename(p),hex(o),r['w'],r['h'],len(r['px']))
            if not r['tail_zero']: badtail+=1; print('TAIL',os.path.basename(p),hex(o),r['tail'][:16].hex())
            if len(r['tail'])>31: longtail+=1; print('LONGTAIL',os.path.basename(p),hex(o),len(r['tail']))
            if r['esc'] in r['px']: esc_in_px+=1
            if r['szimg']!=r['stride']*r['h']: print('SZIMG',os.path.basename(p),hex(o),r['szimg'],r['stride']*r['h'])
    print('records',tot,'timeline',tl,'timeline entries reusing a record',reuse,'esc values',dict(escs))
    print('wrong length',badlen,'nonzero tail',badtail,'tail>31',longtail,'escape index appears in pixels',esc_in_px)
    sheet(CD+'/BBWB.ANX','bbwb_all.png')
    sheet(CD+'/BKWB.ANX','bkwb_all.png')
    sheet(CD+'/WBBR.ANX','wbbr_sample.png',30)
