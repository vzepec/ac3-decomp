#!/usr/bin/env python3
"""interp_dark — pixeles de las imagenes intermedias mucho mas oscuros que en las dos reales.

Lee las capturas de native_main con AC3_INTERP=3, AC3_INTERP_FROM y AC3_INTERP_LEN
(i<paso>_<k>-4.ppm) y cuenta, en cada intermedia del paso s, los pixeles cuya luminancia es mas
de 60 niveles menor que la menor de las reales s-1 y s en ese pixel: puntos y astillas oscuras
(caras de ladera, HUD deformado). Como ruido de la medida, lo mismo con tres reales seguidas.
Requiere numpy y Pillow.

Uso:
    python3 tools/interp_dark.py <dir_capturas>
"""
import sys,glob,os,re,numpy as np
from PIL import Image
d=sys.argv[1]
F={}
for p in glob.glob(d+'/i*_*-*.ppm'):
    m=re.match(r'i(\d+)_(\d+)-(\d+)',os.path.basename(p)); F[(int(m[1]),int(m[2]),int(m[3]))]=p
L=lambda p: np.asarray(Image.open(p).convert('L'),dtype=np.int16)
steps=sorted({s for s,k,dd in F}); tot=0;n=0;worst=(0,None); base=0;nb=0
for s in steps:
    if (s,4,4) not in F or (s-1,4,4) not in F: continue
    a=L(F[(s-1,4,4)]); c=L(F[(s,4,4)])
    for k in (1,2,3):
        if (s,k,4) not in F: continue
        b=L(F[(s,k,4)]); x=int(((np.minimum(a,c)-b)>60).sum()); tot+=x;n+=1
        if x>worst[0]: worst=(x,'%d_%d'%(s,k))
    if (s+1,4,4) in F:
        e=L(F[(s+1,4,4)]); base+=int(((np.minimum(a,e)-c)>60).sum()); nb+=1
print('intermedias %d: %.1f px oscuros por imagen (peor %s); reales: %.1f'%(n,tot/max(n,1),worst,base/max(nb,1)))
