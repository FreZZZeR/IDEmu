/* pngload.c: decodifica um PNG/JPEG do cache de mapas para RGBA (ImageIO). Separado de host.c por causa dos cabeçalhos da Apple. */
#include <ApplicationServices/ApplicationServices.h>
#include <stdlib.h>
#include <string.h>
unsigned char *pngload(const char *path,int *w,int *h){
  CFStringRef s=CFStringCreateWithCString(0,path,kCFStringEncodingUTF8); if(!s) return 0;
  CFURLRef u=CFURLCreateWithFileSystemPath(0,s,kCFURLPOSIXPathStyle,0); CFRelease(s); if(!u) return 0;
  CGImageSourceRef src=CGImageSourceCreateWithURL(u,0); CFRelease(u); if(!src) return 0;
  CGImageRef img=CGImageSourceCreateImageAtIndex(src,0,0); CFRelease(src); if(!img) return 0;
  int W=(int)CGImageGetWidth(img),H=(int)CGImageGetHeight(img);
  unsigned char *px=calloc(1,(size_t)W*H*4);
  CGColorSpaceRef cs=CGColorSpaceCreateDeviceRGB();
  CGContextRef c=px?CGBitmapContextCreate(px,W,H,8,(size_t)W*4,cs,kCGImageAlphaPremultipliedLast):0;
  if(c){ CGContextDrawImage(c,CGRectMake(0,0,W,H),img); CGContextRelease(c); } else { free(px); px=0; }
  CGColorSpaceRelease(cs); CGImageRelease(img); *w=W; *h=H; return px; }
