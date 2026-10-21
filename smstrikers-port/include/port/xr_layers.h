// strikers-quest: which draws belong on the Tabletop field and which are the stadium around it.
//
// The Tabletop mode draws the game's own field (pitch, goals, nets, fence) with the players on it,
// small and movable; the stands, crowd, sky and jumbotron stay out of it so the whole field can be
// seen from any side. Every DrawableModel gets a matrix handle of its own each frame, so
// DrawableModel::DrawModel marks the handles of stadium objects, and the eye re-send for the table
// layer (glPlat.cpp) skips packets carrying a marked handle at the draw (glxSend.cpp), keeping the
// render-state changes in step.
#ifndef PORT_XR_LAYERS_H
#define PORT_XR_LAYERS_H

#include <stdint.h>

#ifdef __cplusplus

class DrawableObject;

// DrawableModel::DrawModel, while a headset 3D mode is on: this object's handle for this frame.
void PortXrClassifyAndMark(const DrawableObject* object, uintptr_t matrixHandle);
// True while a headset 3D mode is on (the marks below only matter then).
bool PortXrLayersActive();
// Draws that are stadium by nature but have no DrawableObject (the floodlight glows): the caller
// gives them a matrix handle of their own for this frame and marks it.
void PortXrMarkStadiumHandle(uintptr_t matrixHandle);
// world.cpp DoTranslucency, in the stadium-layer modes: this object is camera-side (the gameplay
// camera hides or fades it); hiddenForCamera = the game would hide it from the broadcast view this
// frame. It is drawn solid for the eyes and left out of the normal (broadcast) send only.
void PortXrNoteCameraSide(const DrawableObject* object, bool hiddenForCamera);
// glxSend.cpp, at the draw: true when the packet is stadium and either the table layer is being
// sent or the object lies wholly outside the camera of the view being sent.
bool PortXrSkipPacketDraw(uintptr_t matrixHandle);
// glPlat.cpp, before each 3D world view is sent (the game camera in the normal send, an eye in the
// eye sends): that view's view matrix (row-vector nlMatrix4) and GX projection, so stadium objects
// it cannot see are left out; NULL turns culling off. The game's own culling is off in the headset
// 3D modes because the eyes look all around; this is per send instead. STRIKERS_XR_CULL=0 disables.
void PortXrSetCullCamera(const float* view16, const float* proj16);
// r59, glxSend.cpp at the draw: 1 when this packet is field ground reaching past the field box and the
// table layer is being sent; `planes16` gets the field box's four sides in the send's eye space for
// GXAuroraSetXrClip around the draw.
int PortXrTableClip(uintptr_t matrixHandle, float planes16[16]);
// xr_quest.cpp's stats line: stadium objects left out by that culling since the last call.
unsigned long PortXrLayersTakeCulled();
// glPlat.cpp: around the table layer's eye re-sends.
void PortXrSetTableLayer(bool table);
// glPlat.cpp: around every eye re-send (either layer).
void PortXrSetEyeSend(bool eyeSend);
// xr_quest.cpp, self-test tuning (xr_tune): leave the crowd, the other stadium objects, or the
// stadium layer's life-size field out of the eye sends, to measure what each costs.
void PortXrTuneSkip(int crowd, int stands, int stadiumField);
// Self-test (xr_tune "probe"): log each field object's centre, flags and on-screen position (NDC)
// at the next table-layer send, to name things seen in a snapshot.
void PortXrProbe();
// xr_quest.cpp, every frame: stadium layer economies. Stadium objects (not the crowd) smaller than
// minPx across in an eye are left out (0 = off); with skipTallField the life-size goals, benches and
// fence are left out when bothLayers (Tabletop · Stadium, where the table carries the game).
void PortXrSetStadiumEconomy(float minPx, float eyeHalfWidthPx, int skipTallField, int bothLayers);
// glPlat.cpp, once per frame after the sends: the handles are frame memory, reused next frame.
void PortXrLayersEndFrame();
// r42: the life-size goal scorer (Game/XrScorer.cpp), a copy of the scorer posed and sent by the
// port: the matrix handle all its packets carry this frame (0 = none). Those packets are drawn only
// in the scorer eye pass, and that pass draws nothing else. Cleared by PortXrLayersEndFrame.
void PortXrSetScorerHandle(uintptr_t matrixHandle);
// glPlat.cpp: around the scorer layer's eye re-sends.
void PortXrSetScorerLayer(bool scorer);
// r43b, DrawableBall::Render: the game ball's drawable. Always field, wherever it is when first
// classified (in the air or in the net when you switch to Tabletop, it would have stayed stadium).
void PortXrSetBall(const DrawableObject* ball);
// r53 draw census, glxSend.cpp at each draw that goes out: counted by send (the broadcast picture,
// the table, the stadium, the scorer) and GL view. xr_quest.cpp's stats line takes the counts as
// text (per game frame over `frames`) and clears them.
void PortXrCountDraw(int view);
void PortXrTakeDrawCensus(char* out, unsigned long size, unsigned long frames);
// r53: a new match is being set up: log the field/stadium split of its objects again (the r20-r52
// log stopped after the first 400 objects of the session, so later stadiums never showed).
void PortXrLayersNewMatch(int stadium);

#endif

#endif // PORT_XR_LAYERS_H
