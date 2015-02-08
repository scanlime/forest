// Circle Engine (c) 2015 Micah Elizabeth Scott
// MIT license

#include "cinder/app/AppNative.h"
#include "cinder/params/Params.h"
#include "cinder/gl/gl.h"
#include "cinder/gl/GlslProg.h"
#include "cinder/gl/Fbo.h"
#include "cinder/gl/Vbo.h"
#include "cinder/svg/Svg.h"
#include "cinder/Triangulate.h"
#include "cinder/TriMesh.h"
#include "Box2D/Box2D.h"
#include "Cinder-AppNap.h"
#include "CircleWorld.h"
#include "ParticleRender.h"
#include "FadecandyGL.h"

using namespace ci;
using namespace ci::app;
using namespace std;

class CircleEngineApp : public AppNative {
public:
    void prepareSettings( Settings *settings );
    void setup();
    void shutdown();
    void update();
    void draw();

private:
    void drawObstacles();
    void drawFrontLayer();
    void drawSpinners();
    void drawForceGrid();
    void reloadColorTable();
    void clearColorCubes();
    void logCurrentSpinnerAngle();
    
    static void physicsThreadFn(CircleEngineApp *self);
    
    FadecandyGL         mFadecandy;
    thread              mPhysicsThread;
    mutex               mPhysicsMutex;
    bool                mExiting;
    CircleWorld         mWorld;
    ParticleRender      mParticleRender;
    Rectf               mParticleRect;
    gl::VboMeshRef      mObstaclesVbo;
    gl::VboMeshRef      mFrontLayerVbo;
    
    params::InterfaceGlRef      mParams;
    float                       mAverageFps;
    float                       mPhysicsHz;
    unsigned                    mNumParticles;
    bool                        mDrawForceGrid;
    bool                        mDrawLedBuffer;
    bool                        mDrawLedModel;
    int                         mDrawSpinnerColorCube;
    bool                        mDrawObstacles;
    bool                        mDrawParticles;
    bool                        mDrawFrontLayer;
    bool                        mSelectedSpinnerControlsAll;
};

void CircleEngineApp::prepareSettings( Settings *settings )
{
    settings->setWindowSize( 1280, 720 );
    settings->disableFrameRate();
}

void CircleEngineApp::setup()
{
    Cinder::AppNap::BeginActivity("CircleEngine LED rendering");

    mWorld.setup(svg::Doc::create(loadAsset("world.svg")));
    mObstaclesVbo = gl::VboMesh::create(mWorld.mObstacles);
    mFrontLayerVbo = gl::VboMesh::create(mWorld.mFrontLayer);
    reloadColorTable();

    mFadecandy.setup(*this);
    mFadecandy.setModel(mWorld.mLedPoints);
    
    float scale = 2;
    mParticleRect = Rectf(0, 0, getWindowWidth(), getWindowHeight());
    mParticleRender.setup( *this, getWindowWidth() / scale, getWindowHeight() / scale, 1.0f / scale / mWorld.kMetersPerPoint );
 
    mParams = params::InterfaceGl::create( getWindow(), "Engine parameters", toPixels(Vec2i(240, 600)) );
    
    mParams->addParam("FPS", &mAverageFps, "readonly=true");
    mParams->addParam("Physics Hz", &mPhysicsHz, "readonly=true");
    mParams->addParam("# particles", &mNumParticles, "readonly=true");
    mParams->addSeparator();
    mParams->addParam("Current table row", &mWorld.mCurrentTableRow, "readonly=true");
    mParams->addButton("Reload color table", bind( &CircleEngineApp::reloadColorTable, this ), "key=c");
    mParams->addSeparator();
    mParams->addParam("Particle brightness", &mParticleRender.mBrightness).min(0.f).max(5.f).step(0.01f);
    mParams->addParam("Particle rate", &mWorld.mNewParticleRate);
    mParams->addParam("Particle lifetime", &mWorld.mNewParticleLifetime);
    mParams->addParam("LED sampling radius", &mFadecandy.samplingRadius).min(0.f).max(500.f).step(0.1f);
    mParams->addSeparator();
    mParams->addParam("Draw force grid", &mDrawForceGrid, "key=1");
    mParams->addParam("Draw LED model", &mDrawLedModel, "key=2");
    mParams->addParam("Draw LED buffer", &mDrawLedBuffer, "key=3");
    mParams->addParam("Draw obstacles", &mDrawObstacles, "key=4");
    mParams->addParam("Draw particles", &mDrawParticles, "key=5");
    mParams->addParam("Draw front layer", &mDrawFrontLayer, "key=6");
    mParams->addSeparator();
    mParams->addParam("Spin randomly", &mWorld.mMoveSpinnersRandomly);
    mParams->addParam("Spinner motor power", &mWorld.mSpinnerPower).min(0.f).max(100.f).step(.01f);
    mParams->addParam("Show color cube test", &mDrawSpinnerColorCube).min(-1).max(40).keyDecr("[").keyIncr("]");
    mParams->addParam("One spinner controls all", &mWorld.mOneSpinnerControlsAll);
    mParams->addButton("Clear all color cubes", bind( &CircleEngineApp::clearColorCubes, this ), "key=q");
    mParams->addButton("Log current spinner angle", bind( &CircleEngineApp::logCurrentSpinnerAngle, this ), "key=l");
    
    gl::disableVerticalSync();
    gl::disable(GL_DEPTH_TEST);
    gl::disable(GL_CULL_FACE);
    
    mDrawLedBuffer = false;
    mDrawForceGrid = false;
    mDrawSpinnerColorCube = -1;
    mDrawObstacles = true;
    mDrawLedModel = false;
    mDrawParticles = true;
    mDrawFrontLayer = false;
    mExiting = false;

    mPhysicsThread = thread(physicsThreadFn, this);
}

void CircleEngineApp::reloadColorTable()
{
    // Do this with the lock held, since we're reallocating the image
    mPhysicsMutex.lock();
    mWorld.initColors(loadImage(loadAsset("colors.png")));
    mPhysicsMutex.unlock();
}

void CircleEngineApp::clearColorCubes()
{
    mPhysicsMutex.lock();
    mWorld.clearColorCubes();
    mPhysicsMutex.unlock();
}

void CircleEngineApp::physicsThreadFn(CircleEngineApp *self)
{
    const unsigned kStepsPerMeasurement = 10;
    ci::midi::Hub midi;
    
    while (!self->mExiting) {
        self->mPhysicsMutex.lock();
        ci::Timer stepTimer(true);
        for (unsigned i = kStepsPerMeasurement; i; i--) {
            self->mWorld.update(midi);
        }
        self->mPhysicsHz = kStepsPerMeasurement / stepTimer.getSeconds();
        self->mPhysicsMutex.unlock();
    }
}

void CircleEngineApp::shutdown()
{
    mExiting = true;
    mPhysicsThread.join();
    Cinder::AppNap::EndActivity();
}

void CircleEngineApp::update()
{

    mAverageFps = getAverageFps();
    mNumParticles = mWorld.mParticleSystem->GetParticleCount();
}

void CircleEngineApp::draw()
{
    mParticleRender.render(*mWorld.mParticleSystem);

    gl::setViewport(Area(Vec2f(0,0), getWindowSize()));
    gl::setMatricesWindowPersp( getWindowSize() );
    gl::clear(Color( 0, 0, 0 ));

    if (mDrawParticles) {
        gl::enable(GL_TEXTURE_2D);
        mParticleRender.getTexture().bind();
        gl::color(1,1,1,1);
        gl::drawSolidRect(mParticleRect);
        gl::disable(GL_TEXTURE_2D);
    }
        
    if (mDrawForceGrid) {
        drawForceGrid();
    }
    
    if (mDrawFrontLayer) {
        if (mDrawObstacles) {
            drawSpinners();
        }
        drawFrontLayer();
    } else {
        if (mDrawObstacles) {
            drawObstacles();
            drawSpinners();
        }
    }

    if (mDrawLedModel) {
        mFadecandy.drawModel();
    }

    if (mDrawLedBuffer) {
        const gl::Texture& tex = mFadecandy.getFramebufferTexture();
        float scale = 4.0;
        Vec2f topLeft(400, 10);
        gl::disableAlphaBlending();
        gl::color(1.0f, 1.0f, 1.0f, 1.0f);
        gl::draw(tex, Rectf(topLeft, topLeft + tex.getSize() * scale));
    }

    if (mDrawSpinnerColorCube >= 0 && mDrawSpinnerColorCube < mWorld.mSpinners.size()) {
        auto& spinner = mWorld.mSpinners[mDrawSpinnerColorCube];
        auto& cube = spinner.mColorCube;
        float s = getWindowWidth() * 0.25f;

        gl::pushModelView();
        gl::translate(getWindowWidth() * 0.5f, getWindowHeight() * 0.5f, 0.0f);
        gl::scale(Vec3f(s,s,s));
        gl::rotate(Vec3f(-10 - getMousePos().y * 0.06, 40 + getMousePos().x * 0.1, 0));
        gl::translate(-0.5f, -0.5f, -0.5f);
        cube.draw();
        gl::popModelView();

        gl::enableAlphaBlending();
        Vec2f cursor = Vec2f(300, getWindowHeight() * 0.75f);
        char str[128];

        snprintf(str, sizeof str, "Spinner #%d", mDrawSpinnerColorCube);
        gl::drawString(str, cursor);
        cursor.y += 15.0f;

        snprintf(str, sizeof str, "%d points", (int)cube.getPoints().size());
        gl::drawString(str, cursor);
        cursor.y += 15.0f;

        snprintf(str, sizeof str, "Sensor angle: %.1f deg  (reliable = %d)", cube.getCurrentAngle() * 180.0 / M_PI, cube.isAngleReliable());
        gl::drawString(str, cursor);
        cursor.y += 15.0f;

        snprintf(str, sizeof str, "Target angle: %.1f deg", spinner.mTargetAngle * 180.0 / M_PI);
        gl::drawString(str, cursor);
        cursor.y += 15.0f;

        snprintf(str, sizeof str, "RGB range: [%f, %f] [%f, %f] [%f, %f]",
                 cube.getRangeRGB().getMin().x, cube.getRangeRGB().getMax().x,
                 cube.getRangeRGB().getMin().y, cube.getRangeRGB().getMax().y,
                 cube.getRangeRGB().getMin().z, cube.getRangeRGB().getMax().z);
        gl::drawString(str, cursor);
        cursor.y += 15.0f;

        snprintf(str, sizeof str, "RGB size: %f, %f %f",
                 cube.getRangeRGB().getSize().x,
                 cube.getRangeRGB().getSize().y,
                 cube.getRangeRGB().getSize().z);
        gl::drawString(str, cursor);
        cursor.y += 15.0f;

        snprintf(str, sizeof str, "XYZ range: [%f, %f] [%f, %f] [%f, %f]",
                 cube.getRangeXYZ().getMin().x, cube.getRangeXYZ().getMax().x,
                 cube.getRangeXYZ().getMin().y, cube.getRangeXYZ().getMax().y,
                 cube.getRangeXYZ().getMin().z, cube.getRangeXYZ().getMax().z);
        gl::drawString(str, cursor);
        cursor.y += 15.0f;

        snprintf(str, sizeof str, "XYZ size: %f, %f %f",
                 cube.getRangeXYZ().getSize().x,
                 cube.getRangeXYZ().getSize().y,
                 cube.getRangeXYZ().getSize().z);
        gl::drawString(str, cursor);
        cursor.y += 15.0f;

        snprintf(str, sizeof str, "XY size: %f", cube.getRangeXYZ().getSize().xy().length());
        gl::drawString(str, cursor);
        cursor.y += 15.0f;

        snprintf(str, sizeof str, "XY / Z ratio: %f", cube.getRangeXYZ().getSize().xy().length() / cube.getRangeXYZ().getSize().z);
        gl::drawString(str, cursor);
        cursor.y += 15.0f;
}
    
    mParams->draw();
    
    // Update LEDs from contents of the particle rendering FBO.
    // Only runs if the simulation has produced a new step; if the physics
    // sim is running slower, rely on FC to interpolate between its frames.
    if (mWorld.mUpdatedSinceLastDraw) {
        mWorld.mUpdatedSinceLastDraw = false;
        mFadecandy.update(mParticleRender.getTexture(),
                Matrix33f::createScale( Vec2f(1.0f / mParticleRect.getWidth(),
                                              1.0f / mParticleRect.getHeight()) ));
    }
}

void CircleEngineApp::drawObstacles()
{
    gl::disableAlphaBlending();
    gl::color(0.33f, 0.33f, 0.33f);
    gl::draw(mObstaclesVbo);

    gl::enableWireframe();
    gl::color(0.5f, 0.5f, 0.5f);
    gl::draw(mObstaclesVbo);
    gl::disableWireframe();
}

void CircleEngineApp::drawFrontLayer()
{
    gl::enableAlphaBlending();
    gl::color(0.f, 0.f, 0.f);
    gl::draw(mFrontLayerVbo);
}

void CircleEngineApp::drawSpinners()
{
    gl::enableAlphaBlending();
    gl::color(0.1f, 0.1f, 0.1f);
    
    for (unsigned i = 0; i < mWorld.mSpinners.size(); i++) {
        b2Body *body = mWorld.mSpinners[i].mBody;
        TriMesh2d &mesh = mWorld.mSpinners[i].mMesh;

        gl::pushMatrices();
        gl::translate(mWorld.vecFromBox(body->GetPosition()));
        gl::rotate(body->GetAngle() * 180 / M_PI);
        gl::draw(mesh);
        gl::popMatrices();
    }
}

void CircleEngineApp::drawForceGrid()
{
    gl::color(1.0f, 1.0f, 1.0f, 0.25f);
    gl::enableAlphaBlending();
    for (unsigned idx = 0; idx < mWorld.mForceGrid.size(); idx++) {
        Vec2f pos(idx % mWorld.mForceGridWidth, idx / mWorld.mForceGridWidth);
        Vec2f force = mWorld.mForceGrid[idx];
        pos = mWorld.mForceGridExtent.getUpperLeft() + pos * mWorld.mForceGridResolution;
        gl::drawLine(pos, pos + force * 0.05);
    }
}

void CircleEngineApp::logCurrentSpinnerAngle()
{
    if (mDrawSpinnerColorCube >= 0 && mDrawSpinnerColorCube < mWorld.mSpinners.size()) {
        auto& cube = mWorld.mSpinners[mDrawSpinnerColorCube].mColorCube;
        printf("%f\n", cube.getCurrentAngle());
    } else {
        printf("No spinner selected in color cube debug view\n");
    }
}

CINDER_APP_NATIVE( CircleEngineApp, RendererGl )
