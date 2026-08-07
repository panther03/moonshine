#pragma once

#include <Dolphin/types.h>
#include <JSystem/J2D/J2DPane.hxx>
#include <JSystem/JUtility/JUTRect.hxx>

class J2DOrthoGraph : public J2DGrafContext {
public:
    J2DOrthoGraph(int x, int y, int w, int h);
    J2DOrthoGraph(const JUTRect &);
    ~J2DOrthoGraph();

    void setPort();
    void setLookat();
    void scissorBounds(JUTRect *, JUTRect *);

    // The coordinate space setPort() feeds to C_MTXOrtho, as distinct from
    // mBounds, which is the viewport it is mapped onto. The two constructors
    // always set this to (0, 0, w, h); assign it afterwards for a space whose
    // origin is not (0, 0). See afterDraw() in src/main.cpp.
    /* 0xD8 */ JUTRect mOrtho;
    /* 0xE8 */ f32 mNear;
    /* 0xEC */ f32 mFar;
};