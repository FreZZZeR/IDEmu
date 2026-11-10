// mguhost: renderizador do iDrive 8 no Mac. Lê os canais do guest direto da RAM compartilhada do QEMU e executa as
// chamadas GLES no ANGLE (Metal). Cada thread do guest tem um canal e uma thread aqui. Quadros vão para <dir>/frame.rgba.
// Uso: mguhost <nome shm> <tamanho em MiB> <dir> <pasta do ANGLE>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include "chan.h"
#include "gen_ops.h"
typedef unsigned long ul;
#define GUEST_BASE 0x40000000ul
static char *ram; static ul ramsz; static const char *outdir;
static void *hegl, *hgl;
static void *(*egp)(const char *);
static void *gp(const char *n) { void *p = dlsym(hgl, n); if (!p && egp) p = egp(n); if (!p) fprintf(stderr, "[mguhost] sem %s\n", n); return p; }
#define E(ret, name, ...) static ret (*e_##name)(__VA_ARGS__);
E(void *, GetPlatformDisplay, unsigned, void *, const intptr_t *) E(int, Initialize, void *, int *, int *) E(int, BindAPI, unsigned)
E(int, ChooseConfig, void *, const int *, void **, int, int *) E(int, GetConfigAttrib, void *, void *, int, int *)
E(void *, CreateContext, void *, void *, void *, const int *) E(int, DestroyContext, void *, void *)
E(void *, CreatePbufferSurface, void *, void *, const int *) E(int, DestroySurface, void *, void *)
E(int, MakeCurrent, void *, void *, void *, void *) E(int, SwapBuffers, void *, void *) E(int, GetError, void) E(int, WaitGL, void)
#undef E
static void *dpy;
#define GEN_DECL
#include "gen_host.inc"
#undef GEN_DECL
static void (*g_BufferData)(unsigned, long, const void *, unsigned);
static void (*g_TexImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
static void (*g_TexSubImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
static void (*g_TexSubImage3D)(unsigned, int, int, int, int, int, int, int, unsigned, unsigned, const void *);
static void (*g_CompressedTexSubImage2D)(unsigned, int, int, int, int, int, unsigned, int, const void *);
static void (*g_CompressedTexSubImage3D)(unsigned, int, int, int, int, int, int, int, unsigned, int, const void *);
#define GD(N) static void (*g_Gen##N)(int, unsigned *); static void (*g_Delete##N)(int, const unsigned *);
GD(Buffers) GD(Framebuffers) GD(Renderbuffers) GD(Samplers) GD(Textures) GD(VertexArrays)
static void (*g_DrawBuffers)(int, const unsigned *); static void (*g_InvalidateFramebuffer)(unsigned, int, const unsigned *);
static void (*g_GetIntegerv)(unsigned, int *); static void (*g_GetInternalformativ)(unsigned, unsigned, unsigned, int, int *);
static void (*g_GetProgramiv)(unsigned, unsigned, int *); static void (*g_GetShaderiv)(unsigned, unsigned, int *);
static void (*g_GetProgramInfoLog)(unsigned, int, int *, char *); static void (*g_GetShaderInfoLog)(unsigned, int, int *, char *);
static void (*g_GetProgramBinary)(unsigned, int, int *, unsigned *, void *); static void (*g_ProgramBinary)(unsigned, unsigned, const void *, int);
static void (*g_ShaderSource)(unsigned, int, const char *const *, const int *); static void (*g_ReadPixels)(int, int, int, int, unsigned, unsigned, void *);
static void (*g_PixelStorei)(unsigned, int); static void *(*g_MapBufferRange)(unsigned, long, long, unsigned); static unsigned char (*g_UnmapBuffer)(unsigned); static const unsigned char *(*g_GetString)(unsigned); static const unsigned char *(*g_GetStringi)(unsigned, unsigned);
#define U1(N, T) static void (*g_Uniform##N)(int, int, const T *);
U1(1fv, float) U1(2fv, float) U1(3fv, float) U1(4fv, float) U1(1iv, int) U1(2iv, int) U1(3iv, int) U1(4iv, int)
#define UM(N) static void (*g_UniformMatrix##N)(int, int, unsigned char, const float *);
UM(2fv) UM(3fv) UM(4fv)
#define LG(N) g_##N = gp("gl" #N)
static void load_gl(void) {
  gen_load(); LG(BufferData); LG(TexImage2D); LG(TexSubImage2D); LG(TexSubImage3D); LG(CompressedTexSubImage2D); LG(CompressedTexSubImage3D);
  LG(GenBuffers); LG(GenFramebuffers); LG(GenRenderbuffers); LG(GenSamplers); LG(GenTextures); LG(GenVertexArrays);
  LG(DeleteBuffers); LG(DeleteFramebuffers); LG(DeleteRenderbuffers); LG(DeleteSamplers); LG(DeleteTextures); LG(DeleteVertexArrays);
  LG(DrawBuffers); LG(InvalidateFramebuffer); LG(GetIntegerv); LG(GetInternalformativ); LG(GetProgramiv); LG(GetShaderiv); LG(GetProgramInfoLog); LG(GetShaderInfoLog);
  LG(GetProgramBinary); LG(ProgramBinary); LG(ShaderSource); LG(ReadPixels); LG(PixelStorei); LG(MapBufferRange); LG(UnmapBuffer); LG(GetString); LG(GetStringi);
  LG(Uniform1fv); LG(Uniform2fv); LG(Uniform3fv); LG(Uniform4fv); LG(Uniform1iv); LG(Uniform2iv); LG(Uniform3iv); LG(Uniform4iv);
  LG(UniformMatrix2fv); LG(UniformMatrix3fv); LG(UniformMatrix4fv);
}
// --- objetos EGL compartilhados entre canais
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
#define MAXO 256
static void *cfgs[MAXO]; static int ncfg;
static void *ctxs[MAXO]; static void *ctxcfg[MAXO]; static int nctx = 1;
static struct { void *s; int w, h; void *cfg; } sfs[MAXO];
// --- saída de quadros
#define MAXDIM 16384
static int frame_fd = -1; static char *frame_map; static ul frame_sz;
static volatile unsigned long frames; static double last_write;
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
// Mesmo formato do glhost: "IDFR", seq [1], w [2], h [3], quadro em escrita [4], livres até 32, RGB de cima para baixo.
// [4] = seq + 1 antes dos pixels e [1] = seq + 1 depois: o leitor (runtime.frame_bytes) descarta cópias rasgadas.
// Várias threads de canal podem trocar buffers ao mesmo tempo: frame_mu protege o arquivo, o buffer e o throttle.
static pthread_mutex_t frame_mu = PTHREAD_MUTEX_INITIALIZER;
static unsigned char *frame_tmp; static ul frame_tmp_sz; static unsigned frame_seq;
static double frame_cost; static unsigned long frame_cost_n;   // tempo médio de write_frame, no log "quadro N"
// o arquivo só cresce: o IDEmu (Python) o mantém mapeado, e um arquivo encolhido sob o mapeamento derruba o servidor com SIGBUS
static int frame_map_ensure(ul need) {
  if (frame_map && frame_sz >= need) return 1;
  if (frame_fd < 0) {
    char p[1024]; snprintf(p, sizeof p, "%s/frame.rgb", outdir);
    frame_fd = open(p, O_RDWR | O_CREAT, 0644); if (frame_fd < 0) { perror("[mguhost] frame.rgb"); return 0; }
  }
  struct stat st; if (fstat(frame_fd, &st)) { perror("[mguhost] fstat frame.rgb"); return 0; }
  ul sz = (ul)st.st_size;
  if (sz < need) { if (ftruncate(frame_fd, need)) { perror("[mguhost] ftruncate frame.rgb"); return 0; } sz = need; }
  char *m = mmap(0, sz, PROT_READ | PROT_WRITE, MAP_SHARED, frame_fd, 0);
  if (m == MAP_FAILED) { perror("[mguhost] mmap frame.rgb"); return 0; }
  if (frame_map) munmap(frame_map, frame_sz);
  frame_map = m; frame_sz = sz; return 1;
}
#if defined(__aarch64__)
#include <arm_neon.h>
#endif
static void rgba_to_rgb(unsigned char *o, const unsigned char *r, int w) {
  int x = 0;
#if defined(__aarch64__)
  for (; x + 16 <= w; x += 16) { uint8x16x4_t v = vld4q_u8(r + (ul)x * 4); uint8x16x3_t t = { { v.val[0], v.val[1], v.val[2] } }; vst3q_u8(o + (ul)x * 3, t); }
#endif
  for (; x < w; x++) { o[x * 3] = r[x * 4]; o[x * 3 + 1] = r[x * 4 + 1]; o[x * 3 + 2] = r[x * 4 + 2]; }
}
// Leitura do quadro: assíncrona por PBO (um por contexto). A troca N pede a cópia para o PBO; o quadro é publicado quando o canal fica
// ocioso (frame_idle) ou, no máximo, na próxima exportação. Sem MapBufferRange (ou se o mapeamento falhar), leitura síncrona como antes.
static __thread unsigned long cur_ctx;   // contexto atual desta thread de canal (EGLMakeCurrent)
static struct { unsigned pbo; int w, h, pending; } rbk[MAXO];
static int pbo_off;
typedef struct { int rb, al, pbo, rl, sr, sp; } PackSave;
// o estado de empacotamento do guest (PBO, ROW_LENGTH, SKIP_*, quadro de leitura) não pode mudar onde/como o GL escreve o quadro
static void pack_push(PackSave *s) {
  g_GetIntegerv(0x8CAA, &s->rb); g_GetIntegerv(0x0D05, &s->al); g_GetIntegerv(0x88ED, &s->pbo); g_GetIntegerv(0x0D02, &s->rl); g_GetIntegerv(0x0D03, &s->sr); g_GetIntegerv(0x0D04, &s->sp);
  g_PixelStorei(0x0D05, 1); g_PixelStorei(0x0D02, 0); g_PixelStorei(0x0D03, 0); g_PixelStorei(0x0D04, 0);
}
static void pack_pop(PackSave *s) {
  g_PixelStorei(0x0D05, s->al); g_PixelStorei(0x0D02, s->rl); g_PixelStorei(0x0D03, s->sr); g_PixelStorei(0x0D04, s->sp);
  p_BindBuffer(0x88EB, s->pbo); p_BindFramebuffer(0x8CA8, s->rb);
}
// RGBA de baixo para cima (como o GL lê) -> frame.rgb, com o protocolo de quadro em escrita
static void publish(const unsigned char *rgba, int w, int h) {
  if (!frame_map_ensure(32 + (ul)w * h * 3)) return;
  volatile unsigned *hd = (volatile unsigned *)frame_map;
  hd[0] = 0x52464449; __atomic_store_n(&hd[4], frame_seq + 1, __ATOMIC_RELEASE); __atomic_thread_fence(__ATOMIC_SEQ_CST);
  unsigned char *o = (unsigned char *)frame_map + 32;
  for (int y = 0; y < h; y++) rgba_to_rgb(o + (ul)y * w * 3, rgba + (ul)(h - 1 - y) * w * 4, w);
  hd[2] = w; hd[3] = h; __atomic_thread_fence(__ATOMIC_SEQ_CST);
  __atomic_store_n(&hd[1], ++frame_seq, __ATOMIC_RELEASE);
}
// publica a cópia pendente do contexto k (o PACK_BUFFER fica alterado: quem chama restaura)
static void rb_finish(unsigned long k) {
  if (!rbk[k].pending) return;
  rbk[k].pending = 0; int w = rbk[k].w, h = rbk[k].h;
  p_BindBuffer(0x88EB, rbk[k].pbo);
  const unsigned char *p = g_MapBufferRange(0x88EB, 0, (long)w * h * 4, 0x0001);   // MAP_READ_BIT
  if (!p) { fprintf(stderr, "[mguhost] MapBufferRange falhou (%#x): leitura síncrona daqui em diante\n", p_GetError ? p_GetError() : 0); pbo_off = 1; return; }
  publish(p, w, h); g_UnmapBuffer(0x88EB);
}
static void write_frame(int w, int h) {
  if (w <= 0 || h <= 0 || w > MAXDIM || h > MAXDIM) return;
  if (pthread_mutex_trylock(&frame_mu)) return;           // outra thread já está gravando: não para a renderização desta
  double t0 = now();
  if (t0 - last_write < 0.033) goto out;
  last_write = t0;
  PackSave s; pack_push(&s);
  unsigned long k = cur_ctx; ul px = (ul)w * h;
  if (!pbo_off && k && g_MapBufferRange && g_UnmapBuffer) rb_finish(k);   // a anterior, se o canal não ficou ocioso desde então
  if (!pbo_off && k && g_MapBufferRange && g_UnmapBuffer) {
    if (!rbk[k].pbo) g_GenBuffers(1, &rbk[k].pbo);
    p_BindBuffer(0x88EB, rbk[k].pbo);
    if (rbk[k].w != w || rbk[k].h != h) { g_BufferData(0x88EB, (long)px * 4, 0, 0x88E1); rbk[k].w = w; rbk[k].h = h; }   // STREAM_READ
    p_BindFramebuffer(0x8CA8, 0); g_ReadPixels(0, 0, w, h, 0x1908, 0x1401, 0);
    rbk[k].pending = 1;
  } else {
    if (frame_tmp_sz < px * 4) { unsigned char *n = malloc(px * 4); if (!n) { pack_pop(&s); goto out; } free(frame_tmp); frame_tmp = n; frame_tmp_sz = px * 4; }
    p_BindBuffer(0x88EB, 0); p_BindFramebuffer(0x8CA8, 0);
    g_ReadPixels(0, 0, w, h, 0x1908, 0x1401, frame_tmp);
    publish(frame_tmp, w, h);
  }
  pack_pop(&s);
  frame_cost += now() - t0; frame_cost_n++;
out:
  pthread_mutex_unlock(&frame_mu);
}
// canal ocioso (rd sem dados há ~1 ms): termina a cópia pendente nesta thread, que tem o contexto atual
static void frame_idle(void) {
  unsigned long k = cur_ctx;
  if (!k || !rbk[k].pending || pbo_off) return;
  if (pthread_mutex_trylock(&frame_mu)) return;
  double t0 = now();
  if (rbk[k].pending) { int pb = 0; g_GetIntegerv(0x88ED, &pb); rb_finish(k); p_BindBuffer(0x88EB, pb); frame_cost += now() - t0; }
  pthread_mutex_unlock(&frame_mu);
}
// --- canal
typedef struct Chan {
  ul hphys; ch_hdr *h; unsigned *pfn; ul a_rd, b_wr; unsigned pa, pb; char *buf; ul bufsz; int dead; unsigned long nonce;
  struct Chan *next;
} Chan;
// PFN do guest -> ponteiro na RAM mapeada. Só PFNs validados por pfn_ok (em scan) chegam aqui.
static inline int pfn_ok(ul pfn) { ul pa = pfn << 12; return pa >= GUEST_BASE && pa - GUEST_BASE <= ramsz - CH_PAGE; }
static inline char *pg(ul pfn) { return ram + ((pfn << 12) - GUEST_BASE); }
static inline ul acq(volatile ul *p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static inline void rel(volatile ul *p, ul v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
static int alive(Chan *c) { return !memcmp(c->h->magic, CH_MAGIC, 16) && c->h->nonce == c->nonce; }
static void rd(Chan *c, void *dst, ul n) {
  char *d = dst; int idle = 0;
  while (n) {
    ul wr = acq(&c->h->a_wr), av = wr - c->a_rd;
    if (!av) {
      if (++idle > 2000) { usleep(200); if (!alive(c)) { c->dead = 1; pthread_exit(0); } }
      else if (idle > 200) { if (idle == 250) frame_idle(); usleep(20); }
      continue;
    }
    idle = 0;
    ul pos = c->a_rd % CH_RING_A, off = pos & (CH_PAGE - 1), ch = CH_PAGE - off;
    if (ch > av) ch = av;
    if (ch > n) ch = n;
    memcpy(d, pg(c->pfn[pos >> 12]) + off, ch);
    d += ch; n -= ch; c->a_rd += ch;
    rel(&c->h->a_rd, c->a_rd);
  }
}
static void wr(Chan *c, const void *src, ul n) {
  const char *s = src;
  while (n) {
    ul rdp = acq(&c->h->b_rd), fr = CH_RING_B - (c->b_wr - rdp);
    if (!fr) { usleep(20); if (!alive(c)) { c->dead = 1; pthread_exit(0); } continue; }
    ul pos = c->b_wr % CH_RING_B, off = pos & (CH_PAGE - 1), ch = CH_PAGE - off;
    if (ch > fr) ch = fr;
    if (ch > n) ch = n;
    memcpy(pg(c->pfn[c->pa + (pos >> 12)]) + off, s, ch);
    s += ch; n -= ch; c->b_wr += ch;
    rel(&c->h->b_wr, c->b_wr);
  }
}
// Tamanhos e contagens vêm do guest: nada passa de MAXPAY, contagens negativas derrubam o canal (o fluxo já estaria dessincronizado).
#define MAXPAY (256ul << 20)
#define MAXCNT (1l << 20)
static void bad(Chan *c, const char *why, ul v) { fprintf(stderr, "[mguhost] canal %#lx: %s (%#lx); canal abandonado\n", c->hphys + GUEST_BASE, why, v); c->dead = 1; pthread_exit(0); }
// contagem de itens vinda do guest (int com sinal no protocolo): 0..MAXCNT
static int cnt(Chan *c, ul v) { long n = (int)v; if (n < 0 || n > MAXCNT) bad(c, "contagem invalida", v); return (int)n; }
// buffer de trabalho com pelo menos n + 16 bytes; 0 se n passa do limite ou falta memória (o buffer antigo continua válido)
static char *scratch(Chan *c, ul n) {
  if (n > MAXPAY) return 0;
  if (n + 16 > c->bufsz) { char *p = malloc(n + (1 << 20)); if (!p) return 0; free(c->buf); c->buf = p; c->bufsz = n + (1 << 20); }
  return c->buf;
}
static char *scratch_or_die(Chan *c, ul n) { char *p = scratch(c, n); if (!p) bad(c, "buffer de resposta grande demais", n); return p; }
static inline float bitsf(ul v) { union { unsigned u; float f; } x; x.u = (unsigned)v; return x.f; }
// lê n bytes do anel; grande demais: descarta (mantém o fluxo em ordem) e devolve 0, e quem chamou pula a chamada GL
static char *payload(Chan *c, ul n) {
  char *p = scratch(c, n);
  if (!p) { fprintf(stderr, "[mguhost] dados de %lu bytes descartados\n", n); char t[4096]; while (n) { ul k = n < sizeof t ? n : sizeof t; rd(c, t, k); n -= k; } return 0; }
  if (n) rd(c, p, n);
  return p;
}
// string terminada em zero (o guest manda strlen + 1, mas o host não confia nisso)
static char *str_payload(Chan *c, ul n) { char *p = payload(c, n); if (!p) return (char *)""; p[n] = 0; return p; }
// --- tamanho de imagens (ReadPixels, Tex*Image) calculado no host, a partir do estado de (des)empacotamento do GL do host
static int px_bytes(unsigned f, unsigned t) {
  int ts;
  switch (t) {
    case 0x1400: case 0x1401: ts = 1; break;                                    // BYTE, UNSIGNED_BYTE
    case 0x1402: case 0x1403: case 0x140B: case 0x8D61: ts = 2; break;          // SHORT, UNSIGNED_SHORT, HALF_FLOAT(_OES)
    case 0x1404: case 0x1405: case 0x1406: ts = 4; break;                       // INT, UNSIGNED_INT, FLOAT
    case 0x8363: case 0x8033: case 0x8034: return 2;                            // 5_6_5, 4_4_4_4, 5_5_5_1
    case 0x8368: case 0x8C3B: case 0x8C3E: case 0x84FA: return 4;               // 2_10_10_10_REV, 10F_11F_11F_REV, 5_9_9_9_REV, 24_8
    case 0x8DAD: return 8;                                                      // FLOAT_32_UNSIGNED_INT_24_8_REV
    default: return 0;
  }
  switch (f) {
    case 0x1903: case 0x8D94: case 0x1902: case 0x1909: case 0x1906: return ts;  // RED(_INTEGER), DEPTH_COMPONENT, LUMINANCE, ALPHA
    case 0x8227: case 0x8228: case 0x190A: return 2 * ts;                        // RG(_INTEGER), LUMINANCE_ALPHA
    case 0x1907: case 0x8D98: return 3 * ts;                                     // RGB(_INTEGER)
    case 0x1908: case 0x8D99: case 0x80E1: return 4 * ts;                        // RGBA(_INTEGER), BGRA_EXT
  }
  return 0;
}
// bytes que o GL lê/escreve para w x h x d pixels com o estado atual (base = GL_PACK_* 0x0D02.. ou GL_UNPACK_* 0x0CF2..); 0 = formato desconhecido
static ul img_bytes(int pack, int w, int h, int d, int is3d, unsigned f, unsigned t) {
  int b = px_bytes(f, t); if (!b) return 0;
  if (w <= 0 || h <= 0 || d <= 0) return 1;   // nada a transferir (1 = "conhecido")
  int al = 4, rl = 0, sr = 0, sp = 0, ih = 0, si = 0;
  g_GetIntegerv(pack ? 0x0D05 : 0x0CF5, &al); g_GetIntegerv(pack ? 0x0D02 : 0x0CF2, &rl);
  g_GetIntegerv(pack ? 0x0D03 : 0x0CF3, &sr); g_GetIntegerv(pack ? 0x0D04 : 0x0CF4, &sp);
  if (!pack && is3d) { g_GetIntegerv(0x806E, &ih); g_GetIntegerv(0x806D, &si); }
  if (al < 1 || al > 8) al = 4; if (rl < 0) rl = 0; if (sr < 0) sr = 0; if (sp < 0) sp = 0; if (ih < 0) ih = 0; if (si < 0) si = 0;
  ul row = (ul)(rl > 0 ? rl : w) * b; row = (row + al - 1) / al * al;
  ul img = (ul)(ih > 0 ? ih : h) * row;
  return (ul)(si + d - 1) * img + (ul)(sr + h - 1) * row + (ul)(sp + w) * b;
}
// dados de textura do guest: o GL pode ler mais do que o guest mandou (estado diferente, guest com defeito); completa com zeros
static char *tex_payload(Chan *c, ul n, int w, int h, int d, int is3d, unsigned f, unsigned t) {
  ul need = img_bytes(0, w, h, d, is3d, f, t);
  if (need > MAXPAY) { payload(c, n); return 0; }   // descarta
  ul m = need > n ? need : n; char *p = scratch(c, m);
  if (!p) { payload(c, n); return 0; }       // descarta
  if (n) rd(c, p, n);
  if (m > n) memset(p + n, 0, m - n);
  return p;
}
static void send_str(Chan *c, const char *s) { ul n = s ? strlen(s) : 0; wr(c, &n, 8); if (n) wr(c, s, n); }
static int getint_count(unsigned p) {
  int n = 0;
  switch (p) {
    case 0x0BA2: case 0x0C10: case 0x0C22: case 0x8005: return 4;  // viewport, scissor, clear color, blend color
    case 0x0D3A: case 0x846D: case 0x846E: return 2;
    case 0x0C23: return 4;
    case 0x86A3: g_GetIntegerv(0x86A2, &n); return n;
    case 0x87FF: g_GetIntegerv(0x87FE, &n); return n;
    case 0x8DF8: g_GetIntegerv(0x8DF9, &n); return n;
  }
  return 1;
}
static volatile int probe_x = -1, probe_y, probe_on; static unsigned probe_last; static int probe_n;
static void dispatch(Chan *c, unsigned op, ul *a) {
  switch (op) {
#define GEN_CASE
#include "gen_host.inc"
#undef GEN_CASE
    case OP_PixelStorei: g_PixelStorei((unsigned)a[0], (int)a[1]); break;
    case OP_BufferData: {
      if ((long)a[1] < 0) bad(c, "BufferData com tamanho negativo", a[1]);
      if (!a[2]) { g_BufferData((unsigned)a[0], (long)a[1], 0, (unsigned)a[3]); break; }
      char *p = payload(c, a[1]); if (p) g_BufferData((unsigned)a[0], (long)a[1], p, (unsigned)a[3]);
    } break;
    case OP_TexImage2D: {
      char *p = a[8] ? tex_payload(c, a[8], (int)a[3], (int)a[4], 1, 0, (unsigned)a[6], (unsigned)a[7]) : 0;
      if (a[8] && !p) break;
      g_TexImage2D((unsigned)a[0], (int)a[1], (int)a[2], (int)a[3], (int)a[4], (int)a[5], (unsigned)a[6], (unsigned)a[7], p);
    } break;
    case OP_TexSubImage2D: { char *p = tex_payload(c, a[8], (int)a[4], (int)a[5], 1, 0, (unsigned)a[6], (unsigned)a[7]); if (p) g_TexSubImage2D((unsigned)a[0], (int)a[1], (int)a[2], (int)a[3], (int)a[4], (int)a[5], (unsigned)a[6], (unsigned)a[7], p); } break;
    case OP_TexSubImage3D: { char *p = tex_payload(c, a[10], (int)a[5], (int)a[6], (int)a[7], 1, (unsigned)a[8], (unsigned)a[9]); if (p) g_TexSubImage3D((unsigned)a[0], (int)a[1], (int)a[2], (int)a[3], (int)a[4], (int)a[5], (int)a[6], (int)a[7], (unsigned)a[8], (unsigned)a[9], p); } break;
    // comprimidas: o GL confere imageSize (o mesmo número que o tamanho dos dados) contra as dimensões
    case OP_CompressedTexSubImage2D: { if (a[7] > 0x7fffffff) bad(c, "imageSize invalido", a[7]); char *p = payload(c, a[7]); if (p) g_CompressedTexSubImage2D((unsigned)a[0], (int)a[1], (int)a[2], (int)a[3], (int)a[4], (int)a[5], (unsigned)a[6], (int)a[7], p); } break;
    case OP_CompressedTexSubImage3D: { if (a[9] > 0x7fffffff) bad(c, "imageSize invalido", a[9]); char *p = payload(c, a[9]); if (p) g_CompressedTexSubImage3D((unsigned)a[0], (int)a[1], (int)a[2], (int)a[3], (int)a[4], (int)a[5], (int)a[6], (int)a[7], (unsigned)a[8], (int)a[9], p); } break;
#define GDC(N) \
    case OP_Gen##N: { int n = cnt(c, a[0]); unsigned *o = (unsigned *)scratch_or_die(c, n * 4ul); memset(o, 0, n * 4ul); g_Gen##N(n, o); wr(c, o, n * 4ul); } break; \
    case OP_Delete##N: { int n = cnt(c, a[0]); unsigned *o = (unsigned *)payload(c, n * 4ul); if (o) g_Delete##N(n, o); } break;
    GDC(Buffers) GDC(Framebuffers) GDC(Renderbuffers) GDC(Samplers) GDC(Textures) GDC(VertexArrays)
    case OP_DrawBuffers: { int n = cnt(c, a[0]); unsigned *p = (unsigned *)payload(c, n * 4ul); if (p) g_DrawBuffers(n, p); } break;
    case OP_InvalidateFramebuffer: { int n = cnt(c, a[1]); unsigned *p = (unsigned *)payload(c, n * 4ul); if (p) g_InvalidateFramebuffer((unsigned)a[0], n, p); } break;
    case OP_GetIntegerv: { int k = getint_count((unsigned)a[0]); ul n = k >= 0 && k <= 4096 ? (ul)k : 1; int *o = (int *)scratch_or_die(c, n * 4 + 64); memset(o, 0, n * 4 + 64); g_GetIntegerv((unsigned)a[0], o); wr(c, &n, 8); wr(c, o, n * 4); } break;
    case OP_GetInternalformativ: { int n = cnt(c, a[3]); int *o = (int *)scratch_or_die(c, n * 4ul); memset(o, 0, n * 4ul); g_GetInternalformativ((unsigned)a[0], (unsigned)a[1], (unsigned)a[2], n, o); wr(c, o, n * 4ul); } break;
    case OP_GetProgramiv: { int o = 0; g_GetProgramiv((unsigned)a[0], (unsigned)a[1], &o); wr(c, &o, 4); } break;
    case OP_GetShaderiv: { int o = 0; g_GetShaderiv((unsigned)a[0], (unsigned)a[1], &o); wr(c, &o, 4); } break;
    case OP_GetProgramInfoLog: { int n = 0; g_GetProgramiv((unsigned)a[0], 0x8B84, &n); if (n < 0 || n > MAXCNT) n = 0; char *b = scratch_or_die(c, n + 1ul); int l = 0; b[0] = 0; if (n) g_GetProgramInfoLog((unsigned)a[0], n, &l, b); if (l < 0 || l >= n) l = n ? n - 1 : 0; ul L = l ? l + 1 : 0; b[l] = 0; wr(c, &L, 8); if (L) wr(c, b, L); } break;
    case OP_GetShaderInfoLog: { int n = 0; g_GetShaderiv((unsigned)a[0], 0x8B84, &n); if (n < 0 || n > MAXCNT) n = 0; char *b = scratch_or_die(c, n + 1ul); int l = 0; b[0] = 0; if (n) g_GetShaderInfoLog((unsigned)a[0], n, &l, b); if (l < 0 || l >= n) l = n ? n - 1 : 0; ul L = l ? l + 1 : 0; b[l] = 0; wr(c, &L, 8); if (L) wr(c, b, L); } break;
    case OP_GetProgramBinary: { int bs = (int)a[1]; if (bs < 0 || (ul)bs > MAXPAY) bad(c, "bufSize invalido", a[1]); char *b = scratch_or_die(c, bs + 16ul); int l = 0; unsigned f = 0; g_GetProgramBinary((unsigned)a[0], bs, &l, &f, b); if (l < 0 || l > bs) l = 0; ul L = l, F = f; wr(c, &L, 8); wr(c, &F, 8); if (L) wr(c, b, L); } break;
    case OP_ProgramBinary: { if (a[2] > 0x7fffffff) bad(c, "tamanho invalido", a[2]); char *b = payload(c, a[2]); if (b) g_ProgramBinary((unsigned)a[0], (unsigned)a[1], b, (int)a[2]); } break;
    case OP_ShaderSource: { if (a[1] > 0x7fffffff) bad(c, "tamanho invalido", a[1]); char *b = payload(c, a[1]); if (!b) break; const char *s = b; int l = (int)a[1]; g_ShaderSource((unsigned)a[0], 1, &s, &l); } break;
    case OP_ReadPixels: {
      // o tamanho sai do estado de empacotamento do GL do host (o mesmo que o guest pediu por PixelStorei), não de uma tabela parcial
      int w = (int)a[2], h = (int)a[3]; unsigned f = (unsigned)a[4], t = (unsigned)a[5]; int pbo = 0; g_GetIntegerv(0x88ED, &pbo);
      ul n = w > 0 && h > 0 && w <= MAXDIM && h <= MAXDIM ? img_bytes(1, w, h, 1, 0, f, t) : 0;
      char *b = n && n <= MAXPAY && !pbo ? scratch(c, n) : 0;
      if (!b) { static int warned; if (n > 1 && warned++ < 5) fprintf(stderr, "[mguhost] ReadPixels %dx%d %#x/%#x recusado (pbo %d)\n", w, h, f, t, pbo); n = 0; }
      else { memset(b, 0, n); g_ReadPixels((int)a[0], (int)a[1], w, h, f, t, b); }
      wr(c, &n, 8); if (n) wr(c, b, n);
    } break;
#define UVC(N, SZ) case OP_Uniform##N: { ul n = cnt(c, a[1]) * (ul)(SZ); void *p = payload(c, n); if (p) g_Uniform##N((int)a[0], (int)a[1], p); } break;
    UVC(1fv, 4) UVC(2fv, 8) UVC(3fv, 12) UVC(4fv, 16) UVC(1iv, 4) UVC(2iv, 8) UVC(3iv, 12) UVC(4iv, 16)
#define UMC(N, SZ) case OP_UniformMatrix##N: { ul n = cnt(c, a[1]) * (ul)(SZ); float *p = (float *)payload(c, n); if (p) g_UniformMatrix##N((int)a[0], (int)a[1], (unsigned char)a[2], p); } break;
    UMC(2fv, 16) UMC(3fv, 36) UMC(4fv, 64)
    case OP_GetString: send_str(c, (const char *)g_GetString((unsigned)a[0])); break;
    case OP_GetStringi: send_str(c, (const char *)g_GetStringi((unsigned)a[0], (unsigned)a[1])); break;
    // --- EGL
    case OP_EGLChooseConfig: {
      int n = cnt(c, a[0]); int *at = (int *)payload(c, n * 4ul); if (!at) bad(c, "atributos", a[0]); at[n] = 0x3038;
      for (int i = 0; i + 1 < n; i += 2) if (at[i] == 0x3033) at[i + 1] = 1;  // SURFACE_TYPE: pbuffer no host
      void *res[64]; int num = 0, want = a[1] > 64 ? 64 : (int)a[1]; if (want < 1) want = 64;
      e_ChooseConfig(dpy, at, res, want, &num);
      fprintf(stderr, "[mguhost] ChooseConfig: %d\n", num);
      ul N = num; wr(c, &N, 8);
      pthread_mutex_lock(&mu);
      for (int i = 0; i < num; i++) { int k = -1; for (int j = 0; j < ncfg; j++) if (cfgs[j] == res[i]) k = j; if (k < 0 && ncfg < MAXO) { k = ncfg; cfgs[ncfg++] = res[i]; } ul id = k + 1; wr(c, &id, 8); }
      pthread_mutex_unlock(&mu);
    } break;
    case OP_EGLGetConfigAttrib: { int v = 0; int ok = a[0] && a[0] <= (ul)ncfg ? e_GetConfigAttrib(dpy, cfgs[a[0] - 1], (int)a[1], &v) : 0; ul r[2] = { (ul)ok, (ul)(long)v }; wr(c, r, 16); } break;
    case OP_EGLCreateContext: {
      int n = cnt(c, a[2]); int *at = (int *)payload(c, n * 4ul); if (!at) bad(c, "atributos", a[2]); at[n] = 0x3038;
      void *share = a[1] && a[1] < MAXO ? ctxs[a[1]] : 0, *cfg = a[0] && a[0] <= (ul)ncfg ? cfgs[a[0] - 1] : 0;
      void *x = e_CreateContext(dpy, cfg, share, at); ul id = 0;
      if (x) { pthread_mutex_lock(&mu); if (nctx < MAXO) { id = nctx++; ctxs[id] = x; ctxcfg[id] = cfg; } pthread_mutex_unlock(&mu);
               if (!id) { fprintf(stderr, "[mguhost] contextos demais (%d)\n", MAXO); e_DestroyContext(dpy, x); } }
      fprintf(stderr, "[mguhost] CreateContext share=%lu attrs(%d) -> id %lu err %x\n", a[1], n, id, x ? 0 : e_GetError());
      wr(c, &id, 8);
    } break;
    case OP_EGLDestroyContext: break;  // mantido: o processo inteiro morre junto com o HMI
    case OP_EGLCreateSurface: {
      int pa[] = { 0x3057, (int)a[2], 0x3056, (int)a[3], 0x3038 };
      void *cfg = a[1] && a[1] <= (ul)ncfg ? cfgs[a[1] - 1] : 0;
      void *s = e_CreatePbufferSurface(dpy, cfg, pa);
      if (s && a[0] >= MAXO) { e_DestroySurface(dpy, s); s = 0; }
      if (s) { sfs[a[0]].s = s; sfs[a[0]].w = (int)a[2]; sfs[a[0]].h = (int)a[3]; sfs[a[0]].cfg = cfg; }
      fprintf(stderr, "[mguhost] superficie %lu %lux%lu -> %p err %x\n", a[0], a[2], a[3], s, s ? 0 : e_GetError());
      ul ok = s != 0; wr(c, &ok, 8);
    } break;
    case OP_EGLDestroySurface: break;
    case OP_EGLMakeCurrent: {
      void *x = a[2] && a[2] < MAXO ? ctxs[a[2]] : 0, *ds = a[0] < MAXO ? sfs[a[0]].s : 0, *rs = a[1] < MAXO ? sfs[a[1]].s : 0;
      int ok = e_MakeCurrent(dpy, ds, rs, x);
      if (ok) cur_ctx = x ? a[2] : 0;
      if (!ok) fprintf(stderr, "[mguhost] MakeCurrent falhou %x\n", e_GetError());
      ul r = ok; wr(c, &r, 8);
    } break;
    case OP_EGLSwap: {
      ul id = a[0];
      if (id < MAXO && sfs[id].s) {
        unsigned long f = __atomic_add_fetch(&frames, 1, __ATOMIC_RELAXED);
        if (probe_on) { probe_on--; fprintf(stderr, "[sonda] fim de quadro %d\n", probe_on); probe_last = 0; probe_n = 0; }
        write_frame(sfs[id].w, sfs[id].h);     // limitado a ~30 quadros/s lá dentro
        e_SwapBuffers(dpy, sfs[id].s);
        if (f <= 5 || f % 120 == 1) fprintf(stderr, "[mguhost] quadro %lu (exportacao media %.2f ms)\n", f, frame_cost_n ? frame_cost * 1e3 / frame_cost_n : 0.0);
      }
      ul ok = 1; wr(c, &ok, 8);
    } break;
    default:
      fprintf(stderr, "[mguhost] op desconhecido %u\n", op); c->dead = 1; pthread_exit(0);
  }
  static int errs;
  if (errs < 60 && p_GetError) { unsigned e = p_GetError(); if (e) { errs++; fprintf(stderr, "[mguhost] GL erro %#x depois do op %u (args %#lx %#lx %#lx)\n", e, op, a[0], a[1], a[2]); } }
}
// --- sonda de depuração: "x <px> <py>" no input.sock registra, no próximo quadro, cada desenho que muda aquele pixel
static void probe_dump_prog(unsigned prog) {
  static void (*attached)(unsigned, int, int *, unsigned *); static void (*src)(unsigned, int, int *, char *);
  static void (*act)(unsigned, unsigned, int, int *, int *, unsigned *, char *); static void (*getf)(unsigned, int, float *); static void (*geti)(unsigned, int, int *);
  if (!attached) { attached = gp("glGetAttachedShaders"); src = gp("glGetShaderSource"); act = gp("glGetActiveUniform"); getf = gp("glGetUniformfv"); geti = gp("glGetUniformiv"); }
  unsigned sh[4]; int n = 0; attached(prog, 4, &n, sh);
  static char big[65536];
  for (int i = 0; i < n; i++) { int l = 0; src(sh[i], sizeof big - 1, &l, big); big[l] = 0; fprintf(stderr, "---- shader %u\n%s\n", sh[i], big); }
  int nu = 0; g_GetProgramiv(prog, 0x8B86, &nu);
  for (int i = 0; i < nu; i++) {
    char nm[128]; int sz, l; unsigned ty; act(prog, i, 127, &l, &sz, &ty, nm); nm[l] = 0; int loc = p_GetUniformLocation(prog, nm);
    float f[16] = {0}; int iv[4] = {0};
    if (ty == 0x1406 || (ty >= 0x8B50 && ty <= 0x8B52)) { getf(prog, loc, f); fprintf(stderr, "  u %s sz%d = %g %g %g %g\n", nm, sz, f[0], f[1], f[2], f[3]); }
    else if (ty == 0x1404 || ty >= 0x8B5E) { geti(prog, loc, iv); int tb = 0; fprintf(stderr, "  u %s ty%#x = %d\n", nm, ty, iv[0]); }
    else fprintf(stderr, "  u %s ty%#x\n", nm, ty);
  }
}
static void probe_after_draw(void) {
  int fb = 0, prog = 0; g_GetIntegerv(0x8CA6, &fb); g_GetIntegerv(0x8B8D, &prog);
  g_GetIntegerv(0x8CAA, &(int){0});
  int vp[4]; g_GetIntegerv(0x0BA2, vp);
  if (fb) { static int lastfb = -1; if (fb != lastfb) { lastfb = fb; probe_last = 0; fprintf(stderr, "[sonda] fbo %d viewport %dx%d prog %d\n", fb, vp[2], vp[3], prog); } if (vp[2] != 2880) return; }
  unsigned char px[4] = {0}; int al = 4; g_GetIntegerv(0x0D05, &al); g_PixelStorei(0x0D05, 1);
  g_ReadPixels(probe_x, probe_y, 1, 1, 0x1908, 0x1401, px); g_PixelStorei(0x0D05, al);
  unsigned v = px[0] | px[1] << 8 | px[2] << 16 | (unsigned)px[3] << 24;
  if (v != probe_last) {
    probe_last = v; int bl = 0, bs = 0, bd = 0; g_GetIntegerv(0x0BE2, &bl); g_GetIntegerv(0x80C9, &bs); g_GetIntegerv(0x80C8, &bd); int tex = 0; g_GetIntegerv(0x8069, &tex);
    fprintf(stderr, "[sonda] draw %d prog %d -> %02x%02x%02x%02x blend %d src %#x dst %#x tex0? %d\n", probe_n, prog, px[0], px[1], px[2], px[3], bl, bs, bd, tex);
    probe_dump_prog(prog);
  }
  probe_n++;
}
static void *chan_thread(void *arg) {
  Chan *c = arg;
  for (;;) {
    unsigned hd[2]; rd(c, hd, 8);
    ul a[16]; if (hd[1] > 16) { fprintf(stderr, "[mguhost] mensagem invalida op %u n %u\n", hd[0], hd[1]); return 0; }
    if (hd[1]) rd(c, a, hd[1] * 8ul);
    dispatch(c, hd[0], a);
    if (probe_on && (hd[0] == OP_DrawElements || hd[0] == OP_DrawArrays || hd[0] == OP_DrawElementsInstanced || hd[0] == OP_DrawArraysInstanced)) probe_after_draw();
  }
}
static Chan *chans;
static Chan *input_chan, *hmi_chan;
static void *input_thread(void *arg) {
  // datagramas de texto em <dir>/input.sock: "d x y", "m x y", "u"
  char p[1024]; snprintf(p, sizeof p, "%s/input.sock", outdir); unlink(p);
  int s = socket(AF_UNIX, SOCK_DGRAM, 0); struct sockaddr_un a; memset(&a, 0, sizeof a); a.sun_family = AF_UNIX; strncpy(a.sun_path, p, sizeof a.sun_path - 1);
  if (bind(s, (void *)&a, sizeof a)) { perror("input.sock"); return 0; }
  char b[256];
  for (;;) {
    long n = recv(s, b, sizeof b - 2, 0); if (n <= 0) continue;
    if (b[n - 1] != '\n') b[n++] = '\n';
    if (b[0] == 'x') { int x = 0, y = 0; b[n] = 0; sscanf(b + 1, "%d %d", &x, &y); probe_x = x; probe_y = 960 - y; probe_on = 2; fprintf(stderr, "[sonda] armada %d %d\n", x, y); continue; }
    int touch = b[0] == 'd' || b[0] == 'm' || b[0] == 'u';
    Chan *c = touch ? input_chan : hmi_chan; if (!c || !alive(c)) continue;
    wr(c, b, n);
  }
}
static void scan(void) {
  // só páginas já tocadas: ler o resto obrigaria o macOS a alocar toda a RAM do guest
  static char *vec; const ul HP = 16384; if (!vec) vec = malloc(ramsz / HP + 1); if (!vec) return;
  if (mincore(ram, ramsz, vec)) memset(vec, 1, ramsz / HP + 1);
  for (ul off = 0; off + CH_PAGE <= ramsz; off += CH_PAGE) {
    if (!(vec[off / HP] & 1)) { off = (off / HP + 1) * HP - CH_PAGE; continue; }
    ch_hdr *h = (ch_hdr *)(ram + off);
    if (*(uint64_t *)h->magic != 0x4d2d554d45444949ul && memcmp(h->magic, CH_MAGIC, 8)) continue;
    if (memcmp(h->magic, CH_MAGIC, 16)) continue;
    int known = 0; for (Chan *k = chans; k; k = k->next) if (k->hphys == off && k->nonce == h->nonce && !k->dead) known = 1;
    if (known) continue;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    // o cabeçalho e as tabelas vêm do guest: copia uma vez e valida tamanhos e cada PFN antes de virar ponteiro
    ch_hdr hc; memcpy(&hc, (const void *)h, sizeof hc);
    ul tot = (ul)hc.pages_a + hc.pages_b, need_tbl = (tot * 4 + CH_PAGE - 1) / CH_PAGE;
    const char *why = 0; ul badv = 0;
    if (hc.pages_a != CH_RING_A / CH_PAGE || hc.pages_b != CH_RING_B / CH_PAGE) why = "tamanho dos aneis", badv = (ul)hc.pages_a << 32 | hc.pages_b;
    else if (hc.ntable > CH_MAXTABLE || hc.ntable < need_tbl) why = "ntable", badv = hc.ntable;
    else for (ul i = 0; i < need_tbl && !why; i++) if (!pfn_ok(hc.table_pfn[i])) why = "pfn da tabela", badv = hc.table_pfn[i];
    unsigned *pfn = why ? 0 : malloc(tot * 4);
    if (!why && !pfn) why = "sem memoria";
    for (ul i = 0; !why && i < tot; i++) { pfn[i] = *(volatile unsigned *)(pg(hc.table_pfn[i * 4 / CH_PAGE]) + (i * 4) % CH_PAGE); if (!pfn_ok(pfn[i])) why = "pfn do anel", badv = pfn[i]; }
    if (why) {
      free(pfn);
      static ul rej[64]; static int nrej; int seen = 0;    // cada canal recusado aparece uma vez no log
      for (int i = 0; i < nrej; i++) if (rej[i] == (off ^ hc.nonce)) seen = 1;
      if (!seen) { if (nrej < 64) rej[nrej++] = off ^ hc.nonce; fprintf(stderr, "[mguhost] canal recusado em %#lx: %s (%#lx)\n", off + GUEST_BASE, why, badv); }
      continue;
    }
    Chan *c = calloc(1, sizeof(Chan)); if (!c) { free(pfn); continue; }
    c->hphys = off; c->h = h; c->nonce = hc.nonce; c->pa = hc.pages_a; c->pb = hc.pages_b; c->pfn = pfn;
    c->next = chans; chans = c;
    fprintf(stderr, "[mguhost] canal novo pid %u tid %u em %#lx\n", hc.pid, hc.tid, off + GUEST_BASE);
    if (hc.role == 1) { input_chan = c; continue; }
    if (hc.role == 2) { hmi_chan = c; continue; }
    pthread_t t; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 8 << 20); pthread_create(&t, &at, chan_thread, c);
  }
}
int main(int argc, char **argv) {
  if (argc < 5) { fprintf(stderr, "uso: mguhost ram.bin MiB dir angle\n"); return 2; }
  ramsz = (ul)atol(argv[2]) << 20; outdir = argv[3];
  int fd = shm_open(argv[1], O_RDWR); if (fd < 0) { perror("ram"); return 1; }
  ram = mmap(0, ramsz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); if (ram == MAP_FAILED) { perror("mmap"); return 1; }
  char b[2048]; snprintf(b, sizeof b, "%s/libEGL.dylib", argv[4]); hegl = dlopen(b, RTLD_NOW | RTLD_GLOBAL);
  snprintf(b, sizeof b, "%s/libGLESv2.dylib", argv[4]); hgl = dlopen(b, RTLD_NOW | RTLD_GLOBAL);
  if (!hegl || !hgl) { fprintf(stderr, "ANGLE: %s\n", dlerror()); return 1; }
  egp = dlsym(hegl, "eglGetProcAddress");
#define EL(N) e_##N = dlsym(hegl, "egl" #N)
  EL(GetPlatformDisplay); EL(Initialize); EL(BindAPI); EL(ChooseConfig); EL(GetConfigAttrib); EL(CreateContext); EL(DestroyContext);
  EL(CreatePbufferSurface); EL(DestroySurface); EL(MakeCurrent); EL(SwapBuffers); EL(GetError); EL(WaitGL);
  intptr_t at[] = { 0x3203, 0x3489, 0x3038, 0 };
  dpy = e_GetPlatformDisplay(0x3202, 0, at); int ma = 0, mi = 0;
  if (!e_Initialize(dpy, &ma, &mi)) { fprintf(stderr, "eglInitialize falhou\n"); return 1; }
  e_BindAPI(0x30A0); load_gl();
  fprintf(stderr, "[mguhost] ANGLE EGL %d.%d pronto, RAM %lu MiB\n", ma, mi, ramsz >> 20);
  pthread_t it; pthread_create(&it, 0, input_thread, 0);
  double started = now();
  for (;;) { scan(); usleep(now() - started < 90 ? 250000 : 1000000); }
}
