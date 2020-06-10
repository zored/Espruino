var a = new Uint8Array([5,6,7,8,9,10,11,12]);
var b = new Uint8Array(8);

E.mapInPlace(a,b, function(x) { return x+1; });
var r1 = b.toString()=="6,7,8,9,10,11,12,13";

E.mapInPlace(a,b, [12,11,10,9,8,7,6,5,4,3,2,1,0]);
var r2 = b.toString()=="7,6,5,4,3,2,1,0";

E.mapInPlace(a,b, new Uint8Array([12,11,10,9,8,7,6,5,4,3,2,1,0]));
var r3 = b.toString()=="7,6,5,4,3,2,1,0";

E.mapInPlace(a,b, function(a) { return a; }, 4);
var r4 = b.toString()=="0,5,0,6,0,7,0,8"; // high nibble, low nibble

E.mapInPlace(a,b, function(a) { return a; }, -4);
var r5 = b.toString()=="5,0,6,0,7,0,8,0"; // low nibble, high nibble

E.mapInPlace(a,b, function(a) { return a; }, 1);
var r6 = b.toString()=="0,0,0,0,0,1,0,1"; // 5 in binary

// 11 bit decode
s = new Uint8Array(9)
for (var i=0;i<s.length;i++)s[i]=Math.random()*256
var r = [s[0]|(s[1]<<8)&0x7FF,
      (s[1]>>3)|(s[2]<<5)&0x7FF,
      (s[2]>>6)|(s[3]<<2)|(s[4]<<10)&0x7FF,
      (s[4]>>1)|(s[5]<<7)&0x7FF,
      (s[5]>>4)|(s[6]<<4)&0x7FF,
      (s[6]>>7)|(s[7]<<1)|(s[8]<<9)&0x7FF].join(",");
var m = new Uint16Array(6);
E.mapInPlace(s,m,undefined,-11);
m = m.join(",");
var r7 = m == r; 

result = r1 && r2 && r3 && r4 && r5 && r6 && r7;
