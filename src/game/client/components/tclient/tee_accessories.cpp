#include "tee_accessories.h"
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <game/client/animstate.h>
#include <game/client/render.h>
#include <cmath>
#include <algorithm>

void RenderTeeAccessories(IGraphics *G, const CAnimState *Anim, const CTeeRenderInfo *Info, vec2 Direction, vec2 Position, float Alpha)
{
 if((!g_Config.m_XcTeeHat && !g_Config.m_XcTeeGlasses) || Alpha <= 0) return;
 float Size = Info->m_Size;
 if(g_Config.m_TcTinyTees) Size *= 0.7f * g_Config.m_TcTinyTeeSize / 100.0f;
 const float S = Size / 64.0f;
 const vec2 Body = Position + vec2(Anim->GetBody()->m_X, Anim->GetBody()->m_Y) * S;
 const float Angle = Anim->GetBody()->m_Angle * 360.0f;
 const auto Point = [&](vec2 P) { return Body + rotate(P * S, Angle); };
 const ColorRGBA Ink(0.07f, 0.08f, 0.12f, 1);
 G->TextureClear(); G->QuadsBegin(); G->QuadsSetRotation(0);
 // Ellipses use a triangle fan, keeping every ornament in body-local coordinates.
 const auto Oval = [&](float X, float Y, float W, float H, ColorRGBA C) {
  G->SetColor(C.WithAlpha(C.a * Alpha));
  const vec2 Center = Point(vec2(X,Y));
  for(int i=0;i<32;++i) {
   const float A = i * 2*pi/32, B = (i+1)*2*pi/32;
   const vec2 P = Point(vec2(X+std::cos(A)*W,Y+std::sin(A)*H));
   const vec2 Q = Point(vec2(X+std::cos(B)*W,Y+std::sin(B)*H));
   IGraphics::CFreeformItem F(Center.x,Center.y,P.x,P.y,Q.x,Q.y,Q.x,Q.y); G->QuadsDrawFreeform(&F,1);
  }
 };
 const auto Tri = [&](vec2 A,vec2 B,vec2 C,ColorRGBA Color) {
  A=Point(A); B=Point(B); C=Point(C); G->SetColor(Color.WithAlpha(Color.a*Alpha));
  IGraphics::CFreeformItem F(A.x,A.y,B.x,B.y,C.x,C.y,C.x,C.y); G->QuadsDrawFreeform(&F,1);
 };
 const auto Hat = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_XcTeeHatColor));
 const auto Light = ColorRGBA(std::min(1.0f,Hat.r+0.2f),std::min(1.0f,Hat.g+0.2f),std::min(1.0f,Hat.b+0.2f));
 switch(g_Config.m_XcTeeHat) {
 case 1: // Rounded cap with curved visor, seam and button.
  Oval(0,-25,22,13,Ink); Oval(0,-26,20,11,Hat);
  Oval(Direction.x<0?-16:16,-20,15,4,Ink); Oval(Direction.x<0?-16:16,-21,13,2.5f,Hat);
  Oval(-6,-30,8,3,Light); Oval(0,-38,3,2,Ink); break;
 case 2:
  Oval(0,-24,26,5,Ink); Oval(0,-25,24,3,Hat);
  Tri(vec2(-16,-25),vec2(-14,-46),vec2(16,-25),Ink); Tri(vec2(-14,-46),vec2(14,-46),vec2(16,-25),Ink);
  Tri(vec2(-13,-27),vec2(-11,-44),vec2(13,-27),Hat); Tri(vec2(-11,-44),vec2(11,-44),vec2(13,-27),Hat);
  Oval(0,-30,13,2,ColorRGBA(0.93f,0.68f,0.25f)); Oval(0,-46,14,3,Ink); Oval(0,-46,12,1.5f,Light); break;
 case 3: // Beret.
  Oval(-3,-28,25,11,Ink); Oval(-3,-29,23,9,Hat); Oval(0,-22,19,3,Ink); Oval(0,-23,17,1.5f,Hat); Oval(-5,-39,2,4,Ink); Oval(-12,-32,8,2,Light); break;
 case 4: // Crown.
  for(int i=-1;i<=1;++i) { const float X=i*15.0f;
   Tri(vec2(X-9,-25),vec2(X,-44+(i?5:0)),vec2(X+9,-25),Ink);
   Tri(vec2(X-6,-27),vec2(X,-40+(i?5:0)),vec2(X+6,-27),Hat);
   Oval(X,-43+(i?5:0),2.5f,2.5f,Light);
  }
  Oval(0,-25,24,4,Ink); Oval(0,-26,22,2,Hat); Oval(0,-30,3,4,ColorRGBA(0.95f,0.24f,0.4f)); break;
 case 5: // Cat ears.
  for(int i : {-1,1}) { float X=i*17.0f;
   Tri(vec2(X-10,-20),vec2(X+i*3,-43),vec2(X+10,-20),Ink);
   Tri(vec2(X-7,-22),vec2(X+i*3,-39),vec2(X+7,-22),Hat);
   Tri(vec2(X-4,-24),vec2(X+i*3,-34),vec2(X+4,-24),ColorRGBA(1,0.64f,0.72f));
  } break;
 case 6: // Bunny ears with soft pink inner panels.
  for(int i : {-1,1}) { Oval(i*12,-39,7,21,Ink); Oval(i*12,-39,5,19,Hat); Oval(i*12,-40,2.5f,14,ColorRGBA(1,0.65f,0.75f)); } break;
 }
 if(g_Config.m_XcTeeGlasses) {
  const auto Frame = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_XcTeeGlassesColor));
  const vec2 Eye(Direction.x*7,-3+Direction.y*5);
  const int Kind=g_Config.m_XcTeeGlasses;
  for(int i : {-1,1}) {
   if(Kind==4 && i<0) continue;
   const float X=Eye.x+i*8, Y=Eye.y;
   const float H=Kind==3?4.5f:6;
   Oval(X,Y,8,H+1,Ink); Oval(X,Y,6.5f,H,Frame);
   Oval(X,Y,5,H-1.5f,Kind==1?ColorRGBA(0.12f,0.2f,0.29f):ColorRGBA(0.65f,0.86f,1,0.32f));
   Oval(X-1.5f,Y-2,2,0.7f,ColorRGBA(1,1,1,0.7f));
  }
  if(Kind!=4) Oval(Eye.x,Eye.y-1,3,1.2f,Frame);
  else { Oval(Eye.x+13,Eye.y+9,1,6,ColorRGBA(0.9f,0.72f,0.3f)); Oval(Eye.x+13,Eye.y+17,1,2,ColorRGBA(0.9f,0.72f,0.3f)); }
 }
 G->QuadsEnd(); G->SetColor(1,1,1,1);
}
