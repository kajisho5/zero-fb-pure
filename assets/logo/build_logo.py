# ZERO-FB PURE のロゴ一式を作る。記号（開いたループ）は式から作り、文字は DM Sans（OFL）の輪郭を元にする。
import math
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.boundsPen import BoundsPen

INK="#0A0A0C"; TEAL="#18E0D0"; PAPER="#F4F4F1"

def mark_path(gap=34.0, wall=40.0, ox=0.0, oy=0.0, s=1.0):
    """角丸の四角い輪（外 32〜224・半径 60）を、右上の角で斜め 45° に幅 gap だけ切る。"""
    R=60.0; r=R-wall+4.0            # 内側の角の半径（壁 40 のとき 24）
    o0,o1=32.0,224.0; i0,i1=o0+wall,o1-wall
    d=gap/2*math.sqrt(2)            # x + y = 256 ± d の 2 本の線で切る
    def hit(lo,hi,rad,c):
        """輪郭（lo〜hi の角丸四角・角の半径 rad）と線 x + y = c の、右上の角まわりの交点。
        戻り値は (点, 種類)。種類は 'top'（上の辺）・'arc'（角の弧）・'right'（右の辺）。"""
        cx,cy=hi-rad,lo+rad
        if c-lo<=cx: return (c-lo,lo),'top'
        if c-hi>=cy: return (hi,c-hi),'right'
        k=c-cy; A=2.0; B=-2*(cx+k); C=cx*cx+k*k-rad*rad
        disc=math.sqrt(B*B-4*A*C); xs=[(-B+disc)/(2*A),(-B-disc)/(2*A)]
        pts=[(x,c-x) for x in xs if x>=cx-1e-6 and (c-x)<=cy+1e-6]
        return (max(pts) if c>256 else min(pts)),'arc'
    (P1,k1),(P2,k2)=hit(o0,o1,R,256-d),hit(o0,o1,R,256+d)
    (Q1,j1),(Q2,j2)=hit(i0,i1,r,256-d),hit(i0,i1,r,256+d)
    T=lambda x,y:(ox+x*s,oy+y*s)
    f=lambda p:"%.2f %.2f"%T(*p)
    a=lambda rad,sw,p:"A%.2f %.2f 0 0 %d %s"%(rad*s,rad*s,sw,f(p))
    out="M"+f(P2)
    if k2=='arc': out+=a(R,1,(o1,o0+R))                      # 外側：切り口の下の端から時計回り
    out+="L"+f((o1,o1-R))+a(R,1,(o1-R,o1))+"L"+f((o0+R,o1))+a(R,1,(o0,o1-R))+"L"+f((o0,o0+R))+a(R,1,(o0+R,o0))
    if k1=='top': out+="L"+f(P1)
    else: out+="L"+f((o1-R,o0))+a(R,1,P1)
    out+="L"+f(Q1)                                            # 切り口を渡って内側へ
    if j1=='arc': out+=a(r,0,(i1-r,i0))
    out+="L"+f((i0+r,i0))+a(r,0,(i0,i0+r))+"L"+f((i0,i1-r))+a(r,0,(i0+r,i1))+"L"+f((i1-r,i1))+a(r,0,(i1,i1-r))
    if j2=='right': out+="L"+f(Q2)
    else: out+="L"+f((i1,i0+r))+a(r,0,Q2)
    return out+"Z"

SRC="/usr/share/fonts/truetype/google-fonts/ofl/dmsans/DMSans[opsz,wght].ttf"
def font(w): return instantiateVariableFont(TTFont(SRC),{"wght":w,"opsz":14})
BOLD,LIGHT=font(800),font(300)

def text_path(f,text,x,baseline,size,track=0.0):
    gs=f.getGlyphSet(); cmap=f.getBestCmap(); upm=f["head"].unitsPerEm; k=size/upm; out=[]; pen_x=x
    for ch in text:
        g=cmap[ord(ch)]; sp=SVGPathPen(gs)
        gs[g].draw(TransformPen(sp,(k,0,0,-k,pen_x,baseline)))
        out.append(sp.getCommands()); pen_x+=gs[g].width*k+track
    return " ".join(out), pen_x-track

def cap_height(f): return f["OS/2"].sCapHeight/f["head"].unitsPerEm

def wordmark(x,baseline,size,col_main,col_pure):
    p1,e1=text_path(BOLD,"ZERO-FB",x,baseline,size,size*0.02)
    p2,e2=text_path(LIGHT,"PURE",e1+size*0.30,baseline,size,size*0.10)
    return f'<path fill="{col_main}" d="{p1}"/><path fill="{col_pure}" d="{p2}"/>', e2

def svg(w,h,body,title="ZERO-FB PURE"): return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w:.0f} {h:.0f}" width="{w:.0f}" height="{h:.0f}"><title>{title}</title>{body}</svg>\n'
def save(name,s): open(name,"w",encoding="utf-8").write(s)

# 記号（色つき・小さいサイズ用）
save("zfp-symbol.svg", svg(256,256,f'<path fill="{INK}" d="{mark_path()}"/>'))
save("zfp-symbol-small.svg", svg(256,256,f'<path fill="{INK}" d="{mark_path(gap=50,wall=46)}"/>',"ZERO-FB PURE (small sizes)"))
save("zfp-symbol-teal.svg", svg(256,256,f'<path fill="{TEAL}" d="{mark_path()}"/>'))

# 横組み：記号の高さ 192、文字の大文字の高さを記号の内側の高さ（112/192）に近づける
def horizontal(main,pure,markcol,name,bg=None):
    size=150.0; cap=cap_height(BOLD)*size; base=128+cap/2
    w_body,end=wordmark(272,base,size,main,pure)
    W=end+32
    body=(f'<rect width="{W:.0f}" height="256" fill="{bg}"/>' if bg else "")+f'<path fill="{markcol}" d="{mark_path()}"/>'+w_body
    save(name,svg(W,256,body)); return W
horizontal(INK,INK,INK,"zfp-horizontal.svg")
horizontal("#E8E8EC",TEAL,TEAL,"zfp-horizontal-on-dark.svg",bg=INK)
horizontal("#E8E8EC",TEAL,TEAL,"zfp-horizontal-dark-transparent.svg")

# 縦組み
def stacked(main,pure,markcol,name,bg=None):
    size=74.0; _,end=wordmark(0,0,size,main,pure); tw=end; W=max(tw+64,320); x=(W-tw)/2
    w_body,_=wordmark(x,330,size,main,pure)
    body=(f'<rect width="{W:.0f}" height="372" fill="{bg}"/>' if bg else "")+f'<path fill="{markcol}" d="{mark_path(ox=(W-208)/2-26,oy=-6,s=208/192)}"/>'+w_body
    save(name,svg(W,372,body))
stacked(INK,INK,INK,"zfp-stacked.svg"); stacked("#E8E8EC",TEAL,TEAL,"zfp-stacked-on-dark.svg",bg=INK)

# アプリのアイコン（黒い面に青緑の記号）
save("zfp-app-icon.svg", svg(256,256,f'<rect width="256" height="256" rx="56" fill="{INK}"/><path fill="{TEAL}" d="{mark_path(ox=25.6,oy=25.6,s=0.8)}"/>'))
print("ok")
