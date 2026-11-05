// strikers-quest r149: the autographs' handwriting model data (xr_sign.cpp; C:\StrikersMR\tools\outside\sign_proof.py
// reads the same blocks for the PC proof). Hand-authored control points, no fonts.
//
// GLYPHS: "<set><char> <advance> <flags> <strokes>". Units: x-height 10, baseline 0, capitals / ascenders 20,
// descenders -9, y UP. Sets: c = cursive (a word's letters join: a glyph's first point is where the pen arrives, its
// last point the exit the next letter's join leaves from), p = print (and BLOCK = print capitals drawn as straight
// segments), x = specials a style maps a character to. Flags: j = joins on to the next letter, n = the pen lifts
// before it (no join in: a joined c reads as an e), - = neither.
// Strokes are split by " / ". A point "x,y", "x,y!" = a corner (the curve does not round it). A stroke's first token
// may be a kind: "." the i / j tittle (a style's dot: dot, circle, HEART, star ...), "-" a t / f / x cross (cursive
// writers add it after the word), "*" a plain dot, "O" filled discs "x,y,diameter".
static const char kGlyphData[] = R"GLY(
# ---- cursive lowercase
ca 12 j 8,9 5.5,10.2 2.5,9 0.8,5.5 1.2,1.8 3.5,0 6,1 8,4.5 8.8,10! 8.6,5 9,1.3 10.5,0 12,2.5
cb 11 j 0.6,4 3.5,12 5,17 4.6,20 3,20.4 2,18.5 2,13 2.3,6 2.5,1 4.5,0 7.5,2 8.2,6 7,9.3 5,9 5.6,8 8.5,8.8 11,9
cc 9.5 jn 6.4,8.4 7.4,9.6 6.2,10.4 3.8,10 1.6,8 0.8,4.4 2,1.2 4.6,0 7.4,0.8 9.5,2.6
cd 12 j 8,9 5.5,10.2 2.5,9 0.8,5.5 1.2,1.8 3.5,0 6,1 8,4.5 9.2,13 9.6,20! 9,12 8.8,5 9.2,1.3 10.6,0 12,2.5
ce 9 j 0.8,3.2 4.5,5 7,7.2 6.8,9.6 4.8,10.2 2.4,8.7 0.8,5.2 1.2,1.8 3.4,0.1 6.2,0.4 9,2.6
cf 7 j 0.8,4 3.5,12 5,17 4.8,20 3.4,20.5 2.5,19 2.3,13 2.2,4 2.1,-4 2.4,-8.4 3.4,-9 4.2,-7.6 3.8,-3.6 2.6,0.2! 4.5,0.6 7,2.6
cg 11 j 8,9 5.5,10.2 2.5,9 0.8,5.5 1.2,1.8 3.5,0 6,1 8,4.5 8.8,10! 8.6,2 8.2,-4.5 7,-8.4 5,-9.2 3.5,-8 4.2,-5.4 6.8,-2.4 9,-0.2 11,2.4
ch 11 j 0.6,4 3.5,12 5,17 4.6,20 3,20.4 2,18.5 2,13 2.2,6 2.4,0! 2.8,4.5 4.4,8.6 6.4,9.8 8,8.6 8.2,5 8.2,1.5 9.2,0 11,2.5
ci 6 j 2.2,10! 1.8,4 2.2,1 3.5,0 6,2.5 / . 2.6,14.5
cj 6 j 2.4,10! 2.2,2 1.8,-5 0.6,-8.8 -1.2,-9.2 -2.2,-7.8 -0.8,-5 2.5,-2 5.5,1.8 / . 2.8,14.5
ck 11 j 0.6,4 3.5,12 5,17 4.6,20 3,20.4 2,18.5 2,13 2.2,6 2.4,0! 2.8,4 5,8 7.5,9.5 8.6,8 7,5.5 3.5,4.6! 6,3.5 7.6,1 9,0 11,2.5
cl 7 j 0.6,4 3.5,12 5,17 4.6,20 3,20.4 2,18.5 2,13 2.2,6 2.6,1.2 4,0 7,2.5
cm 15.5 j 0.6,6.5 1.6,9.4 3,10 4,8.6 4,5 3.9,0! 4.4,5 5.6,9 7.3,10 8.6,8.6 8.6,4.5 8.5,0! 9,5 10.3,9 12,10 13.2,8.4 13.2,4 13.4,1 14.2,0 15.5,2.5
cn 11.2 j 0.6,6.5 1.6,9.4 3,10 4,8.6 4,5 3.9,0! 4.4,5 5.6,9 7.3,10 8.6,8.6 8.6,4 8.8,1 9.8,0 11.2,2.5
co 10 j 6.5,9.6 4.2,10.2 1.8,8.6 0.6,5 1.3,1.5 3.6,0 6.2,0.8 7.6,4 7.4,8 6,10 5.2,9.2 7.2,8.8 10,9
cp 11 j 0.5,6 2.2,10! 2,2 1.8,-9! 2,-2 2.6,4 4.4,8.6 6.6,10 8.6,8.4 8.8,4.4 7.4,1 5,0 3,0.8 5.6,0.6 8.4,1 11,2.6
cq 12 j 8,9 5.5,10.2 2.5,9 0.8,5.5 1.2,1.8 3.5,0 6,1 8,4.5 8.8,10! 8.6,2 8.4,-8.6! 9.6,-5 10.6,-1.2 12,2.5
cr 9.5 j 0.8,6 2.2,10.5! 3.4,9 5.4,9.6! 5.6,5 6,1 7.4,0 9.5,2.5
cs 8.5 j 0.6,6 3.4,10.4! 5.6,6.5 6.4,3 5,0.4 2.6,0.2 1.4,1.4! 3,1 5.5,0.6 8.5,2.5
ct 8 j 0.6,6 3,16! 2.6,6 2.8,1.2 4,0 6,0.8 8,2.5 / - 0.2,10.5 6.5,11
cu 11.6 j 0.6,6.5 2.2,10! 1.8,4 2.4,1 4.4,0 6.6,1.2 8.2,4.6 8.6,10! 8.4,4 8.8,1 10,0 11.6,2.5
cv 10 j 0.6,6.5 2,10! 3,5 4.8,0.4! 6.6,4 7.8,8 8,10 6.6,9.6 8.4,9 10,9.2
cw 14 j 0.6,6.5 2,10! 2,4 3.4,0.4 5.4,0.8 6.8,4.5 7.2,10! 7.6,4 9,0.6 11,1 12.2,5 12.2,10 11,9.6 12.4,9 14,9.2
cx 10 j 0.4,6.4 2.4,9.8 3.8,9.6 5,7 6.4,2 7.6,0.2 10,2.5 / - 8,10 1.6,0
cy 11 j 0.6,6.5 2.2,10! 1.8,4 2.4,1 4.4,0 6.6,1.2 8.2,4.6 8.6,10! 8.4,2 8,-4.5 6.8,-8.4 4.8,-9.2 3.3,-8 4,-5.4 6.6,-2.4 9,-0.2 11,2.4
cz 9.5 j 1,9 3.5,10.2 6.5,9.6! 1.2,0.4! 4.5,1.2 7.5,0.2 9.5,2.4
# ---- cursive capitals (signature capitals: big, loopy)
cA 15.5 j 0,0 3.5,9 7,17 8.6,20! 10.4,11 12.2,0.6! 8.5,5.2 3,6.6 1.6,5.6 3.5,4.8 9,6 13,5.4 15.5,2.5
cB 14 - 1.5,18 3.2,20 3.6,17! 3.3,9 3,0.3! 3.3,9 3.8,17 5.8,20.2 9.4,19.4 10.8,16.4 9.6,12.6 6,10.8 5,10.2 6.2,9.8 10,9.2 12,6 11.4,2 7.8,0 4,0.4 2,1.6 2.6,2.6 6,1.4 11,1 14,2
cC 13 j 12,16 11.5,19.2 9,20.4 5,19 1.8,14.5 0.8,8 2.4,2.5 6,0 10,0.8 13,3
cD 15 - 6,19 6.2,10 5.6,0.6 3.6,0 2,1 3,2.2 6,1 10.5,1.5 13.5,6 13.4,13 10.6,18.4 6,20.4 2,19 0.8,16.6 2.4,15.6
cE 12.5 j 11,17.5 9.8,19.8 6.6,20.4 3.6,18.6 3.4,15 5.6,11.8 7.4,10.8 5.5,10.6 2,9 0.8,5 2.2,1.2 5.6,0 9.6,0.8 12.5,3
cF 13 - 1.6,17.6 4.6,19.6 9,19 13.4,20.4 / 8.6,19.6 8,10 7.2,2 6.4,0.2 5,0.4 / 4.6,10.4 11,10.8
cG 13.5 j 11.5,16.5 10.8,19.6 8,20.4 4.2,18.4 1.6,13 1.2,6 3,1.4 6.4,0 9.4,1.4 11,6 11.2,10! 11,2 10.4,-5 8.8,-8.6 6.4,-9.2 5,-7.6 6.4,-4.6 9.4,-1.6 12,1 13.5,2.5
cH 15 j 0.8,18.4 3.6,20! 3.4,10 2.8,0.3 / 1,9 7,10.8 12.6,10.6 / 12.4,20.2 11.8,10 11.6,1.8 12.6,0.1 15,2.5
cI 12.5 j 2,17.6 4.6,20 9,20.4 12,19.8 / 7.6,20.2 7,10 6.4,2 7.4,0 9.6,0.6 12,2.5
cJ 13 j 6,13 7.6,18 9.6,20.4 10.6,18.6 9.6,12 8.4,4 7.2,-4 5.2,-8.6 2.8,-9.2 1.6,-7.4 3.6,-4.6 8.4,-1.6 12,1.4 13,2.5
cK 14.5 j 1.6,17.8 4.2,20.2 4.8,17! 4.2,9 3.6,0.3 / 13.2,19.6 11,17 6.8,12 4.8,10.2 6.6,10.6 8.4,8.6 9.4,3.6 10.6,0.6 12.6,0.4 14.5,2.5
cL 13.5 j 7.5,15 9.6,19 9.2,20.4 7.4,19.4 6,14 4.8,6 3,1.4 0.8,0.4 0.6,1.8 3,1.6 6,0.4 9,0 11.6,0.6 13.5,2.5
cM 15 j 0.6,0 1.6,10 2.4,17 4,20.2 5.6,17 6,9 5.8,0.4! 6.8,10 8.6,18.8 10.4,20.2 11.6,17 11.8,9 12,2 13,0 15,2.5
cN 13 - 0.6,0 1.8,11 3,18.4 4.6,20.2 6,17 7,8 8.4,1 9.8,0.2 11,4 11.8,12 12.6,20 13.4,21
cO 13 - 8,18.5 6,20.2 3,18.6 1,13.6 0.8,7 2.8,1.6 6.4,0 9.8,1.6 11.4,7 10.8,14 8.6,19 6.4,20.4 4.6,19.6 6,18 9.6,18.6 13,19.8
cP 12.5 - 3,18 4.4,20.2 4.8,17.6! 4.4,9 3.6,0.3! 4.2,9 5,16 7,19.8 10.6,19.6 12.2,16 11,12 7.6,9.8 4.4,10.2 2.2,11.6
cQ 14.5 - 8,18.5 6,20.2 3,18.6 1,13.6 0.8,7 2.8,1.6 6.4,0 9.8,1.6 11.4,7 10.8,14 8.6,19 6.4,20.4 4.6,19.6 / 6,4.4 9,1.8 12.4,0 14.5,1.5
cR 15 j 3,18 4.4,20.2 4.8,17.6! 4.4,9 3.6,0.3! 4.2,9 5,16 7,19.8 10.6,19.6 12.2,16 11,12 7.6,9.8 5,10! 8,8 9.6,3.4 11,0.6 13,0.6 15,2.5
cS 14 j 12,17.6 11,19.8 8,20.4 5,19 3.8,16 5,12.8 8.6,10 10.6,6 9.6,2 6.4,0 3,0.4 1,2 2.4,3.2 6,2 11,1 14,2.5
cT 12 j 1,17.6 3.6,19.8 8,19.4 13.6,20.6 / 8.2,19.6 7.6,10 7,2 8,0 10,0.8 12,2.5
cU 15 j 0.6,16 2.4,19.8 3.6,18! 3,10 3.4,3 5.6,0 8.6,0.6 10.6,4.4 11.6,12 12.2,20! 12,10 12.2,2 13.2,0 15,2.5
cV 15 - 0.6,16 2.4,19.8 3.6,18! 4.6,10 6.6,0.4! 9,10 11.2,17 13.6,20.4 15,19.6
cW 18.6 - 0.6,16 2.2,19.8 3.2,17.6! 3.8,9 5,0.4! 7.6,10 9,15! 10.4,8 12,0.4! 13.8,10 15.4,17 17.4,20.4 18.6,19.6
cX 14.5 j 12,19.6 7,11 1.6,0.6 / 1.2,18.4 3.6,20 5.8,16 8,6 10,1 12.4,0 14.5,2.5
cY 14.5 j 0.6,16 2.4,19.8 3.6,18! 3.2,12 4.6,8.4 7.6,8 10.6,10.8 11.8,20! 11.4,8 10.6,-2 9,-7.6 6.4,-9.2 4.8,-7.6 6.6,-4.4 10.4,-1.2 13,1.2 14.5,2.5
cZ 15 j 1.4,17.4 4.6,20 9.5,19.4 14.6,20.2! 6.6,9.6 0.6,0.6! 5,1.8 9,0.4 12.6,1 15,2.5
# ---- print lowercase
pa 10 - 8,8.4 6.4,10 3.6,9.8 1.4,7.2 1,3.6 2.6,0.6 5,0 7.2,1.6 8.4,5 / 8.6,10 8.4,0
pb 10 - 1.5,20 1.4,0 / 1.6,5.6 3.6,9.2 6.4,10 8.6,7.6 8.8,3 6.6,0.2 3.8,0.4 1.6,2.6
pc 9 - 8,8.4 6,10 3.4,9.6 1.2,6.8 1,3.4 2.8,0.6 5.4,0 8,1.6
pd 10 - 8,8.4 6.4,10 3.6,9.8 1.4,7.2 1,3.6 2.6,0.6 5,0 7.2,1.6 8.4,5 / 8.6,20 8.4,0
pe 9.5 - 1,5 8.6,5.4 8.4,8 6.4,10 3.4,9.6 1.2,6.6 1,3.2 2.8,0.6 5.4,0 8,1.4
pf 7.5 - 7.6,18.6 6.2,20 4.2,19.6 3.2,17 3,0 / 0.4,10 6.6,10.2
pg 10 - 8,8.4 6.4,10 3.6,9.8 1.4,7.2 1,3.6 2.6,0.6 5,0 7.2,1.6 8.4,5 / 8.6,10 8.4,-5 7.2,-8.6 4.4,-9.2 1.6,-7.6
ph 10 - 1.5,20 1.4,0 / 1.6,5.6 3.4,9 6,10 8,8.4 8.4,5 8.4,0
pi 4.5 - 2,10 2,0 / . 2,14.5
pj 5 - 3,10 3,-5 2,-8.4 0,-9 -1.8,-7.6 / . 3,14.5
pk 9.5 - 1.5,20 1.4,0 / 8,10.2 1.6,4 / 3.8,5.6 8.6,0
pl 4.5 - 2,20 2,2 3,0 4.4,0.6
pm 14 - 1.4,10 1.4,0 / 1.5,6 3,9.4 5,10 6.6,8.4 6.8,0 / 6.9,6 8.4,9.4 10.4,10 12,8.4 12.2,0
pn 10 - 1.4,10 1.4,0 / 1.5,6 3.2,9.4 5.6,10 7.8,8.4 8.2,5 8.2,0
po 10.5 - 5,10 2.2,9 0.8,5 2,1 5,0 8,1 9.2,5 8,9 5,10 4.2,9.6
pp 10 - 1.4,10 1.4,-9 / 1.6,6.6 3.6,9.6 6.4,10 8.6,7.6 8.8,3 6.6,0.2 3.8,0.4 1.6,3
pq 10.5 - 8,8.4 6.4,10 3.6,9.8 1.4,7.2 1,3.6 2.6,0.6 5,0 7.2,1.6 8.4,5 / 8.6,10 8.5,-9 10,-7.6
pr 8 - 1.4,10 1.4,0 / 1.5,5.6 3,8.8 5.4,10 7.6,9.4
ps 9 - 7.6,8.6 5.6,10 2.6,9.8 1.2,7.8 2.4,5.8 6,4.4 7.8,2.4 6.8,0.4 3.6,0 0.8,1.4
pt 7 - 3.2,16 3,2 3.8,0.2 5.6,0.4 / 0.4,10 6.4,10.2
pu 10 - 1.4,10 1.4,3 2.6,0.6 4.8,0 7,1 8.2,4 / 8.4,10 8.4,0
pv 9 - 0.6,10 4.4,0! 8.4,10
pw 12.5 - 0.6,10 3.2,0! 6.2,8! 9.2,0! 11.8,10
px 9 - 0.8,10 8,0 / 8,10 0.8,0
py 9 - 0.6,10 4.4,0.6 / 8.4,10 4,-3 2.4,-7.4 0.4,-9
pz 9 - 0.8,10 8,10! 0.6,0! 8.4,0
# ---- print capitals (BLOCK = these as straight segments)
pA 13 - 0.6,0 6.5,20! 12.4,0 / 2.8,7 10.2,7
pB 12.5 - 1.5,20 1.5,0 / 1.5,20! 7,20 9.8,18.6 10.6,15.6 9.6,12 6.6,10.6 1.5,10.6! / 6.6,10.6 10.2,9 11.4,5.6 10.4,1.8 7,0 1.5,0
pC 13 - 12,16.4 10,19.4 7,20.2 3.6,19 1.2,14.6 0.8,8.6 2,3.4 5,0.4 8.6,0 12,2.6
pD 13.5 - 1.5,20 1.5,0 / 1.5,20! 6,20 10,18 12.2,13 12.2,7 10.2,2 6,0 1.5,0!
pE 12 - 10.5,20 1.5,20! 1.5,0! 10.5,0 / 1.5,10.2 8.6,10.2
pF 11.5 - 10.5,20 1.5,20! 1.5,0 / 1.5,10.2 8.6,10.2
pG 13.5 - 12,16.4 10,19.4 7,20.2 3.6,19 1.2,14.6 0.8,8.6 2,3.4 5,0.4 8.6,0 12,2.6 12.2,8.6! 7.4,8.6
pH 13 - 1.5,20 1.5,0 / 11.5,20 11.5,0 / 1.5,10.2 11.5,10.2
pI 10 - 5,20 5,0 / 1.6,20 8.4,20 / 1.6,0 8.4,0
pJ 12 - 9.6,20 9.6,5 8.2,1.2 5.4,0 2.4,0.8 0.8,4
pK 13 - 1.5,20 1.5,0 / 11.6,20 1.6,8 / 4.8,11.6 12,0
pL 11.5 - 1.5,20 1.5,0! 10.5,0
pM 15.5 - 1,0 1.6,20! 7.6,4! 13.6,20! 14.2,0
pN 13 - 1.5,0 1.5,20! 11.5,0! 11.5,20
pO 14 - 7,20 3.4,18.6 1,14 0.6,8 2.2,2.6 6,0 9.6,1 12.6,5.6 13,11.6 11.6,17.4 8.4,20 6,19.6
pP 12.5 - 1.5,0 1.5,20! 7.4,20 10.4,18.4 11.2,15 10.2,11.6 7.2,10 1.5,10
pQ 14 - 7,20 3.4,18.6 1,14 0.6,8 2.2,2.6 6,0 9.6,1 12.6,5.6 13,11.6 11.6,17.4 8.4,20 6,19.6 / 8,5 13.2,-1
pR 13 - 1.5,0 1.5,20! 7.4,20 10.4,18.4 11.2,15 10.2,11.6 7.2,10 1.5,10! / 6.4,10 11.6,0
pS 12 - 11,17.4 9.4,19.6 6.2,20.2 2.8,19 1.4,16 2.6,12.8 6.4,10.8 10,8.6 11.4,5.2 10,1.4 6.4,0 2.8,0.6 0.6,3
pT 13 - 0.6,20 12.4,20 / 6.5,20 6.5,0
pU 13 - 1.5,20 1.5,6 3,1.4 6.4,0 9.8,1.4 11.4,6 11.4,20
pV 13 - 0.6,20 6.5,0! 12.4,20
pW 17.2 - 0.6,20 4.4,0! 8.6,14! 12.8,0! 16.6,20
pX 12.6 - 1,20 11.6,0 / 11.6,20 1,0
pY 13 - 0.8,20 6.4,10! 12,20 / 6.4,10 6.4,0
pZ 13 - 1,20 11.6,20! 1,0! 11.8,0
# ---- digits and marks (print; cursive falls back to these)
p0 11.5 - 6,20 2.6,18.4 0.8,12 1.2,5 3.4,0.6 6,0 8.8,1.4 10.6,7 10.4,14 8.6,19 6,20
p1 9 - 2.4,16.6 5.6,20! 5.6,0
p2 12 - 1.2,15.6 3,19.2 6,20.2 9,19 10.2,15.6 8.6,11 1,0.4! 10.8,0.4
p3 12 - 1.4,17.6 4,20 7.6,19.8 9.6,16.8 8.4,12.6 5,11! 9,9.6 10.6,5.6 9,1.4 5.4,0 2.4,0.8 0.8,3
p4 12.5 - 8.4,0 8.4,20! 0.6,6! 11.4,6
p5 12 - 10,20 2,20! 1.4,11! 5,12.4 8.6,11.4 10.6,7.6 9.6,2.4 6,0 2.6,0.4 0.8,2.6
p6 11.5 - 9.4,18.4 6.6,20.2 3.4,18.6 1.2,13 1,6 2.6,1.4 5.8,0 9,1.4 10.2,5.4 8.8,9.4 5.6,10.8 2.6,9.6 1.2,6.6
p7 12 - 0.8,20 11,20! 4.4,0
p8 11.5 - 5.8,10.8 2.2,13.4 2,17.4 4.6,20 8,19.6 9.6,16.8 8,12.6 4.6,10.4 1.4,7.4 1.4,2.6 4.6,0 8.4,0.6 10.4,4.4 9,8.4 5.8,10.8
p9 11.5 - 10,13 8.6,9.6 5.4,8.6 2.4,10 1,13.6 2.4,18.6 5.6,20.2 8.8,18.6 10.2,13.6 9.6,6 7.6,1.4 4.4,0 1.4,1.6
p. 3.5 - * 1.4,0.6
p- 7.5 - 0.6,7.6 6.6,8
p& 12.5 - 11,0 3.6,11 2.8,15.6 4.4,19.4 7,19.6 8,16.6 6,12.8 1.8,8.4 1,4 2.6,0.8 5.4,0 8.4,1.6 11,5.6
p' 3.5 - 2,20 1.4,15.6
p! 4.5 - 2.2,20 1.6,6 / * 1.4,0.6
p? 11 - 1,16 2.6,19.4 6,20.4 9,18.6 9,15 5.4,11.4 5,7 / * 5,0.6
p, 3.5 - 1.6,1 0.6,-3
p# 11.5 - 3.6,18 2.4,0 / 8.6,18 7.4,0 / 0.6,12.6 11,12.6 / 0,5.6 10.4,5.6
# ---- specials
xT 13 - 0.6,18.4 4,20.2 9,19.6 13.4,20.6 / 8.4,20.2 4.6,11.6! 8.6,11.8! 4.2,1.2! 5.4,5.2! 2.8,3.6! 4.2,1.2
xspider 9 - O 4.5,8,4.2 / O 4.5,11.2,2.6 / 3,9.2 0.4,12 / 3,8.2 -0.2,8.6 / 3,7.2 0.2,4.6 / 6,9.2 8.6,12 / 6,8.2 9.2,8.6 / 6,7.2 8.8,4.6
xbolt 9 - 6.6,20 1.8,9.6! 5.8,10.4! 2.4,-1 / O 2.4,-1,1.2
xheart 11 - 5.5,8.6! 4.6,10.8 2.6,11.4 0.6,9.6 0.8,6.6 2.8,3.8 5.5,0.6! 8.2,3.8 10.2,6.6 10.4,9.6 8.4,11.4 6.4,10.8 5.5,8.6
)GLY";

// DOODLES: "<name> <strokes>", the same stroke syntax, drawn in a 10 x 10 cell (y up).
static const char kDoodleData[] = R"DOO(
star 1.6,0.4 5,10! 8.4,0.4! 0,6.6! 10,6.6! 1.6,0.4!
ostar 5,10 6.2,6.4! 10,6.4! 7,4! 8.2,0.4! 5,2.6! 1.8,0.4! 3,4! 0,6.4! 3.8,6.4! 5,10!
heart 5,7.6! 4.2,9.6 2.4,10.2 0.6,8.6 0.8,5.8 2.6,3.4 5,0.6! 7.4,3.4 9.2,5.8 9.4,8.6 7.6,10.2 5.8,9.6 5,7.6
circle 5,10 1.6,8.6 0,5 1.4,1.4 5,0 8.6,1.4 10,5 8.6,8.6 5,10 4,9.6
egg 5,10 2.4,8.6 1,5 1.6,1.6 5,0 8.4,1.6 9,5 7.6,8.6 5,10 4.2,9.6 / 2.8,6.6 3.6,7.4 4.6,6.8 4.2,5.6 3.2,5.6 2.8,6.6 / 5.8,3.4 6.6,4.2 7.6,3.6 7.2,2.4 6.2,2.4 5.8,3.4 / 6.4,7.4 7,7.8 7.6,7.2 7.2,6.6 6.6,6.8 6.4,7.4
banana 1,8.8 1.4,5.2 3.6,2.4 7,1.2 9.8,2! 9.6,2.8 6.8,3.2 4,4.8 2.4,7.6 1.6,9.6! 1,8.8 / 9.6,2.4 10.6,3.2
crown 0.6,1.4 0,8! 3,4.4! 5,9.4! 7,4.4! 10,8! 9.4,1.4! 0.6,1.4! / O 0,8.6,1.1 / O 5,10,1.1 / O 10,8.6,1.1
bolt 6.6,10 1.8,4.6! 5.4,4.8! 3,0
flame 5,0 1.6,1.8 0.4,5 1.8,8 2.8,6.4! 3.8,10 5.6,7! 6.6,9.4 7.4,6.6! 8.6,8 9.6,4.6 8.4,1.6 5,0 / 4.8,1.6 3.6,3.6 4.4,5.8! 5.4,4 6.2,5! 6.4,2.8 4.8,1.6
drop 5,10! 2.8,6 1.4,3.4 2,1 5,0 8,1 8.6,3.4 7.2,6 5,10!
leaf 0,0! 1.6,5 5,8.6 10,10! 8.4,5 5,1.4 0,0! / 0,0 6.4,6.4
snow 5,0 5,10 / 0.6,2.4 9.4,7.6 / 0.6,7.6 9.4,2.4 / 3.8,9.2 5,8! 6.2,9.2 / 3.8,0.8 5,2! 6.2,0.8
paw O 5,3,4.2 / O 1.4,6.4,2.2 / O 3.8,8.8,2.2 / O 6.4,8.8,2.2 / O 8.8,6.4,2.2
claw 1,9 4.6,0.6 / 3.8,10 7.4,1.6 / 6.6,10.6 10.2,2.6
web 5,5 5,10 / 5,5 9.4,7.6 / 5,5 9.4,2.4 / 5,5 5,0 / 5,5 0.6,2.4 / 5,5 0.6,7.6 / 5,7.6! 6,6.6 7.2,6.2! 6.8,5 7.2,3.8! 6,3.4 5,2.4! 4,3.4 2.8,3.8! 3.2,5 2.8,6.2! 4,6.6 5,7.6! / 5,9.4! 7,8 8.6,7! 8.2,5 8.6,3! 7,2 5,0.8! 3,2 1.4,3! 1.8,5 1.4,7! 3,8 5,9.4!
triforce 0,0 10,0! 5,8.66! 0,0! / 2.5,4.33 7.5,4.33! 5,0! 2.5,4.33!
ghost 1,0.4 1,6 2.4,9.2 5,10.2 7.6,9.2 9,6 9,0.4! 7.6,1.8! 6.2,0.4! 5,1.8! 3.8,0.4! 2.4,1.8! 1,0.4! / O 3.6,6,1.4 / O 6.4,6,1.4
bone 1,6.4 2,7.6 3.2,6.6 6.8,6.6 8,7.6 9,6.4 8.4,5 9,3.6 8,2.4 6.8,3.4 3.2,3.4 2,2.4 1,3.6 1.6,5 1,6.4
skull 2,4 1,7 2.6,9.6 5,10.2 7.4,9.6 9,7 8,4 6.8,3.6 6.8,1.4! 3.2,1.4! 3.2,3.6 2,4 / O 3.6,6.2,1.8 / O 6.4,6.2,1.8 / 4.4,1.4 4.4,2.6 / 5.6,1.4 5.6,2.6
pill 0.8,4.4 1.4,2.2 3.2,1.6 8.6,4.6 9.4,7.2 8.2,8.6 6.6,8.4 1.4,5.4 0.8,4.4 / 4.2,6 5.4,3.4
note O 2.4,1.8,3 / 3.8,2.2 3.8,10! 6.6,8.6 7.6,6.2
moon 6,10 3,8.8 1,5.4 1.8,2 5,0.2 8.6,1 6,1.6 4,3.4 3.6,6 4.6,8.4 6,10
gem 5,10 9,5.6! 5,0! 1,5.6! 5,10! / 1,5.6 9,5.6!
reactor 5,10 1.6,8.6 0,5 1.4,1.4 5,0 8.6,1.4 10,5 8.6,8.6 5,10 4,9.6 / 5,8.2 8,3.2! 2,3.2! 5,8.2!
vcrest 0.6,10 9.4,10! 9.4,5 5,0! 0.6,5 0.6,10! / 2.6,8.4 5,2.8! 7.4,8.4
hourglass 1.4,10 8.6,10! 5,5! 1.4,10! / 1.4,0 8.6,0! 5,5! 1.4,0!
spiral 5,5 6,5.6 5.6,6.6 4.2,6.6 3.4,5 4.4,3.4 6.4,3.4 7.6,5.4 7,7.6 4.6,8.6 2.2,7.4 1.4,4.6 2.8,2 5.6,1.2 8.6,2.6 9.6,5.8
feather 1.4,1.4 3.6,6 7.6,9.6 9.6,10! 8.6,7 5.6,3.2 1.4,1.4 / 0,0 8.8,9.4
cloud 1.6,2 0.4,3.6 1.2,5.6 3,6 3.6,8.4 6,9.2 7.8,7.6 9.4,7.4 10,5 8.8,2.6 6,2 1.6,2!
mushroom 0.4,4 1.4,8 5,10 8.6,8 9.6,4 0.4,4! / 3,4 2.8,0.4! 7.2,0.4! 7,4 / O 3,7,2 / O 6.8,6.6,1.6
shell 0.4,2 1.8,7 5,9 8.2,7 9.6,2! 0.4,2! / 0,1.4 10,1.4 / 3.6,2.6 3.6,4.6! 5,5.8! 6.4,4.6! 6.4,2.6
hammer 2,0 6.2,6 / 3.6,8.6 6,10.4! 9.2,6.2! 6.8,4.4! 3.6,8.6!
bow 5,5 1.4,8 0.4,5 1.4,2 5,5! 8.6,8 9.6,5 8.6,2 5,5! / 4.6,4.8 3.4,0.6 / 5.4,4.8 6.6,0.6 / O 5,5,1.6
mask 5,10 1.8,8.8 0.6,5 1.8,1.2 5,0 8.2,1.2 9.4,5 8.2,8.8 5,10 4.2,9.8 / O 3.4,6.2,2 / O 6.6,6.2,2 / O 5,2.8,1.6
teeth 0.6,5 2,8.2 5,9.4 8,8.2 9.4,5 8,1.8 5,0.6 2,1.8 0.6,5 / 1,5 2.4,3.2! 3.8,5.6! 5.2,3.2! 6.6,5.6! 8,3.2! 9.2,5
burst 5,10 6,6.8! 9.4,8! 7.2,5! 10,2.6! 6.6,3! 5,0! 3.6,3! 0,2.6! 2.8,5! 0.6,8! 4,6.8! 5,10!
rock 1,1 0.4,4.6 2.4,7.8 6,8.6 9,6.2 9.6,2.4 7,0.6 1,1! / 4,8.4 4.6,5.4! 3.4,3.6
nut 2.5,0.7 7.5,0.7! 10,5! 7.5,9.3! 2.5,9.3! 0,5! 2.5,0.7! / 5,7 3.2,6 3.2,4 5,3 6.8,4 6.8,6 5,7
eye 0,5 2.6,7.6 5,8.4 7.4,7.6 10,5! 7.4,2.4 5,1.6 2.6,2.4 0,5! / O 5,5,3 / 5,1.6 5.6,-0.6 5,-2.2 4.4,-0.6 5,1.6
bug 5,9 1.6,7.6 0.4,4.4 1.6,1.2 5,0 8.4,1.2 9.6,4.4 8.4,7.6 5,9 / 5,9 5,0 / O 3,5,1.6 / O 7,5,1.6 / O 3.4,2.2,1.2 / O 6.6,2.2,1.2 / 4,9.2 3,11 / 6,9.2 7,11
fish 0,5 2,7.6 5.6,8.4 8.4,6.4! 10,8.4! 10,1.6! 8.4,3.6! 5.6,1.6 2,2.4 0,5! / O 2.4,5.6,1
male 4,7.4 1.4,6.4 0.6,4 1.4,1.4 4,0.6 6.6,1.4 7.4,4 6.8,6.2 4,7.4 / 6.4,6.4 9.8,9.8 / 6.4,9.8 9.8,9.8! 9.8,6.4
female 5,9.8 2.4,8.8 1.6,6.4 2.4,3.8 5,3 7.6,3.8 8.4,6.4 7.6,8.8 5,9.8 / 5,3 5,-2 / 3,0 7,0
coin 5,10 1.6,8.6 0,5 1.4,1.4 5,0 8.6,1.4 10,5 8.6,8.6 5,10 4,9.6 / 5,7 4,6 4,4 5,3 6,4 6,6 5,7
dpmask 5,10 1.6,8.6 0,5 1.4,1.4 5,0 8.6,1.4 10,5 8.6,8.6 5,10 4,9.6 / 5,10 5,0 / 1.6,6.4 3.4,4.8 4.4,6.6 3.2,7.6 1.6,6.4 / 8.4,6.4 6.6,4.8 5.6,6.6 6.8,7.6 8.4,6.4
xmen 5,10 1.6,8.6 0,5 1.4,1.4 5,0 8.6,1.4 10,5 8.6,8.6 5,10 4,9.6 / 2.4,7.8 7.6,2.2 / 2.4,2.2 7.6,7.8
four 5,10 1.6,8.6 0,5 1.4,1.4 5,0 8.6,1.4 10,5 8.6,8.6 5,10 4,9.6 / 6,1.6 6,8.4! 2.4,3.8! 8,3.8
spider O 5,5,3.6 / O 5,7.8,2.2 / 4,6 1.6,8.8 / 3.8,5 0.4,5.8 / 3.8,4.2 0.8,2.2 / 4.2,3.6 2.6,0.4 / 6,6 8.4,8.8 / 6.2,5 9.6,5.8 / 6.2,4.2 9.2,2.2 / 5.8,3.6 7.4,0.4
tail 0,6 2,7.6 4,6.4 5.6,3.8 7.6,3.2 / 7.6,3.2 8.6,5.2! 10.2,2.8! 7.6,3.2!
eyepatch 0,8 3.4,6.6 / 6.6,5.8 10,4.4 / O 5,5.4,4.4
sun 5,7.4 3.2,6.6 2.6,5 3.2,3.4 5,2.6 6.8,3.4 7.4,5 6.8,6.6 5,7.4 / 5,8.6 5,10 / 5,1.4 5,0 / 8.6,5 10,5 / 1.4,5 0,5 / 7.6,7.6 8.6,8.6 / 2.4,2.4 1.4,1.4 / 7.6,2.4 8.6,1.4 / 2.4,7.6 1.4,8.6
zzz 0,4 3,4! 0,0! 3,0 / 4,8 6.4,8! 4,4.8! 6.4,4.8 / 7.4,11 9.4,11! 7.4,8.6! 9.4,8.6
puff 2,1 0.4,3 1.4,5.4 3.4,5.6 4.4,7.8 7,8 8.2,6 9.8,4.8 9.2,2 7,1 2,1!
cap 0.6,3 1.4,7 5,9.4 8.6,7.6 9.4,3.6 0.6,3! / 9.4,3.6 12.4,3.2 9.6,2.6 / O 5,6.2,1.8
ball 5,10 1.6,8.6 0,5 1.4,1.4 5,0 8.6,1.4 10,5 8.6,8.6 5,10 4,9.6 / 2,8.4 3.4,5 2,1.6 / 8,8.4 6.6,5 8,1.6
sword 0,5 1,5.6 12,5.6 13.6,5! 12,4.4 1,4.4 0,5 / 2.6,7.8 2.6,2.2 / 2.6,5 -1.6,5 / O -2.2,5,1.4
arrowhd 0,0 10,5! 0,10
wing 0,0 10,4 9,6.6 7,6.4 8,8.6 5,9 3.6,6.4 0,0!
bat 5,4 3.6,6.6! 2,5.6 0,7.6 0.6,4.6 2,3 3.6,2.4 5,3.4 6.4,2.4 8,3 9.4,4.6 10,7.6 8,5.6 6.4,6.6! 5,4
tornado 0,10 10,10 / 1,8 9,8.4 / 2.4,6 7.6,6.4 / 3.6,4 6.6,4.4 / 4.4,2 5.8,2.6 / 4.6,0 5.2,0.6
bubble 5,10 1.6,8.6 0,5 1.4,1.4 5,0 8.6,1.4 10,5 8.6,8.6 5,10 4,9.6 / 6.8,7.4 7.8,6.2 / O 13,9,2.4 / O 12,2,1.4
daisy 9.3,7.5 8.5,8.0 7.2,7.7 5.9,6.5 6.2,8.3 5.8,9.5 5.0,10.0 4.2,9.5 3.8,8.3 4.1,6.5 2.8,7.7 1.5,8.0 0.7,7.5 0.7,6.6 1.6,5.6 3.3,5.0 1.6,4.4 0.7,3.4 0.7,2.5 1.5,2.0 2.8,2.3 4.1,3.5 3.8,1.7 4.2,0.5 5.0,0.0 5.8,0.5 6.2,1.7 5.9,3.5 7.2,2.3 8.5,2.0 9.3,2.5 9.3,3.4 8.4,4.4 6.7,5.0 8.4,5.6 9.3,6.6 9.3,7.5 / O 5,5,2.4
garlic 5,9.6 4.8,8.4 4.4,8.2 2,6.6 0.8,3.8 2,1 5,0.2 8,1 9.2,3.8 8,6.6 5.6,8.2 5.2,8.4 5,9.6 / 4.8,7.6 3.4,4 4.2,0.8 / 5.2,7.6 6.6,4 5.8,0.8 / 4,0.4 3.4,-1 / 5,0.2 5,-1.2 / 6,0.4 6.6,-1
foxhead 0.6,10! 3,7! 7,7! 9.4,10! 8.6,5.6 5,0.6! 1.4,5.6 0.6,10! / O 3.6,5.4,1 / O 6.4,5.4,1 / O 5,1.4,1.2
pkball 5,10 1.6,8.6 0,5 1.4,1.4 5,0 8.6,1.4 10,5 8.6,8.6 5,10 4,9.6 / 0,5 3.4,5 / 6.6,5 10,5 / 5,6.6 3.4,5 5,3.4 6.6,5 5,6.6
)DOO";

// STYLES: "@defaults k=v ..." every signature's starting point; "@name k=v ..." a preset; "<key> k=v ..." a character
// (roster key; strikers_ / costume _cN suffixes fall back to the base key). use=a+b applies presets first; doo+=
// adds doodles. Params: fam c cursive / p print / b BLOCK capitals; caps 1 = all capitals; slant (shear); wid
// (letter width); capr (capital size); asc (loops); spc (letter spacing); wgap; smooth 0 = angular; jit (mess);
// wob / wobf (baseline wobble); bnc (letter bounce); szj / rotj (letter size / tilt jitter); trem (shake); rise
// (baseline slope); scrawl (lowercase flattened); fill / offx / offy (how much of the box, where); speed; pen s
// sharpie / m marker / b ballpoint / f fountain (broad nib) / g gel / p paint / c crayon / r brush; pw (pen width);
// col / glow / dooc / ulc (RRGGBB ink, wet-ink glow, doodle ink, underline ink); alpha; fade (later letters
// fainter); halo RRGGBBAA + halow (a glow / scorch under the ink); dot d dot / o circle / c big circle / h HEART /
// s star / l dash / b blob / z bolt / f flame / k skull / n none; dotx (dot lands right); tx n / l (long t-cross
// over the word) / s (steep); ul n / line / double / swoosh / long / zigzag / spiky / wave / loop / web / arrow /
// sword / bolt / flame / tail / dash; ulw; lead 1 hook / 2 swash into the first capital; tail 1 = the last letter
// whips back under the name; doo name@pos:size (pos e end / a above the first letter / t top right / u under right /
// l left / c under / x over the name); xt extra text ("_" = space) in preset xst at xpos u / e / a, xsz; blot (ink
// blots); xout "<index><letter>" a wrong letter crossed out; kid 1 = a backwards letter; pix (pixel grid);
// flames (licks off the letter tops); gl "<char>:<glyph>;..." special glyphs; txt (what they sign); lines 1 / 2.
static const char kStyleData[] = R"STY(
@defaults fam=c caps=0 slant=0.2 wid=1 capr=1.25 asc=1 spc=0 spj=0.6 wgap=7 bow=1 ovr=0.7 smooth=1 jit=0.25 wob=0.6 wobf=1 bnc=0 szj=0.05 rotj=0.02 trem=0 rise=0.04 scrawl=0 fill=0.92 offx=0 offy=0 speed=1 pen=s pw=1 col=F2F2F2 glow=FFFFFF alpha=1 fade=0 dot=d dotx=0.6 tx=n ul=n ulw=1 lead=0 tail=0 xsz=0.42 xpos=u xst=note lines=0 kid=0 pix=0 flames=0 halow=2.2
# ---- presets
@note fam=p slant=0.1 pen=b pw=1.2 jit=0.2 wob=0.3 rise=0.02 capr=1
@generic slant=0.2 capr=1.35 lead=1 ul=swoosh pen=s col=E8C25A glow=FFE9A0
@marvel_generic slant=0.22 capr=1.4 lead=1 ul=line pen=p col=D8DDE6 glow=FFFFFF
@pk_generic fam=c slant=0.15 pen=m col=F0E6D2 doo=paw@e:8
@confident fam=c slant=0.22 capr=1.45 lead=2 tail=1 pen=s pw=1.15 jit=0.3 rise=0.06
@elegant fam=c slant=0.3 capr=1.6 lead=2 pen=f pw=1.1 jit=0.15 wob=0.4 asc=1.15 wid=1.05
@bubbly fam=c slant=0.04 wid=1.15 asc=0.85 bnc=1.2 szj=0.08 rotj=0.05 pen=m pw=1.15 dot=o capr=1.3
@roundprint fam=p slant=0.04 wid=1.25 bnc=1.6 rotj=0.09 szj=0.12 jit=0.55 wob=1 pen=m pw=1.5 dot=o capr=1.15
@messyblock fam=b smooth=0 jit=1.7 wob=2.2 wobf=2.5 bnc=1.8 rotj=0.14 szj=0.2 slant=0.06 pen=s pw=1.7 capr=1.25 spc=1.5
@heavyblock fam=b smooth=0 jit=0.85 wob=1 rotj=0.05 slant=0.1 capr=1.3 pen=m pw=1.9 spc=1 spj=0.25
@techy fam=p caps=1 smooth=0 jit=0.15 wob=0.15 rise=0.01 slant=0.1 pen=g pw=1.2 rotj=0.01 szj=0.02 spc=1.2 spj=0.2
@kid fam=p bnc=2.2 szj=0.2 rotj=0.12 jit=0.6 slant=-0.06 pen=c pw=1.5 capr=1.5 wob=1.2 kid=1 spc=1
@angular fam=c smooth=0 jit=0.2 slant=0.3
@shy fill=0.62 offx=-0.1 offy=0.1 capr=1.05 jit=0.1 wob=0.25 rise=0.01 slant=0.08
@gothic fam=c smooth=0 pen=f pw=1.2 wid=0.82 capr=1.6 slant=0.04 jit=0.1 wob=0.2 rise=0
@scrawl fam=c scrawl=0.45 jit=0.7 speed=1.6 slant=0.35 wid=1.25 tx=l dot=l dotx=2.5
@shaky trem=0.5 jit=0.3
# ---- STRIKERS captains + sidekicks
mario use=confident col=FF4A3D glow=FFB0A0 doo=star@e:9 dooc=FFD34A dot=d
luigi use=shy fam=c pen=b pw=1.6 col=5BD75B glow=C0FFC0 ul=dash doo=ghost@e:5 dot=o
peach use=elegant col=FF8ACB glow=FFD0EC doo=crown@a:8 dooc=FFD34A ul=loop
daisy use=bubbly col=FFA53A glow=FFE0A0 dot=h doo=daisy@e:9 dooc=FFE45A ul=swoosh
yoshi use=roundprint col=6BE06B glow=D0FFD0 doo=egg@e:14 wid=1.35 capr=1.25
donkeykong txt=DK use=messyblock jit=0.8 wob=0.8 bnc=0.6 capr=1 slant=0.18 pen=m pw=2.4 col=FF6B3D glow=FFC0A0 doo=banana@e:13 dooc=FFE14D ul=swoosh ulw=1.3
wario use=messyblock szj=0.25 rotj=0.18 bnc=2.4 col=FFD23F glow=FFF0A0 xout=3R blot=3 doo=garlic@e:9 dooc=F2F2F2
waluigi use=angular wid=0.62 capr=1.75 asc=1.5 slant=0.34 col=B57BFF glow=E0C8FF ul=long ulw=1.2 dot=l lead=1 pen=s pw=1.1
toad use=roundprint col=FF5C5C glow=FFC8C8 doo=mushroom@e:9 bnc=1.6 pw=1.3
koopa use=roundprint col=6BD86B doo=shell@e:9 slant=0.08
hammerbro use=heavyblock col=4FD1C5 doo=hammer@e:10 pw=1.6
birdo use=bubbly col=FF8AD8 glow=FFD0F0 doo=bow@a:8 dooc=FF4A6A dot=c
# ---- CHARGED
bowser use=heavyblock jit=0.9 capr=1.4 pw=2.2 col=FF7A1A glow=FFC060 halo=C0200070 ul=spiky ulw=1.3 doo=flame@e:10
bowserjr use=kid col=FF9A3C glow=FFD0A0 doo=teeth@e:9 dooc=F2F2F2
diddykong txt=Diddy use=bubbly slant=0.25 bnc=1.6 col=FF5A4A glow=FFC0B0 dot=s doo=banana@e:10 dooc=FFE14D
petey txt=Petey use=roundprint wid=1.5 jit=0.9 rotj=0.1 col=7CE07C doo=leaf@e:9 pw=1.8
boo use=shy fill=0.8 offx=0 col=F4F4FF glow=FFFFFF alpha=0.85 fade=0.75 trem=0.25 halo=FFFFFF28 halow=3 doo=ghost@e:9 xt=boo! xpos=e
drybones fam=p use=shaky trem=1.0 col=E8E2D0 glow=FFFFFF smooth=0 spc=2 doo=bone@e:10 pen=g pw=1.4
montymole txt=Monty use=roundprint jit=0.6 col=D9A066 doo=paw@e:9 dooc=D9A066 pw=1.6
shyguy use=shy fill=0.55 col=FF4D4D glow=FFC0C0 doo=mask@e:8 pen=g
# ---- MELEE
drmario use=scrawl col=E8F4FF glow=FFFFFF doo=pill@e:9 dooc=7EC8FF pen=b pw=1.5
falcon txt=FALCON use=heavyblock jit=0.3 slant=0.42 capr=1.15 col=FFC93A glow=FFF0A0 ul=double ulw=1.2 doo=burst@e:10 dooc=FF5A3A xt=Falcon_PUNCH! xpos=u
fox use=angular slant=0.38 col=9FD3FF glow=E0F4FF tx=l ul=swoosh doo=foxhead@e:9 pen=g pw=1.4 speed=1.4
falco use=confident col=6FA8FF glow=C8E0FF capr=1.7 ul=long doo=feather@e:10 tail=0
link col=6BD35B glow=D0FFC0 slant=0.18 lead=1 ul=swoosh doo=triforce@e:9 dooc=FFD34A pen=s
younglink use=roundprint col=6BD35B doo=triforce@e:8 dooc=FFD34A pw=1.2 kid=0 lines=2
zelda use=elegant col=FFD7A0 glow=FFF0D0 doo=triforce@a:7 dooc=FFD34A ul=loop
sheik use=angular slant=0.4 col=BFD8FF glow=FFFFFF doo=eye@e:9 dooc=FF5A5A ul=dash speed=1.5
samus use=techy col=FF9E3A glow=FFD8A0 doo=ball@e:9 dooc=FFD34A ul=line
kirby use=bubbly col=FF9AD6 glow=FFD8F0 dot=s doo=ostar@e:11 dooc=FFE14D
ness use=kid pen=m kid=0 col=FF5A5A glow=FFC8C8 doo=cap@e:9 dooc=7FA8FF
popo use=kid pen=m kid=0 col=7FB8FF glow=D8ECFF doo=snow@e:9
nana use=kid pen=m kid=0 col=FF9AC8 glow=FFD8EC doo=snow@e:9
marth use=elegant col=8FB4FF glow=D8E8FF ul=sword ulc=D8DDE6
roy use=confident col=FF6A4A glow=FFC8A0 doo=flame@e:9 dooc=FFA53A halo=FF300040 tail=0 ul=swoosh
ganondorf use=gothic col=E0A040 glow=FFD080 halo=60100080 ul=spiky doo=triforce@e:10 dooc=FFD34A
gamewatch txt=GAME_&_WATCH use=techy pix=2 jit=0.4 col=D8E4C8 glow=FFFFFF pen=m pw=1.6 spc=2 lines=2
# ---- MARVEL (costumes _c2.._c4 sign like the hero, their own hand on the day)
mua_spiderman col=FF4A4A glow=FFC0C0 slant=0.28 capr=1.45 lead=1 ul=web ulc=F2F2F2 gl=-:xspider pen=s pw=1.1
mua_spiderwoman use=elegant col=FF5A7A glow=FFC8D8 doo=spider@e:9 ul=swoosh gl=-:xspider
mua_captainamerica col=F4F4F0 glow=FFFFFF slant=0.14 capr=1.45 jit=0.12 wob=0.3 lead=1 ul=line ulc=FF4A4A doo=star@e:10 dooc=6FA0FF pen=p
mua_thor gl=T:xT col=BFD8FF glow=E0F0FF halo=4080FF50 capr=1.5 slant=0.15 ul=bolt ulc=FFE14D pen=p pw=1.2
mua_ghostrider use=heavyblock jit=0.5 col=FF8A2A glow=FFE060 halo=FF200080 halow=2.6 flames=1.4 doo=skull@e:9 dooc=F2F2F2
mua_deadpool fam=p bnc=1.2 rotj=0.09 szj=0.1 jit=0.4 slant=0.12 col=FF4A4A glow=FFC0C0 pen=m pw=1.2 doo=dpmask@e:10,heart@t:5 xt=chimichanga! xpos=u xsz=0.45
mua_wolverine slant=0.32 jit=0.5 capr=1.4 col=FFE14D glow=FFF0A0 pen=m pw=1.3 doo=claw@x:26 dooc=D8DDE6
mua_ironman use=techy col=E8C25A glow=FFE9A0 doo=reactor@e:9 dooc=9FE8FF ul=line ulc=FF5A4A
mua_drdoom txt=Doom use=gothic capr=1.8 col=D8DDE6 glow=FFFFFF doo=vcrest@l:12 dooc=6CD86C ul=line
mua_professorx use=elegant pen=f col=9FC8FF glow=E0F0FF doo=xmen@e:9 dooc=FFD34A
mua_storm use=elegant col=F0F4FF glow=FFFFFF doo=cloud@e:10,bolt@u:6 dooc=FFE14D ul=wave
mua_iceman use=bubbly pen=p col=BFE8FF glow=FFFFFF halo=A0E0FF40 doo=snow@e:9 dot=d
mua_colossus use=heavyblock col=D8DDE6 glow=FFFFFF doo=xmen@e:9 dooc=FFD34A
mua_elektra use=angular slant=0.4 col=FF4A5A glow=FFC0C8 ul=swoosh tx=l
mua_blackpanther slant=0.2 capr=1.35 col=C9B8FF glow=E8E0FF doo=claw@e:9 pen=p ul=line
mua_daredevil wob=2.2 wobf=1.6 rise=0.0 bnc=0.6 spc=0.8 col=FF4A4A glow=FFC0C0 capr=1.4 lead=1 pen=s ul=dash
mua_blade use=angular slant=0.45 col=D8DDE6 glow=FFFFFF ul=sword ulc=FF4A4A pen=p
mua_thing use=heavyblock pw=2.4 capr=1.2 col=FF9A3C glow=FFD0A0 doo=four@e:10 dooc=7FB0FF xt=It's_clobberin'_time! xpos=u xsz=0.36
mua_humantorch col=FF8A2A glow=FFE060 halo=FF300060 flames=0.9 slant=0.3 capr=1.4 doo=flame@e:10 dooc=FFD34A speed=1.4
mua_hawkeye col=B57BFF glow=E0C8FF slant=0.25 capr=1.35 ul=arrow ulc=F2F2F2 pen=s
mua_blackwidow use=elegant pen=s col=FF4A4A glow=FFC0C0 doo=hourglass@e:8
mua_invisiblewoman use=elegant pen=g col=9FD0FF glow=FFFFFF alpha=0.7 fade=0.7 doo=four@e:9 dooc=7FB0FF
mua_moonknight col=F4F4F0 glow=FFFFFF slant=0.2 capr=1.4 doo=moon@e:10 pen=p ul=line
mua_mrfantastic wid=1.95 asc=1.3 col=7FB0FF glow=D8E8FF slant=0.12 doo=four@e:9 ul=swoosh
mua_nickfury txt=Fury use=heavyblock col=E8E8E8 glow=FFFFFF doo=eyepatch@e:9 ul=line
mua_silversurfer use=elegant pen=p col=E8ECF4 glow=FFFFFF halo=C0D8FF40 ul=swoosh ulw=1.6
mua_msmarvel col=FFD34A glow=FFF0A0 slant=0.22 capr=1.4 lead=1 doo=bolt@e:9 pen=p
mua_captainmarvel col=FFD34A glow=FFF0A0 slant=0.2 capr=1.45 doo=ostar@e:10 dooc=FF5A4A pen=p ul=line
mua_lukecage use=heavyblock col=FFD34A glow=FFF0A0 doo=burst@e:9
mua_ronin pen=r pw=1.1 jit=0.4 slant=0.08 capr=1.5 col=F0F0F0 glow=FFFFFF ul=dash
mua_nightcrawler col=7F9CFF glow=D0DCFF slant=0.25 capr=1.4 ul=tail ulc=7F9CFF xt=BAMF! xpos=e xsz=0.5 doo=puff@t:8 dooc=C8C8D8
mua_jeangrey use=elegant col=FF7A3A glow=FFD080 halo=FF400040 doo=flame@e:9 dooc=FFD34A
mua_vision use=elegant pen=g col=FFD34A glow=FFF0A0 doo=gem@e:9 dooc=6CD86C
mua_drstrange use=elegant col=9FC8FF glow=E0F0FF halo=4080FF30 doo=eye@e:9 dooc=FFD34A
mua_mysterio col=7FE08A glow=D0FFD8 halo=40FF6030 slant=0.25 trem=0.15 capr=1.4 doo=bubble@e:10 dooc=BFE8FF
mua_lizard use=shaky col=7CE07C glow=D0FFD0 doo=claw@e:9 fam=p
mua_rhino use=heavyblock col=C8C8C8 glow=FFFFFF doo=burst@e:9
mua_wintersoldier use=techy col=D8DDE6 glow=FFFFFF doo=star@e:9 dooc=FF4A4A
# ---- POKEMON favourites (the rest sign by type + mood, kPokeData)
pk_pikachu dot=z doo=bolt@e:10
pk_raichu doo=bolt@e:11 ul=bolt
pk_jigglypuff doo=note@e:8 dooc=FF9AD6
pk_wigglytuff doo=note@e:8
pk_igglybuff doo=note@e:7
pk_snorlax xt=zzz... xpos=e
pk_slakoth xt=zzz xpos=e
pk_slaking xt=zzz xpos=e
pk_psyduck xt=?? xpos=e xsz=0.6
pk_meowth doo=coin@e:9 dooc=FFD34A
pk_persian doo=coin@e:8 dooc=FFD34A
pk_magikarp doo=fish@e:9 dooc=FF7A3A
pk_gyarados ul=wave ulw=1.4
pk_zubat doo=bat@e:9
pk_golbat doo=bat@e:10
pk_crobat doo=bat@e:10
pk_mewtwo halo=B060FF50 ul=swoosh
pk_gengar doo=teeth@e:9
pk_charizard halo=FF300050 ul=swoosh
pk_nidoranm txt=Nidoran doo=male@e:8
pk_nidoranf txt=Nidoran doo=female@e:8
pk_cubone doo=bone@e:9
pk_marowak doo=bone@e:10
pk_togepi doo=egg@e:9
pk_exeggcute doo=egg@e:9
pk_staryu doo=ostar@e:9
pk_starmie doo=ostar@e:10
pk_spinarak doo=spider@e:8
pk_ariados doo=spider@e:9
pk_porygon pix=1.5 smooth=0 fam=p caps=1
pk_porygon2 pix=1.5 smooth=0 fam=p caps=1
pk_voltorb doo=pkball@e:9 dooc=FF5A4A
pk_electrode doo=pkball@e:10 dooc=FF5A4A
pk_shedinja halo=FFFFFF30 fade=0.5
pk_mrmime doo=star@e:8
pk_chansey doo=egg@e:9
pk_blissey doo=egg@e:9 dot=h
)STY";

// POKEMON: each species' type + mood ("name:TM"). Types (@type): F fire, W water, G grass, E electric, I ice,
// X fighting, P poison, D ground, R rock, Y flying, S psychic, B bug, H ghost, A dragon, K dark, M steel, N normal.
// Moods (@mood): c cute (bubbly), n normal, t tough (heavy block), e elegant, s sleepy / lazy (droops), w weird /
// creepy (shaky print), g goofy (bouncy print).
static const char kPokeData[] = R"PKD(
@type F col=FF7A2E glow=FFC060 doo=flame@e:8 dooc=FFC93A
@type W col=4FB3FF glow=C0E4FF doo=drop@e:7
@type G col=6BD86B glow=D0FFC8 doo=leaf@e:8
@type E col=FFE03A glow=FFF4B0 doo=bolt@e:9
@type I col=BFE8FF glow=FFFFFF doo=snow@e:8 pen=p
@type X col=FF5A4A glow=FFC0B0 doo=burst@e:9
@type P col=C27BFF glow=E8D0FF doo=bubble@e:7
@type D col=D9A066 glow=FFE0B0 doo=rock@e:8
@type R col=C8B08A glow=FFF0D0 doo=rock@e:9
@type Y col=9FD8FF glow=E0F4FF doo=feather@e:9
@type S col=FF7AD9 glow=FFD0F0 doo=spiral@e:8
@type B col=A8E05A glow=E0FFC0 doo=bug@e:8
@type H col=B48CFF glow=E0D0FF doo=ghost@e:8 halo=8040FF30 fade=0.3
@type A col=5FD3C8 glow=C8FFF8 doo=claw@e:9
@type K col=C9C3D9 glow=B090FF doo=moon@e:8
@type M col=D8DDE6 glow=FFFFFF doo=nut@e:8 pen=p
@type N col=F0E6D2 glow=FFFFFF doo=paw@e:8
@mood c use=bubbly
@mood n fam=c slant=0.18 capr=1.35 lead=1 pen=s pw=1.1
@mood t use=heavyblock
@mood e use=elegant ul=swoosh
@mood s fam=c rise=-0.09 wob=1.2 scrawl=0.3 speed=0.7 slant=0.05 pen=m pw=1.2
@mood w fam=p use=shaky rotj=0.1 jit=0.5 pen=g pw=1.3
@mood g fam=p bnc=2 rotj=0.12 szj=0.15 jit=0.55 wob=1 pen=m pw=1.3 slant=0.05 wid=1.15
bulbasaur:Gc ivysaur:Gn venusaur:Gt charmander:Fc charmeleon:Fn charizard:Ft squirtle:Wc wartortle:Wn blastoise:Wt
mrmime:Sg mankey:Xg primeape:Xt machop:Xn machoke:Xt machamp:Xt geodude:Rg graveler:Rt golem:Rt gastly:Hw haunter:Hw
gengar:Hg onix:Rt steelix:Mt voltorb:Eg electrode:Eg dratini:Ac dragonair:Ae dragonite:Ag pichu:Ec pikachu:Ec raichu:En
igglybuff:Nc jigglypuff:Nc wigglytuff:Nc torchic:Fc combusken:Fn blaziken:Ft drowzee:Sw hypno:Sw cubone:Dw marowak:Dt
koffing:Pg weezing:Pg mewtwo:Se snorlax:Ns magby:Fc magmar:Ft treecko:Gn grovyle:Gn sceptile:Ge mudkip:Wc marshtomp:Wg
swampert:Wt shuppet:Hw banette:Hw bagon:Ac shelgon:An salamence:At poliwag:Wc poliwhirl:Wn poliwrath:Wt politoed:Wg
scyther:Bt scizor:Mt houndour:Kn houndoom:Kt beldum:Mw metang:Mt metagross:Mt cyndaquil:Fc quilava:Fn typhlosion:Ft
totodile:Wg croconaw:Wn feraligatr:Wt chikorita:Gc bayleef:Gn meganium:Ge abra:Ss kadabra:Sn alakazam:Se ralts:Sc
kirlia:Se gardevoir:Se larvitar:Rc pupitar:Rn tyranitar:Rt aron:Mc lairon:Mt aggron:Mt trapinch:Dg vibrava:An flygon:Ae
elekid:Ec electabuzz:Et psyduck:Wg golduck:We eevee:Nc vaporeon:We jolteon:En flareon:Fc espeon:Se umbreon:Kn tyrogue:Xc
hitmonlee:Xt hitmonchan:Xt hitmontop:Xg mareep:Ec flaaffy:En ampharos:Ee teddiursa:Nc ursaring:Nt lotad:Wc lombre:Wg
ludicolo:Wg seedot:Gc nuzleaf:Kw shiftry:Kt shroomish:Gc breloom:Xn slakoth:Ns vigoroth:Ng slaking:Ns makuhita:Xc
hariyama:Xt whismur:Nc loudred:Ng exploud:Nt meditite:Xn medicham:Xe cacnea:Gn cacturne:Kw nidoranm:Pn nidorino:Pn
nidoking:Pt nidoranf:Pc nidorina:Pn nidoqueen:Pt kabuto:Rw kabutops:Rt anorith:Bw armaldo:Rt wooper:Wg quagsire:Wg
smoochum:Ic jynx:Ie sandshrew:Dc sandslash:Dt slowpoke:Ws slowbro:Ws slowking:We caterpie:Bc metapod:Bs butterfree:Be
weedle:Bc kakuna:Bs beedrill:Bt pidgey:Yc pidgeotto:Yn pidgeot:Ye rattata:Nc raticate:Nn spearow:Yn fearow:Yt ekans:Pw
arbok:Pt vulpix:Fc ninetales:Fe zubat:Pw golbat:Pw crobat:Pe oddish:Gc gloom:Gg vileplume:Gn bellossom:Gc paras:Bc
parasect:Bw venonat:Bc venomoth:Be diglett:Dg dugtrio:Dg meowth:Ng persian:Ne bellsprout:Gg weepinbell:Gg
victreebel:Gn tentacool:Wn tentacruel:Wt ponyta:Fc rapidash:Fe magnemite:Mg magneton:Mg doduo:Yg dodrio:Yt seel:Wc
dewgong:We grimer:Pg muk:Pg shellder:Wg cloyster:Wt krabby:Wg kingler:Wt exeggcute:Gg exeggutor:Gg rhyhorn:Dt rhydon:Rt
chansey:Nc blissey:Nc horsea:Wc seadra:Wn kingdra:We goldeen:Wc seaking:We staryu:Wn starmie:We magikarp:Wg gyarados:Wt
porygon:Nw porygon2:Nw omanyte:Rc omastar:Rw sentret:Nc furret:Nc hoothoot:Yn noctowl:Ye ledyba:Bc ledian:Bn
spinarak:Bw ariados:Bw chinchou:Wc lanturn:En cleffa:Nc clefairy:Nc clefable:Ne togepi:Nc togetic:Nc natu:Sw xatu:Sw
hoppip:Gc skiploom:Gc jumpluff:Gc sunkern:Gc sunflora:Gg pineco:Bn forretress:Mt snubbull:Nc granbull:Nt slugma:Fs
magcargo:Fs swinub:Ic piloswine:Is remoraid:Wn octillery:Wg phanpy:Dc donphan:Dt poochyena:Kn mightyena:Kt
zigzagoon:Nc linoone:Nn wurmple:Bc silcoon:Bs beautifly:Be cascoon:Bs dustox:Pw taillow:Yc swellow:Yt wingull:Wc
pelipper:Wg surskit:Bc masquerain:Be nincada:Bs ninjask:Bt shedinja:Hw azurill:Wc marill:Wc azumarill:Wn skitty:Nc
delcatty:Ne electrike:En manectric:Et gulpin:Pg swalot:Pg carvanha:Wt sharpedo:Wt wailmer:Wg wailord:Ws numel:Fs
camerupt:Ft spoink:Sg grumpig:Sn swablu:Yc altaria:Ae barboach:Wg whiscash:Ws corphish:Wn crawdaunt:Kt baltoy:Dw
claydol:Dw lileep:Rw cradily:Rw feebas:Ws milotic:We duskull:Hw dusclops:Hw wynaut:Sg wobbuffet:Sg snorunt:Ic glalie:It
spheal:Ic sealeo:In walrein:It clamperl:Wc huntail:Wt gorebyss:We
)PKD";
