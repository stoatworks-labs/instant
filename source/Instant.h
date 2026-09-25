#pragma once

#include "Model.h"
#include "PassBuffer.h"
#include "StoatworksAboutParams.h"

#include <FFGLSDK.h>

#include <string>

/**
	Instant -- instant film developing in front of you, as an FFGL effect.

	**The one idea.** An integral instant print develops in the open, over
	minutes, and the picture arrives in a known order. A Take captures the
	clip's frame as the exposure. The rollers burst the reagent pod at the
	print's thick bottom border and spread it up the frame; the dye layers
	then migrate to the image layer each at its own rate, set by chemistry
	and temperature, under an opacifier that clears as the timing layer
	drops the pH -- which also ends development. So the picture comes up
	from a dark green-grey, pale and cyan first, and warms to full colour.

	**Four passes**, in `Shaders.h`. The development state is two doses per
	texel, accumulated every frame from the frame's film-seconds after the
	reagent front arrived, each weighted by an Arrhenius rate: first-order
	development is then exact in the dose, whatever the temperature did.
	Time is reduced in double here, frame-relative; nothing absolute crosses
	into GLSL. See AGENTS.md for the traps.
*/
class Instant : public CFFGLPlugin
{
public:
	Instant();

	//CFFGLPlugin
	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	FFResult SetTime( double time ) override;

	char* GetTextParameter( unsigned int index ) override;

	/// Declared only so the About line can accept its own default.
	/// instantiateGL pushes every declared default back through the setters
	/// and deletes the whole instance if one fails, and CFFGLPlugin's
	/// SetTextParameter is a stub that returns exactly that failure.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;

	/// Clock test hook: the harness DECLARES its unit rather than leaving the
	/// voting to infer one.
	void SetClockScaleForTest( double scale );

	/// Negative-control hooks, a bitmask of `model::Perturb`. Always 0 in
	/// the plugin; each bit perturbs the model so a check can be shown to fail.
	void SetPerturbForTest( int bits );

	/// Probe hook: `model::kProbeDensities` makes the print pass write raw floats.
	void SetProbeForTest( int probe );

	/// Bytes of GPU state held across frames right now: the capture and both
	/// halves of the development state, colour textures only. The SDK's FBO
	/// also attaches a depth renderbuffer to each, which this does not count
	/// and the plugin never uses.
	size_t StateBytesForTest() const;

	/// The print's age in film-seconds, and how many takes there have been.
	double AgeForTest() const
	{
		return age;
	}
	int TakesForTest() const
	{
		return takes;
	}

	/// Everything the operator can reach, in the order Resolume shows them.
	enum ParamID : FFUInt32
	{
		//Film
		PT_FILM,
		PT_TEMPERATURE,
		PT_EXPIRED,

		//Exposure
		PT_TAKE,
		PT_MODE,
		PT_INTERVAL,
		PT_EXPOSURE,

		//Chemistry
		PT_SPREAD,
		PT_ROLLER,
		PT_SPEED,

		//Print
		PT_BORDER,
		PT_MIX,

		//About. FFGL has no window, so the name, the version and the links are
		//parameters the host draws. Last, so no saved composition's ids shift.
		PT_ABOUT_FIRST,
		PT_COUNT = PT_ABOUT_FIRST + stoatworks::about::kParamCount
	};

private:
	/// The host's clock in seconds, whatever unit it arrived in.
	double nowSeconds();

	/// Carry the capture and the development state across to a new raster,
	/// resampled, instead of letting a reallocation clear them.
	bool rescale( int width, int height );

	ffglex::FFGLShader captureShader;
	ffglex::FFGLShader meterShader;
	ffglex::FFGLShader developShader;
	ffglex::FFGLShader resampleShader;
	ffglex::FFGLShader printShader;
	ffglex::FFGLScreenQuad quad;

	instant::PassBuffer capture;     ///< the light at the take, RGBA16F
	instant::PassBuffer dose[ 2 ];   ///< ( dye dose, stop dose ), RG32F, ping-pong
	instant::PassBuffer scratch;     ///< the other side of a resize
	instant::PassBuffer meter;       ///< the camera's cell: 64 x 64 R32F, mipmapped
	int current = 0;                 ///< which dose buffer holds the state

	bool hasPrint     = false;
	bool takePending  = false;
	float takeWas     = 0.0f;
	double lastTake   = 0.0;///< host seconds of the last take
	double age        = 0.0;///< film-seconds since the last take
	int takes         = 0;

	int lastWidth  = 0;
	int lastHeight = 0;

	//--- the clock (readout's unit voting) -----------------------------------
	bool hostTimeSeen   = false;
	double clockScale   = 0.0;///< 0 until decided; then 1.0 or 0.001
	double wallStart    = -1.0;
	double lastWallTime = -1.0;
	double lastRawTime  = -1.0;
	int secondsVotes    = 0;
	int millisVotes     = 0;
	double lastNow      = -1.0;
	int clockFrames     = 0;

	int perturb = 0;
	int probe   = 0;

	/// Zero-initialised: the About block's ids are never stored to, so
	/// without this GetFloatParameter hands the host whatever was on the
	/// stack for them.
	float params[ PT_COUNT ] = {};

	/// GetTextParameter hands the host a bare pointer, so the string has to
	/// outlive the call.
	std::string aboutText;
};
